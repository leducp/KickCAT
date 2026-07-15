#ifndef KICKCAT_TOOLS_KICKUI_BUS_SESSION_H
#define KICKCAT_TOOLS_KICKUI_BUS_SESSION_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kickcat/AbstractSocket.h"
#include "kickcat/CoE/CiA/DS402/Drive.h"
#include "kickcat/OS/ConditionVariable.h"
#include "kickcat/OS/Mutex.h"
#include "kickcat/OS/Thread.h"
#include "kickcat/protocol.h"

#include "BusProtocol.h"
#include "Profile.h"
#include "kickcat/LockedRing.h"

namespace kickcat
{
    class Bus;
    class Link;
    namespace mailbox::request { class AbstractMessage; }
}

namespace kickcat::kickui
{

    // Per-slave OD discovery state, fed by the bus actor's events. The whole
    // struct is returned by value so a panel reads one coherent scan per frame.
    struct OdScan
    {
        bool scanned = false;   // a discovery has produced results for this slave
        bool running = false;
        int  count   = 0;
        int  total   = 0;
        std::vector<OdObject> objects;
        std::string error;
    };

    // Per-slave PDO-mapping read-back state.
    struct PdoScan
    {
        bool       running = false;
        PdoMapping mapping;
    };

    class BusSession;

    // The motor "object": a UI-side handle to one operated DS402 drive. The UI
    // calls its verbs (enable/setJog/edit/...); each verb mutates the UI-side mirror
    // cmd_ and submits the whole command set to the bus actor via the command
    // queue (no shared command mutex). Feedback is read from the published
    // snapshot, never from here. Heap-held (shared_ptr) for a stable address.
    class MotorControl
    {
    public:
        // One coherent command set. Each UI verb mutates the mirror cmd_ and submits
        // the WHOLE struct as one queued command, so the bus actor never observes a
        // half-applied multi-field change. Use edit() to set several fields then
        // submit once (e.g. a mode change that also reseeds target).
        using Command = MotorCmd;

        explicit MotorControl(int slave_index) : slave_index_(slave_index) {}

        void enable(bool on)                              { cmd_.enable = on; submit(); }
        void setSetpointSource(SetpointSource s)          { cmd_.source = static_cast<int>(s); submit(); }
        void setManualTarget(double v)                    { cmd_.manual_target = v; submit(); }
        void setManualRampRate(double rate)               { cmd_.manual_ramp = rate; submit(); }
        void setJog(double signed_rate)                   { cmd_.jog_rate = signed_rate; submit(); }
        void setGenerator(Waveform w, double amplitude, double frequency, double offset)
        {
            cmd_.gen_wave   = static_cast<int>(w);
            cmd_.gen_amp    = amplitude;
            cmd_.gen_freq   = frequency;
            cmd_.gen_offset = offset;
            submit();
        }
        // Apply several fields atomically (e.g. a mode change that also reseeds
        // target) and submit the whole set as one command.
        template<class Fn> void edit(Fn&& fn)             { fn(cmd_); submit(); }

        void setUnits(CoE::CiA::DS402::UnitConfig const& u);

    private:
        // Only the bus actor touches these members: it seeds/configures the
        // control and publishes feedback (fb_mtx_) into the snapshot.
        friend class BusSession;

        void submit();   // enqueue cmd_ to the bus actor (defined after BusSession)

        DriveFeedback feedback() const { LockGuard lock(fb_mtx_); return fb_; }
        std::string   error()    const { LockGuard lock(fb_mtx_); return error_; }
        std::vector<uint8_t> inputPdo()  const { LockGuard lock(fb_mtx_); return in_raw_; }
        std::vector<uint8_t> outputPdo() const { LockGuard lock(fb_mtx_); return out_raw_; }

        int           slave_index_ = -1;
        bool          is_drive_    = true;  // false = generic CoE slave (mapped, no DS402 Drive)
        OperateConfig config_;

        BusSession*   session_ = nullptr;   // set by BusSession; submit() enqueues here
        Command       cmd_;                 // UI-side mirror (UI thread only)

        mutable Mutex        fb_mtx_;
        DriveFeedback        fb_;
        std::string          error_;
        std::vector<uint8_t> in_raw_;   // last TxPDO bytes (slave -> master)
        std::vector<uint8_t> out_raw_;  // last RxPDO bytes (master -> slave)
    };

