#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>
#include <chrono>
#include <vector>
#include <numeric>
#include <cstdio>
#include <cinttypes>
#include <cstdarg>
#include <ctime>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/MailboxSequencer.h"

using namespace kickcat;

void log_msg(const char *format, ...)
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char time_buf[64];
    struct tm *tm_info = std::localtime(&in_time_t);
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("[%s.%03d] ", time_buf, static_cast<int>(ms.count()));

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    argparse::ArgumentParser program("hw_test_bench");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    int expected_slaves = 2;
    program.add_argument("-s", "--slaves")
        .help("expected number of slaves")
        .default_value(2)
        .scan<'i', int>()
        .store_into(expected_slaves);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        log_msg("%s\n", err.what());
        std::cerr << program;
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, "");
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const &e)
    {
        log_msg("%s\n", e.what());
        return 1;
    }

    auto report_redundancy = []()
    {
        log_msg("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    uint8_t io_buffer[2048];
    try
    {
        log_msg("Initializing EtherCAT Master on %s...\n", nom_interface_name.c_str());
        bus.init(100ms);

        if (bus.slaves().size() != static_cast<size_t>(expected_slaves))
        {
            log_msg("Error: Expected %d slaves, but found %zu\n", expected_slaves, bus.slaves().size());
            return 1;
        }

        bus.createMapping(io_buffer);

        auto cyclic_process_data = [&]()
        {
            auto noop = [](DatagramState const &) {};
            bus.processDataRead(noop);
            bus.processDataWrite(noop);
        };

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);

        for (auto &slave : bus.slaves())
        {
            for (int32_t i = 0; i < slave.output.bsize; ++i)
            {
                slave.output.data[i] = 0xBB;
            }
        }
        cyclic_process_data();

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 1s, cyclic_process_data);
    }
    catch (std::exception const &e)
    {
        log_msg("%s\n", e.what());
        return 1;
    }

    uint64_t wc_errors_in_sec = 0;
    uint64_t lost_frames_in_sec = 0;

    auto callback_error = [&](DatagramState const &state)
    {
        if (state != DatagramState::OK)
        {
            wc_errors_in_sec++;
        }
    };

    link->setTimeout(10ms);
    MailboxSequencer mailbox_sequencer(bus);

    std::vector<uint64_t> wc_window(60, 0);
    std::vector<uint64_t> lost_window(60, 0);
    size_t window_pos = 0;

    uint64_t baseline_wc = 0;
    uint64_t baseline_lost = 0;

    auto start_time = std::chrono::steady_clock::now();
    auto last_tick = start_time;
    bool calibrated = false;

    log_msg("Starting 1-minute calibration cycle...\n");

    while (true)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            mailbox_sequencer.step(callback_error);
            bus.finalizeDatagrams();

            bus.processAwaitingFrames();
        }
        catch (std::exception const &e)
        {
            lost_frames_in_sec++;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= 1s)
        {
            last_tick = now;

            wc_window[window_pos] = wc_errors_in_sec;
            lost_window[window_pos] = lost_frames_in_sec;

            wc_errors_in_sec = 0;
            lost_frames_in_sec = 0;

            window_pos = (window_pos + 1) % 60;

            uint64_t current_wc_sum = std::accumulate(wc_window.begin(), wc_window.end(), 0ULL);
            uint64_t current_lost_sum = std::accumulate(lost_window.begin(), lost_window.end(), 0ULL);

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

            if (!calibrated)
            {
                if (elapsed >= 60)
                {
                    calibrated = true;
                    baseline_wc = current_wc_sum;
                    baseline_lost = current_lost_sum;
                    log_msg("Calibration complete. Baseline WC Errors: %" PRIu64 ", Baseline Lost Frames: %" PRIu64 "\n", baseline_wc, baseline_lost);
                }
                else if (elapsed % 10 == 0)
                {
                    log_msg("Calibration in progress... %" PRId64 "s/60s\n", elapsed);
                }
            }
            else
            {
                if (elapsed % 60 == 0)
                {
                    log_msg("WC Errors in last minute: %" PRIu64 ", Lost Frames in last minute: %" PRIu64 "\n", current_wc_sum, current_lost_sum);
                }
                if (current_wc_sum > baseline_wc)
                {
                    log_msg("Error: WC errors (%" PRIu64 ") exceeded baseline threshold (%" PRIu64 ")\n", current_wc_sum, baseline_wc);
                    return 1;
                }
                if (current_lost_sum > baseline_lost)
                {
                    log_msg("Error: Lost frames (%" PRIu64 ") exceeded baseline threshold (%" PRIu64 ")\n", current_lost_sum, baseline_lost);
                    return 1;
                }
            }
        }
    }

    return 0;
}
