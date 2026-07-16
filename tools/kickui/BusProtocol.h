#ifndef KICKCAT_TOOLS_KICKUI_BUS_PROTOCOL_H
#define KICKCAT_TOOLS_KICKUI_BUS_PROTOCOL_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "kickcat/CoE/CiA/DS402/Drive.h"   // UnitConfig, ControlMode
#include "kickcat/CoE/OD.h"                // DataType, ObjectCode, Access
#include "kickcat/protocol.h"              // State

namespace kickcat::kickui
{
    // The data exchanged between the UI thread and the bus-owning actor:
    // plain value types, the discrete Event stream and the published snapshot.
    // No live Bus/Link types belong here.

    enum class SetpointSource { Manual, Jog, Generator };
    enum class Waveform       { Sine, Step, Triangle };

    // Snapshot of the controlled drive, published by the RT thread each cycle
    // and read by the UI thread.
    struct DriveFeedback
    {
        uint16_t status_word       = 0;
        uint16_t control_word      = 0;
        uint16_t error_code        = 0;
        int8_t   mode_display      = 0;
        int32_t  actual_pos_raw    = 0;
        int32_t  actual_vel_raw    = 0;
        int16_t  actual_torque_raw = 0;
        double   actual_pos        = 0.0;
        double   actual_vel        = 0.0;
        double   actual_torque     = 0.0;
        double   target            = 0.0;
        bool     enabled           = false;
        bool     faulted           = false;
        bool     operational       = false;
    };

    // One mapped PDO entry (which object/subindex, and its bit length).
    struct PdoEntry
    {
        uint16_t pdo   = 0;
        uint16_t index = 0;
        uint8_t  sub   = 0;
        uint8_t  bits  = 0;
    };

    // Per-drive bring-up parameters for operate(). PDO indices default to the
    // canonical 0x1600/0x1A00 but are overridable (Marvin uses 0x1601/0x1A01).
    // DC is a bus-wide setting (see setDcConfig), not per-drive.
    struct OperateConfig
    {
        CoE::CiA::DS402::UnitConfig           units;
        CoE::CiA::DS402::control::ControlMode  mode = CoE::CiA::DS402::control::POSITION_CYCLIC;
        uint16_t rx_pdo_map  = 0x1600;
        uint16_t tx_pdo_map  = 0x1A00;

        // How the INT8 mode entry is aligned to 16 bits in the PDO. Auto tries the
        // spec dummy pad then widens on a CoE abort; some drives (e.g. marvin's
        // CoolDrive) reject the dummy only at the SAFE_OP transition, which Auto
        // cannot catch, so they need WidenObject pinned explicitly.
        CoE::CiA::DS402::Drive::PaddingStyle padding = CoE::CiA::DS402::Drive::PaddingStyle::Auto;

        // Manual PDO mapping: when set, the bring-up writes these entries to the
        // rx_pdo_map/tx_pdo_map objects (CoE remap sequence) before createMapping
        // instead of relying on the slave's SII default. Each entry's pdo field is
        // ignored on apply (the owning object is rx_pdo_map/tx_pdo_map).
        bool                  manual_mapping = false;
        std::vector<PdoEntry> manual_rx;   // -> rx_pdo_map / 0x1C12
        std::vector<PdoEntry> manual_tx;   // -> tx_pdo_map / 0x1C13
    };

    // Result of one async SDO read/write; the UI polls `done`.
    struct SdoResult
    {
        std::atomic<bool>    done{false};
        bool                 ok = false;
        std::string          message;
        std::vector<uint8_t> data;
    };

    // One subindex of a RECORD/ARRAY object (its own description + value).
    struct OdEntry
    {
        uint8_t              subindex  = 0;
        CoE::DataType        data_type = CoE::DataType::UNKNOWN;
        uint16_t             access    = 0;  // CoE::Access bitmask
        std::string          name;
        std::vector<uint8_t> value;
        std::string          value_error;
    };

    // One object discovered via the SDO-Information service.
    struct OdObject
    {
        uint16_t             index        = 0;
        uint8_t              max_subindex = 0;
        CoE::ObjectCode      object_code  = CoE::ObjectCode::NIL;
        CoE::DataType        data_type    = CoE::DataType::UNKNOWN;
        uint16_t             access       = 0;  // CoE::Access bitmask of subindex 0 (from GetED)
        std::string          name;
        std::vector<uint8_t> value;        // sub0 value (object value for VAR, count for RECORD/ARRAY)
        std::string          value_error;  // abort/error text when a value read failed (surfaced to UI)
        std::vector<OdEntry> entries;      // sub1..N for RECORD/ARRAY (empty for VAR)
    };

