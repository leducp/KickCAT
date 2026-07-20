// Distributed Clocks end-to-end tests: a real master Bus driving emulated slaves
// whose local clocks (drift injection, time control loop) derive from the mocked
// clock, so propagation delay measurement, offset compensation and drift
// convergence are fully deterministic.
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mocks/EmulatedNetworkHelpers.h"
#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/SocketNull.h"

using namespace kickcat;

namespace
{
    // Smallest EEPROM a Bus::init() boot needs: device emulation on (the ESC
    // mirrors AL_CONTROL into AL_STATUS), no mailbox, no PDO categories.
    std::vector<uint16_t> minimalEeprom()
    {
        std::vector<uint16_t> image(64, 0);
        image[0] = 0x0100;  // word 0 high byte -> ESC configuration: device emulation
        return image;
    }

    uint32_t timeDiffRaw(EmulatedESC& esc)
    {
        uint32_t raw = 0;
        esc.read(reg::DC_SYSTEM_TIME_DIFF, &raw, sizeof(raw));
        return raw;
    }

    uint32_t diffMagnitude(uint32_t raw)
    {
        return raw & 0x7FFFFFFF;
    }

    auto noop = [](DatagramState const&) {};


    // Decorator over the segment socket: swallows outgoing frames carrying a datagram
    // matching (command, register offset), to inject frame loss in a specific bus phase.
    class DropFilterSocket final : public AbstractSocket
    {
    public:
        explicit DropFilterSocket(std::shared_ptr<AbstractSocket> inner)
            : inner_(std::move(inner))
        {
        }

        void open(std::string const& iface) override { inner_->open(iface); }
        void setTimeout(nanoseconds timeout) override { inner_->setTimeout(timeout); }
        void close() noexcept override { inner_->close(); }
        int32_t read(void* data, int32_t size) override { return inner_->read(data, size); }

        int32_t write(void const* data, int32_t size) override
        {
            if ((drop_remaining != 0) and matches(data, size))
            {
                if (drop_remaining > 0)
                {
                    --drop_remaining;
                }
                return size;    // swallowed: the frame never reaches the segment
            }
            return inner_->write(data, size);
        }

        Command  target_command{Command::NOP};
        uint16_t target_offset{0};
        int      drop_remaining{0};     // -1: drop forever

    private:
        bool matches(void const* data, int32_t size)
        {
            Frame frame(data, size);
            frame.resetContext();
            while (true)
            {
                auto [header, payload, wkc] = frame.peekDatagram();
                if (header == nullptr)
                {
                    return false;
                }
                if ((header->command == target_command) and
                    (static_cast<uint16_t>(header->address >> 16) == target_offset))
                {
                    return true;
                }
            }
        }

        std::shared_ptr<AbstractSocket> inner_;
    };


    // Bus over a single in-process segment (LoopbackSocket).
    class DcBusTest : public testing::Test
    {
    protected:
        void createSlaves(size_t n, nanoseconds forwarding_delay)
        {
            resetMockClock();
            slaves_ = makeSlaves(n);
            for (auto& s : slaves_)
            {
                s->loadEeprom(minimalEeprom());
                s->setForwardingDelay(forwarding_delay);
            }
            socket_ = std::make_shared<LoopbackSocket>(pointers(slaves_), [](){});
            filter_ = std::make_shared<DropFilterSocket>(socket_);
            link_   = std::make_shared<Link>(filter_, std::make_shared<SocketNull>(), [](){});
            link_->setTimeout(2ms);
            bus_ = std::make_unique<Bus>(link_);
            bus_->configureWaitLatency(0ns, 0ns);
        }

        void initBus()
        {
            bus_->init();
        }

        std::vector<std::unique_ptr<EmulatedESC>> slaves_;
        std::shared_ptr<LoopbackSocket> socket_;
        std::shared_ptr<DropFilterSocket> filter_;
        std::shared_ptr<Link> link_;
        std::unique_ptr<Bus> bus_;
    };


    // Bus over a redundant ring (two PhysicalSockets), for the split-ring DC test.
    class DcRingTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            resetMockClock();
            slaves_ = makeSlaves(3);
            for (auto& s : slaves_)
            {
                s->loadEeprom(minimalEeprom());
            }
            net_ = std::make_unique<EmulatedNetwork>(pointers(slaves_));
            net_->setRedundancyInjection(2, 1); // close the ring on the tail slave

