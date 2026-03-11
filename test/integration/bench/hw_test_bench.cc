#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>
#include <chrono>
#include <vector>
#include <numeric>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/MailboxSequencer.h"

using namespace kickcat;

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
        std::cerr << err.what() << std::endl;
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
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    uint8_t io_buffer[2048];
    try
    {
        std::cout << "Initializing EtherCAT Master on " << nom_interface_name << "..." << std::endl;
        bus.init(100ms);

        if (bus.slaves().size() != static_cast<size_t>(expected_slaves))
        {
            std::cerr << "Error: Expected " << expected_slaves << " slaves, but found " << bus.slaves().size() << std::endl;
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
        std::cerr << e.what() << std::endl;
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

    std::cout << "Starting 1-minute calibration cycle..." << std::endl;

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
                    std::cout << "Calibration complete. Baseline WC Errors: " << baseline_wc
                              << ", Baseline Lost Frames: " << baseline_lost << std::endl;
                }
                else if (elapsed % 10 == 0)
                {
                    std::cout << "Calibration in progress... " << elapsed << "s/60s" << std::endl;
                }
            }
            else
            {
                if (current_wc_sum > baseline_wc)
                {
                    std::cerr << "Error: WC errors (" << current_wc_sum << ") exceeded baseline threshold (" << baseline_wc << ")" << std::endl;
                    return 1;
                }
                if (current_lost_sum > baseline_lost)
                {
                    std::cerr << "Error: Lost frames (" << current_lost_sum << ") exceeded baseline threshold (" << baseline_lost << ")" << std::endl;
                    return 1;
                }
            }
        }
    }

    return 0;
}
