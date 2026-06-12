/// Operator-guided hardware validation of EtherCAT cable redundancy: frame splice
/// under a ring split, degraded-start positional addressing, and DC across a break.
/// Designed for a small bench ring (2+ slaves, last slave wired back to the second
/// NIC). Each phase ends with a PASS / FAIL / INCONCLUSIVE verdict and the counters
/// that justify it.

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/Error.h"
#include "kickcat/Link.h"
#include "kickcat/OS/Time.h"
#include "kickcat/OS/Timer.h"
#include "kickcat/helpers.h"

using namespace kickcat;

namespace
{
    // ESC user RAM: persists across re-initialization, used to fingerprint each
    // physical slave independently of (often zeroed) serial numbers.
    constexpr uint16_t USER_RAM = 0x0F80;

    enum class Verdict
    {
        PASS,
        FAIL,
        INCONCLUSIVE,
        SKIPPED
    };

    char const* toString(Verdict verdict)
    {
        switch (verdict)
        {
            case Verdict::PASS:         { return "PASS";         }
            case Verdict::FAIL:         { return "FAIL";         }
            case Verdict::INCONCLUSIVE: { return "INCONCLUSIVE"; }
            default:                    { return "SKIPPED";      }
        }
    }

    struct PhaseResult
    {
        std::string name;
        Verdict verdict{Verdict::SKIPPED};
        std::string detail;
    };

    void waitOperator(char const* instruction)
    {
        printf("\n>>> %s\n>>> Press Enter when done.\n", instruction);
        std::string line;
        std::getline(std::cin, line);
    }

    bool readRegister(Link& link, uint16_t slave_address, uint16_t reg_address, void* data, uint16_t size)
    {
        bool ok = false;
        auto process = [&](DatagramHeader const*, uint8_t const* payload, uint16_t wkc)
        {
            if (wkc == 1)
            {
                std::memcpy(data, payload, size);
                ok = true;
            }
            return DatagramState::OK;
        };
        auto error = [](DatagramState const&) {};
        link.addDatagram(Command::FPRD, createAddress(slave_address, reg_address), nullptr, size, process, error);
        link.processDatagrams();
        return ok;
    }

