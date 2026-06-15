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

    union ControlPayload
    {
        SetLink set_link;
    };

    // Out-of-band control bus for network_simulator, off the EtherCAT wire so frame
    // routing stays pure. The envelope carries only the type tag; the payload is
    // command-specific.
    struct ControlCommand
    {
        enum class Type : uint16_t
        {
            SetLink,
        };

        Type           type;
        ControlPayload payload;
    };

    struct ControlResponse
    {
        ControlCommand::Type type;
        uint8_t              ok;        // 0: command rejected (bad arguments / unknown type)
        ControlPayload       payload;   // result, valid only when ok == 1
    };

    // The messages are byte-copied through a shared-memory ring.
    static_assert(std::is_trivially_copyable_v<ControlCommand>);
    static_assert(std::is_trivially_copyable_v<ControlResponse>);

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
        using CommandRing  = LockedRing<ControlCommand,  RING_SIZE>;
        using ResponseRing = LockedRing<ControlResponse, RING_SIZE>;

        ControlChannel()                                 = default;
        ControlChannel(ControlChannel const&)            = delete;
        ControlChannel& operator=(ControlChannel const&) = delete;
        ControlChannel(ControlChannel&&)                 = delete;
        ControlChannel& operator=(ControlChannel&&)      = delete;

        void create(std::string const& name) { open(name, true);  }
        void attach(std::string const& name) { open(name, false); }

        bool sendCommand(ControlCommand const& cmd) { return commands_->push(cmd); }
        bool nextCommand(ControlCommand& out)       { return commands_->tryPop(out); }
        bool sendResponse(ControlResponse const& r) { return responses_->push(r); }
        bool nextResponse(ControlResponse& out)     { return responses_->tryPop(out); }

        // Command and response rings are the same size and used 1:1, so room for a
        // response guarantees the matching command's ack will fit.
        bool responseSpaceAvailable() { return responses_->size() < RING_SIZE; }

    private:
        static constexpr uint32_t MAGIC = 0x53494d43;   // 'SIMC'

        struct Storage
        {
            uint32_t              magic;
            uint32_t              layout_size;
            CommandRing::Context  commands;
            ResponseRing::Context responses;
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
            commands_  = std::make_unique<CommandRing>(storage->commands);
            responses_ = std::make_unique<ResponseRing>(storage->responses);
            if (init)
            {
                commands_->init();
                responses_->init();
                storage->layout_size = sizeof(Storage);
                storage->magic       = MAGIC;   // stamp last: a peer only sees it once ready
            }
            else if (storage->magic != MAGIC or storage->layout_size != sizeof(Storage))
            {
                throw std::runtime_error("control channel " + name + " is not initialised or has a layout mismatch");
            }
        }

        SharedMemory                  shm_{};
        std::unique_ptr<CommandRing>  commands_;
        std::unique_ptr<ResponseRing> responses_;
    };

    // Master-side producer: creates and owns the channel.
    class SimulatorControlClient
    {
    public:
        void open(std::string const& name) { channel_.create(name); }

        bool breakLink(uint16_t a, uint16_t b) { return setLink(a, b, 0); }
        bool healLink(uint16_t a, uint16_t b)  { return setLink(a, b, 1); }

        bool send(ControlCommand const& cmd)     { return channel_.sendCommand(cmd); }
        bool nextResponse(ControlResponse& out)  { return channel_.nextResponse(out); }

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