    struct PdoMapping
    {
        int                   slave = -1;
        bool                  valid = false;
        std::string           error;
        std::vector<PdoEntry> rx;   // SM2 / 0x1C12 outputs
        std::vector<PdoEntry> tx;   // SM3 / 0x1C13 inputs
    };

    // One slave as the master sees it on the wire: its address, the per-port link
    // state read from DL_STATUS (0x110), and its parent in the discovered tree.
    struct TopologyNode
    {
        int         index          = -1;
        uint16_t    address        = 0;
        uint16_t    parent_address = 0;   // == address for a root (connected to the master)
        std::string name;
        int         open_ports     = 0;
        bool        port_loop[4]   = {false, false, false, false};  // LOOP_portX (open / looped back)
        bool        port_com[4]    = {false, false, false, false};  // COM_portX  (communicating)
    };

    // The bus topology discovered from the master's point of view.
    struct TopologyInfo
    {
        std::vector<TopologyNode> nodes;   // in bus (discovery) order
        bool                      valid = false;
        std::string               error;  // set when the tree could not be derived
    };

    // ---- UI -> bus actor ----------------------------------------------------
    // The complete motor command set, sent whole so multi-field edits (STOP,
    // mode-change-with-reseed) are atomic by construction.
    struct MotorCmd
    {
        bool   enable       = false;
        int    target_state = static_cast<int>(State::SAFE_OP);
        int    mode         = static_cast<int>(CoE::CiA::DS402::control::POSITION_CYCLIC);
        int    source       = static_cast<int>(SetpointSource::Manual);
        double manual_target = 0.0;
        double manual_ramp   = 0.0;  // unit/s toward manual_target; <=0 = step
        double jog_rate      = 0.0;
        int    gen_wave      = static_cast<int>(Waveform::Sine);
        double gen_amp       = 0.0;
        double gen_freq      = 0.0;
        double gen_offset    = 0.0;
    };

    // ---- bus actor -> UI (discrete, must not be lost) -----------------------
    struct Event
    {
        enum class Kind
        {
            OdScanObject, OdScanProgress, OdScanDone,
            MappingResult,
            StateActionResult,   // per-slave; an empty message means success
        };

        Kind        kind  = Kind::OdScanObject;
        int         slave = -1;
        std::string message;

        OdObject   od_object;        // OdScanObject
        int        count = 0;        // OdScanProgress
        int        total = 0;
        PdoMapping mapping;          // MappingResult
    };

    // ---- bus actor -> UI (high-rate, lossy snapshot) ------------------------
    // Per-slave error counters, accumulated by the bus thread and published as
    // one block (a new counter is a new field here, nothing else to keep in sync).
    // The slave counters are uint8 and saturate, so the bus thread reads the
    // delta each sweep, sums it here, and clears the slave before saturation.
    // Totals are "since the last reset".
    struct SlaveErrorStats
    {
        uint16_t al_status_code = 0;              // reason when the AL error bit is set
        uint64_t lost_total[4]  = {0, 0, 0, 0};   // lost-link per port
        uint64_t rxerr_total[4] = {0, 0, 0, 0};   // rx invalid-frame per port
        bool     saturated      = false;          // a raw read hit 255 -> totals are a lower bound
    };

    struct SlaveSnapshot
    {
        uint8_t       al_status    = 0;
        bool          port_com[4]  = {false, false, false, false};  // live link state per port
        DriveFeedback fb;
        std::vector<uint8_t> in_raw;
        std::vector<uint8_t> out_raw;
        std::string     motor_error;
        SlaveErrorStats stats;
    };

    // Distributed-clock discipline telemetry: the state of the master's soft PLL
    // (Timer::sync_to) locking the RT cadence to the DC reference clock, plus the
    // currently injected master-side jitter. Published only while cyclic + DC on.
    struct DcStats
    {
        bool     active          = false;  // DC on and the loop is disciplining
        bool     locked          = false;  // PLL has held phase lock
        int64_t  phase_error_ns  = 0;      // last raw wrapped phase error
        int64_t  phase_peak_ns   = 0;      // decaying peak |phase error| (RT-tracked, ~1s fade)
        int64_t  filtered_error_ns = 0;    // EMA-filtered phase error
        uint64_t samples         = 0;      // PLL samples since bring-up
        bool     overran         = false;  // last cycle woke late
        int64_t  master_jitter_ns = 0;     // injected master-side jitter amplitude
    };

    struct BusSnapshot
    {
        bool        redundancy_active = false;
        std::string status;
        std::string error;
        TopologyInfo topology;
        std::vector<SlaveSnapshot> slaves;   // index-aligned with the device list
        DcStats     dc;
    };
}

#endif