    // One EtherCAT device (slave): the UI-side object for a detected slave. It
    // carries identity + mailbox capabilities and forwards control to the owning
    // BusSession's thread-safe paths (it never touches the live Bus directly).
    // Capabilities gate which panels apply. CoE gives SDO/OD; a CoE device whose
    // device-type is 402 is a DS402 motor (motor() handle). FoE/EoE are detected
    // here too -- their behaviours/panels are planned and slot in as more methods
    // on BusSession (this forwarding facade should not grow per capability).
    class Device
    {
    public:
        Device() = default;

        // --- identity (public data: set by BusSession on connect) ---
        int             index        = -1;
        uint16_t        address      = 0;
        std::string     name;
        uint32_t        vendor_id    = 0;
        uint32_t        product_code = 0;
        bool            has_coe      = false;  // CoE mailbox (SDO/OD)
        bool            has_foe      = false;  // FoE mailbox (file transfer)
        bool            has_eoe      = false;  // EoE mailbox (Ethernet over EtherCAT)
        bool            sii_pdo      = false;  // SII declares process data (TxPDO/RxPDO); mappable without CoE
        Profile         detected_profile = Profile::Unknown;  // discovered (see profileFromCoeDeviceType)
        // ESC_CONFIG (0x141) bit 0: device emulation, AL status mirrors AL control.
        // The master's ERROR_ACK request bit echoes back as a phantom error
        // indication, so the AL error bit is masked for these slaves.
        bool            is_emulated  = false;
        // Manual override of the discovered profile; Unknown = follow detection.
        Profile         forced_profile = Profile::Unknown;
        BusSession*     session      = nullptr;

        // --- capabilities ---
        // The effective profile: the manual override if set, else what was detected.
        Profile profile() const
        {
            if (forced_profile != Profile::Unknown) { return forced_profile; }
            return detected_profile;
        }
        bool isMotor() const { return profileInfo(profile()).is_drive; }
        void forceProfile(Profile p) { forced_profile = p; }

        // --- EtherCAT state (every device) ---
        void requestState(uint8_t state);

        // --- CoE behaviours (valid when has_coe) ---
        bool sdoAvailable() const;
        std::shared_ptr<SdoResult> readSDO(uint16_t obj_index, uint8_t subindex, int access);
        std::shared_ptr<SdoResult> writeSDO(uint16_t obj_index, uint8_t subindex, int access,
                                            std::vector<uint8_t> data);
        void discoverOD();
        OdScan odScan() const;
        void readPdoMapping();
        PdoScan pdoScan() const;

        // --- DS402 motor (valid when isMotor) ---
        void configureSlave(OperateConfig const& config);  // record config (PRE-OP); applyMapping() starts the loop
        void includeSlave();                                // generic CoE slave: map via SII, no DS402 Drive
        void includeSlave(OperateConfig const& config);     // generic CoE slave with a manual PDO mapping
        void unconfigureSlave();
        bool isConfigured() const;
        bool isOperating() const;
        std::shared_ptr<MotorControl> motor() const;
    };

    // Owns the EtherCAT link/bus and the connection lifecycle. Connect runs on a
    // background worker (raw-socket open + bus bring-up to PRE-OP can block and
    // can throw on missing privileges); the UI polls for completion. The live
    // Bus is only touched from one thread at a time.
    class BusSession
    {
        friend class MotorControl;   // MotorControl::submit() enqueues SdoCommands here
    public:
        BusSession();
        ~BusSession();

        BusSession(BusSession const&) = delete;
        BusSession& operator=(BusSession const&) = delete;

        // --- UI thread ---
        void refreshInterfaces();
        std::vector<NetworkInterface> const& interfaces() const { return interfaces_; }

        // Probe every real NIC for an EtherCAT network (background worker): one
        // broadcast read of reg::TYPE per interface, the WKC is the slave count.
        // tap: pseudo-interfaces are skipped (opening the shared-memory pair as a
        // probe would claim one of its two endpoint slots).
        void detectNetworks();
        bool isDetecting() const { return detecting_; }
        // -1 = not probed, 0 = probed and no EtherCAT answer, >0 = slaves found.
        int  detectResult(std::string const& interface) const;
        std::string detectStatus() const;   // one-line outcome summary

