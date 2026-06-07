// Rigorous catalog boot test: for each ESI device, build the EEPROM + CoE
// dictionary, instantiate an emulated slave (EmulatedESC + Slave + optional CoE
// mailbox), connect a real master Bus through an in-process loopback, and drive
// the EtherCAT state machine INIT -> PRE_OP -> SAFE_OP -> OPERATIONAL. Unlike
// esi_validate (a static mapping-resolution check), this actually transitions
// the slave, so reaching OPERATIONAL is the real proof of bootability.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <argparse/argparse.hpp>

#include "kickcat/OS/Mutex.h"

#include "kickcat/CoE/OD.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"
#include "kickcat/PDO.h"
#include "kickcat/slave/Slave.h"
#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/SocketNull.h"

using namespace kickcat;
namespace fs = std::filesystem;

// Master frames are processed in place by the emulated slave, then the slave's
// ESM is ticked once. One writeThenRead == one simulator iteration.
class LoopbackSocket final : public AbstractSocket
{
public:
    LoopbackSocket(std::vector<EmulatedESC*> escs, std::function<void()> tick)
        : escs_(std::move(escs)), tick_(std::move(tick)) {}

    void open(std::string const&) override {}
    void setTimeout(nanoseconds) override {}
    void close() noexcept override {}

    int32_t write(void const* data, int32_t size) override
    {
        Frame frame;
        std::memcpy(frame.data(), data, static_cast<size_t>(size));
        while (true)
        {
            auto [header, payload, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }
            for (auto* esc : escs_)
            {
                esc->processDatagram(header, payload, wkc);
            }
        }
        tick_();
        std::memcpy(buffer_, frame.data(), static_cast<size_t>(size));
        size_ = size;
        pending_ = true;
        return size;
    }

    int32_t read(void* data, int32_t) override
    {
        if (not pending_)
        {
            return 0;
        }
        std::memcpy(data, buffer_, static_cast<size_t>(size_));
        pending_ = false;
        return size_;
    }

private:
    std::vector<EmulatedESC*> escs_;
    std::function<void()> tick_;
    uint8_t buffer_[ETH_MAX_SIZE];
    int32_t size_    = 0;
    bool    pending_ = false;
};

enum class Reached { INIT_FAIL, INIT, PRE_OP, SAFE_OP, OP };

static Reached bootDevice(ESI::Device& dev, nanoseconds wait_timeout)
{
    // Catalog-wide: a slave's process data can be far larger than the 32-byte
    // toy buffers used for hand-written sim configs. Size for a full frame so
    // updateInput/updateOutput (which copy the SM length) can't overflow.
    constexpr uint32_t PDO_MAX_SIZE = 4096;

    CoE::materializeStorage(dev.dictionary);
    auto esc = std::make_unique<EmulatedESC>();
    esc->loadEeprom(ESI::buildEepromImage(dev));

    auto pdo = std::make_unique<PDO>(esc.get());
    auto sl  = std::make_unique<slave::Slave>(esc.get(), pdo.get());

    std::unique_ptr<mailbox::response::Mailbox> mbx;
    if (dev.mailbox and dev.mailbox->coe)
    {
        mbx = std::make_unique<mailbox::response::Mailbox>(esc.get(), 1024);
        mbx->enableCoE(std::move(dev.dictionary));
        sl->setMailbox(mbx.get());
    }

    std::vector<uint8_t> input_pdo(PDO_MAX_SIZE);
    std::iota(input_pdo.begin(), input_pdo.end(), 0);
    std::vector<uint8_t> output_pdo(PDO_MAX_SIZE, 0xFF);
    pdo->setInput(input_pdo.data(), PDO_MAX_SIZE);
    pdo->setOutput(output_pdo.data(), PDO_MAX_SIZE);

    uint16_t dl_status = (1 << 4) | (1 << 9);  // single slave: only port0 connected
    esc->write(reg::ESC_DL_STATUS, &dl_status, sizeof(dl_status));
    sl->start();

    // Once the master is driving process data (OP phase), a slave with outputs
    // needs its output data declared valid to leave SAFE_OP. Keyed on the phase,
    // not a buffer byte (which never changes for <=1-byte outputs like EL2008).
    bool op_phase = false;
    auto tick = [&]()
    {
        sl->routine();
        if (op_phase and sl->state() == State::SAFE_OP)
        {
            sl->validateOutputData();
        }
    };

    std::vector<EmulatedESC*> escs{esc.get()};
    auto sock_nominal = std::make_shared<LoopbackSocket>(escs, tick);
    auto sock_redundancy = std::make_shared<SocketNull>();
    auto link = std::make_shared<Link>(sock_nominal, sock_redundancy, [](){});
    link->setTimeout(2ms);

    Bus bus(link);

    std::vector<uint8_t> io_buffer(16384);
    try
    {
        bus.init(100ms);
        if (bus.slaves().size() != 1)
        {
            return Reached::INIT_FAIL;
        }
        bus.createMapping(io_buffer.data(), io_buffer.size());
    }
    catch (std::exception const&)
    {
        return Reached::INIT_FAIL;
    }

    Reached reached = Reached::PRE_OP;

    auto noop = [](DatagramState const&) {};
    auto cyclic = [&]()
    {
        bus.processDataRead(noop);
        bus.processDataWrite(noop);
    };

    try
    {
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, wait_timeout);
    }
    catch (std::exception const&)
    {
    }
    if (sl->state() != State::SAFE_OP and sl->state() != State::OPERATIONAL)
    {
        return reached;
    }
    reached = Reached::SAFE_OP;

    for (auto& s : bus.slaves())
    {
        for (int32_t i = 0; i < s.output.bsize; ++i)
        {
            s.output.data[i] = 0xBB;
        }
    }
    try
    {
        cyclic();
    }
    catch (std::exception const&)
    {
    }

    op_phase = true;
    try
    {
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, wait_timeout, cyclic);
    }
    catch (std::exception const&)
    {
    }
    if (sl->state() == State::OPERATIONAL)
    {
        reached = Reached::OP;
    }
    return reached;
}