            socket_nominal_    = std::make_shared<PhysicalSocket>(*net_, NIC::NOMINAL);
            socket_redundancy_ = std::make_shared<PhysicalSocket>(*net_, NIC::REDUNDANCY);
            socket_nominal_->setPeer(socket_redundancy_.get());
            socket_redundancy_->setPeer(socket_nominal_.get());
            link_ = std::make_shared<Link>(socket_nominal_, socket_redundancy_, [](){});
            bus_ = std::make_unique<Bus>(link_);
            bus_->configureWaitLatency(0ns, 0ns);
        }

        std::vector<std::unique_ptr<EmulatedESC>> slaves_;
        std::unique_ptr<EmulatedNetwork> net_;
        std::shared_ptr<PhysicalSocket> socket_nominal_;
        std::shared_ptr<PhysicalSocket> socket_redundancy_;
        std::shared_ptr<Link> link_;
        std::unique_ptr<Bus> bus_;
    };
}


TEST_F(DcBusTest, emulated_slaves_advertise_dc_support)
{
    createSlaves(3, 100ns);
    initBus();

    // DC support comes from the ESC feature register (0x0008), fetched by
    // Bus::fetchESC() - no EEPROM flag involved.
    for (auto& slave : bus_->slaves())
    {
        EXPECT_TRUE(slave.isDCSupport());
    }
}


TEST_F(DcBusTest, propagation_delay_line_topology)
{
    createSlaves(3, 100ns);
    initBus();
    bus_->enableDC(1ms, 0ns, 0ns);

    // Line with a 100ns store-and-forward delay per hop: each slave latches the
    // frame 100ns after its parent, and the measured delay must match exactly.
    auto& slaves = bus_->slaves();
    ASSERT_EQ(3u, slaves.size());
    EXPECT_EQ(0ns,   slaves[0].delay);
    EXPECT_EQ(100ns, slaves[1].delay);
    EXPECT_EQ(200ns, slaves[2].delay);

    // The measured delay is what landed in DC_SYSTEM_TIME_DELAY (0x928).
    for (size_t i = 0; i < slaves_.size(); ++i)
    {
        uint32_t delay_reg = 0;
        slaves_[i]->read(reg::DC_SYSTEM_TIME_DELAY, &delay_reg, sizeof(delay_reg));
        EXPECT_EQ(static_cast<uint32_t>(slaves[i].delay.count()), delay_reg) << "slave " << i;
    }
}


TEST_F(DcBusTest, propagation_delay_tree_topology)
{
    createSlaves(4, 100ns);

    // node0 is a junction: branch on port3 -> node2 -> node3, branch on port1 -> node1.
    // Physical processing order (port order 0->3->1->2): 0, 2, 3, 1.
    auto& net = socket_->network();
    net.connect(0, 3, 2, 0);
    net.connect(0, 1, 1, 0);
    net.connect(2, 1, 3, 0);
    net.setInjection(0, 0);

    initBus();
    bus_->enableDC(1ms, 0ns, 0ns);

    // Bus order is processing order: [node0, node2, node3, node1]. Latch offsets
    // with 100ns per hop: node2 at 100, node3 at 200; node1 only sees the frame
    // after the round trip through the port-3 branch (400ns) plus node0's
    // forwarding: 500.
    auto& slaves = bus_->slaves();
    ASSERT_EQ(4u, slaves.size());
    EXPECT_EQ(0ns,   slaves[0].delay);
    EXPECT_EQ(100ns, slaves[1].delay);
    EXPECT_EQ(200ns, slaves[2].delay);
    EXPECT_EQ(500ns, slaves[3].delay);
}


