#include "Simulator.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#ifdef __linux__
#include <climits>
#include <csignal>
#include <cstring>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kickcat/OS/SharedMemory.h"
#endif

namespace kickcat::kickui
{
    namespace
    {
        // Downstream ports handed out to a parent's children in add order; port 0 is
        // the upstream link. The 1 -> 3 -> 2 order matches EtherCAT processing order.
        constexpr int SIM_DOWNSTREAM_PORTS[3] = {1, 3, 2};
    }

    std::vector<int> SimScene::assignedPorts() const
    {
        std::vector<int> ports(slaves.size(), -1);
        std::vector<int> used(slaves.size(), 0);   // downstream ports taken per parent
        for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
        {
            int p = slaves[i].parent;
            if (p < 0) { continue; }
            if ((p < static_cast<int>(used.size())) and (used[p] < 3))
            {
                ports[i] = SIM_DOWNSTREAM_PORTS[used[p]];
                used[p] += 1;
            }
        }
        return ports;
    }

    void SimScene::save(char const* path, std::string& message) const
    {
        std::ofstream f(path);
        if (not f) { message = std::string("cannot write ") + path; return; }
        int red = 0;
        if (redundancy) { red = 1; }
        f << "# kickui sim scene\n";
        f << "redundancy " << red << "\n";
        for (SimSlave const& s : slaves)
        {
            f << "slave " << s.config << " " << s.parent << "\n";
        }
        if (not f)
        {
            message = std::string("write error on ") + path;
            return;
        }
        message = std::string("saved ") + path;
    }

    void SimScene::load(char const* path, std::string& message)
    {
        std::ifstream f(path);
        if (not f) { message = std::string("cannot open ") + path; return; }
        std::vector<SimSlave> loaded;
        bool red = false;
        std::string tok;
        while (f >> tok)
        {
            if (tok == "redundancy")
            {
                int v = 0;
                f >> v;
                red = (v != 0);
            }
            else if (tok == "slave")
            {
                SimSlave s;
                std::string cfg;
                int parent = -1;
                f >> cfg >> parent;
                std::snprintf(s.config, sizeof(s.config), "%s", cfg.c_str());
                s.parent = parent;
                loaded.push_back(s);
            }
            else
            {
                std::string rest;   // comment / unknown: skip the line
                std::getline(f, rest);
            }
        }
        if (loaded.empty()) { message = std::string("no slaves in ") + path; return; }
        // Keep the tree acyclic: a parent must reference an earlier slave.
        for (int i = 0; i < static_cast<int>(loaded.size()); ++i)
        {
            if (loaded[i].parent >= i) { loaded[i].parent = -1; }
        }
        slaves     = std::move(loaded);
        redundancy = red;
        message    = std::string("loaded ") + path;
    }

    bool SimScene::writeTopologyFile(std::string const& path, std::string& error) const
    {
        int roots = 0;
        int root_index = 0;
        for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
        {
            if (slaves[i].parent < 0)
            {
                ++roots;
                root_index = i;
            }
        }
        if (roots != 1)
        {
            error = "topology needs exactly one master-connected slave (root)";
            return false;
        }

        // Each child gets a DISTINCT downstream port on its parent: two children
        // sharing a port would overwrite each other in EmulatedNetwork::connect()
        // and silently orphan a slave.
        std::vector<int> ports = assignedPorts();

        std::string json = "{\n";
        json += "  \"injection\": {\"node\": " + std::to_string(root_index) + ", \"port\": 0},\n";
        json += "  \"links\": [\n";
        bool first = true;
        for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
        {
            SimSlave const& s = slaves[i];
            if (s.parent < 0) { continue; }
            if (ports[i] < 0)
            {
                error = "S" + std::to_string(s.parent) + " has more than 3 children (max 3 downstream ports)";
                return false;
            }
            if (not first) { json += ",\n"; }
            first = false;
            json += "    {\"a\": " + std::to_string(s.parent) +
                    ", \"port_a\": " + std::to_string(ports[i]) +
                    ", \"b\": " + std::to_string(i) + ", \"port_b\": 0}";
        }
        json += "\n  ]";
        if (redundancy)
        {
            // The redundant master port closes the ring on the tail slave's
            // spare port (port 1, the downstream side of the last slave).
            int tail = static_cast<int>(slaves.size()) - 1;
            json += ",\n  \"redundancy_injection\": {\"node\": " + std::to_string(tail) + ", \"port\": 1}";
        }
        json += "\n}\n";

        std::ofstream out(path);
        if (not out.is_open())
        {
            error = "cannot write topology file " + path;
            return false;
        }
        out << json;
        if (not out)
        {
            error = "write error on topology file " + path;
            return false;
        }
        return true;
    }

    namespace
    {
        // Visit a configured-tree node and its children in EtherCAT processing
        // order (downstream ports 3,1,2 -- the order the master scans), recording
        // editor indices. ports[child] is the auto-assigned downstream port.
        void dfsEditorOrder(int node, std::vector<std::vector<int>> const& children,
                            std::vector<int> const& ports, std::vector<int>& order)
        {
            order.push_back(node);
            auto procRank = [](int port) -> int
            {
                if (port == 3) { return 0; }
                if (port == 1) { return 1; }
                if (port == 2) { return 2; }
                return 3;
            };
            std::vector<std::pair<int, int>> ranked;   // (processing rank, child)
            for (int child : children[node])
            {
                ranked.push_back({procRank(ports[child]), child});
            }
            std::sort(ranked.begin(), ranked.end(),
                      [](auto const& a, auto const& b) { return a.first < b.first; });
            for (auto const& pr : ranked)
            {
                dfsEditorOrder(pr.second, children, ports, order);
            }
        }
    }