struct Counts
{
    int op         = 0;
    int safe_op    = 0;
    int pre_op     = 0;
    int init_fail  = 0;
    int build_fail = 0;
    int total      = 0;
};

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("esi_boot");

    std::string path_arg;
    program.add_argument("path")
        .help("ESI XML file or directory to sweep")
        .store_into(path_arg);

    int timeout_ms = 200;
    program.add_argument("-t", "--timeout")
        .help("per-state wait timeout (ms)")
        .default_value(200)
        .scan<'i', int>()
        .store_into(timeout_ms);

    int default_threads = static_cast<int>(std::thread::hardware_concurrency());
    int num_threads = default_threads;
    program.add_argument("-j", "--threads")
        .help("worker threads")
        .default_value(default_threads)
        .scan<'i', int>()
        .store_into(num_threads);

    int max_devices = -1;
    program.add_argument("-n", "--max-devices")
        .help("stop after roughly N devices (-1 = all)")
        .default_value(-1)
        .scan<'i', int>()
        .store_into(max_devices);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl << program;
        return 2;
    }

    fs::path root = path_arg;
    if (num_threads < 1) { num_threads = 1; }

    setvbuf(stdout, nullptr, _IONBF, 0);

    std::vector<fs::path> xmls;
    if (fs::is_directory(root))
    {
        for (auto& e : fs::recursive_directory_iterator(root))
        {
            if (e.path().extension() == ".xml") { xmls.push_back(e.path()); }
        }
        std::sort(xmls.begin(), xmls.end());
    }
    else
    {
        xmls.push_back(root);
    }

    // Each device is fully independent (own ESC/Bus/loopback), so files are
    // dispatched to a worker pool. The mutex guards the work index and the merge
    // of per-file results; max_devices is a soft cap (overshoots by in-flight files).
    Mutex mutex;
    std::size_t next_file = 0;
    Counts global;

    auto worker = [&]()
    {
        while (true)
        {
            std::size_t idx;
            {
                LockGuard lock(mutex);
                if (next_file >= xmls.size() or (max_devices >= 0 and global.total >= max_devices))
                {
                    return;
                }
                idx = next_file++;
            }

            ESI::Parser parser;
            std::vector<std::string> errs;
            std::vector<ESI::Device> devs;
            try
            {
                devs = parser.loadAllDevices(xmls[idx].string(), &errs);
            }
            catch (std::exception&)
            {
                continue;
            }

            Counts local;
            std::string failures;
            std::string fname = xmls[idx].filename().string();
            for (auto& dev : devs)
            {
                local.total++;
                Reached r;
                try
                {
                    r = bootDevice(dev, timeout_ms * 1ms);
                }
                catch (std::exception&)
                {
                    local.build_fail++;
                    continue;
                }

                switch (r)
                {
                    case Reached::OP:        { local.op++;        break; }
                    case Reached::SAFE_OP:   { local.safe_op++;   failures += "SAFE_OP-ONLY " + fname + " | " + dev.type + "\n"; break; }
                    case Reached::PRE_OP:    { local.pre_op++;    failures += "PRE_OP-ONLY  " + fname + " | " + dev.type + "\n"; break; }
                    case Reached::INIT_FAIL: { local.init_fail++; failures += "INIT-FAIL    " + fname + " | " + dev.type + "\n"; break; }
                }
            }

            LockGuard lock(mutex);
            global.op         += local.op;
            global.safe_op    += local.safe_op;
            global.pre_op     += local.pre_op;
            global.init_fail  += local.init_fail;
            global.build_fail += local.build_fail;
            global.total      += local.total;
            fputs(failures.c_str(), stdout);
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < num_threads; ++i)
    {
        pool.emplace_back(worker);
    }
    for (auto& t : pool)
    {
        t.join();
    }

    printf("\n=== BOOT SUMMARY (%d threads) ===\ndevices=%d OP=%d SAFE_OP=%d PRE_OP=%d INIT_FAIL=%d build_fail=%d\n",
           num_threads, global.total, global.op, global.safe_op, global.pre_op, global.init_fail, global.build_fail);
    return 0;
}
