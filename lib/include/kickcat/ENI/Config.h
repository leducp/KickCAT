#ifndef KICKCAT_ENI_CONFIG_H
#define KICKCAT_ENI_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kickcat/CoE/protocol.h"
#include "kickcat/ESI/Device.h"
#include "kickcat/OS/Time.h"
#include "kickcat/protocol.h"

namespace kickcat::ENI
{
    namespace requirement
    {
        enum Type : uint8_t
        {
            NONE,
            FRAME,  // command needs its own frame
            CYCLE,  // command needs its own cycle
        };
    }

    // Only the ETG.2100 default comparison (EQ): the reply must equal data once masked.
    struct Validate
    {
        std::vector<uint8_t> data;
        std::vector<uint8_t> mask;      // ANDed with the reply; bytes past its end are compared whole
        milliseconds timeout{0};

        // Compare the first data.size() bytes of a reply. False when the reply is shorter than data.
        bool matches(uint8_t const* reply, std::size_t reply_size) const;
    };

    // An EtherCAT datagram as ENI InitCmds and cyclic commands write it.
    struct Datagram
    {
        std::string comment;
        Command cmd = Command::NOP;
        uint32_t address = 0;           // as on the wire: logical for LRD/LWR/LRW, createAddress(adp, ado) otherwise
        std::vector<uint8_t> data;      // <DataLength> expands to zeroes
        std::optional<uint16_t> expected_wkc;   // <Cnt>; unchecked when absent
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

    struct CoEInitCmd : CoE::InitCmd
    {
        milliseconds timeout{0};
        uint8_t ccs = 0;                // samples use 1 for download (write), spec table says otherwise
        bool fixed = false;
        bool disabled = false;
    };

    struct Pdo
    {
        uint16_t index = 0;
        std::string name;
        bool fixed = false;
        bool mandatory = false;
        std::optional<uint8_t> sm;
        std::vector<uint16_t> exclude;
        std::vector<CoE::PdoMappingEntry> entries;
    };

    struct SyncManagerSettings : ESI::SmInfo
    {
        uint8_t index = 0;              // N of <SmN>
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
        uint16_t protocols = 0;         // OR of eeprom::MailboxProtocol
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

    struct Master
    {
        std::string name;
        std::vector<InitCmd> init_cmds;
    };

    struct CyclicCmd : Datagram
    {
        uint8_t states = 0;             // OR of State::INIT, PRE_OP, SAFE_OP and OPERATIONAL
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
        std::optional<microseconds> cycle_time;     // period the frames were planned for; nullopt leaves it to the application
        std::vector<Frame> frames;
    };

    struct Variable
    {
        std::string name;
        CoE::DataType data_type = CoE::DataType::UNKNOWN;
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