        // Bus-wide distributed-clock setting; applied during connect (PRE-OP).
        void setDcConfig(bool enable, int cycle_ms);

        // Cable redundancy: a non-empty redundant interface makes connect open a
        // second socket and probe the ring (Link::checkRedundancyNeeded). Set on
        // the UI thread before connect(). redundancyActive() reflects the live
        // ring state (set by the Link callback when a cable break is detected).
        void setRedundancy(std::string const& redundant_interface);
        bool redundancyEnabled() const;
        bool redundancyActive()  const { return redundancy_active_; }

        void connect(std::string const& interface);
        void rescan();       // re-detect slaves on the current interface
        void disconnect();

        // Per-frame UI-side housekeeping: promote a finished connect worker's
        // results, reap a stopped RT thread, (re)start the idle service, and
        // republish the snapshot. Call once per frame before reading the getters.
        void update();

        bool isConnected()  const { return connected_; }
        bool isConnecting() const { return connecting_; }
        bool needsPrivilege() const { return needs_privilege_; }
        void clearNeedsPrivilege() { needs_privilege_ = false; }

        std::string interfaceName() const;
        std::string status() const;
        std::string error() const;

        // devices()/selectedDevice()/select() are UI-thread-only: devices_ is read
        // and mutated from the render loop and only ever rebuilt by update() on that
        // same thread. Do not call these off the UI thread.
        std::vector<Device>& devices() { return devices_; }
        int  selected() const { return selected_; }
        void select(int index) { selected_ = index; }
        Device* selectedDevice();

        // The bus topology discovered from the master POV: per-port link state
        // (DL_STATUS) and the parent/child tree. Captured at connect/rescan;
        // refreshTopology() re-reads it live (e.g. after a cable break/heal).
        TopologyInfo topology() const;
        void         refreshTopology();
        void         clearErrorCounters();   // force a reset (slaves + totals)

        // Coherent per-frame view of everything the UI displays, published by the
        // bus-owning side. The read-only getters above (topology/status/error/
        // slaveAlStatus) source from it. Null until the first publish.
        std::shared_ptr<BusSnapshot const> snapshot() const;

        // Raw AL status byte of a slave (low nibble = state, bit 4 = error),
        // polled by the bus-owning thread; 0 if unknown.
        uint8_t slaveAlStatus(int index) const;

        // --- DS402 operation (one shared real-time loop over a set of drives) ---
        // Map-once / always-cycling model (mirrors the master examples):
        //  - configureSlave(): record a drive's intended PDO config (PRE-OP only);
        //    no bus work. unconfigureSlave() drops it from the operated set.
        //  - applyMapping(): map ALL configured slaves ONCE and start the cyclic
        //    loop, which then runs continuously. Per-slave state changes (enable,
        //    PRE-OP/SAFE-OP/OP) NEVER rebuild the mapping or disturb siblings.
        //  - backToPreOp(): stop the loop and return the whole bus to PRE-OP so the
        //    operated set / mapping can be changed and re-applied.
        void configureSlave(int slave_index, OperateConfig const& config);
        void includeSlave(int slave_index);   // generic CoE slave: mapped via SII, no DS402 Drive
        void includeSlave(int slave_index, OperateConfig const& config);  // generic + manual PDO mapping
        void unconfigureSlave(int slave_index);
        void applyMapping();
        void backToPreOp();
        bool isConfigured(int slave_index) const { return findControl(slave_index) != nullptr; }

        // Stored bring-up config for a configured slave (default if none). PRE-OP
        // UI read-back so a panel can reflect what was included.
        OperateConfig configOf(int slave_index) const;

        bool isOperatingAny() const { return rt_running_; }
        bool isOperating(int slave_index) const;   // configured AND the loop is running

        // The motor handle for a slave once operate() has been called on it (null
        // otherwise). Control the drive through it: motor->enable(), edit(), ...
        std::shared_ptr<MotorControl> motor(int slave_index) const { return findControl(slave_index); }

        // --- Generic SDO / Object Dictionary ---
        // Available whenever connected: transfers are serviced by the idle phase,
        // or interleave with the cyclic loop (one blocking command per tick).
        bool sdoAvailable() const { return connected_; }

        std::shared_ptr<SdoResult> readSDO(int slave_index, uint16_t index, uint8_t subindex, int access);
        std::shared_ptr<SdoResult> writeSDO(int slave_index, uint16_t index, uint8_t subindex,
                                            int access, std::vector<uint8_t> data);