TEST_F(DcBusTest, enable_dc_configures_time_loop_and_sync0)
{
    createSlaves(3, 100ns);
    initBus();

    nanoseconds const cycle = 1ms;
    nanoseconds const shift = 500us;
    nanoseconds const delay = 100ms;
    nanoseconds const ret = bus_->enableDC(cycle, shift, delay);

    for (size_t i = 0; i < slaves_.size(); ++i)
    {
        auto& esc = *slaves_[i];

        // Time control loop setup: speed counter start then filter depths.
        uint16_t speed_start = 0;
        esc.read(reg::DC_SPEED_CNT_START, &speed_start, sizeof(speed_start));
        EXPECT_EQ(0x1000, speed_start);

        uint16_t filters = 0;
        esc.read(reg::DC_TIME_FILTER, &filters, sizeof(filters));
        EXPECT_EQ(0x0c00, filters);  // diff filter depth 0, speed counter filter depth 12

        // System time offset written (and consumed by the local clock): the local
        // copy of the system time of every slave tracks the master's clock.
        nanoseconds const master_now = since_ecat_epoch();
        nanoseconds const system_time = esc.localSystemTime();
        EXPECT_LT(abs((system_time - master_now).count()), duration_cast<nanoseconds>(20ms).count())
            << "slave " << i;

        // SYNC0: cyclic generation activated, start time in the future.
        uint8_t activation = 0;
        esc.read(reg::DC_SYNC_ACTIVATION, &activation, sizeof(activation));
        EXPECT_EQ(0x3, activation);

        uint32_t cycle_reg = 0;
        esc.read(reg::DC_SYNC0_CYCLE_TIME, &cycle_reg, sizeof(cycle_reg));
        EXPECT_EQ(static_cast<uint32_t>(cycle.count()), cycle_reg);

        uint64_t start_time = 0;
        esc.read(reg::DC_START_TIME, &start_time, sizeof(start_time));
        EXPECT_GT(static_cast<int64_t>(start_time), esc.localSystemTime().count()) << "slave " << i;

        // Pulse length (0x0982) is r/- from ECAT: the master must not touch it,
        // it stays at its reset value (EEPROM-owned on real hardware).
        uint16_t pulse_length = 0xFFFF;
        esc.read(reg::DC_SYNC_PULSE_LENGTH, &pulse_length, sizeof(pulse_length));
        EXPECT_EQ(0, pulse_length);

        // The returned sync point is the DC start grid projected into the Timer's monotonic now()
        // domain: it sits near now(), not at wall/ECAT-epoch scale (the pre-split value that made
        // Timer sleep ~decades). The soft PLL trims the sub-cycle residual afterwards.
        if (i == 0)
        {
            nanoseconds const mono = now();
            EXPECT_LT(abs((ret - mono).count()), duration_cast<nanoseconds>(100ms).count());
            EXPECT_LT(ret.count(), to_unix_epoch(0ns).count());
        }
    }

    EXPECT_TRUE(bus_->isDCSynchronized(10us));
}


TEST_F(DcBusTest, is_dc_synchronized_reads_per_slave_drift)
{
    // 3 DC slaves besides the reference: regression for the callback-captured
    // vector reallocation (with fewer than 3 the old bug never reallocated).
    createSlaves(4, 100ns);
    initBus();
    bus_->enableDC(1ms, 0ns, 0ns);

    // Plant distinct sign-magnitude values straight into 0x92C (PDI write does not
    // feed the time loop): readback must attribute each value to its own slave.
    uint32_t raw = 100;                 // slave 1: 100ns, local < received
    slaves_[1]->write(reg::DC_SYSTEM_TIME_DIFF, &raw, sizeof(raw));
    raw = 0x80000000u | 200;            // slave 2: 200ns, local >= received
    slaves_[2]->write(reg::DC_SYSTEM_TIME_DIFF, &raw, sizeof(raw));
    raw = 5000;                         // slave 3: 5us
    slaves_[3]->write(reg::DC_SYSTEM_TIME_DIFF, &raw, sizeof(raw));

    EXPECT_TRUE(bus_->isDCSynchronized(6us));
    EXPECT_FALSE(bus_->isDCSynchronized(4us));   // only slave 3 exceeds
    EXPECT_FALSE(bus_->isDCSynchronized(150ns)); // slaves 2 and 3 exceed
    EXPECT_TRUE(bus_->isDCSynchronized(5us));    // threshold is strict (> rejects)
}


TEST_F(DcBusTest, is_dc_synchronized_lost_frame_returns_false_without_throwing)
{
    createSlaves(4, 100ns);
    initBus();
    bus_->enableDC(1ms, 0ns, 0ns);
    EXPECT_TRUE(bus_->isDCSynchronized(10us));

    // Break the segment: slaves behind the cut no longer answer (wkc 0), which is
    // a per-datagram error, not an exception.
    socket_->network().setLinkState(1, 2, false);

    bool result = true;
    EXPECT_NO_THROW(result = bus_->isDCSynchronized(10us));
    EXPECT_FALSE(result);
}


