#ifndef KICKCAT_ENI_CONFIG_H
#define KICKCAT_ENI_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kickcat/OS/Time.h"
#include "kickcat/protocol.h"

namespace kickcat::ENI
{
    namespace transition
    {
        enum Type : uint16_t
        {
            NONE = 0,
            IP   = 1 << 0,   // Init      -> PreOp
            PS   = 1 << 1,   // PreOp     -> SafeOp
            PI   = 1 << 2,   // PreOp     -> Init
            SP   = 1 << 3,   // SafeOp    -> PreOp
            SO   = 1 << 4,   // SafeOp    -> Op
            SI   = 1 << 5,   // SafeOp    -> Init
            OS   = 1 << 6,   // Op        -> SafeOp
            OP   = 1 << 7,   // Op        -> PreOp
            OI   = 1 << 8,   // Op        -> Init
            IB   = 1 << 9,   // Init      -> Bootstrap
            BI   = 1 << 10,  // Bootstrap -> Init
            II   = 1 << 11,  // Init      -> Init
            PP   = 1 << 12,  // PreOp     -> PreOp
            SS   = 1 << 13,  // SafeOp    -> SafeOp
            PO   = 1 << 14,  // PreOp     -> Op (XSD only)
        };
        char const* toString(Type t);
        Type fromString(std::string_view text);   // throws std::invalid_argument
        Type between(State from, State to);       // NONE if no ENI transition matches
    }
    using Transitions = uint16_t;   // OR of transition::Type

    namespace requirement
    {
        enum Type : uint8_t
        {
            NONE,
            FRAME,  // command needs its own frame
            CYCLE,  // command needs its own cycle
        };
    }

    namespace compare
    {
        enum Type : uint8_t
        {
            EQ,
            NOT_EQ,
            EQ_OR_G,
            EQ_OR_L,
            G,
            L,
            NONE,
        };
    }

    struct Validate
    {
        std::vector<uint8_t> data;
        std::vector<uint8_t> mask;      // ANDed with the reply; bytes past its end are compared whole
        milliseconds timeout{0};
        compare::Type type = compare::EQ;
        bool is_signed = false;

        // Compare the first data.size() bytes of a reply, as little-endian values of that width.
        // False when the reply is shorter than data.
        bool matches(uint8_t const* reply, std::size_t reply_size) const;
    };

    // An EtherCAT datagram as ENI InitCmds and cyclic commands write it.
    struct Datagram
    {
        std::string comment;
        Command cmd = Command::NOP;
        uint16_t adp = 0;
        uint16_t ado = 0;
        std::optional<uint32_t> logical_address;
        std::vector<uint8_t> data;      // <DataLength> expands to zeroes
        std::optional<uint16_t> wkc;

        uint32_t address() const;       // logical address, or createAddress(adp, ado)
    };

    // ECatCmdType: raw EtherCAT datagram sent during a transition.
    struct InitCmd : Datagram
    {
        Transitions transitions = 0;
        bool before_slave = false;
        requirement::Type frame_requirement = requirement::NONE;
        uint16_t retries = 0;
        std::optional<Validate> validate;
        milliseconds timeout{0};        // only when there is no <Validate>
    };

    struct CoEInitCmd
    {
        Transitions transitions = 0;
        std::string comment;
        milliseconds timeout{0};
        uint8_t ccs = 0;                // samples use 1 for download (write), spec table says otherwise
        uint16_t index = 0;
        uint8_t subindex = 0;
        std::vector<uint8_t> data;
        bool complete_access = false;
        bool fixed = false;
        bool disabled = false;
    };

    struct PdoEntry
    {
        uint16_t index = 0;
        uint8_t subindex = 0;
        uint16_t bitlen = 0;
        std::string name;
        std::string data_type;
    };

    struct Pdo
    {
        uint16_t index = 0;
        std::string name;
        bool fixed = false;
        bool mandatory = false;
        std::optional<uint8_t> sm;
        std::vector<uint16_t> exclude;
        std::vector<PdoEntry> entries;
    };

    struct SyncManagerSettings
    {
        uint8_t index = 0;              // N of <SmN>
        SyncManager::Type type = SyncManager::Unused;
        uint16_t min_size = 0;
        uint16_t max_size = 0;
        uint16_t default_size = 0;
        uint16_t start_address = 0;
        uint8_t control_byte = 0;
        bool enable = false;
        std::vector<uint16_t> pdos;
    };

