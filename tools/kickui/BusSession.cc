#include "BusSession.h"

#include <cerrno>
#include <cmath>
#include <system_error>
#include <unordered_map>

#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/protocol.h"
#include "kickcat/Error.h"
#include "kickcat/Frame.h"
#include "kickcat/Link.h"
#include "kickcat/Mailbox.h"
#include "kickcat/MailboxSequencer.h"
#include "kickcat/OS/Timer.h"
#include "kickcat/Units.h"
#include "kickcat/helpers.h"

using namespace std::chrono;
using namespace kickcat::CoE::CiA::DS402;

namespace kickcat::kickui
{
    namespace
    {
        // A mailboxless terminal's PDO assignment is fixed and fully described by
        // the SII PDO categories, so build the read-back view straight from them
        // (no SDO, nothing to poll on the wire).
        PdoMapping siiPdoMapping(int slave_index, eeprom::SII const& sii)
        {
            auto fromSII = [](std::vector<eeprom::PDOMapping> const& pdos, std::vector<PdoEntry>& out)
            {
                for (auto const& pdo : pdos)
                {
                    for (auto const& e : pdo.entries)
                    {
                        PdoEntry pe;
                        pe.pdo   = pdo.index;
                        pe.index = e.index;
                        pe.sub   = e.subindex;
                        pe.bits  = e.bitlen;
                        out.push_back(pe);
                    }
                }
            };
            PdoMapping m;
            m.slave = slave_index;
            fromSII(sii.RxPDO, m.rx);
            fromSII(sii.TxPDO, m.tx);
            m.valid = true;
            return m;
        }

        // what() plus the decoded AL status when the failure is an AL error.
        std::string describeError(std::exception const& e)
        {
            std::string msg = e.what();
            auto const* al = dynamic_cast<ErrorAL const*>(&e);
            if (al != nullptr)
            {
                msg += std::string("  [AL status: ") + ALStatus_to_string(al->code()) + "]";
            }
            return msg;
        }

        // Read each slave's DL_STATUS port bits and derive the parent/child tree
        // (getTopology). Runs on whichever thread currently owns the bus. With
        // refetch the DL status is re-read from the wire first (live refresh);
        // at connect time bus.init() has already populated it.
        TopologyInfo computeTopology(Bus& bus, bool refetch)
        {
            TopologyInfo info;
            auto& slaves = bus.slaves();
            if (refetch)
            {
                for (auto& slave : slaves)
                {
                    bus.sendGetDLStatus(slave, [](DatagramState const&){});
                }
                bus.processAwaitingFrames();
            }

            std::unordered_map<uint16_t, uint16_t> parents;
            try
            {
                parents = getTopology(slaves);   // child address -> parent address
                info.valid = true;
            }
            catch (std::exception const& e)
            {
                // A slave reporting zero open ports (mid break/heal) throws; fall
                // back to a flat list so the per-port state is still shown.
                info.error = e.what();
            }

            info.nodes.reserve(slaves.size());
            for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
            {
                auto& slave = slaves[i];
                auto& dl    = slave.dl_status;
                TopologyNode node;
                node.index          = i;
                node.address        = slave.address;
                node.name           = slave.name();
                node.parent_address = slave.address;
                auto it = parents.find(slave.address);
                if (it != parents.end())
                {
                    node.parent_address = it->second;
                }
                node.open_ports = slave.countOpenPorts();

                for (int p = 0; p < 4; ++p)
                {
                    node.port_loop[p] = dl.loop(p);
                    node.port_com[p]  = dl.communication(p);
                }

                info.nodes.push_back(std::move(node));
            }
            return info;
        }
    }

    BusSession::BusSession()
    {
        events_.init();
        refreshInterfaces();
    }

    // The bus-owning thread posts a discrete result; the UI thread applies it in
    // drainEvents() (called from update()). false when the queue is full.
    bool BusSession::pushEvent(Event ev)
    {
        return events_.push(ev);
    }

    void BusSession::drainEvents()
    {
        Event ev;
        while (events_.tryPop(ev))
        {
            switch (ev.kind)
            {
                case Event::Kind::OdScanProgress:
                {
                    auto& s = od_scans_[ev.slave];
                    s.count = ev.count;
                    s.total = ev.total;
                    break;
                }
                case Event::Kind::OdScanObject:
                {
                    auto& s = od_scans_[ev.slave];
                    s.objects.push_back(std::move(ev.od_object));
                    s.count = static_cast<int>(s.objects.size());
                    break;
                }
                case Event::Kind::OdScanDone:
                {
                    auto& s = od_scans_[ev.slave];
                    s.running = false;
                    s.error   = ev.message;
                    break;
                }
                case Event::Kind::MappingResult:
                {
                    auto& s = pdo_scans_[ev.slave];
                    s.mapping = std::move(ev.mapping);
                    s.running = false;
                    break;
                }
                case Event::Kind::StateActionResult:
                {
                    state_errors_[ev.slave] = ev.message;   // empty = success (clears the error)
                    break;
                }
            }
        }
    }

    BusSession::~BusSession()
    {
        stopBusThread();
        joinWorker();
        joinDetectWorker();
    }

    void BusSession::refreshInterfaces()
    {
        interfaces_ = listInterfaces();
        // The GUI master attaches to the software simulator as the TapSocket client
        // (tap:server is the simulator's role). createSockets() needs the literal
        // "tap:client"; the combo shows the friendly description instead.
        interfaces_.push_back({"tap:client", "tap to simulation"});
    }