TEST_F(DcBusTest, drift_compensation_converges_with_injected_drift)
{
    createSlaves(4, 100ns);

    // Asymmetric per-slave oscillator deviations: without the time control loop the
    // local clocks would diverge from the reference without bound.
    slaves_[1]->setClockDrift(80.0);
    slaves_[2]->setClockDrift(-120.0);
    slaves_[3]->setClockDrift(40.0);

    initBus();
    bus_->enableDC(1ms, 0ns, 0ns);

    // Static drift compensation (15000 FRMW frames in enableDC) absorbed the
    // injected drift: every slave tracks the reference within the loop's
    // steady-state error.
    EXPECT_TRUE(bus_->isDCSynchronized(10us, true));

    // Step disturbance: shift slave 1's system time by 50us through its offset
    // register. The next cycles must slew it back toward the reference.
    int64_t offset = 0;
    slaves_[1]->read(reg::DC_SYSTEM_TIME_OFFSET, &offset, sizeof(offset));
    offset += 50000;
    slaves_[1]->write(reg::DC_SYSTEM_TIME_OFFSET, &offset, sizeof(offset));

    bus_->processDataWrite(noop);   // cyclic path: appends the drift datagrams
    uint32_t const initial = diffMagnitude(timeDiffRaw(*slaves_[1]));
    EXPECT_GT(initial, 40000u);

    for (int i = 0; i < 30; ++i)
    {
        bus_->processDataWrite(noop);
    }

    uint32_t const final_mag = diffMagnitude(timeDiffRaw(*slaves_[1]));
    EXPECT_LT(final_mag, initial);
    EXPECT_LT(final_mag, 5000u);
    EXPECT_TRUE(bus_->isDCSynchronized(10us));
}


TEST_F(DcBusTest, enable_dc_retries_lost_start_time_read)
{
    createSlaves(3, 100ns);
    initBus();

    // The FPRD of DC_SYSTEM_TIME (SYNC0 start time computation) only happens in this
    // phase: lose it twice, the bounded retry must absorb the losses.
    filter_->target_command = Command::FPRD;
    filter_->target_offset  = reg::DC_SYSTEM_TIME;
    filter_->drop_remaining = 2;

    EXPECT_NO_THROW(bus_->enableDC(1ms, 0ns, 100ms));
    EXPECT_EQ(0, filter_->drop_remaining);

    // The start time was computed from an actual readback, not from zero.
    uint64_t start_time = 0;
    slaves_[0]->read(reg::DC_START_TIME, &start_time, sizeof(start_time));
    EXPECT_GT(static_cast<int64_t>(start_time), slaves_[0]->localSystemTime().count());
}


TEST_F(DcBusTest, enable_dc_throws_when_start_time_read_keeps_failing)
{
    createSlaves(3, 100ns);
    initBus();

    filter_->target_command = Command::FPRD;
    filter_->target_offset  = reg::DC_SYSTEM_TIME;
    filter_->drop_remaining = -1;   // every attempt is lost

    EXPECT_THROW(bus_->enableDC(1ms, 0ns, 0ns), ErrorDatagram);
}


TEST_F(DcRingTest, split_ring_tail_segment_keeps_tracking_master_time)
{
    // Drifting tail slaves: after the split they are cut from the reference clock
    // (slave 0) and only see the FRMW copy injected on the redundancy NIC. The
    // FRMW prefill (master time) must keep them disciplined to the master clock
    // instead of slamming their system time toward zero.
    slaves_[1]->setClockDrift(80.0);
    slaves_[2]->setClockDrift(-120.0);

    bus_->init();
    bus_->enableDC(1ms, 0ns, 0ns);
    EXPECT_TRUE(bus_->isDCSynchronized(10us));

    net_->setLinkState(0, 1, false);
    ASSERT_FALSE(net_->ringIntact());

    // The switch from reference-slave time to master prefill time is a millisecond
    // scale step under the mocked clock (1ms per now() call); the loop
    // slews it away at the speed-counter-start bound (~4us per cycle).
    for (int i = 0; i < 400; ++i)
    {
        bus_->processDataWrite(noop);
    }

    nanoseconds const master_now = since_ecat_epoch();
    for (size_t i = 1; i < slaves_.size(); ++i)
    {
        // Still on master time (a zero payload would leave an offset of ~26 years
        // of ecat epoch, and a saturated 0x92C of ~2.1s).
        EXPECT_LT(abs((slaves_[i]->localSystemTime() - master_now).count()),
                  duration_cast<nanoseconds>(20ms).count()) << "slave " << i;
        EXPECT_LT(diffMagnitude(timeDiffRaw(*slaves_[i])), 10000u) << "slave " << i;
    }

    // The master-side view agrees: both frame copies are routed each cycle and the
    // tail slaves still report synchronized.
    EXPECT_TRUE(bus_->isDCSynchronized(10us));
}
