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
#include <csignal>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/MailboxSequencer.h"
#include "kickcat/OS/Timer.h"

using namespace kickcat;

volatile std::sig_atomic_t running = 1;

void signal_handler(int)
{
    running = 0;
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
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    auto print_trace = [](const char* label)
    {
        auto now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        printf("[%s] %s\n", label, buf);
    };

    print_trace("START");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

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
        printf("%s\n", e.what());
        print_trace("STOP ON ERROR");
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
        printf("Initializing Bus...\n");

        bus.init(100ms);

        if (bus.slaves().size() != static_cast<size_t>(expected_slaves))
        {
            printf("Error: Detected %zu slaves, but expected %d\n", bus.slaves().size(), expected_slaves);
            print_trace("STOP ON ERROR");
            return 1;
        }

        bus.createMapping(io_buffer);

        auto cyclic_process_data = [&]()
        {
            auto noop = [](DatagramState const &) {};
            bus.processDataRead(noop);
            bus.processDataWrite(noop);
        };

        printf("Switching to SAFE_OP...\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
        printf("All slaves in SAFE_OP\n");

        for (auto &slave : bus.slaves())
        {
            for (int32_t i = 0; i < slave.output.bsize; ++i)
            {
                slave.output.data[i] = 0xBB;
            }
        }
        cyclic_process_data();

        printf("Switching to OPERATIONAL...\n");
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 1s, cyclic_process_data);
        printf("All slaves in OPERATIONAL\n");
    }
    catch (std::exception const &e)
    {
        printf("%s\n", e.what());
        print_trace("STOP ON ERROR");
        return 1;
    }

    uint64_t lost_in_sec       = 0;
    uint64_t send_error_in_sec = 0;
    uint64_t invalid_wkc_in_sec = 0;
    uint64_t no_handler_in_sec  = 0;

    auto callback_error = [&](DatagramState const &state)
    {
        switch (state)
        {
            case DatagramState::LOST:        { lost_in_sec++;        break; }
            case DatagramState::SEND_ERROR:  { send_error_in_sec++;  break; }
            case DatagramState::INVALID_WKC: { invalid_wkc_in_sec++; break; }
            case DatagramState::NO_HANDLER:  { no_handler_in_sec++;  break; }
            default: { break; }
        }
    };

    link->setTimeout(1ms);
    MailboxSequencer mailbox_sequencer(bus);

    constexpr uint64_t MAX_ERRORS_PER_MINUTE = 1;

    std::vector<uint64_t> lost_window(60, 0);
    std::vector<uint64_t> send_error_window(60, 0);
    std::vector<uint64_t> invalid_wkc_window(60, 0);
    std::vector<uint64_t> no_handler_window(60, 0);
    size_t window_pos = 0;

    auto last_tick = since_epoch();

    Timer timer{1ms};
    timer.start();

    printf("Running with max %" PRIu64 " error(s) per minute:\n", MAX_ERRORS_PER_MINUTE);
    printf("  - LOST:        %" PRIu64 "\n", MAX_ERRORS_PER_MINUTE);
    printf("  - SEND_ERROR:  %" PRIu64 "\n", MAX_ERRORS_PER_MINUTE);
    printf("  - INVALID_WKC: %" PRIu64 "\n", MAX_ERRORS_PER_MINUTE);
    printf("  - NO_HANDLER:  %" PRIu64 "\n", MAX_ERRORS_PER_MINUTE);

    while (running)
    {
        timer.wait_next_tick();

        bus.sendLogicalRead(callback_error);
        bus.sendLogicalWrite(callback_error);
        bus.sendRefreshErrorCounters(callback_error);
        mailbox_sequencer.step(callback_error);
        bus.finalizeDatagrams();

        bus.processAwaitingFrames();

        auto now = since_epoch();
        if (now - last_tick >= 1s)
        {
            last_tick = now;

            lost_window[window_pos]        = lost_in_sec;
            send_error_window[window_pos]  = send_error_in_sec;
            invalid_wkc_window[window_pos] = invalid_wkc_in_sec;
            no_handler_window[window_pos]  = no_handler_in_sec;

            lost_in_sec       = 0;
            send_error_in_sec = 0;
            invalid_wkc_in_sec = 0;
            no_handler_in_sec  = 0;

            window_pos = (window_pos + 1) % 60;

            uint64_t current_lost        = std::accumulate(lost_window.begin(), lost_window.end(), 0ULL);
            uint64_t current_send_error  = std::accumulate(send_error_window.begin(), send_error_window.end(), 0ULL);
            uint64_t current_invalid_wkc = std::accumulate(invalid_wkc_window.begin(), invalid_wkc_window.end(), 0ULL);
            uint64_t current_no_handler  = std::accumulate(no_handler_window.begin(), no_handler_window.end(), 0ULL);

            if (current_lost > MAX_ERRORS_PER_MINUTE)
            {
                printf("LOST (%" PRIu64 ") exceeded threshold (%" PRIu64 ")\n", current_lost, MAX_ERRORS_PER_MINUTE);
                print_trace("STOP ON ERROR");
                return 1;
            }
            if (current_send_error > MAX_ERRORS_PER_MINUTE)
            {
                printf("SEND_ERROR (%" PRIu64 ") exceeded threshold (%" PRIu64 ")\n", current_send_error, MAX_ERRORS_PER_MINUTE);
                print_trace("STOP ON ERROR");
                return 1;
            }
            if (current_invalid_wkc > MAX_ERRORS_PER_MINUTE)
            {
                printf("INVALID_WKC (%" PRIu64 ") exceeded threshold (%" PRIu64 ")\n", current_invalid_wkc, MAX_ERRORS_PER_MINUTE);
                print_trace("STOP ON ERROR");
                return 1;
            }
            if (current_no_handler > MAX_ERRORS_PER_MINUTE)
            {
                printf("NO_HANDLER (%" PRIu64 ") exceeded threshold (%" PRIu64 ")\n", current_no_handler, MAX_ERRORS_PER_MINUTE);
                print_trace("STOP ON ERROR");
                return 1;
            }
        }
    }

    print_trace("STOP");
    return 0;
}