        // --- Generic EtherCAT state + PDO mapping (any slave, bus-level) ---
        // The only state-change path. An enabled operated drive is de-energized
        // first when leaving OP; INIT stops the cyclic loop (same as backToPreOp).
        void requestSlaveState(int slave_index, uint8_t state);
        std::string stateActionError(int slave_index) const;   // empty = last action succeeded

        // OD/PDO discovery is per-slave (no session-global "busy" flags): each
        // slave's scan/results are keyed by its index.
        void readPdoMapping(int slave_index);
        PdoScan pdoScan(int slave_index) const;

        void discoverOD(int slave_index);
        OdScan odScan(int slave_index) const;

    private:
        void joinWorker();
        // One bus-owning thread runs busActor(), which alternates between the idle
        // phase (serviceLoop: SDO/OD/diagnostics) and the cyclic phase (rtLoop:
        // 1 kHz DS402). applyMapping()/backToPreOp() flip operate_requested_/rt_stop_
        // to move between phases; the thread is never handed off.
        void busActor();
        void startBusThread();   // spawn the actor (SCHED_FIFO, fallback SCHED_OTHER)
        void stopBusThread();    // signal stop + join
        void rtLoop();           // cyclic phase; returns on rt_stop_ / bus-lost
        std::shared_ptr<MotorControl> findControl(int slave_index) const;

        struct SdoCommand
        {
            enum class Kind { Read, Write, Discover, State, ReadMapping, Topology, ClearErrors,
                              Motor, MotorUnits };
            Kind     kind = Kind::Read;
            int      slave_index = 0;
            uint16_t index = 0;
            uint8_t  subindex = 0;
            int      access = 0;
            uint8_t  state = 0;
            int      od_resume = 0;   // Kind::Discover: resume the scan from this object index
            std::vector<uint8_t>       payload;
            std::shared_ptr<SdoResult> result;
            MotorCmd                    motor;        // Kind::Motor: whole coherent command set
            CoE::CiA::DS402::UnitConfig motor_units;  // Kind::MotorUnits: live unit change
        };

        void serviceLoop();      // idle phase; returns on operate_requested_ / bus_stop_
        // One dispatch for both phases. Motor/MotorUnits are phase-local (applied
        // to the runtimes by rtLoop, dropped while idle) and never reach it.
        void dispatchCommand(SdoCommand& cmd, std::function<void()> const& cyclic);
        // `cyclic` is run once per mailbox poll: it carries the cyclic PDO + one
        // mailbox step (RT thread), or just a mailbox step (idle), so SDO/OD
        // transfers interleave with process data instead of blocking it.
        bool driveMessage(std::shared_ptr<mailbox::request::AbstractMessage> const& msg,
                          std::function<void()> const& cyclic);
        void executeSdo(SdoCommand& cmd, std::function<void()> const& cyclic);
        void executeDiscover(int slave_index, std::function<void()> const& cyclic, int resume_from = 0);
        void executeState(int slave_index, uint8_t state, std::function<void()> const& cyclic);
        void executeReadMapping(int slave_index, std::function<void()> const& cyclic);
        void executeTopology(std::function<void()> const& cyclic);
        void enqueue(SdoCommand cmd);
        void refreshSlaveStates();
        void refreshDiagnostics();   // bus thread: error counters + AL code + port link -> slave_diag_
        void setSlaveState(int index, uint8_t state);
        // True when an in-flight mailbox op should abort: the whole bus thread is
        // stopping (bus_stop_), or the cyclic phase is being left (rt_stop_).
        bool aborting() const;

        std::vector<NetworkInterface> interfaces_;

        std::shared_ptr<Link>  link_;
        std::unique_ptr<Bus>   bus_;
        std::string            interface_name_;

        std::vector<Device> devices_;
        int selected_ = -1;

        std::atomic<bool>    dc_enabled_{false};
        std::atomic<int>     cycle_ms_{1};

        std::string          redundancy_interface_;   // UI-thread; read at connect()
        std::atomic<bool>    redundancy_active_{false};

        // Interface probe worker (detectNetworks); reaped by update().
        std::optional<Thread> detect_worker_;
        std::atomic<bool>     detecting_{false};
        std::atomic<bool>     detect_done_{false};
        mutable Mutex              detect_mtx_;
        std::map<std::string, int> detect_results_;   // interface -> slave count
        std::string                detect_status_;
        void joinDetectWorker();