    std::vector<int> SimScene::scanToEditorOrder() const
    {
        int const n = static_cast<int>(slaves.size());
        std::vector<std::vector<int>> children(n);
        int root = -1;
        for (int i = 0; i < n; ++i)
        {
            int p = slaves[i].parent;
            if (p < 0)
            {
                if (root >= 0) { return {}; }   // more than one root
                root = i;
            }
            else if (p < i)
            {
                children[p].push_back(i);
            }
            else
            {
                return {};   // forward/self reference: not a valid tree
            }
        }
        if (root < 0) { return {}; }
        std::vector<int> order;
        dfsEditorOrder(root, children, assignedPorts(), order);
        if (static_cast<int>(order.size()) != n) { return {}; }
        return order;
    }

#ifdef __linux__
    // Resolve the network_simulator binary next to this executable
    // (build/<...>/simulation/network_simulator vs .../tools/kickui/kickui).
    std::string SimulatorProcess::simulatorPath() const
    {
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) { return "network_simulator"; }
        buf[n] = '\0';
        std::string exe = buf;
        std::string dir = exe.substr(0, exe.find_last_of('/'));
        return dir + "/../../simulation/network_simulator";
    }

    bool SimulatorProcess::launch(SimScene const& scene, std::string& error)
    {
        if (pid_ > 0) { return true; }
        if (scene.slaves.empty())
        {
            error = "no slaves configured";
            return false;
        }

        // Per-PID path so concurrent KickUI instances don't clobber each other.
        topo_path_ = "/tmp/kickui_topology_" + std::to_string(getpid()) + ".json";
        if (not scene.writeTopologyFile(topo_path_, error)) { return false; }

        // Stale tap shm from a crashed sim desyncs the frame stream -> connect fails on WKC.
        ::unlink("/dev/shm/tap_nominal");
        ::unlink("/dev/shm/tap_redundant");

        // Control bus, created + stamped before the fork so the child can attach.
        control_shm_ = "/kickui_ctrl_" + std::to_string(getpid());
        control_ = std::make_unique<sim::SimulatorControlClient>();
        try
        {
            control_->open(control_shm_);
        }
        catch (std::exception const& e)
        {
            error = std::string("control channel: ") + e.what();
            return false;
        }

        std::string bin = simulatorPath();
        // network_simulator -i tap:server [-r tap:server] --control <shm> --topology <file> -s <cfg0> ...
        // --topology / -s come last; -s consumes the remaining arguments.
        std::vector<std::string> args = {bin, "-i", "tap:server"};
        if (scene.redundancy)
        {
            args.push_back("-r");
            args.push_back("tap:server");   // redundant pair uses the default tap_redundant shm
        }
        args.push_back("--control");
        args.push_back(control_shm_);
        args.push_back("--topology");
        args.push_back(topo_path_);
        args.push_back("-s");
        for (auto const& s : scene.slaves) { args.push_back(s.config); }
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& a : args) { argv.push_back(a.data()); }
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0)
        {
            releaseControl();
            error = "fork failed";
            return false;
        }
        if (pid == 0)
        {
            // Die with the GUI even on an abnormal exit (Ctrl-C, kill, crash)
            // that never runs the destructors. Re-check the parent didn't
            // already exit between fork and here, else we'd miss the signal.
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() == 1) { _exit(0); }
            execv(bin.c_str(), argv.data());
            _exit(127);  // exec failed
        }
        pid_ = pid;
        broken_links_.clear();
        return true;
    }

    void SimulatorProcess::stop()
    {
        if (pid_ <= 0) { return; }
        kill(pid_, SIGTERM);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
        releaseControl();
        if (not topo_path_.empty())
        {
            ::unlink(topo_path_.c_str());
        }
    }

    bool SimulatorProcess::reap()
    {
        if (pid_ <= 0) { return false; }
        int status = 0;
        if (waitpid(pid_, &status, WNOHANG) == pid_)  // exited on its own
        {
            pid_ = -1;
            releaseControl();
            if (not topo_path_.empty())
            {
                ::unlink(topo_path_.c_str());
                topo_path_.clear();
            }
            return true;
        }
        return false;
    }

    void SimulatorProcess::releaseControl()
    {
        control_.reset();
        if (not control_shm_.empty())
        {
            SharedMemory::unlink(control_shm_);
            control_shm_.clear();
        }
    }

    void SimulatorProcess::setLinkBroken(int a, int b, bool broken)
    {
        if ((not control_) or (a < 0) or (b < 0) or (a == b)) { return; }
        if (broken)
        {
            control_->breakLink(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        }
        else
        {
            control_->healLink(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
        }
        auto key = std::make_pair(std::min(a, b), std::max(a, b));
        if (broken) { broken_links_.insert(key); }
        else        { broken_links_.erase(key); }
    }
#endif
}
