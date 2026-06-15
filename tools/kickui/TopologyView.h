#ifndef KICKCAT_TOOLS_KICKUI_TOPOLOGY_VIEW_H
#define KICKCAT_TOOLS_KICKUI_TOPOLOGY_VIEW_H

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgui.h"

#include "BusProtocol.h"
#include "Simulator.h"

namespace kickcat::kickui
{
    class BusSession;

    // Upstream/parent-port inference from DL_STATUS, shared by the graph, the
    // text export and the break list: the frame enters a slave on its lowest
    // communicating port; the parent reaches each child through its remaining
    // communicating ports in EtherCAT processing order (0 -> 3 -> 1 -> 2).
    struct PortRouting
    {
        std::vector<int> upstream;      // entry port per node (-1 = no communicating port)
        std::vector<int> parent_port;   // port on the parent feeding this node (-1 = root)
    };
    PortRouting computePortRouting(TopologyInfo const& topo);

    // The "Bus Topology" tab: legend, drawn graph (boxes + cables + break marks +
    // break/heal context menu), text tree, and the clipboard/file export.
    class TopologyView
    {
    public:
        // Wiring back into the simulator (all optional: inactive without a sim).
        struct SimHooks
        {
            bool running = false;
            std::set<std::pair<int, int>> const* broken_links = nullptr;   // sim S# pairs (a<b)
            std::function<void(int, int, bool)>  set_link_broken;          // sim S# pair -> break/heal
        };

        // label maps a scan index to the displayed S#; bus_lost greys the ESM
        // colours (stale states must not look live).
        void render(BusSession& session, SimScene const& scene,
                    std::function<int(int)> const& label, bool bus_lost, SimHooks const& sim);

    private:
        int    label(int scan_index) const { return (*label_fn_)(scan_index); }
        ImVec4 slaveStateColor(int slave_index) const;
        bool   linkBroken(int a, int b) const;

        void renderEsmLegend();
        void renderPortChips(TopologyNode const& node);
        void drawTreeNode(TopologyInfo const& topo,
                          std::unordered_map<uint16_t, std::vector<int>> const& children_of, int i);
        float layoutNode(TopologyInfo const& topo,
                         std::unordered_map<uint16_t, std::vector<int>> const& children_of,
                         int i, int depth, float& next_col,
                         std::vector<float>& col, std::vector<int>& row);
        void renderGraph(TopologyInfo const& topo,
                         std::unordered_map<uint16_t, std::vector<int>> const& children_of,
                         std::vector<int> const& roots);
        void exportText(TopologyInfo const& topo);

        // Set at the top of render(); valid for the duration of the frame.
        BusSession*     session_  = nullptr;
        SimScene const* scene_    = nullptr;
        std::function<int(int)> const* label_fn_ = nullptr;
        bool            bus_lost_ = false;
        SimHooks const* sim_      = nullptr;

        std::string export_msg_;        // confirmation after Export topology
        int  link_ctx_a_ = -1;          // cable picked for the break/heal popup
        int  link_ctx_b_ = -1;
        bool link_ctx_master_ = false;
    };
}

#endif