    struct ProcessDataRange
    {
        uint32_t bit_start = 0;
        uint32_t bit_length = 0;
        uint16_t sm_mask = 0;           // @Sm0..@Sm15
    };

    struct ProcessData
    {
        std::vector<ProcessDataRange> send;   // outputs
        std::vector<ProcessDataRange> recv;   // inputs
        std::vector<SyncManagerSettings> sms;
        std::vector<Pdo> rx_pdos;
        std::vector<Pdo> tx_pdos;
    };

    namespace protocol
    {
        enum Type : uint8_t
        {
            AoE = 1 << 0,
            EoE = 1 << 1,
            CoE = 1 << 2,
            FoE = 1 << 3,
            SoE = 1 << 4,
            VoE = 1 << 5,
        };
    }

    struct MailboxArea
    {
        uint16_t start = 0;
        uint16_t length = 0;
    };

    struct Mailbox
    {
        MailboxArea send;               // master -> slave
        MailboxArea recv;               // slave -> master
        std::optional<milliseconds> poll_time;
        std::optional<uint32_t> status_bit_addr;
        std::optional<MailboxArea> bootstrap_send;
        std::optional<MailboxArea> bootstrap_recv;
        uint8_t protocols = 0;          // OR of protocol::Type
        std::vector<CoEInitCmd> coe_init_cmds;
        uint32_t unsupported_init_cmds = 0;   // AoE/EoE/FoE/SoE/VoE InitCmds, not modelled
    };

    struct PreviousPort
    {
        bool selected = false;
        uint8_t port = 0;               // A=0 .. D=3
        std::optional<uint16_t> phys_addr;
    };

    struct Dc
    {
        bool potential_reference_clock = false;
        bool reference_clock = false;
        std::optional<nanoseconds> cycle_time0;
        std::optional<nanoseconds> cycle_time1;
        std::optional<nanoseconds> shift_time;
    };

    // Explicit device identification: the slave register at ado holds value.
    struct Identification
    {
        uint16_t ado = 0;
        uint16_t value = 0;
    };

    struct SlaveInfo
    {
        std::string name;
        std::optional<uint16_t> phys_addr;
        std::optional<uint16_t> auto_inc_addr;
        std::optional<Identification> identification;
        uint32_t vendor_id = 0;
        uint32_t product_code = 0;
        uint32_t revision_no = 0;
        std::optional<uint32_t> serial_no;
    };

    struct Slave
    {
        SlaveInfo info;
        ProcessData process_data;
        std::optional<Mailbox> mailbox;
        std::vector<InitCmd> init_cmds;
        std::vector<PreviousPort> previous_ports;
        std::optional<Dc> dc;
    };

    struct MailboxStates
    {
        uint32_t start_addr = 0;
        uint16_t count = 0;
    };

    struct Master
    {
        std::string name;
        std::array<uint8_t, 6> destination{};
        std::array<uint8_t, 6> source{};
        std::vector<uint8_t> ether_type;    // raw hexBinary as written
        std::optional<MailboxStates> mailbox_states;
        std::vector<InitCmd> init_cmds;
    };

    namespace state
    {
        enum Type : uint8_t
        {
            NONE   = 0,
            INIT   = 1 << 0,
            PREOP  = 1 << 1,
            SAFEOP = 1 << 2,
            OP     = 1 << 3,
        };
        Type from(State s);             // NONE if s has no cyclic state
    }

    struct CyclicCmd : Datagram
    {
        uint8_t states = 0;             // OR of state::Type
        std::optional<uint32_t> input_offs;
        std::optional<uint32_t> output_offs;
    };

    struct Frame
    {
        std::string comment;
        std::vector<CyclicCmd> cmds;
    };

    struct Cyclic
    {
        std::string comment;
        std::optional<microseconds> cycle_time;
        std::optional<uint8_t> priority;
        std::string task_id;
        std::vector<Frame> frames;
    };

    struct Variable
    {
        std::string name;
        std::string data_type;
        uint32_t bit_size = 0;
        uint32_t bit_offs = 0;
    };

    struct ProcessImageArea
    {
        uint32_t byte_size = 0;
        std::vector<Variable> variables;
    };

    struct ProcessImage
    {
        ProcessImageArea inputs;
        ProcessImageArea outputs;
    };

    struct Config
    {
        Master master;
        std::vector<Slave> slaves;
        std::vector<Cyclic> cyclic;
        std::optional<ProcessImage> process_image;
    };
}

#endif