    bool writeRegister(Link& link, uint16_t slave_address, uint16_t reg_address, void const* data, uint16_t size)
    {
        bool ok = false;
        auto process = [&](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc == 1)
            {
                ok = true;
            }
            return DatagramState::OK;
        };
        auto error = [](DatagramState const&) {};
        link.addDatagram(Command::FPWR, createAddress(slave_address, reg_address), data, size, process, error);
        link.processDatagrams();
        return ok;
    }

    struct InputImage
    {
        std::vector<uint8_t> bytes;

        void capture(Bus& bus)
        {
            bytes.clear();
            for (auto const& slave : bus.slaves())
            {
                if (slave.input.bsize > 0)
                {
                    bytes.insert(bytes.end(), slave.input.data, slave.input.data + slave.input.bsize);
                }
            }
        }
    };

    // Bring the bus to OPERATIONAL with a primed process image, marvin-style.
    void goOperational(Bus& bus, nanoseconds cycle)
    {
        auto ignore = [](DatagramState const&) {};

        Timer timer{cycle};
        timer.start();
        for (int i = 0; i < 10; ++i)
        {
            bus.processDataRead(ignore);
            bus.processDataWrite(ignore);
            timer.wait_next_tick();
        }

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 3000ms, [&]()
        {
            bus.processDataRead(ignore);
            bus.processDataWrite(ignore);
            timer.wait_next_tick();
        });
    }

    struct CyclicStats
    {
        int64_t cycles{0};
        int64_t error_cycles{0};
        int64_t max_error_burst{0};
        int64_t tail_error_cycles{0};                  // errors in the last `tail_cycles` of the window
        std::vector<int64_t> liveness;                 // per input byte: number of observed changes
    };

    // Run a cyclic LRW window of `duration` and collect WKC-error and liveness stats.
    CyclicStats runWindow(Bus& bus, nanoseconds cycle, nanoseconds duration, int64_t tail_cycles)
    {
        CyclicStats stats;
        bool cycle_error = false;
        auto on_error = [&](DatagramState const&)
        {
            cycle_error = true;
        };

        InputImage previous;
        previous.capture(bus);
        stats.liveness.assign(previous.bytes.size(), 0);

        int64_t const total = duration / cycle;
        int64_t burst = 0;
        std::vector<int64_t> error_history;
        error_history.reserve(static_cast<size_t>(total));

        Timer timer{cycle};
        timer.start();
        for (int64_t i = 0; i < total; ++i)
        {
            cycle_error = false;
            try
            {
                bus.processDataReadWrite(on_error);
            }
            catch (std::exception const&)
            {
                cycle_error = true;
            }

            ++stats.cycles;
            if (cycle_error)
            {
                ++stats.error_cycles;
                ++burst;
                stats.max_error_burst = std::max(stats.max_error_burst, burst);
            }
            else
            {
                burst = 0;
            }
            error_history.push_back(stats.error_cycles);

            InputImage current;
            current.capture(bus);
            for (size_t b = 0; b < current.bytes.size() and b < previous.bytes.size(); ++b)
            {
                if (current.bytes[b] != previous.bytes[b])
                {
                    ++stats.liveness[b];
                }
            }
            previous = current;

            timer.wait_next_tick();
        }

        if (static_cast<int64_t>(error_history.size()) > tail_cycles)
        {
            stats.tail_error_cycles = error_history.back() - error_history[error_history.size() - 1 - tail_cycles];
        }
        return stats;
    }

    // Map a global input-image byte index back to its slave (for liveness reporting).
    std::vector<int32_t> inputByteOwner(Bus& bus)
    {
        std::vector<int32_t> owner;
        int32_t index = 0;
        for (auto const& slave : bus.slaves())
        {
            for (int32_t b = 0; b < slave.input.bsize; ++b)
            {
                owner.push_back(index);
            }
            ++index;
        }
        return owner;
    }

    std::vector<bool> slavesAlive(CyclicStats const& stats, std::vector<int32_t> const& owner, size_t slave_count)
    {
        std::vector<bool> alive(slave_count, false);
        for (size_t b = 0; b < stats.liveness.size() and b < owner.size(); ++b)
        {
            if (stats.liveness[b] > 0)
            {
                alive[owner[b]] = true;
            }
        }
        return alive;
    }
}


