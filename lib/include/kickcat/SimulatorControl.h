#ifndef KICKCAT_SIMULATOR_CONTROL_H
#define KICKCAT_SIMULATOR_CONTROL_H

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "kickcat/LockedRing.h"
#include "kickcat/OS/SharedMemory.h"

namespace kickcat::sim
{
    // Per-command payloads, decoupled from the envelope below: the message type
    // selects which one is live in the union. POD so the message copies trivially
    // through the ring. A new command adds a payload struct here and a union member.
    struct SetLink
    {
        uint16_t node_a;
        uint16_t node_b;
        uint8_t  up;      // 1: heal, 0: break
    };

    // Inject zero-mean jitter on a node's reported DC system time (EmulatedESC::
    // setClockJitter). Lets the host stress the master's soft PLL. 0 disables.
    struct SetClockJitter
    {
        uint16_t node;
        int64_t  amplitude_ns;
    };

    union ControlPayload
    {
        SetLink        set_link;
        SetClockJitter set_clock_jitter;
    };

    // Out-of-band control bus for network_simulator, off the EtherCAT wire so frame
    // routing stays pure. The envelope carries only the type tag; the payload is
    // command-specific.
    struct ControlCommand
    {
        enum class Type : uint16_t
        {
            SetLink,
            SetClockJitter,
        };

        Type           type;
        ControlPayload payload;
    };

    // Acknowledgement of a command: the echoed argument plus the outcome.
    struct SetLinkAck
    {
        SetLink link;
        uint8_t ok;    // 0: command rejected (bad arguments / unknown type)
    };

    struct SetClockJitterAck
    {
        SetClockJitter cmd;
        uint8_t        ok;
    };

    // Frame-timing window the simulator emits unsolicited (one per N frames).
    // Times are nanoseconds over the last `window` frames.
    struct SimStats
    {
        uint64_t window;
        uint64_t min_ns;
        uint64_t max_ns;
        uint64_t avg_ns;
    };

    union EventPayload
    {
        SetLinkAck        set_link_ack;
        SetClockJitterAck set_clock_jitter_ack;
        SimStats          stats;
    };

    // Everything the simulator sends back to the host travels on one return stream:
    // command acks AND unsolicited events (frame stats today; more later). The host
    // drains the stream and dispatches on `type`. Adding an event kind means a new
    // Type tag and an EventPayload member -- no new channel.
    struct ControlEvent
    {
        enum class Type : uint16_t
        {
            SetLinkAck,
            SetClockJitterAck,
            FrameStats,
        };

        Type         type;
        EventPayload payload;
    };

    // The messages are byte-copied through a shared-memory ring.
    static_assert(std::is_trivially_copyable_v<ControlCommand>);
    static_assert(std::is_trivially_copyable_v<ControlEvent>);
    static_assert(std::is_trivially_copyable_v<SimStats>);

    // Shared-memory transport: the segment holds a small header plus the POD ring
    // Contexts; each side wraps them with its own LockedRing whose pointers are
    // valid only in that mapping (never copy a wrapper across the fork). The
    // creator init()s the rings and stamps the header last; a peer attach()es and
    // refuses a segment that is not stamped or whose layout differs (stale name,
    // version skew, or attach-before-create).
    class ControlChannel
    {
    public:
        static constexpr uint32_t RING_SIZE = 64;   // power of two
        using CommandRing = LockedRing<ControlCommand, RING_SIZE>;
        using EventRing   = LockedRing<ControlEvent,   RING_SIZE>;

        ControlChannel()                                 = default;
        ControlChannel(ControlChannel const&)            = delete;
        ControlChannel& operator=(ControlChannel const&) = delete;
        ControlChannel(ControlChannel&&)                 = delete;
        ControlChannel& operator=(ControlChannel&&)      = delete;

        void create(std::string const& name) { open(name, true);  }
        void attach(std::string const& name) { open(name, false); }

        bool sendCommand(ControlCommand const& cmd) { return commands_->push(cmd); }
        bool nextCommand(ControlCommand& out)       { return commands_->tryPop(out); }
        bool sendEvent(ControlEvent const& e)       { return events_->push(e); }
        bool nextEvent(ControlEvent& out)           { return events_->tryPop(out); }

        bool eventSpaceAvailable() { return events_->size() < RING_SIZE; }

    private:
        static constexpr uint32_t MAGIC = 0x53494d43;   // 'SIMC'

        struct Storage
        {
            uint32_t             magic;
            uint32_t             layout_size;
            CommandRing::Context commands;
            EventRing::Context   events;
        };

        void open(std::string const& name, bool init)
        {
            if (init)
            {
                SharedMemory::unlink(name);   // drop a stale segment from a crashed run
            }
            shm_.open(name, sizeof(Storage));
            Storage* storage = reinterpret_cast<Storage*>(shm_.address());
            if (init)
            {
                std::memset(storage, 0, sizeof(Storage));
            }
            commands_ = std::make_unique<CommandRing>(storage->commands);
            events_   = std::make_unique<EventRing>(storage->events);
            if (init)
            {
                commands_->init();
                events_->init();
                storage->layout_size = sizeof(Storage);
                storage->magic       = MAGIC;   // stamp last: a peer only sees it once ready
            }
            else if (storage->magic != MAGIC or storage->layout_size != sizeof(Storage))
            {
                throw std::runtime_error("control channel " + name + " is not initialised or has a layout mismatch");
            }
        }

        SharedMemory                 shm_{};
        std::unique_ptr<CommandRing> commands_;
        std::unique_ptr<EventRing>   events_;
    };

    // Master-side producer: creates and owns the channel.
    class SimulatorControlClient
    {
    public:
        void open(std::string const& name) { channel_.create(name); }

        bool breakLink(uint16_t a, uint16_t b) { return setLink(a, b, 0); }
        bool healLink(uint16_t a, uint16_t b)  { return setLink(a, b, 1); }

        // Inject zero-mean DC-clock jitter on a node (0 disables).
        bool setClockJitter(uint16_t node, int64_t amplitude_ns)
        {
            ControlCommand cmd{};
            cmd.type = ControlCommand::Type::SetClockJitter;
            cmd.payload.set_clock_jitter = {node, amplitude_ns};
            return send(cmd);
        }

        bool send(ControlCommand const& cmd) { return channel_.sendCommand(cmd); }
        bool nextEvent(ControlEvent& out)    { return channel_.nextEvent(out); }

    private:
        bool setLink(uint16_t a, uint16_t b, uint8_t up)
        {
            ControlCommand cmd{};
            cmd.type = ControlCommand::Type::SetLink;
            cmd.payload.set_link = {a, b, up};
            return send(cmd);
        }

        ControlChannel channel_;
    };
}

#endif