    void BusSession::detectNetworks()
    {
        if (detecting_)
        {
            return;
        }
        joinDetectWorker();
        {
            LockGuard lock(detect_mtx_);
            detect_results_.clear();
            detect_status_ = "Probing interfaces...";
        }
        detecting_   = true;
        detect_done_ = false;

        std::vector<std::string> names;
        for (auto const& nif : interfaces_)
        {
            if (nif.name.rfind("tap:", 0) == 0)
            {
                continue;
            }
            names.push_back(nif.name);
        }

        detect_worker_.emplace("kickui-detect", [this, names]()
        {
            int  networks = 0;
            bool denied   = false;
            for (auto const& name : names)
            {
                int count = 0;
                try
                {
                    auto [socket, no_red] = createSockets(name, "");
                    auto link = std::make_shared<Link>(socket, no_red, []{});
                    link->setTimeout(2ms);

                    Frame frame;
                    frame.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), nullptr, 1);
                    link->writeThenRead(frame);
                    auto [header, _, wkc] = frame.nextDatagram();
                    count = wkc;
                }
                catch (std::system_error const& e)
                {
                    if ((e.code().value() == EPERM) or (e.code().value() == EACCES))
                    {
                        denied = true;
                        needs_privilege_ = true;
                    }
                }
                catch (std::exception const&)
                {
                    // No answer on the wire (timeout): not an EtherCAT network.
                }
                if (count > 0)
                {
                    networks += 1;
                }
                LockGuard lock(detect_mtx_);
                detect_results_[name] = count;
            }

            std::string summary;
            if (denied)
            {
                summary = "Detection needs elevated privileges (raw sockets).";
            }
            else if (networks == 0)
            {
                summary = "No EtherCAT network found.";
            }
            else if (networks == 1)
            {
                summary = "1 EtherCAT network found.";
            }
            else
            {
                summary = std::to_string(networks) + " EtherCAT networks found.";
            }
            {
                LockGuard lock(detect_mtx_);
                detect_status_ = summary;
            }
            detect_done_ = true;
        }, 0);  // priority 0: SCHED_OTHER (probing is not real-time)
        detect_worker_->start();
    }

    int BusSession::detectResult(std::string const& interface) const
    {
        LockGuard lock(detect_mtx_);
        auto it = detect_results_.find(interface);
        if (it == detect_results_.end())
        {
            return -1;
        }
        return it->second;
    }

    std::string BusSession::detectStatus() const
    {
        LockGuard lock(detect_mtx_);
        return detect_status_;
    }

    void BusSession::joinDetectWorker()
    {
        if (detect_worker_)
        {
            detect_worker_->join();
            detect_worker_.reset();
        }
    }

    void BusSession::setDcConfig(bool enable, int cycle_ms)
    {
        dc_enabled_ = enable;
        cycle_ms_   = cycle_ms;
        if (cycle_ms_ < 1)
        {
            cycle_ms_ = 1;
        }
    }

    void BusSession::setRedundancy(std::string const& redundant_interface)
    {
        redundancy_interface_ = redundant_interface;
    }

    bool BusSession::redundancyEnabled() const
    {
        return not redundancy_interface_.empty();
    }

    void BusSession::joinWorker()
    {
        if (worker_)
        {
            worker_->join();
            worker_.reset();
        }
    }

    std::string BusSession::interfaceName() const
    {
        return interface_name_;
    }

    // The read-only display getters source from the published snapshot instead of
    // the per-field mutexes. Null-safe before the first publish.
    std::shared_ptr<BusSnapshot const> BusSession::snapshot() const
    {
        LockGuard lock(snap_mtx_);
        return snapshot_;
    }

    std::string BusSession::status() const
    {
        auto s = snapshot();
        if (s) { return s->status; }
        return {};
    }

    std::string BusSession::error() const
    {
        auto s = snapshot();
        if (s) { return s->error; }
        return {};
    }

    // Build a fresh BusSnapshot and swap it in, once per UI frame from update().
    // Not on the bus thread: that would put allocations on the 1 kHz cyclic path.
    void BusSession::publishSnapshot()
    {
        auto snap = std::make_shared<BusSnapshot>();

        snap->redundancy_active = redundancy_active_;
        {
            LockGuard lock(mtx_);
            snap->status = status_;
            snap->error  = error_;
        }
        {
            LockGuard lock(topo_mtx_);
            snap->topology = topology_;
        }
        {
            LockGuard lock(state_mtx_);
            snap->slaves.resize(slave_states_.size());
            for (size_t i = 0; i < slave_states_.size(); ++i)
            {
                snap->slaves[i].al_status = slave_states_[i];
                if (i < slave_diag_.size())
                {
                    snap->slaves[i].stats = slave_diag_[i].stats;
                }
            }
        }
        // Live per-port link state (topology node index == device index).
        for (auto const& node : snap->topology.nodes)
        {
            if ((node.index >= 0) and (node.index < static_cast<int>(snap->slaves.size())))
            {
                for (int p = 0; p < 4; ++p) { snap->slaves[node.index].port_com[p] = node.port_com[p]; }
            }
        }
        {
            LockGuard lock(controls_mtx_);
            for (auto const& c : controls_)
            {
                int idx = c->slave_index_;
                if ((idx >= 0) and (idx < static_cast<int>(snap->slaves.size())))
                {
                    SlaveSnapshot& ss = snap->slaves[idx];
                    ss.fb           = c->feedback();
                    ss.in_raw       = c->inputPdo();
                    ss.out_raw      = c->outputPdo();
                    ss.motor_error  = c->error();
                }
            }
        }
        snap->dc.active            = dc_active_;
        snap->dc.locked            = dc_locked_;
        snap->dc.phase_error_ns    = dc_phase_error_ns_;
        snap->dc.phase_peak_ns     = dc_phase_peak_ns_;
        snap->dc.filtered_error_ns = dc_filtered_error_ns_;
        snap->dc.samples           = dc_pll_samples_;
        snap->dc.overran           = dc_overran_;
        snap->dc.master_jitter_ns  = master_jitter_ns_;
        {
            LockGuard lock(snap_mtx_);
            snapshot_ = snap;
        }
    }

    Device* BusSession::selectedDevice()
    {
        if ((selected_ < 0) or (selected_ >= static_cast<int>(devices_.size())))
        {
            return nullptr;
        }
        return &devices_[selected_];
    }

    TopologyInfo BusSession::topology() const
    {
        auto s = snapshot();
        if (s) { return s->topology; }
        return {};
    }

    void BusSession::refreshTopology()
    {
        if (not connected_)
        {
            return;
        }
        SdoCommand cmd;
        cmd.kind = SdoCommand::Kind::Topology;
        enqueue(std::move(cmd));
    }

    void BusSession::clearErrorCounters()
    {
        if (not connected_)
        {
            return;
        }
        SdoCommand cmd;
        cmd.kind = SdoCommand::Kind::ClearErrors;
        enqueue(std::move(cmd));
    }

    // --- Device: forwards to the owning BusSession's thread-safe paths ---
    // (session is wired in update() when the device set is promoted.)
    void Device::requestState(uint8_t state)         { session->requestSlaveState(index, state); }

    bool Device::sdoAvailable() const                { return session->sdoAvailable(); }
    std::shared_ptr<SdoResult> Device::readSDO(uint16_t obj_index, uint8_t subindex, int access)
    {
        return session->readSDO(index, obj_index, subindex, access);
    }
    std::shared_ptr<SdoResult> Device::writeSDO(uint16_t obj_index, uint8_t subindex, int access,
                                                std::vector<uint8_t> data)
    {
        return session->writeSDO(index, obj_index, subindex, access, std::move(data));
    }
    void Device::discoverOD()                        { session->discoverOD(index); }
    OdScan Device::odScan() const                    { return session->odScan(index); }
    void Device::readPdoMapping()                    { session->readPdoMapping(index); }
    PdoScan Device::pdoScan() const                  { return session->pdoScan(index); }

    void Device::configureSlave(OperateConfig const& config) { session->configureSlave(index, config); }
    void Device::includeSlave()                       { session->includeSlave(index); }
    void Device::includeSlave(OperateConfig const& c) { session->includeSlave(index, c); }
    void Device::unconfigureSlave()                   { session->unconfigureSlave(index); }
    bool Device::isConfigured() const                 { return session->isConfigured(index); }
    bool Device::isOperating() const                  { return session->isOperating(index); }
    std::shared_ptr<MotorControl> Device::motor() const { return session->motor(index); }

    void BusSession::connect(std::string const& interface)
    {
        if (connecting_)
        {
            return;
        }
        joinWorker();

        needs_privilege_  = false;
        done_             = false;
        connecting_       = true;
        interface_name_   = interface;
        redundancy_active_ = false;
        {
            LockGuard lock(mtx_);
            dropStagedConnection();   // a leftover staged bus must never be promoted
            status_ = "Connecting to " + interface + "...";
            error_.clear();
        }

        std::string interface_name      = interface;
        std::string redundant_interface = redundancy_interface_;
        worker_.emplace("kickui-connect", [this, interface_name, redundant_interface]()
        {
            auto set_status = [this](std::string const& s)
            {
                LockGuard lock(mtx_);
                status_ = s;
            };

            try
            {
                auto [nominal, redundancy] = createSockets(interface_name, redundant_interface);
                auto report_redundancy = [this]{ redundancy_active_ = true; };
                auto link = std::make_shared<Link>(nominal, redundancy, report_redundancy);
                link->setTimeout(2ms);

                if (not redundant_interface.empty())
                {
                    // Probe the ring once: if the redundant port answers, the wire
                    // is intact. A probe failure is non-fatal -- the nominal path
                    // still runs, just without a backup.
                    try
                    {
                        link->checkRedundancyNeeded();
                    }
                    catch (std::exception const& e)
                    {
                        set_status(std::string("Redundancy probe failed: ") + e.what());
                    }
                }

                auto bus = std::make_unique<Bus>(link);
                bus->configureWaitLatency(1ms, 10ms);

                set_status("Scanning bus (INIT -> PRE-OP)...");
                bus->init();   // ends in PRE-OP

                if (dc_enabled_)
                {
                    // DC is enabled here (PRE-OP) and the cyclic timer is left
                    // unaligned on purpose: these drives tolerate a free-running
                    // master cycle, and phase-aligning the RT loop to the
                    // connect-time sync point (stale by the time operate() runs)
                    // mis-phases against SYNC0 and makes CSP drives fault.
                    set_status("Enabling distributed clocks...");
                    bus->enableDC(milliseconds(cycle_ms_.load()), 500us, 100ms);
                }

                set_status("Detecting devices...");
                std::vector<Device> devices;
                auto& slaves = bus->slaves();

                // Device-emulation ESCs (ESC_CONFIG bit 0: no PDI application,
                // AL status mirrored from AL control) echo the master's ERROR_ACK
                // request bit back as a phantom error indication. Flag them so the
                // AL error bit can be masked when polling their state.
                std::vector<uint8_t> emulated(slaves.size(), 0);
                try
                {
                    for (size_t i = 0; i < slaves.size(); ++i)
                    {
                        uint8_t config = 0;
                        link->addDatagram(Command::FPRD,
                            createAddress(slaves[i].address, reg::ESC_CONFIG), config,
                            [&emulated, i](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
                            {
                                if (wkc == 1)
                                {
                                    emulated[i] = data[0] & 0x01;
                                }
                                return DatagramState::OK;
                            },
                            [](DatagramState const&){});
                    }
                    link->processDatagrams();
                }
                catch (std::exception const&)
                {
                    // No answer: treat every slave as a regular ESC.
                }
                for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
                {
                    auto& slave = slaves[i];
                    Device dev;
                    dev.index        = i;
                    dev.address      = slave.address;
                    dev.name         = slave.name();
                    dev.vendor_id    = slave.sii.info.vendor_id;
                    dev.product_code = slave.sii.info.product_code;

                    // Mailbox capabilities come straight from the SII mailbox
                    // protocol bits (what the device declares it speaks).
                    uint16_t mbx = slave.sii.info.mailbox_protocol;
                    dev.has_coe = (mbx & eeprom::MailboxProtocol::CoE) != 0;
                    dev.has_foe = (mbx & eeprom::MailboxProtocol::FoE) != 0;
                    dev.has_eoe = (mbx & eeprom::MailboxProtocol::EoE) != 0;
                    dev.is_emulated = (emulated[i] != 0);

                    // Process data comes from the SII PDO categories (parsed during
                    // init), independent of any mailbox: a mailboxless I/O terminal
                    // (e.g. EL1004) is still mappable via its fixed SII PDO.
                    dev.sii_pdo = (not slave.sii.TxPDO.empty()) or (not slave.sii.RxPDO.empty());

                    // The CiA profile is the low word of the device-type object (0x1000).
                    if (dev.has_coe)
                    {
                        uint32_t device_type = 0;
                        uint32_t size        = sizeof(device_type);
                        try
                        {
                            bus->readSDO(slave, 0x1000, 0, Bus::Access::PARTIAL, &device_type, &size, 100ms);
                            dev.detected_profile = profileFromCoeDeviceType(static_cast<uint16_t>(device_type & 0xFFFF));
                        }
                        catch (std::exception const&)
                        {
                            // 0x1000 unreadable (abort/busy/write-only) is not proof CoE
                            // is dead -- the SII declared it. Keep the SDO/OD panels;
                            // treat the profile as generic until the user forces one.
                            dev.detected_profile = Profile::Generic;
                        }
                    }
                    devices.push_back(std::move(dev));
                }

                TopologyInfo topo = computeTopology(*bus, false);

                {
                    LockGuard lock(mtx_);
                    staged_link_     = std::move(link);
                    staged_bus_      = std::move(bus);
                    staged_devices_  = std::move(devices);
                    staged_topology_ = std::move(topo);
                    status_          = "Connected.";
                }
                done_ = true;
            }
            catch (std::system_error const& e)
            {
                LockGuard lock(mtx_);
                if ((e.code().value() == EPERM) or (e.code().value() == EACCES))
                {
                    needs_privilege_ = true;
                    error_ = "Permission denied: raw socket requires elevated privileges.";
                }
                else
                {
                    error_ = std::string("Connection failed: ") + e.what();
                }
                done_ = true;
            }
            catch (std::exception const& e)
            {
                LockGuard lock(mtx_);
                error_ = std::string("Connection failed: ") + e.what();
                done_ = true;
            }
        }, 0);  // priority 0: SCHED_OTHER (connect is not real-time)
        worker_->start();
    }

    void BusSession::rescan()
    {
        if (connecting_)
        {
            return;
        }
        std::string interface = interface_name_;
        if (interface.empty())
        {
            return;
        }
        disconnect();          // tear down the current link before re-opening
        connect(interface);
    }

    void BusSession::update()
    {
        drainEvents();   // apply discrete bus->UI results before the frame reads them

        if (detecting_ and detect_done_)
        {
            joinDetectWorker();
            detecting_   = false;
            detect_done_ = false;
        }

        bool just_connected = false;
        if (connecting_ and done_)
        {
            joinWorker();
            connecting_ = false;
            done_       = false;

            LockGuard lock(mtx_);
            if (staged_bus_)
            {
                link_      = std::move(staged_link_);
                bus_       = std::move(staged_bus_);
                devices_   = std::move(staged_devices_);
                for (auto& dev : devices_) { dev.session = this; }  // wire the forwarding handle

                // Seed the static SII mapping for mailboxless slaves now (bus_ is
                // set, the bus thread is not started yet): they have no SDO to read,
                // so the panel shows their fixed assignment without any action.
                // (CoE slaves instead get theirs on demand via executeReadMapping
                // -> Event::MappingResult.)
                pdo_scans_.clear();
                auto const& bus_slaves = bus_->slaves();
                for (int i = 0; i < static_cast<int>(devices_.size()); ++i)
                {
                    if (devices_[i].has_coe or not devices_[i].sii_pdo) { continue; }
                    if (i >= static_cast<int>(bus_slaves.size()))       { continue; }
                    pdo_scans_[i].mapping = siiPdoMapping(i, bus_slaves[i].sii);
                }
                {
                    LockGuard tlock(topo_mtx_);
                    topology_ = std::move(staged_topology_);
                }
                connected_ = true;
                selected_  = -1;
                if (not devices_.empty())
                {
                    selected_ = 0;
                }
                LockGuard state_lock(state_mtx_);
                slave_states_.assign(devices_.size(), static_cast<uint8_t>(State::PRE_OP));
                just_connected = true;
            }
        }

        if (just_connected)
        {
            // Snapshot the immutable per-slave flags for the bus thread before
            // it exists (no lock needed: thread start orders the writes).
            slave_emulated_.clear();
            for (auto const& dev : devices_)
            {
                uint8_t flag = 0;
                if (dev.is_emulated)
                {
                    flag = 1;
                }
                slave_emulated_.push_back(flag);
            }
            startBusThread();   // one bus-owning thread, idle phase first
        }

        // Republish the UI view from the current authoritative state (once per
        // frame; render() calls update() before reading the getters).
        publishSnapshot();
    }

    // Under mtx_: discard an unpromoted connect result, so a later connect's
    // update() can never promote a stale Bus.
    void BusSession::dropStagedConnection()
    {
        staged_link_.reset();
        staged_bus_.reset();
        staged_devices_.clear();
        staged_topology_ = TopologyInfo{};
    }

    void BusSession::disconnect()
    {
        stopBusThread();   // graceful-stops the cyclic phase, then joins the one thread
        {
            LockGuard lock(controls_mtx_);
            controls_.clear();
        }
        joinWorker();
        connecting_ = false;
        connected_  = false;
        done_       = false;
        bus_.reset();
        link_.reset();
        devices_.clear();
        slave_emulated_.clear();
        selected_ = -1;
        interface_name_.clear();
        {
            LockGuard state_lock(state_mtx_);
            slave_states_.clear();
            slave_diag_.clear();
        }
        // UI-thread-only now (the bus threads were joined above); drop any stale
        // events so they don't repopulate the cleared scan state next frame.
        Event discard;
        while (events_.tryPop(discard)) {}
        od_scans_.clear();
        pdo_scans_.clear();
        state_errors_.clear();
        LockGuard lock(mtx_);
        dropStagedConnection();   // an in-flight connect result dies with the session
        status_.clear();
        error_.clear();
    }

    std::shared_ptr<MotorControl> BusSession::findControl(int slave_index) const
    {
        LockGuard lock(controls_mtx_);
        for (auto const& c : controls_)
        {
            if (c->slave_index_ == slave_index)
            {
                return c;
            }
        }
        return nullptr;
    }

    bool BusSession::isOperating(int slave_index) const
    {
        return rt_running_ and (findControl(slave_index) != nullptr);
    }

    void BusSession::configureSlave(int slave_index, OperateConfig const& config)
    {
        // operate_requested_: the cyclic bring-up snapshots the controls/configs
        // asynchronously -- no edits once it is requested.
        if (not connected_ or rt_running_ or operate_requested_)
        {
            return;  // mapping is fixed while operating; backToPreOp() first
        }
        if ((slave_index < 0) or (slave_index >= static_cast<int>(devices_.size())))
        {
            return;
        }

        LockGuard lock(controls_mtx_);
        std::shared_ptr<MotorControl> control;
        for (auto const& c : controls_)
        {
            if (c->slave_index_ == slave_index) { control = c; break; }
        }
        if (control == nullptr)
        {
            control = std::make_shared<MotorControl>(slave_index);
            controls_.push_back(control);
        }
        control->session_  = this;
        control->is_drive_ = true;
        control->config_   = config;
        // UI-side mirror only; the bus actor seeds DriveRuntime.cmd from config_ at
        // operate (these defaults match), then takes live updates off the queue.
        control->cmd_              = MotorControl::Command{};
        control->cmd_.mode         = static_cast<int>(config.mode);
        control->cmd_.source       = static_cast<int>(SetpointSource::Manual);
        control->cmd_.target_state = static_cast<int>(State::SAFE_OP);
        {
            LockGuard f(control->fb_mtx_);
            control->fb_ = DriveFeedback{};
            control->error_.clear();
        }
    }

    void BusSession::includeSlave(int slave_index)
    {
        includeSlave(slave_index, OperateConfig{});  // SII default mapping
    }

    void BusSession::includeSlave(int slave_index, OperateConfig const& config)
    {
        // Generic CoE slave: include it in the bus mapping (via its SII PDO, or a
        // manual PDO mapping if config.manual_mapping) and let the loop transition
        // it, but give it NO DS402 Drive.
        if (not connected_ or rt_running_ or operate_requested_)
        {
            return;
        }
        if ((slave_index < 0) or (slave_index >= static_cast<int>(devices_.size())))
        {
            return;
        }
        LockGuard lock(controls_mtx_);
        std::shared_ptr<MotorControl> control;
        for (auto const& c : controls_)
        {
            if (c->slave_index_ == slave_index) { control = c; break; }
        }
        if (control == nullptr)
        {
            control = std::make_shared<MotorControl>(slave_index);
            controls_.push_back(control);
        }
        control->session_  = this;
        control->is_drive_ = false;
        control->config_   = config;   // re-including updates the config, same as configureSlave
        control->cmd_.target_state = static_cast<int>(State::SAFE_OP);  // UI mirror; generic slave has no Drive
    }

    OperateConfig BusSession::configOf(int slave_index) const
    {
        auto control = findControl(slave_index);
        if (control != nullptr)
        {
            return control->config_;
        }
        return OperateConfig{};
    }

    void BusSession::unconfigureSlave(int slave_index)
    {
        if (rt_running_ or operate_requested_)
        {
            return;  // changing the operated set is a PRE-OP action
        }
        LockGuard lock(controls_mtx_);
        for (auto it = controls_.begin(); it != controls_.end(); ++it)
        {
            if ((*it)->slave_index_ == slave_index) { controls_.erase(it); break; }
        }
    }

    void BusSession::applyMapping()
    {
        // PRE-OP -> map every configured slave ONCE and run the cyclic phase
        // continuously. Per-slave state changes after this never rebuild.
        if (rt_running_ or not connected_)
        {
            return;
        }
        bool any = false;
        {
            LockGuard lock(controls_mtx_);
            any = not controls_.empty();
        }
        if (any)
        {
            // Ask the bus thread to leave the idle phase and enter rtLoop().
            operate_requested_ = true;
            LockGuard lock(sdo_mtx_);
            sdo_cv_.signal();
        }
    }

    void BusSession::backToPreOp()
    {
        // Leave the cyclic phase: rtLoop graceful-stops and the bus thread returns
        // to the idle phase by itself (same thread -- no hand-off, no restart race).
        rt_stop_ = true;
    }

    // The single bus-owning thread: alternate between the idle phase (serviceLoop)
    // and the cyclic phase (rtLoop). One thread owns the Bus for the whole connected
    // lifetime, so the old connect-worker -> service -> RT hand-off invariants are gone.
    void BusSession::busActor()
    {
        while (not bus_stop_)
        {
            serviceLoop();                 // idle: returns on operate_requested_ or bus_stop_
            if (bus_stop_) { break; }
            if (operate_requested_)
            {
                operate_requested_ = false;
                rt_stop_    = false;
                rt_running_ = true;
                rtLoop();                  // cyclic: graceful-stops + clears rt_running_ on exit
            }
        }
    }

    void BusSession::startBusThread()
    {
        bus_stop_          = false;
        operate_requested_ = false;
        rt_stop_           = false;
        rt_running_        = false;

        // SCHED_FIFO for low cyclic jitter; the idle phase just CV-waits, so running
        // it at RT priority is harmless. Fall back to SCHED_OTHER without cap_sys_nice.
        constexpr int RT_PRIORITY = 80;
        std::function<void()> body = [this]{ busActor(); };
        bus_thread_.emplace("kickui-bus", body, RT_PRIORITY);
        try
        {
            bus_thread_->start();
        }
        catch (std::exception const&)
        {
            bus_thread_.emplace("kickui-bus", body, 0);
            try
            {
                bus_thread_->start();
            }
            catch (std::exception const& e)
            {
                // No worker means a wedged "connected" session; surface it instead.
                bus_thread_.reset();
                connected_ = false;
                LockGuard lock(mtx_);
                error_ = std::string("could not start the bus thread: ") + e.what();
            }
        }
    }

    void BusSession::stopBusThread()
    {
        bus_stop_ = true;
        rt_stop_  = true;   // leave the cyclic phase if we are in it
        {
            LockGuard lock(sdo_mtx_);
            sdo_cv_.signal();
        }
        if (bus_thread_)
        {
            bus_thread_->join();
            bus_thread_.reset();
        }
        rt_running_ = false;

        // Fail any still-queued SDO requests so the UI stops polling them.
        LockGuard lock(sdo_mtx_);
        for (auto& cmd : sdo_queue_)
        {
            if (cmd.result)
            {
                cmd.result->ok      = false;
                cmd.result->message = "cancelled";
                cmd.result->done    = true;
            }
        }
        sdo_queue_.clear();
    }

    namespace
    {
        // Per-operated-drive runtime state living only on the RT thread. config/
        // is_drive are copied under controls_mtx_ at bring-up, so a UI-side
        // configure racing the phase start can never tear them.
        struct DriveRuntime
        {
            std::shared_ptr<MotorControl> control;
            OperateConfig config;
            bool          is_drive = true;
            std::unique_ptr<Drive> drive;
            MotorCmd                    cmd;            // bus-owned, fed by the command queue
            CoE::CiA::DS402::UnitConfig pending_units;  // last live unit change
            bool units_dirty     = false;
            double      jog_accum    = 0.0;
            double      ramped       = 0.0;   // current manual-ramp position (SI)
            double      last_target  = 0.0;
            nanoseconds gen_start{0};
            int  prev_source     = -1;
            bool was_enabled     = false;
            bool prev_cmd_enable = false;
            bool seeded          = false;
            int  current_state   = static_cast<int>(State::PRE_OP);
        };

        // Write a hand-edited PDO mapping to one mapping object (e.g. 0x1600) and
        // assign it to its sync manager (0x1C12/0x1C13) -- the standard CoE remap
        // sequence, reusing the lib's mapPDO. No-op for an empty entry list.
        void writeManualMapping(Bus& bus, Slave& slave, uint16_t pdo_obj,
                                std::vector<PdoEntry> const& entries, uint16_t sm_map)
        {
            if (entries.empty()) { return; }
            std::vector<uint32_t> words;
            words.reserve(entries.size());
            for (auto const& e : entries)
            {
                words.push_back(CoE::toMappingWord({e.index, e.sub, e.bits}));
            }
            mapPDO(bus, slave, pdo_obj, words.data(), static_cast<uint8_t>(words.size()), sm_map);
        }
    }

    void BusSession::rtLoop()
    {
        // Snapshot the operated set (shared_ptrs keep the controls alive) and copy
        // each control's config/is_drive while still holding controls_mtx_.
        std::vector<DriveRuntime> rts;
        {
            LockGuard lock(controls_mtx_);
            rts.reserve(controls_.size());
            for (auto const& control : controls_)
            {
                DriveRuntime r;
                r.control       = control;
                r.config   = control->config_;
                r.is_drive = control->is_drive_;
                rts.push_back(std::move(r));
            }
        }
        if (rts.empty())
        {
            rt_running_ = false;
            return;
        }

        auto set_all_errors = [&](std::string const& msg)
        {
            for (auto& r : rts)
            {
                LockGuard lock(r.control->fb_mtx_);
                r.control->error_ = msg;
            }
        };

        int const cycle_ms = cycle_ms_.load();  // bus cycle: loop period + interpolation

        Bus* bus = bus_.get();

        // True when at least one operated drive exchanges process data; gates the
        // LRD/LWR sends each tick.
        auto pd_active_any = [&]
        {
            for (auto const& r : rts)
            {
                if ((r.current_state == static_cast<int>(State::SAFE_OP))
                 or (r.current_state == static_cast<int>(State::OPERATIONAL))) { return true; }
            }
            return false;
        };

        try
        {
            bus->requestState(State::PRE_OP);
            bus->waitForState(State::PRE_OP, 2000ms);

            // Configure every operated DS402 drive BEFORE the single createMapping.
            // Generic (non-DS402) participants get NO Drive -- they are mapped via
            // their SII PDO and just transitioned/exchanged (like the freedom and
            // xmc4800 master examples).
            for (auto& r : rts)
            {
                Slave& slave = bus->slaves().at(r.control->slave_index_);
                // Seed the bus-owned command from config (matches configureSlave's
                // defaults); live UI changes then arrive via the command queue.
                r.cmd.mode         = static_cast<int>(r.config.mode);
                r.cmd.source       = static_cast<int>(SetpointSource::Manual);
                r.cmd.target_state = static_cast<int>(State::SAFE_OP);
                if (r.is_drive)
                {
                    r.drive = std::make_unique<Drive>(*bus, slave);
                    r.drive->setUnits(r.config.units);
                    r.drive->configure(r.config.mode, r.config.rx_pdo_map, r.config.tx_pdo_map,
                                       r.config.padding);
                    int ip_ms = cycle_ms;
                    if (ip_ms > 255) { ip_ms = 255; }   // 0x60C2/sub1 is uint8_t (ms)
                    r.drive->setInterpolationTimePeriod(static_cast<uint8_t>(ip_ms), -3);
                }
                else if (r.config.manual_mapping)
                {
                    // Generic slave with a hand-edited mapping: remap via SDO before
                    // createMapping sizes the process image.
                    writeManualMapping(*bus, slave, r.config.rx_pdo_map, r.config.manual_rx, 0x1C12);
                    writeManualMapping(*bus, slave, r.config.tx_pdo_map, r.config.manual_tx, 0x1C13);
                }
            }

            // Map each slave's mailbox status (can_read/can_write) into the cyclic
            // PDO frame so SDO/OD interleave without extra polling. Needs spare FMMUs.
            try { bus->configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK); }
            catch (std::exception const&) {}

            // One mapping for the whole bus. createMapping throws before writing if
            // the buffer is too small (it never overflows), so grow and retry until
            // the whole process image fits.
            io_map_.resize(8192);
            while (true)
            {
                try
                {
                    bus->createMapping(io_map_.data(), io_map_.size());
                    break;
                }
                catch (std::exception const&)
                {
                    if (io_map_.size() >= 1u << 20) { throw; }  // 1 MiB: far beyond any real bus
                    io_map_.resize(io_map_.size() * 2);
                }
            }
            for (auto& r : rts)
            {
                if (r.drive) { r.drive->attach(); }
                // Pre-size the display mirrors now (process image is mapped, bsizes
                // known) so the cyclic copy never allocates under fb_mtx_.
                Slave& slave = bus->slaves().at(r.control->slave_index_);
                LockGuard lock(r.control->fb_mtx_);
                if (slave.input.bsize  > 0) { r.control->in_raw_.reserve(slave.input.bsize); }
                if (slave.output.bsize > 0) { r.control->out_raw_.reserve(slave.output.bsize); }
            }

            // Arm the AL-event IRQ: the callback fires inline on this RT thread
            // during frame processing and only sets a flag, which we drain each
            // tick to refresh per-slave AL state. Best-effort -- a sim/ESC that
            // does not raise AL events just leaves us on the idle-loop poll.
            al_status_dirty_ = false;
            try
            {
                bus->enableIRQ(EcatEvent::AL_STATUS, [this]{ al_status_dirty_ = true; });
            }
            catch (std::exception const&) {}

            link_->setTimeout(500us);

            for (auto& r : rts)
            {
                r.current_state = static_cast<int>(State::PRE_OP);
                setSlaveState(r.control->slave_index_, static_cast<uint8_t>(State::PRE_OP));
            }

            // Soft-PLL tuning for KickUI. The bus loop runs on a kickcat Thread at SCHED_FIFO 80
            // when the process has cap_sys_nice, else it falls back to SCHED_OTHER (see
            // startBusThread). Either way its residual jitter is larger than a lean hardware
            // master's: it does more per cycle (GUI actor, mailbox stepping, snapshot publish,
            // multi-slave) over the tap transport, and an unprivileged run is not real-time at all.
            // Loosen the lock criteria and outlier reject so a lock can actually accumulate;
            // Marvin keeps the tight SoftPll defaults.
            SoftPll::Config pll_cfg;
            pll_cfg.ema_alpha        = 0.05;    // heavier smoothing -> lower filtered-error noise floor
            pll_cfg.slew_limit       = 30us;
            pll_cfg.integrator_clamp = 500us;
            pll_cfg.outlier_reject   = 500us;   // soft-RT stalls spike well past the hardware 150us
            pll_cfg.lock_threshold   = 30us;
            pll_cfg.unlock_threshold = 80us;
            pll_cfg.lock_samples     = 200;
            pll_cfg.unlock_samples   = 100;
            Timer timer{milliseconds(cycle_ms), pll_cfg};
            timer.start();
            double const dt = cycle_ms * 1.0e-3;

            int consecutive_pd_errors = 0;
            constexpr int PD_ERROR_LIMIT = 1000;  // ~1 s of dead bus at 1 ms

            MailboxSequencer sequencer(*bus);
            bool const fmmu_opt = (bus->mailboxStatusFMMUMode() != MailboxStatusFMMU::NONE);

            // Tolerate working-counter mismatches without aborting, exactly like
            // the marvin example's false_alarm. A short WKC is expected here (the
            // mapping covers the whole bus but only the operated drives are at
            // SAFE-OP/OP) and WKC is not the disconnect signal anyway -- the
            // per-slave EtherCAT watchdog de-energizes a drive on bus loss.
            auto bus_error = [](DatagramState const&){};

            // Set while servicing a mailbox transfer for a slave that is NOT in the
            // operated mapping (or a whole-bus topology read): the FMMU mailbox-status
            // path only pumps mapped slaves, so force the generic sequencer step so the
            // unmapped slave's mailbox actually advances. Reset right after the command.
            bool service_unmapped = false;

            uint64_t jitter_rng = 0x9e3779b97f4a7c15ull ^ static_cast<uint64_t>(cycle_ms);  // xorshift64 state

            // One full cyclic tick: compute + apply each drive's setpoint, exchange
            // all PDOs together, advance one mailbox step, publish per-drive feedback.
            auto rt_cyclic = [&]
            {
                // Phase-lock the RT cadence to the DC reference captured by the previous cycle's
                // drift datagram. No-op until a valid reference exists; the timer remembers its own
                // overrun, so a late wake rejects its own suspect sample.
                bus->sync(timer);
                timer.wait_next_tick();

                // Optional master-side jitter: busy-wait a bounded pseudo-random delay before
                // sending the frame so the sampled DC phase is perturbed -- exercises the PLL.
                int64_t jitter = master_jitter_ns_.load();
                if (jitter > 0)
                {
                    int64_t const cap = milliseconds(cycle_ms).count() / 4;
                    if (jitter > cap) { jitter = cap; }
                    jitter_rng ^= jitter_rng << 13;
                    jitter_rng ^= jitter_rng >> 7;
                    jitter_rng ^= jitter_rng << 17;
                    nanoseconds const until = since_epoch()
                        + nanoseconds{static_cast<int64_t>(jitter_rng % static_cast<uint64_t>(jitter + 1))};
                    while (since_epoch() < until) { /* burn the injected jitter */ }
                }

                for (auto& r : rts)
                {
                    if (not r.drive) { continue; }  // generic slave: mapped + transitioned, no Drive control
                    Drive& drive = *r.drive;

                    if (r.units_dirty)
                    {
                        try { drive.setUnits(r.pending_units); }
                        catch (std::exception const&) {}
                        r.units_dirty = false;
                    }

                    // The bus-owned command set for this tick (fed by the queue).
                    MotorControl::Command cmd = r.cmd;

                    control::ControlMode m = static_cast<control::ControlMode>(cmd.mode);
                    drive.setModeOfOperationRaw(static_cast<int8_t>(m));
                    bool position_mode = (m == control::POSITION_CYCLIC);
                    bool velocity_mode = (m == control::VELOCITY_CYCLIC);
                    bool torque_mode   = (m == control::TORQUE_CYCLIC);

                    // Seed the command to the actual position the instant enable is
                    // commanded (marvin sets target_position = actual_position right
                    // before enable()): a CSP drive faults if commanded a step.
                    bool enable_now = cmd.enable;
                    if (enable_now and not r.prev_cmd_enable)
                    {
                        double seed = 0.0;
                        if (position_mode) { seed = drive.actualPosition(); }
                        cmd.manual_target  = seed;         // this tick
                        r.cmd.manual_target = seed;        // persist for the next ticks
                        r.jog_accum = drive.actualPosition();
                        r.ramped    = seed;
                        r.gen_start = since_epoch();
                    }

                    int source = cmd.source;
                    double si_target = 0.0;
                    if (source == static_cast<int>(SetpointSource::Manual))
                    {
                        // Slew toward the manual target at the ramp rate so a new
                        // setpoint never steps (which would fault a CSP drive).
                        double tgt  = cmd.manual_target;
                        double rate = cmd.manual_ramp;
                        if (rate > 0.0)
                        {
                            double step  = rate * dt;
                            double delta = tgt - r.ramped;
                            if (delta >  step) { delta =  step; }
                            if (delta < -step) { delta = -step; }
                            r.ramped += delta;
                        }
                        else
                        {
                            r.ramped = tgt;
                        }
                        si_target = r.ramped;
                    }
                    else if (source == static_cast<int>(SetpointSource::Jog))
                    {
                        if (r.prev_source != source) { r.jog_accum = drive.actualPosition(); }
                        if (position_mode) { r.jog_accum += cmd.jog_rate * dt; si_target = r.jog_accum; }
                        else               { si_target = cmd.jog_rate; }
                    }
                    else  // Generator
                    {
                        // Reset the phase origin (and re-center on the actual
                        // position) when the generator is first selected, so the
                        // waveform starts at zero -- mirrors marvin resetting
                        // start_time/initial_position when the sine begins.
                        if (r.prev_source != source)
                        {
                            r.gen_start = since_epoch();
                            if (position_mode)
                            {
                                double a = drive.actualPosition();
                                cmd.manual_target  = a;
                                r.cmd.manual_target = a;
                            }
                        }
                        double t     = seconds_f(elapsed_time(r.gen_start)).count();
                        double amp   = cmd.gen_amp;
                        double phase = tau * cmd.gen_freq * t;
                        double wave  = 0.0;
                        int wf = cmd.gen_wave;
                        if (wf == static_cast<int>(Waveform::Sine)) { wave = amp * std::sin(phase); }
                        else if (wf == static_cast<int>(Waveform::Step))
                        {
                            if (std::sin(phase) >= 0.0) { wave = amp; }
                            else                        { wave = -amp; }
                        }
                        else { wave = amp * (2.0 / pi) * std::asin(std::sin(phase)); }
                        double center = cmd.gen_offset;
                        if (position_mode) { center = cmd.manual_target; }
                        si_target = center + wave;
                    }
                    r.prev_source = source;

                    if (position_mode)
                    {
                        drive.setTargetPosition(si_target);
                        drive.setTargetVelocityRaw(0);
                        drive.setTargetTorqueRaw(0);
                    }
                    else if (velocity_mode)
                    {
                        drive.setTargetVelocity(si_target);
                        drive.setTargetPositionRaw(drive.actualPositionRaw());
                        drive.setTargetTorqueRaw(0);
                    }
                    else if (torque_mode)
                    {
                        drive.setTargetTorque(si_target);
                        drive.setTargetPositionRaw(drive.actualPositionRaw());
                        drive.setTargetVelocityRaw(0);
                    }

                    if (enable_now) { drive.enable(); }
                    else            { drive.disable(); }
                    r.prev_cmd_enable = enable_now;
                    drive.update();
                    if (drive.isFaulted() and r.was_enabled) { r.cmd.enable = false; }
                    r.was_enabled = drive.isEnabled();
                    r.last_target = si_target;
                }

                // Exchange every drive's process data in the same frame batch.
                bool any_pd = pd_active_any();
                std::string cyclic_error;
                try
                {
                    if (any_pd)
                    {
                        bus->sendLogicalRead(bus_error);
                        bus->sendLogicalWrite(bus_error);
                    }
                    if (service_unmapped)
                    {
                        // PDO only in this frame; the unmapped target's mailbox has no
                        // mailbox-status FMMU in the process image, so service ALL
                        // mailboxes below with the explicit-check path (same as idle).
                    }
                    else if (fmmu_opt and any_pd)
                    {
                        bus->sendReadMessages(bus_error);
                        bus->sendWriteMessages(bus_error);
                    }
                    else
                    {
                        sequencer.step(bus_error);   // generic: pumps EVERY slave's mailbox
                    }
                    bus->finalizeDatagrams();
                    bus->processAwaitingFrames();
                    // Explicit FPRD SM-status check + read/write for every slave (covers
                    // slaves with no mailbox-status FMMU). One extra frame round-trip,
                    // only while a mailbox transfer to an unmapped slave is in flight.
                    if (service_unmapped)
                    {
                        bus->checkMailboxes(bus_error);
                        bus->processMessages(bus_error);
                    }
                    consecutive_pd_errors = 0;
                }
                catch (std::exception const& e)
                {
                    ++consecutive_pd_errors;
                    cyclic_error = e.what();
                }

                for (auto& r : rts)
                {
                    Slave& slave = bus->slaves().at(r.control->slave_index_);
                    // Non-blocking: this is the 1 kHz RT thread; if the GUI is mid-read
                    // we skip this slave's display mirror this tick (<=1 ms stale, never
                    // affects the wire) rather than stall on the lock.
                    TryLockGuard lock(r.control->fb_mtx_);
                    if (not lock.owns())
                    {
                        continue;
                    }
                    r.control->error_ = cyclic_error;
                    // Raw process-image snapshot (works for generic + DS402 slaves).
                    if (slave.input.data and slave.input.bsize > 0)
                    {
                        r.control->in_raw_.assign(slave.input.data, slave.input.data + slave.input.bsize);
                    }
                    if (slave.output.data and slave.output.bsize > 0)
                    {
                        r.control->out_raw_.assign(slave.output.data, slave.output.data + slave.output.bsize);
                    }
                    if (not r.drive)  // generic slave: no DS402 feedback
                    {
                        continue;
                    }
                    Drive& drive = *r.drive;
                    r.control->fb_.status_word       = drive.statusWord();
                    r.control->fb_.control_word      = drive.controlWord();
                    r.control->fb_.error_code        = drive.errorCode();
                    r.control->fb_.mode_display      = drive.modeOfOperationDisplay();
                    r.control->fb_.actual_pos_raw    = drive.actualPositionRaw();
                    r.control->fb_.actual_vel_raw    = drive.actualVelocityRaw();
                    r.control->fb_.actual_torque_raw = drive.actualTorqueRaw();
                    r.control->fb_.actual_pos        = drive.actualPosition();
                    r.control->fb_.actual_vel        = drive.actualVelocity();
                    r.control->fb_.actual_torque     = drive.actualTorque();
                    r.control->fb_.target            = r.last_target;
                    r.control->fb_.enabled           = drive.isEnabled();
                    r.control->fb_.faulted           = drive.isFaulted();
                    r.control->fb_.operational       = (r.current_state == static_cast<int>(State::OPERATIONAL));
                }

                // Publish soft-PLL telemetry for the UI (DC lock panel).
                dc_active_.store(dc_enabled_.load());
                if (dc_enabled_)
                {
                    int64_t const pe = timer.pll().phase_error().count();
                    dc_locked_.store(timer.locked());
                    dc_phase_error_ns_.store(pe);
                    dc_filtered_error_ns_.store(static_cast<int64_t>(std::llround(timer.pll().filtered_error())));
                    dc_pll_samples_.store(timer.pll().samples());
                    dc_overran_.store(timer.overran());

                    // Decaying peak-hold of |phase error|, tracked on the RT thread so it catches
                    // every cycle's spike (the UI samples far slower). Fades over ~1 s so a spike
                    // stays visible long enough to read, then relaxes back toward the live error.
                    int64_t peak = dc_phase_peak_ns_.load();
                    int64_t const abs_pe = std::llabs(pe);
                    peak -= peak / 1024;
                    if (abs_pe > peak) { peak = abs_pe; }
                    dc_phase_peak_ns_.store(peak);
                }
            };

            // Apply one queued motor/units command to its DriveRuntime (latest-wins).
            auto apply_motor = [&](SdoCommand const& c)
            {
                for (auto& r : rts)
                {
                    if (r.control->slave_index_ != c.slave_index) { continue; }
                    if (c.kind == SdoCommand::Kind::Motor)
                    {
                        r.cmd = c.motor;
                    }
                    else  // MotorUnits
                    {
                        r.pending_units = c.motor_units;
                        r.units_dirty   = true;
                    }
                    break;
                }
            };

            // Seed pass: drain any motor/units commands already queued (e.g. from
            // configure or pre-operate edits) into the runtimes; keep blocking ones.
            {
                LockGuard lock(sdo_mtx_);
                std::deque<SdoCommand> keep;
                for (auto& c : sdo_queue_)
                {
                    if ((c.kind == SdoCommand::Kind::Motor) or (c.kind == SdoCommand::Kind::MotorUnits))
                    {
                        apply_motor(c);
                    }
                    else
                    {
                        keep.push_back(std::move(c));
                    }
                }
                sdo_queue_.swap(keep);
            }

            // Warm up the FMMU mailbox-status bits before the loop services any
            // mailbox transfer: the can_read/can_write flags are read from the LRD
            // image, so without a first read they are stale on the opening tick and
            // the first SDO/OD message would not be sent (then a retry succeeds).
            if (fmmu_opt)
            {
                bus->sendLogicalRead(bus_error);
                bus->finalizeDatagrams();
                bus->processAwaitingFrames();
            }

            while (not rt_stop_)
            {
                rt_cyclic();

                // Bus lost: de-energize every drive and bail out so the UI reflects
                // the dead bus instead of spinning forever on commanded drives.
                if (consecutive_pd_errors >= PD_ERROR_LIMIT)
                {
                    for (auto& r : rts) { r.cmd.enable = false; }
                    set_all_errors("Bus communication lost (" + std::to_string(consecutive_pd_errors)
                                 + " consecutive cycle errors) -- drives disabled.");
                    break;
                }

                // Per-drive EtherCAT state changes requested from the UI.
                for (auto& r : rts)
                {
                    int target_state = r.cmd.target_state;
                    if (target_state == r.current_state) { continue; }

                    Slave& slave = bus->slaves().at(r.control->slave_index_);
                    std::string transition_error;
                    try
                    {
                        bus->requestState(slave, static_cast<State>(target_state));
                        bus->waitForState(slave, static_cast<State>(target_state), 1000ms, rt_cyclic);
                        r.current_state = target_state;
                        setSlaveState(r.control->slave_index_, static_cast<uint8_t>(target_state));

                        bool pd_now = (target_state == static_cast<int>(State::SAFE_OP))
                                   or (target_state == static_cast<int>(State::OPERATIONAL));
                        if (not r.seeded and pd_now and r.drive)
                        {
                            double seed = 0.0;
                            if (static_cast<control::ControlMode>(r.cmd.mode) == control::POSITION_CYCLIC)
                            {
                                seed = r.drive->actualPosition();
                            }
                            r.cmd.manual_target = seed;
                            r.jog_accum = r.drive->actualPosition();
                            r.ramped    = seed;
                            r.seeded    = true;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        transition_error = "State change failed: " + describeError(e);
                        r.cmd.target_state = r.current_state;  // abandon the request, hold
                    }
                    Event ev;
                    ev.kind    = Event::Kind::StateActionResult;
                    ev.slave   = r.control->slave_index_;
                    ev.message = transition_error;
                    pushEvent(std::move(ev));
                }

                // An AL-event IRQ means a slave changed state on its own (e.g. a
                // drive dropped to SAFE-OP+error): re-read every slave's AL status.
                if (al_status_dirty_.exchange(false))
                {
                    refreshSlaveStates();
                }
                // NOTE: no diagnostics sweep (error counters / topology re-read) in the
                // cyclic loop -- those do synchronous register reads that stall the PDO
                // and risk the SM watchdog (OP->SAFE-OP flap). Diagnostics refresh only
                // in the idle phase, which is the connect-and-scan path the UI uses.

                // Leaving the cyclic phase (backToPreOp, or an INIT request that
                // just stopped it): leave the queue untouched. A State command
                // consumed here would die with the loop (translated into a runtime
                // that never ticks again); the idle phase services it instead.
                if (rt_stop_)
                {
                    continue;
                }

                // Apply all queued motor/units updates and state requests for operated
                // slaves (non-blocking, latest-wins), then service at most one blocking
                // mailbox request (SDO/OD/state/topology) this tick so a slow transfer
                // never stalls live motor control.
                auto runtime_for = [&](int slave_index) -> DriveRuntime*
                {
                    for (auto& r : rts)
                    {
                        if (r.control->slave_index_ == slave_index) { return &r; }
                    }
                    return nullptr;
                };
                SdoCommand cmd;
                bool have_cmd = false;
                {
                    LockGuard lock(sdo_mtx_);
                    while (not sdo_queue_.empty())
                    {
                        SdoCommand& front = sdo_queue_.front();
                        SdoCommand::Kind k = front.kind;
                        if ((k == SdoCommand::Kind::Motor) or (k == SdoCommand::Kind::MotorUnits))
                        {
                            apply_motor(front);
                            sdo_queue_.pop_front();
                            continue;
                        }
                        // A state change for an operated slave is a per-drive transition
                        // serviced by this loop (no mailbox work, never rebuilds).
                        if (k == SdoCommand::Kind::State)
                        {
                            DriveRuntime* r = runtime_for(front.slave_index);
                            if (r != nullptr)
                            {
                                r->cmd.target_state = front.state;
                                sdo_queue_.pop_front();
                                continue;
                            }
                        }
                        cmd = std::move(front);
                        sdo_queue_.pop_front();
                        have_cmd = true;
                        break;
                    }
                }
                if (have_cmd)
                {
                    // A slave not in the operated mapping has no mailbox-status FMMU, so
                    // its mailbox only advances under the generic step. Topology reads the
                    // whole bus, so treat it the same.
                    service_unmapped = (cmd.kind == SdoCommand::Kind::Topology)
                                    or (runtime_for(cmd.slave_index) == nullptr);
                    dispatchCommand(cmd, rt_cyclic);
                    service_unmapped = false;
                }
            }

            dc_active_.store(false);   // left the cyclic phase: PLL no longer disciplining
            dc_phase_peak_ns_.store(0);

            try { bus->disableIRQ(EcatEvent::AL_STATUS); } catch (std::exception const&) {}

            // Graceful stop: de-energize all and cycle until the state machines settle.
            for (auto& r : rts) { if (r.drive) { r.drive->disable(); } }
            for (int i = 0; i < 300; ++i)
            {
                bool busy = false;
                for (auto& r : rts) { if (r.drive and (r.drive->isEnabled() or r.drive->isFaulted())) { busy = true; } }
                if (not busy) { break; }
                timer.wait_next_tick();
                try
                {
                    bus->sendLogicalRead(bus_error);
                    bus->sendLogicalWrite(bus_error);
                    bus->finalizeDatagrams();
                    bus->processAwaitingFrames();
                }
                catch (std::exception const&) {}
                for (auto& r : rts) { if (r.drive) { r.drive->update(); } }
            }

            try
            {
                link_->setTimeout(2ms);
                bus->requestState(State::PRE_OP);
                bus->waitForState(State::PRE_OP, 2000ms);
            }
            catch (std::exception const&) {}
        }
        catch (std::exception const& e)
        {
            set_all_errors("Operation failed: " + describeError(e));
        }

        for (auto& r : rts)
        {
            setSlaveState(r.control->slave_index_, static_cast<uint8_t>(State::PRE_OP));
        }
        rt_running_ = false;
    }

    // ---------------- SDO / Object Dictionary service ----------------

    void BusSession::enqueue(SdoCommand cmd)
    {
        LockGuard lock(sdo_mtx_);
        sdo_queue_.push_back(std::move(cmd));
        sdo_cv_.signal();
    }

    // --- MotorControl -> bus actor (no shared command mutex; latest-wins) ---
    void MotorControl::submit()
    {
        if (session_ == nullptr) { return; }
        BusSession::SdoCommand c;
        c.kind        = BusSession::SdoCommand::Kind::Motor;
        c.slave_index = slave_index_;
        c.motor       = cmd_;
        session_->enqueue(std::move(c));
    }

    void MotorControl::setUnits(CoE::CiA::DS402::UnitConfig const& u)
    {
        if (session_ == nullptr) { return; }
        BusSession::SdoCommand c;
        c.kind        = BusSession::SdoCommand::Kind::MotorUnits;
        c.slave_index = slave_index_;
        c.motor_units = u;
        session_->enqueue(std::move(c));
    }

    void BusSession::serviceLoop()
    {
        // Idle mailbox step: no cyclic PDO exchange, just advance the mailbox
        // queue and poll states.
        auto idle_cyclic = [this]
        {
            auto noerr = [](DatagramState const&){};
            bus_->checkMailboxes(noerr);
            bus_->processMessages(noerr);
            sleep(200us);  // pace the poll, don't busy-spin
        };

        while (true)
        {
            SdoCommand cmd;
            bool have_cmd = false;
            {
                LockGuard lock(sdo_mtx_);
                sdo_cv_.wait_until(sdo_mtx_, 200ms,
                    [this]{ return bus_stop_ or operate_requested_ or not sdo_queue_.empty(); });
                if (bus_stop_ or operate_requested_)
                {
                    return;   // leave the idle phase (shutdown, or enter cyclic)
                }
                if (not sdo_queue_.empty())
                {
                    cmd = std::move(sdo_queue_.front());
                    sdo_queue_.pop_front();
                    have_cmd = true;
                }
            }

            if (not have_cmd)
            {
                refreshSlaveStates();   // periodic AL-status poll while idle
                refreshDiagnostics();   // + per-port error counters / AL codes
            }
            else if ((cmd.kind == SdoCommand::Kind::Motor) or (cmd.kind == SdoCommand::Kind::MotorUnits))
            {
                // No drive runs while idle; motor commands are seeded from config at
                // operate, so any that arrive here are simply dropped.
            }
            else
            {
                dispatchCommand(cmd, idle_cyclic);
            }
        }
    }

    void BusSession::dispatchCommand(SdoCommand& cmd, std::function<void()> const& cyclic)
    {
        switch (cmd.kind)
        {
            case SdoCommand::Kind::Discover:    { executeDiscover(cmd.slave_index, cyclic, cmd.od_resume); break; }
            case SdoCommand::Kind::State:       { executeState(cmd.slave_index, cmd.state, cyclic); break; }
            case SdoCommand::Kind::ReadMapping: { executeReadMapping(cmd.slave_index, cyclic); break; }
            case SdoCommand::Kind::Topology:    { executeTopology(cyclic); break; }
            case SdoCommand::Kind::ClearErrors: { diag_clear_pending_ = true; refreshDiagnostics(); break; }  // apply now, any phase
            case SdoCommand::Kind::Read:
            case SdoCommand::Kind::Write:       { executeSdo(cmd, cyclic); break; }
            case SdoCommand::Kind::Motor:
            case SdoCommand::Kind::MotorUnits:  { break; }   // phase-local; handled before dispatch
        }
    }

    bool BusSession::aborting() const
    {
        // Abort an in-flight mailbox op on any phase change: the whole thread is
        // stopping (bus_stop_), we are leaving the cyclic phase (rt_stop_), or an
        // idle scan must yield to an operate request. An OD scan that hits this
        // re-enqueues itself and resumes in the new phase (executeDiscover).
        if (bus_stop_)
        {
            return true;
        }
        if (rt_running_)
        {
            return rt_stop_;
        }
        return operate_requested_;   // idle phase: yield to applyMapping
    }

    bool BusSession::driveMessage(std::shared_ptr<mailbox::request::AbstractMessage> const& msg,
                                  std::function<void()> const& cyclic)
    {
        while ((msg->status() == mailbox::request::MessageStatus::RUNNING) and (not aborting()))
        {
            cyclic();
        }
        return msg->status() == mailbox::request::MessageStatus::SUCCESS;
    }

    void BusSession::executeState(int slave_index, uint8_t state, std::function<void()> const& cyclic)
    {
        Bus* bus = bus_.get();
        std::string error;
        try
        {
            if (bus == nullptr)
            {
                THROW_ERROR("no bus");
            }
            Slave& slave = bus->slaves().at(slave_index);
            bus->requestState(slave, static_cast<State>(state));
            bus->waitForState(slave, static_cast<State>(state), 1500ms, cyclic);  // keeps PDO alive
        }
        catch (std::exception const& e)
        {
            error = std::string("State change to ") + toString(static_cast<State>(state))
                  + " failed: " + describeError(e);
        }
        Event ev;
        ev.kind    = Event::Kind::StateActionResult;
        ev.slave   = slave_index;
        ev.message = error;
        pushEvent(std::move(ev));
        refreshSlaveStates();
    }

    void BusSession::executeReadMapping(int slave_index, std::function<void()> const& cyclic)
    {
        Bus* bus = bus_.get();
        PdoMapping mapping;
        mapping.slave = slave_index;

        auto readU = [&](Slave& s, uint16_t index, uint8_t sub, int nbytes) -> uint32_t
        {
            uint8_t buf[4] = {0, 0, 0, 0};
            uint32_t size = static_cast<uint32_t>(nbytes);
            auto msg = s.mailbox.createSDO(index, sub, false, CoE::SDO::request::UPLOAD, buf, &size, 1s);
            if (not driveMessage(msg, cyclic))
            {
                THROW_ERROR("mailbox read failed");
            }
            uint32_t value = 0;
            for (uint32_t i = 0; (i < size) and (i < 4); ++i)
            {
                value |= static_cast<uint32_t>(buf[i]) << (8 * i);
            }
            return value;
        };

        try
        {
            if (bus == nullptr)
            {
                THROW_ERROR("no bus");
            }
            Slave& slave = bus->slaves().at(slave_index);

            auto readAssign = [&](uint16_t sm_assign, std::vector<PdoEntry>& out)
            {
                uint32_t pdo_count = readU(slave, sm_assign, 0, 1);
                for (uint32_t i = 1; i <= pdo_count; ++i)
                {
                    uint16_t pdo = static_cast<uint16_t>(readU(slave, sm_assign, static_cast<uint8_t>(i), 2));
                    if (pdo == 0)
                    {
                        continue;
                    }
                    uint32_t entry_count = readU(slave, pdo, 0, 1);
                    for (uint32_t j = 1; j <= entry_count; ++j)
                    {
                        uint32_t word = readU(slave, pdo, static_cast<uint8_t>(j), 4);
                        CoE::PdoMappingEntry me = CoE::fromMappingWord(word);
                        PdoEntry pe;
                        pe.pdo   = pdo;
                        pe.index = me.index;
                        pe.sub   = me.subindex;
                        pe.bits  = me.bitlen;
                        out.push_back(pe);
                    }
                }
            };

            readAssign(0x1C12, mapping.rx);
            readAssign(0x1C13, mapping.tx);
            mapping.valid = true;
        }
        catch (std::exception const& e)
        {
            mapping.error = e.what();
        }

        Event ev;
        ev.kind    = Event::Kind::MappingResult;
        ev.slave   = slave_index;
        ev.mapping = std::move(mapping);
        pushEvent(std::move(ev));
    }

    void BusSession::executeTopology(std::function<void()> const& /*cyclic*/)
    {
        if (bus_ == nullptr)
        {
            return;
        }
        TopologyInfo info = computeTopology(*bus_, true);  // re-read DL status from the wire
        LockGuard lock(topo_mtx_);
        topology_ = std::move(info);
    }

    void BusSession::refreshSlaveStates()
    {
        Bus* bus = bus_.get();
        if (bus == nullptr)
        {
            return;
        }
        // One batched round-trip for every slave: per-slave getCurrentState would
        // be N sequential reads, each racing the link timeout (500us during the
        // cyclic phase, where this runs on the AL-event IRQ) and stalling the PDO.
        auto& slaves = bus->slaves();
        auto noerr = [](DatagramState const&){};
        try
        {
            for (auto& slave : slaves)
            {
                bus->sendGetALStatus(slave, noerr);
            }
            bus->processAwaitingFrames();
        }
        catch (std::exception const&) {}

        std::vector<uint8_t> states;
        {
            LockGuard lock(state_mtx_);
            states = slave_states_;
        }
        states.resize(slaves.size(), 0);
        for (size_t i = 0; i < slaves.size(); ++i)
        {
            uint8_t s = static_cast<uint8_t>(slaves[i].al_status);
            // A late answer leaves State::INVALID (the lib clobbers the cache
            // before sending): no new information, keep the last-known state
            // rather than flashing "?" -- the ESC answered, the deadline lost.
            if (s != static_cast<uint8_t>(State::INVALID))
            {
                // Device emulation mirrors AL control: the error bit is the
                // master's own ack request echoed back, not an error.
                if ((i < slave_emulated_.size()) and (slave_emulated_[i] != 0))
                {
                    s = static_cast<uint8_t>(s & ~AL_STATUS_ERR_IND);
                }
                states[i] = s;
            }
        }
        LockGuard lock(state_mtx_);
        slave_states_ = std::move(states);
    }

    void BusSession::refreshDiagnostics()
    {
        Bus* bus = bus_.get();
        if (bus == nullptr)
        {
            return;
        }
        auto noerr = [](DatagramState const&){};
        try
        {
            bus->sendRefreshErrorCounters(noerr);   // FPRD per slave -> slave.error_counters
            bus->processAwaitingFrames();
        }
        catch (std::exception const&)
        {
            return;   // a dead/garbled bus: keep the last good diagnostics
        }

        bool const manual = diag_clear_pending_;
        diag_clear_pending_ = false;
        bool need_clear = manual;
        constexpr uint8_t HIGH_WATER = 0xC0;   // clear before the uint8 saturates at 0xFF

        auto& slaves = bus->slaves();
        {
            LockGuard lock(state_mtx_);
            slave_diag_.resize(slaves.size());
            for (size_t i = 0; i < slaves.size(); ++i)
            {
                SlaveDiag& d = slave_diag_[i];
                ErrorCounters const& cur = slaves[i].errorCounters();
                d.stats.al_status_code = slaves[i].al_status_code;  // valid when the AL error bit is set
                if (manual)
                {
                    for (int p = 0; p < 4; ++p) { d.stats.lost_total[p] = 0; d.stats.rxerr_total[p] = 0; }
                    d.stats.saturated = false;
                }
                for (int p = 0; p < 4; ++p)
                {
                    // Accumulate the monotone delta; a drop means it was cleared.
                    uint8_t ll = cur.lost_link[p];
                    uint8_t dll = ll;
                    if (ll >= d.last_lost[p]) { dll = ll - d.last_lost[p]; }
                    d.stats.lost_total[p] += dll;
                    d.last_lost[p]         = ll;

                    uint8_t rx = cur.rx[p].invalid_frame;
                    uint8_t drx = rx;
                    if (rx >= d.last_rxerr[p]) { drx = rx - d.last_rxerr[p]; }
                    d.stats.rxerr_total[p] += drx;
                    d.last_rxerr[p]         = rx;

                    if ((ll >= HIGH_WATER) or (rx >= HIGH_WATER)) { need_clear = true; }
                    if ((ll == 0xFF) or (rx == 0xFF))             { d.stats.saturated = true; }
                }
            }
        }

        if (need_clear)
        {
            try { bus->clearErrorCounters(); }   // broadcast write; throws on WKC short -- tolerated
            catch (std::exception const&) {}
            LockGuard lock(state_mtx_);
            for (auto& d : slave_diag_)
            {
                for (int p = 0; p < 4; ++p) { d.last_lost[p] = 0; d.last_rxerr[p] = 0; }
            }
        }
    }

    void BusSession::setSlaveState(int index, uint8_t state)
    {
        LockGuard lock(state_mtx_);
        if ((index >= 0) and (index < static_cast<int>(slave_states_.size())))
        {
            slave_states_[index] = state;
        }
    }

    uint8_t BusSession::slaveAlStatus(int index) const
    {
        auto s = snapshot();
        if (s and (index >= 0) and (index < static_cast<int>(s->slaves.size())))
        {
            return s->slaves[index].al_status;
        }
        return 0;
    }

    std::shared_ptr<SdoResult> BusSession::readSDO(int slave_index, uint16_t index, uint8_t subindex, int access)
    {
        auto result = std::make_shared<SdoResult>();
        if (not sdoAvailable())
        {
            result->message = "SDO unavailable (not connected).";
            result->done    = true;
            return result;
        }
        SdoCommand cmd;
        cmd.kind        = SdoCommand::Kind::Read;
        cmd.slave_index = slave_index;
        cmd.index       = index;
        cmd.subindex    = subindex;
        cmd.access      = access;
        cmd.result      = result;
        enqueue(std::move(cmd));
        return result;
    }

    std::shared_ptr<SdoResult> BusSession::writeSDO(int slave_index, uint16_t index, uint8_t subindex,
                                                    int access, std::vector<uint8_t> data)
    {
        auto result = std::make_shared<SdoResult>();
        if (not sdoAvailable())
        {
            result->message = "SDO unavailable (not connected).";
            result->done    = true;
            return result;
        }
        SdoCommand cmd;
        cmd.kind        = SdoCommand::Kind::Write;
        cmd.slave_index = slave_index;
        cmd.index       = index;
        cmd.subindex    = subindex;
        cmd.access      = access;
        cmd.payload     = std::move(data);
        cmd.result      = result;
        enqueue(std::move(cmd));
        return result;
    }

    void BusSession::requestSlaveState(int slave_index, uint8_t state)
    {
        if (not sdoAvailable())
        {
            return;
        }
        State const target = static_cast<State>(state);
        auto c = findControl(slave_index);

        // Safety: never tear the PDO down underneath a moving motor. An enabled
        // operated drive leaving OP is de-energized first.
        if (c and rt_running_ and (target != State::OPERATIONAL))
        {
            bool enabled = false;
            {
                LockGuard lock(c->fb_mtx_);
                enabled = c->fb_.enabled;
            }
            if (enabled)
            {
                c->enable(false);
            }
        }
        // INIT tears down the mailbox/FMMU config; the cyclic loop can't take an
        // OPERATED slave there (its mailbox step fails mid-transition and the
        // request is abandoned). Stop the loop first -- same as backToPreOp --
        // then the request runs on the idle bus owner. A slave outside the
        // operated set transitions live without disturbing the loop.
        if ((target == State::INIT) and rt_running_ and c)
        {
            backToPreOp();
        }
        // Keep the UI mirror coherent: a later motor verb must not carry a stale
        // target_state and silently revert this request.
        if (c)
        {
            c->cmd_.target_state = static_cast<int>(target);
        }
        // The cyclic loop translates this into the per-drive runtime when it
        // operates the slave; otherwise it runs as a mailbox transition.
        SdoCommand cmd;
        cmd.kind        = SdoCommand::Kind::State;
        cmd.slave_index = slave_index;
        cmd.state       = state;
        enqueue(std::move(cmd));
    }

    std::string BusSession::stateActionError(int slave_index) const
    {
        auto it = state_errors_.find(slave_index);
        if (it == state_errors_.end())
        {
            return {};
        }
        return it->second;
    }

    void BusSession::readPdoMapping(int slave_index)
    {
        if (not sdoAvailable())
        {
            return;
        }
        auto& s = pdo_scans_[slave_index];   // UI thread
        if (s.running) { return; }
        s.running = true;
        SdoCommand cmd;
        cmd.kind        = SdoCommand::Kind::ReadMapping;
        cmd.slave_index = slave_index;
        enqueue(std::move(cmd));
    }

    PdoScan BusSession::pdoScan(int slave_index) const
    {
        auto it = pdo_scans_.find(slave_index);
        if (it == pdo_scans_.end()) { return PdoScan{}; }
        return it->second;
    }

    OdScan BusSession::odScan(int slave_index) const
    {
        auto it = od_scans_.find(slave_index);
        if (it == od_scans_.end()) { return OdScan{}; }
        OdScan scan = it->second;
        scan.scanned = true;
        return scan;
    }

    void BusSession::discoverOD(int slave_index)
    {
        if (not sdoAvailable())
        {
            return;
        }
        auto& s = od_scans_[slave_index];   // UI thread
        if (s.running) { return; }
        s.running = true;
        s.count   = 0;
        s.total   = 0;
        s.objects.clear();
        s.error.clear();
        SdoCommand cmd;
        cmd.kind        = SdoCommand::Kind::Discover;
        cmd.slave_index = slave_index;
        enqueue(std::move(cmd));
    }

    void BusSession::executeSdo(SdoCommand& cmd, std::function<void()> const& cyclic)
    {
        Bus* bus = bus_.get();
        if ((bus == nullptr) or (cmd.result == nullptr))
        {
            if (cmd.result != nullptr)   // never leave a caller polling forever
            {
                cmd.result->ok      = false;
                cmd.result->message = "bus not available";
                cmd.result->done    = true;
            }
            return;
        }
        bool complete = (cmd.access == static_cast<int>(Bus::Access::COMPLETE));
        try
        {
            Slave& slave = bus->slaves().at(cmd.slave_index);
            if (cmd.kind == SdoCommand::Kind::Read)
            {
                std::vector<uint8_t> buf(1024);
                uint32_t const capacity = static_cast<uint32_t>(buf.size());
                uint32_t size = capacity;
                auto msg = slave.mailbox.createSDO(cmd.index, cmd.subindex, complete,
                                                   CoE::SDO::request::UPLOAD, buf.data(), &size, 1s);
                if (driveMessage(msg, cyclic))
                {
                    buf.resize(size);
                    cmd.result->data    = std::move(buf);
                    cmd.result->ok      = true;
                    cmd.result->message = "read OK (" + std::to_string(size) + " bytes)";
                    if (size >= capacity)
                    {
                        cmd.result->message += " -- may be truncated to " + std::to_string(capacity) + " bytes";
                    }
                }
                else
                {
                    cmd.result->ok      = false;
                    cmd.result->message = CoE::SDO::abort_to_str(msg->status());
                }
            }
            else
            {
                uint32_t size = static_cast<uint32_t>(cmd.payload.size());
                auto msg = slave.mailbox.createSDO(cmd.index, cmd.subindex, complete,
                                                   CoE::SDO::request::DOWNLOAD, cmd.payload.data(), &size, 1s);
                if (driveMessage(msg, cyclic))
                {
                    cmd.result->ok      = true;
                    cmd.result->message = "write OK (" + std::to_string(cmd.payload.size()) + " bytes)";
                }
                else
                {
                    cmd.result->ok      = false;
                    cmd.result->message = CoE::SDO::abort_to_str(msg->status());
                }
            }
        }
        catch (std::exception const& e)
        {
            cmd.result->ok      = false;
            cmd.result->message = e.what();
        }
        cmd.result->done = true;
    }

    void BusSession::executeDiscover(int slave_index, std::function<void()> const& cyclic, int resume_from)
    {
        namespace info = CoE::SDO::information;
        Bus* bus = bus_.get();
        std::string error;

        // Continue this scan in the next bus phase from object `from` (the OD list is
        // deterministic, so the resumed call just re-fetches it). Used when a phase
        // change -- backToPreOp / applyMapping -- interrupts a scan that is NOT done.
        auto requeue = [&](int from)
        {
            SdoCommand r;
            r.kind        = SdoCommand::Kind::Discover;
            r.slave_index = slave_index;
            r.od_resume   = from;
            enqueue(std::move(r));
        };

        try
        {
            if (bus == nullptr)
            {
                THROW_ERROR("no bus");
            }
            Slave& slave = bus->slaves().at(slave_index);

            // The OD index list is deterministic, so a resumed scan just re-fetches it
            // (one cheap transaction) and skips to where it left off -- no need to carry
            // the list across the phase change.
            uint16_t list[2048];
            uint32_t list_size = sizeof(list);
            auto od_list = slave.mailbox.createSDOInfoGetODList(info::ListType::ALL, list, &list_size, 500ms);
            if (not driveMessage(od_list, cyclic))
            {
                if (aborting() and (not bus_stop_))
                {
                    requeue(resume_from);   // interrupted before the list -> continue next phase
                    return;
                }
                THROW_ERROR("GetODList failed");
            }

            int n = static_cast<int>(list_size / 2) - 1;  // list[0] is the list type echo
            if (n < 0)
            {
                n = 0;
            }
            if (resume_from == 0)   // a fresh scan announces the total; a resume keeps it
            {
                Event ev;
                ev.kind  = Event::Kind::OdScanProgress;
                ev.slave = slave_index;
                ev.total = n;
                pushEvent(std::move(ev));
            }

            // Bail if reads keep timing out: a desynced mailbox would otherwise hang
            // the scan (and block mapping). Reset on every successful read.
            int consec_fail = 0;
            constexpr int FAIL_LIMIT = 10;

            // On resume the list must still hold objects past resume_from. If it
            // shrank to at or below it (slave reloaded, or a transient SDO-Info error
            // returned a short list), the loop below skips silently -- without this
            // we'd report a complete scan that is actually truncated.
            if ((resume_from > 0) and (resume_from >= n))
            {
                error = "discovery incomplete: object list shrank on resume (slave changed)";
            }

            int i = resume_from;
            for (; (i < n) and (consec_fail < FAIL_LIMIT); ++i)
            {
                if (aborting()) { break; }   // don't start object i (resume from it later)
                OdObject obj;
                obj.index = list[i + 1];

                char buf[1024];
                uint32_t bsize = sizeof(buf);
                auto od = slave.mailbox.createSDOInfoGetOD(obj.index, buf, &bsize, 500ms);
                if (driveMessage(od, cyclic) and (bsize >= sizeof(info::ObjectDescription)))
                {
                    auto const* d = reinterpret_cast<info::ObjectDescription const*>(buf);
                    obj.data_type    = static_cast<CoE::DataType>(d->data_type);
                    obj.max_subindex = d->max_subindex;
                    obj.object_code  = static_cast<CoE::ObjectCode>(d->object_code);
                    obj.name.assign(buf + sizeof(info::ObjectDescription),
                                    bsize - sizeof(info::ObjectDescription));
                }

                // Describe one subindex (GetED) and, when readable, read its value.
                // ed_ok reports whether the entry exists; fallback reads the value
                // when GetED is unavailable (sub0 only). Reading a non-existent or
                // write-only entry aborts, so we never do it.
                auto readEntry = [&](uint8_t sub, bool& ed_ok, bool fallback) -> OdEntry
                {
                    OdEntry e;
                    e.subindex = sub;
                    char ed[512];
                    uint32_t esize = sizeof(ed);
                    auto edm = slave.mailbox.createSDOInfoGetED(obj.index, sub, 0, ed, &esize, 200ms);
                    ed_ok = driveMessage(edm, cyclic) and (esize >= sizeof(info::EntryDescription));
                    uint32_t bytes = 256;  // fallback size when the entry size is unknown
                    if (ed_ok)
                    {
                        auto const* d = reinterpret_cast<info::EntryDescription const*>(ed);
                        e.access    = d->access;
                        e.data_type = static_cast<CoE::DataType>(d->data_type);
                        e.name.assign(ed + sizeof(info::EntryDescription), esize - sizeof(info::EntryDescription));
                        // Size the read buffer to the entry: a too-small buffer aborts
                        // with CLIENT_BUFFER_TOO_SMALL and desyncs the mailbox.
                        bytes = (d->bit_length + 7u) / 8u;
                        if (bytes == 0) { bytes = 1; }
                    }
                    bool readable = (e.access & CoE::Access::READ) != 0;
                    if ((ed_ok and readable) or (not ed_ok and fallback))
                    {
                        std::vector<uint8_t> v(bytes);
                        uint32_t vsize = static_cast<uint32_t>(v.size());
                        auto vm = slave.mailbox.createSDO(obj.index, sub, false, CoE::SDO::request::UPLOAD,
                                                          v.data(), &vsize, 200ms);
                        if (driveMessage(vm, cyclic))
                        {
                            v.resize(vsize);
                            e.value = std::move(v);
                            consec_fail = 0;
                        }
                        else
                        {
                            e.value_error = CoE::SDO::abort_to_str(vm->status());
                            ++consec_fail;
                        }
                    }
                    return e;
                };

                bool ed0 = false;
                OdEntry sub0 = readEntry(0, ed0, true);
                obj.access      = sub0.access;
                obj.value_error = sub0.value_error;
                int count = obj.max_subindex;          // sub0 holds the populated count
                if (not sub0.value.empty()) { count = sub0.value[0]; }
                obj.value = std::move(sub0.value);

                // RECORD/ARRAY: enumerate the actual fields -- otherwise only sub0
                // (the count) is visible, which is useless without the entries. Stop
                // at the first absent subindex: sub0 often over-reports the count and
                // reading the missing tail just aborts.
                bool is_complex = (obj.object_code == CoE::ObjectCode::ARRAY)
                               or (obj.object_code == CoE::ObjectCode::RECORD);
                if (is_complex)
                {
                    if (count > obj.max_subindex) { count = obj.max_subindex; }
                    for (int sub = 1; (sub <= count) and (not aborting()) and (consec_fail < FAIL_LIMIT); ++sub)
                    {
                        bool ed_sub = false;
                        OdEntry e = readEntry(static_cast<uint8_t>(sub), ed_sub, false);
                        if (not ed_sub) { break; }
                        obj.entries.push_back(std::move(e));
                    }
                }

                if (aborting()) { break; }   // aborted mid-object: drop the partial, redo it on resume

                Event ev;
                ev.kind      = Event::Kind::OdScanObject;
                ev.slave     = slave_index;
                ev.od_object = std::move(obj);
                if (not pushEvent(std::move(ev)))
                {
                    // The UI has not drained for thousands of objects -- it is
                    // wedged. Stop rather than spin; the bound did its job.
                    error = "discovery stopped: UI event queue full (consumer stalled)";
                    break;
                }
            }

            // Interrupted by a phase change (not a disconnect, not a desync) before the
            // end: continue from object i in the next phase instead of stopping short.
            if ((i < n) and (consec_fail < FAIL_LIMIT) and aborting() and (not bus_stop_))
            {
                requeue(i);
                return;
            }
            if (consec_fail >= FAIL_LIMIT)
            {
                error = "discovery stopped after repeated SDO timeouts (mailbox out of sync)";
            }
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }

        Event done;
        done.kind    = Event::Kind::OdScanDone;
        done.slave   = slave_index;
        done.message = error;
        pushEvent(std::move(done));
    }

}