        std::optional<Thread> worker_;
        std::atomic<bool> connecting_{false};
        std::atomic<bool> done_{false};
        std::atomic<bool> connected_{false};
        std::atomic<bool> needs_privilege_{false};

        mutable Mutex             mtx_;
        std::string               status_;
        std::string               error_;
        std::shared_ptr<Link>     staged_link_;
        std::unique_ptr<Bus>      staged_bus_;
        std::vector<Device> staged_devices_;
        TopologyInfo        staged_topology_;
        void dropStagedConnection();   // under mtx_: discard an unpromoted connect result

        // Discovered topology, read by the UI thread and written by update() (promote)
        // and the idle service thread (refresh); guarded by its own mutex.
        mutable Mutex       topo_mtx_;
        TopologyInfo        topology_;

        // Published UI view. Built from the current authoritative fields and
        // swapped under snap_mtx_; the read-only getters source from it. The
        // pointer swap is guarded by a dedicated mutex rather than
        // atomic<shared_ptr> (C++20) for -std=c++17 portability; the lock only
        // covers a pointer copy.
        mutable Mutex                       snap_mtx_;
        std::shared_ptr<BusSnapshot const>  snapshot_;
        void publishSnapshot();        // build from current state + swap

        // --- single bus-owning thread (idle <-> cyclic) ---
        // Per-slave device-emulation flags, copied from devices_ before the bus
        // thread starts (immutable while it runs): refreshSlaveStates masks the
        // AL error bit for these slaves (it is the echoed ack bit, not an error).
        std::vector<uint8_t>      slave_emulated_;
        std::vector<uint8_t>      io_map_;
        std::optional<Thread>     bus_thread_;
        std::atomic<bool>         bus_stop_{false};         // tear the whole actor down
        std::atomic<bool>         operate_requested_{false};// idle -> cyclic (applyMapping)
        std::atomic<bool>         rt_running_{false};       // true while in the cyclic phase
        std::atomic<bool>         rt_stop_{false};          // leave the cyclic phase (backToPreOp)
        std::atomic<bool>         al_status_dirty_{false};  // set by the AL-event IRQ callback

        // The operated set: UI-thread manages the vector (operate/stop), the RT
        // loop snapshots the shared_ptrs (and copies each config) at bring-up.
        // controls_mtx_ guards the vector structure.
        mutable Mutex                              controls_mtx_;
        std::vector<std::shared_ptr<MotorControl>> controls_;

        // --- command intake (drained by both phases of the bus thread) ---
        mutable Mutex           sdo_mtx_;
        ConditionVariable       sdo_cv_;
        std::deque<SdoCommand>  sdo_queue_;

        mutable Mutex           state_mtx_;
        std::vector<uint8_t>    slave_states_;   // raw AL status per slave (state_mtx_)
        // Per-slave diagnostics, refreshed by the bus thread, copied by publishSnapshot
        // (all under state_mtx_). stats is published as-is; last_* are the previous
        // raw reads used to accumulate the deltas.
        struct SlaveDiag
        {
            SlaveErrorStats stats;
            uint8_t last_lost[4]  = {0, 0, 0, 0};
            uint8_t last_rxerr[4] = {0, 0, 0, 0};
        };
        std::vector<SlaveDiag>  slave_diag_;
        bool diag_clear_pending_ = false;   // bus thread only; set by a ClearErrors command

        // Discrete bus->UI results (SDO-info/mapping/state-action). The bus-owning
        // thread emits Events; the UI thread drains them in update() into the
        // scan state below, which is therefore UI-thread-only (no mutex).
        LockedRing<Event, 4096>::Context events_ctx_{};
        LockedRing<Event, 4096>          events_{events_ctx_};
        bool pushEvent(Event ev);   // bus-owning thread; false when the queue is full
        void drainEvents();         // UI thread (update())

        // Per-slave OD / PDO discovery + state-action results (keyed by slave
        // index; UI-thread-only, fed by drainEvents()).
        std::map<int, std::string> state_errors_;   // last state-change error per slave
        std::map<int, PdoScan>     pdo_scans_;
        std::map<int, OdScan>      od_scans_;
    };
}

#endif
