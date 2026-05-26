#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>
#include <chrono>
#include <vector>
#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <csignal>
#include <exception>
#include <typeinfo>

#include <execinfo.h>
#include <unistd.h>
#include <sys/resource.h>

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

namespace
{
    char const* signal_name(int signum)
    {
        if (signum == SIGSEGV) { return "SIGSEGV"; }
        if (signum == SIGABRT) { return "SIGABRT"; }
        if (signum == SIGBUS)  { return "SIGBUS"; }
        if (signum == SIGFPE)  { return "SIGFPE"; }
        if (signum == SIGILL)  { return "SIGILL"; }
        return "UNKNOWN";
    }

    // async-signal-safe: write() + backtrace_symbols_fd() only, no malloc.
    void crash_handler(int signum, siginfo_t* info, void* /*ucontext*/)
    {
        char const* name = signal_name(signum);
        void* fault_addr = nullptr;
        if (info != nullptr)
        {
            fault_addr = info->si_addr;
        }

        char header[160];
        int n = std::snprintf(header, sizeof(header),
            "\n--- CRASH: %s (signo=%d, addr=%p, pid=%d) ---\n",
            name, signum, fault_addr, getpid());
        if (n > 0)
        {
            ssize_t w = write(STDERR_FILENO, header, static_cast<size_t>(n));
            (void)w;
        }

        void* frames[64];
        int frame_count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);

        fsync(STDERR_FILENO);
        fsync(STDOUT_FILENO);

        // Restore default handler and re-raise so a core dump is still produced.
        signal(signum, SIG_DFL);
        raise(signum);
    }

    void install_crash_handlers()
    {
        struct rlimit rl;
        rl.rlim_cur = RLIM_INFINITY;
        rl.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_CORE, &rl);

        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_flags     = SA_SIGINFO | SA_RESETHAND;
        sa.sa_sigaction = crash_handler;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGBUS,  &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
        sigaction(SIGILL,  &sa, nullptr);
    }

    void terminate_handler()
    {
        std::exception_ptr ex = std::current_exception();
        if (ex)
        {
            try
            {
                std::rethrow_exception(ex);
            }
            catch (std::exception const& e)
            {
                fprintf(stderr, "\n--- TERMINATE: uncaught %s: %s ---\n",
                        typeid(e).name(), e.what());
            }
            catch (...)
            {
                fprintf(stderr, "\n--- TERMINATE: uncaught unknown exception ---\n");
            }
        }
        else
        {
            fprintf(stderr, "\n--- TERMINATE: called without active exception ---\n");
        }
        fflush(stderr);
        fflush(stdout);
        std::abort();
    }
}

int main(int argc, char *argv[])
{
    // Make sure any output is flushed line-by-line even when stdout/stderr are
    // redirected to a file: a SIGSEGV otherwise loses the last ~8 KiB of logs.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    install_crash_handlers();
    std::set_terminate(terminate_handler);

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