int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("redundancy_bench");

    std::string nominal_if;
    program.add_argument("-i", "--interface").help("nominal network interface").required().store_into(nominal_if);

    std::string redundant_if;
    program.add_argument("-r", "--redundancy").help("redundant network interface").required().store_into(redundant_if);

    int cycle_us = 1000;
    program.add_argument("--cycle").help("cycle time in microseconds").default_value(1000).store_into(cycle_us);

    bool allow_pattern = false;
    program.add_argument("--allow-output-pattern")
        .help("fill outputs with 0xFF during phase 2 - ONLY on non-actuated bench slaves")
        .default_value(false)
        .implicit_value(true)
        .store_into(allow_pattern);

    int drift_threshold_us = 50;
    program.add_argument("--drift-threshold").help("max |DC drift| in microseconds during a break (phase 5)")
        .default_value(50).store_into(drift_threshold_us);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::runtime_error const& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    nanoseconds const cycle = std::chrono::microseconds(cycle_us);
    std::vector<PhaseResult> results;

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nominal_if, redundant_if);
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("NOTE: bus started degraded - redundancy already active\n");
    };
    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    static uint8_t iomap[16384];

    // ---------------------------------------------------------------- phase 0
    printf("\n=== Phase 0: baseline and fingerprinting ===\n");
    Bus bus(link);
    size_t slave_count = 0;
    try
    {
        bus.init(0ms); // watchdog disabled: operator pauses between phases must not trip it
        slave_count = static_cast<size_t>(bus.detectedSlaves());
        printf("Detected %zu slave(s)\n", slave_count);

        bus.createMapping(iomap, sizeof(iomap));
        for (auto const& slave : bus.slaves())
        {
            printf("  slave %04x: input %d byte(s), output %d byte(s)\n",
                   slave.address, slave.input.bsize, slave.output.bsize);
        }

        // Fingerprint: physical identity that survives re-initialization.
        uint8_t index = 0;
        for (auto const& slave : bus.slaves())
        {
            if (not writeRegister(*link, slave.address, USER_RAM, &index, sizeof(index)))
            {
                THROW_ERROR("cannot write fingerprint to user RAM");
            }
            uint8_t check = 0xFF;
            if ((not readRegister(*link, slave.address, USER_RAM, &check, sizeof(check))) or (check != index))
            {
                THROW_ERROR("fingerprint readback mismatch");
            }
            ++index;
        }

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 3000ms);
        goOperational(bus, cycle);

        results.push_back({"0 baseline + fingerprint", Verdict::PASS,
                           std::to_string(slave_count) + " slaves, fingerprints verified, OPERATIONAL"});
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        results.push_back({"0 baseline + fingerprint", Verdict::FAIL, e.what()});
        printf("Baseline failed: aborting\n");
        return 1;
    }

    auto ignore = [](DatagramState const&) {};
    std::vector<int32_t> const owner = inputByteOwner(bus);

    // ---------------------------------------------------------------- phase 2
    printf("\n=== Phase 2: intact-ring LRW equivalence ===\n");
    {
        // Classify volatile input bytes from LRD-only cycles: only stable bytes are comparable.
        InputImage reference;
        std::vector<bool> is_volatile;
        {
            Timer timer{cycle};
            timer.start();
            bus.processDataRead(ignore);
            reference.capture(bus);
            is_volatile.assign(reference.bytes.size(), false);
            for (int i = 0; i < 200; ++i)
            {
                bus.processDataRead(ignore);
                bus.processDataWrite(ignore);
                InputImage current;
                current.capture(bus);
                for (size_t b = 0; b < current.bytes.size(); ++b)
                {
                    if (current.bytes[b] != reference.bytes[b])
                    {
                        is_volatile[b] = true;
                    }
                }
                reference = current;
                timer.wait_next_tick();
            }
        }

        bool outputs_nonzero = false;
        if (allow_pattern)
        {
            for (auto& slave : bus.slaves())
            {
                if (slave.output.bsize > 0)
                {
                    std::memset(slave.output.data, 0xFF, static_cast<size_t>(slave.output.bsize));
                    outputs_nonzero = true;
                }
            }
        }
        else
        {
            for (auto const& slave : bus.slaves())
            {
                for (int32_t b = 0; b < slave.output.bsize; ++b)
                {
                    if (slave.output.data[b] != 0)
                    {
                        outputs_nonzero = true;
                    }
                }
            }
        }

        int64_t mismatching_bytes = 0;
        Timer timer{cycle};
        timer.start();
        for (int i = 0; i < 5000; ++i)
        {
            bus.processDataRead(ignore);
            InputImage lrd;
            lrd.capture(bus);
            timer.wait_next_tick();

            bus.processDataReadWrite(ignore);
            InputImage lrw;
            lrw.capture(bus);
            timer.wait_next_tick();

            for (size_t b = 0; b < lrw.bytes.size(); ++b)
            {
                if ((not is_volatile[b]) and (lrw.bytes[b] != lrd.bytes[b]))
                {
                    ++mismatching_bytes;
                }
            }
        }

        if (allow_pattern)
        {
            for (auto& slave : bus.slaves())
            {
                if (slave.output.bsize > 0)
                {
                    std::memset(slave.output.data, 0, static_cast<size_t>(slave.output.bsize));
                }
            }
        }

        Verdict verdict = Verdict::FAIL;
        std::string detail = std::to_string(mismatching_bytes) + " stable-byte mismatch(es) over 5000 cycles";
        if (mismatching_bytes == 0)
        {
            if (outputs_nonzero)
            {
                verdict = Verdict::PASS;
            }
            else
            {
                verdict = Verdict::INCONCLUSIVE;
                detail += " - but all outputs are zero, OR-corruption undetectable"
                          " (rerun with --allow-output-pattern on non-actuated slaves)";
            }
        }
        results.push_back({"2 intact-ring LRW equivalence", verdict, detail});
        printf("Phase 2: %s (%s)\n", toString(verdict), detail.c_str());
    }

    // ---------------------------------------------------------------- phase 3
    printf("\n=== Phase 3: break/heal under cyclic LRW ===\n");
    {
        printf("Baseline window (5s)...\n");
        CyclicStats baseline = runWindow(bus, cycle, 5s, 0);
        auto baseline_alive = slavesAlive(baseline, owner, slave_count);
        size_t monitored = static_cast<size_t>(std::count(baseline_alive.begin(), baseline_alive.end(), true));
        printf("Slaves with changing inputs: %zu/%zu", monitored, slave_count);
        if (monitored < slave_count)
        {
            printf(" - make the silent ones change (toggle a DI, turn a shaft) for a conclusive verdict");
        }
        printf("\n");

        printf("\n>>> PULL one cable between two mid-chain slaves within the next 20 seconds.\n");
        CyclicStats break_window = runWindow(bus, cycle, 20s, 5s / cycle);
        auto break_alive = slavesAlive(break_window, owner, slave_count);

        printf("\n>>> RE-PLUG the cable within the next 20 seconds.\n");
        CyclicStats heal_window = runWindow(bus, cycle, 20s, 5s / cycle);

        bool tail_clean = (break_window.tail_error_cycles == 0) and (heal_window.tail_error_cycles == 0);
        bool burst_bounded = (break_window.max_error_burst <= 10) and (heal_window.max_error_burst <= 10);
        bool all_monitored_alive = true;
        for (size_t s = 0; s < slave_count; ++s)
        {
            if (baseline_alive[s] and (not break_alive[s]))
            {
                all_monitored_alive = false;
            }
        }

        std::string detail = "break window: " + std::to_string(break_window.error_cycles) + " error cycle(s), max burst "
                           + std::to_string(break_window.max_error_burst) + ", steady-state errors "
                           + std::to_string(break_window.tail_error_cycles)
                           + "; heal window steady-state errors " + std::to_string(heal_window.tail_error_cycles);

        Verdict verdict = Verdict::FAIL;
        if (tail_clean and burst_bounded and all_monitored_alive)
        {
            if (monitored == slave_count)
            {
                verdict = Verdict::PASS;
            }
            else
            {
                verdict = Verdict::INCONCLUSIVE;
                detail += "; only " + std::to_string(monitored) + "/" + std::to_string(slave_count)
                        + " slaves had changing inputs";
            }
        }
        else if (not all_monitored_alive)
        {
            detail += "; INPUTS FROZE on a segment during the break (splice failure signature)";
        }
        results.push_back({"3 break/heal under LRW", verdict, detail});
        printf("Phase 3: %s (%s)\n", toString(verdict), detail.c_str());
    }

    // ---------------------------------------------------------------- phase 4
    printf("\n=== Phase 4: degraded-start positional addressing ===\n");
    {
        waitOperator("PULL one cable between two mid-chain slaves (and keep it pulled).");

        Verdict verdict = Verdict::FAIL;
        std::string detail;
        try
        {
            Bus degraded(link);
            degraded.init(0ms);
            size_t redetected = static_cast<size_t>(degraded.detectedSlaves());

            std::vector<int32_t> sequence;
            for (auto const& slave : degraded.slaves())
            {
                uint8_t fingerprint = 0xFF;
                if (not readRegister(*link, slave.address, USER_RAM, &fingerprint, sizeof(fingerprint)))
                {
                    THROW_ERROR("cannot read fingerprint back");
                }
                sequence.push_back(fingerprint);
            }

            printf("Measured physical order by station address:");
            for (int32_t f : sequence)
            {
                printf(" %d", f);
            }
            printf("\n");

            if (redetected != slave_count)
            {
                verdict = Verdict::INCONCLUSIVE;
                detail = "only " + std::to_string(redetected) + "/" + std::to_string(slave_count)
                       + " slaves reachable - break the ring between slaves, not at a master port";
            }
            else
            {
                // Port-0/EPU model prediction: tail segment is addressed from the break
                // outward, so the full positional order equals the intact order.
                bool increasing = true;
                for (size_t k = 0; k < sequence.size(); ++k)
                {
                    if (sequence[k] != static_cast<int32_t>(k))
                    {
                        increasing = false;
                    }
                }
                if (increasing)
                {
                    verdict = Verdict::PASS;
                    detail = "tail segment addressed from the break outward (port-0/EPU model confirmed)";
                }
                else
                {
                    detail = "tail segment NOT addressed from the break outward - emulator order model contradicted";
                }
            }
        }
        catch (std::exception const& e)
        {
            verdict = Verdict::INCONCLUSIVE;
            detail = e.what();
        }
        results.push_back({"4 degraded positional addressing", verdict, detail});
        printf("Phase 4: %s (%s)\n", toString(verdict), detail.c_str());

        waitOperator("RE-PLUG the cable.");
    }

    // ---------------------------------------------------------------- phase 5
    printf("\n=== Phase 5: DC across a break ===\n");
    {
        Verdict verdict = Verdict::SKIPPED;
        std::string detail = "fewer than 2 DC-capable slaves";

        try
        {
            Bus dc_bus(link);
            dc_bus.init(0ms);
            dc_bus.createMapping(iomap, sizeof(iomap));

            size_t dc_slaves = 0;
            for (auto const& slave : dc_bus.slaves())
            {
                if (slave.isDCSupport())
                {
                    ++dc_slaves;
                }
            }

            if (dc_slaves >= 2)
            {
                dc_bus.requestState(State::SAFE_OP);
                dc_bus.waitForState(State::SAFE_OP, 3000ms);
                dc_bus.enableDC(cycle);
                goOperational(dc_bus, cycle);

                auto maxDriftOver = [&](nanoseconds duration) -> int64_t
                {
                    int64_t max_drift = 0;
                    int64_t const total = duration / cycle;
                    Timer timer{cycle};
                    timer.start();
                    for (int64_t i = 0; i < total; ++i)
                    {
                        dc_bus.processDataRead(ignore);
                        dc_bus.processDataWrite(ignore);
                        if ((i % 1000) == 0)
                        {
                            for (auto const& slave : dc_bus.slaves())
                            {
                                if (not slave.isDCSupport())
                                {
                                    continue;
                                }
                                uint32_t raw = 0;
                                if (readRegister(*link, slave.address, reg::DC_SYSTEM_TIME_DIFF, &raw, sizeof(raw)))
                                {
                                    // sign-magnitude: bit 31 = sign, bits 30:0 = magnitude
                                    max_drift = std::max(max_drift, static_cast<int64_t>(raw & 0x7FFFFFFF));
                                }
                            }
                        }
                        timer.wait_next_tick();
                    }
                    return max_drift;
                };

                printf("DC settling (10s)...\n");
                int64_t settled = maxDriftOver(10s);
                printf("Settled max |drift|: %" PRId64 " ns\n", settled);

                printf("\n>>> PULL one cable between two mid-chain slaves within the next 10 seconds.\n");
                int64_t during_break = maxDriftOver(30s);
                printf("Max |drift| during break window: %" PRId64 " ns\n", during_break);

                printf("\n>>> RE-PLUG the cable within the next 10 seconds.\n");
                maxDriftOver(20s);
                int64_t after_heal = maxDriftOver(5s);
                printf("Max |drift| after heal: %" PRId64 " ns\n", after_heal);

                int64_t const threshold = static_cast<int64_t>(drift_threshold_us) * 1000;
                detail = "break max drift " + std::to_string(during_break) + " ns (threshold "
                       + std::to_string(threshold) + "), after heal " + std::to_string(after_heal) + " ns";
                if ((during_break < threshold) and (after_heal < (settled * 4 + 2000)))
                {
                    verdict = Verdict::PASS;
                }
                else
                {
                    verdict = Verdict::FAIL;
                }
            }
        }
        catch (std::exception const& e)
        {
            verdict = Verdict::INCONCLUSIVE;
            detail = e.what();
        }
        results.push_back({"5 DC across a break", verdict, detail});
        printf("Phase 5: %s (%s)\n", toString(verdict), detail.c_str());
    }

    // ---------------------------------------------------------------- summary
    printf("\n========================= VERDICTS =========================\n");
    for (auto const& result : results)
    {
        printf("%-36s %-13s %s\n", result.name.c_str(), toString(result.verdict), result.detail.c_str());
    }

    for (auto const& result : results)
    {
        if (result.verdict == Verdict::FAIL)
        {
            return 1;
        }
    }
    return 0;
}
