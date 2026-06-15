#include "TopologyView.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include "BusSession.h"
#include "Theme.h"

namespace kickcat::kickui
{
    PortRouting computePortRouting(TopologyInfo const& topo)
    {
        int const N = static_cast<int>(topo.nodes.size());
        PortRouting r;
        r.upstream.assign(N, -1);
        r.parent_port.assign(N, -1);
        for (int i = 0; i < N; ++i)
        {
            for (int p = 0; p < 4; ++p)
            {
                if (topo.nodes[i].port_com[p]) { r.upstream[i] = p; break; }
            }
        }

        // Downstream ports in EtherCAT processing order (0 -> 3 -> 1 -> 2),
        // skipping the upstream/entry port. getTopology discovers children in
        // that same order, so child[k] leaves the parent by down[k] -- collecting
        // them numerically (1,2,3) mislabels branches (e.g. a port-3 branch
        // shown as port 1).
        static constexpr int PORT_ORDER[4] = {0, 3, 1, 2};
        for (int i = 0; i < N; ++i)
        {
            std::vector<int> down;
            for (int oi = 0; oi < 4; ++oi)
            {
                int p = PORT_ORDER[oi];
                if (topo.nodes[i].port_com[p] and (p != r.upstream[i])) { down.push_back(p); }
            }
            int k = 0;
            for (int j = 0; j < N; ++j)
            {
                if (j == i) { continue; }
                if (topo.nodes[j].parent_address == topo.nodes[i].address)
                {
                    if (k < static_cast<int>(down.size())) { r.parent_port[j] = down[k]; }
                    else                                   { r.parent_port[j] = 1; }
                    k += 1;
                }
            }
        }
        return r;
    }

    ImVec4 TopologyView::slaveStateColor(int slave_index) const
    {
        return esmColor(session_->slaveAlStatus(slave_index), bus_lost_);
    }

    bool TopologyView::linkBroken(int a, int b) const
    {
        if ((sim_->broken_links == nullptr) or (a < 0) or (b < 0)) { return false; }
        return sim_->broken_links->count(std::make_pair(std::min(a, b), std::max(a, b))) > 0;
    }

    // ESM colour key for the topology view.
    void TopologyView::renderEsmLegend()
    {
        struct LegendItem { ImVec4 color; char const* label; };
        LegendItem const items[] = {
            {COLOR_ESM_INIT,   "INIT"},
            {COLOR_ESM_PREOP,  "PRE-OP"},
            {COLOR_ESM_SAFEOP, "SAFE-OP"},
            {COLOR_ESM_OP,     "OP"},
            {COLOR_RED,        "Error"},
        };
        for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(items)); ++i)
        {
            if (i > 0) { ImGui::SameLine(0.0f, 12.0f); }
            ImGui::TextColored(items[i].color, "\xe2\x97\x8f");   // filled circle
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(items[i].label);
        }
    }

    // One slave's four ESC ports, as the master reads them from DL_STATUS:
    // green "up" = physical link + communicating, "loop" = open/looped back.
    void TopologyView::renderPortChips(TopologyNode const& node)
    {
        for (int p = 0; p < 4; ++p)
        {
            if (p > 0) { ImGui::SameLine(); }
            if (node.port_com[p])
            {
                ImGui::TextColored(COLOR_GREEN, "P%d:up", p);
            }
            else if (node.port_loop[p])
            {
                ImGui::TextDisabled("P%d:loop", p);
            }
            else
            {
                ImGui::TextDisabled("P%d:off", p);
            }
        }
    }

    void TopologyView::drawTreeNode(TopologyInfo const& topo,
                                    std::unordered_map<uint16_t, std::vector<int>> const& children_of,
                                    int i)
    {
        TopologyNode const& node = topo.nodes[i];
        auto it = children_of.find(node.address);
        bool has_children = (it != children_of.end()) and (not it->second.empty());

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
        if (not has_children)
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(node.address)),
                        flags, "S%d  @%u  %s", label(node.index), node.address, node.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("  ");
        ImGui::SameLine();
        renderPortChips(node);

        if (open)
        {
            if (has_children)
            {
                for (int child : it->second)
                {
                    drawTreeNode(topo, children_of, child);
                }
            }
            ImGui::TreePop();
        }
    }

    // Assign each node a column (leaf order; parents centre over children) and
    // a row (depth). Returns this node's column so the parent can centre on it.
    float TopologyView::layoutNode(TopologyInfo const& topo,
                                   std::unordered_map<uint16_t, std::vector<int>> const& children_of,
                                   int i, int depth, float& next_col,
                                   std::vector<float>& col, std::vector<int>& row)
    {
        row[i] = depth;
        auto it = children_of.find(topo.nodes[i].address);
        if ((it == children_of.end()) or it->second.empty())
        {
            col[i] = next_col;
            next_col += 1.0f;
            return col[i];
        }
        float first = 0.0f;
        float last  = 0.0f;
        bool seen = false;
        for (int child : it->second)
        {
            float c = layoutNode(topo, children_of, child, depth + 1, next_col, col, row);
            if (not seen) { first = c; seen = true; }
            last = c;
        }
        col[i] = (first + last) * 0.5f;
        return col[i];
    }

    // A bus diagram drawn like the physical layer: each ESC is a square with
    // its four ports on the faces (0 top, 1 bottom, 2 left, 3 right) and cables
    // run port-to-port. The port mapping comes from computePortRouting().
    void TopologyView::renderGraph(TopologyInfo const& topo,
                                   std::unordered_map<uint16_t, std::vector<int>> const& children_of,
                                   std::vector<int> const& roots)
    {
        int const N = static_cast<int>(topo.nodes.size());

        PortRouting routing = computePortRouting(topo);
        auto upstream = [&](int i) -> int
        {
            if (routing.upstream[i] < 0) { return 0; }   // no com port: draw from port 0
            return routing.upstream[i];
        };

        // Horizontal placement follows the port the child hangs off: a left
        // port (2) goes to the left, a right port (3) to the right, the main
        // downstream (1) straight below. So the cable leaves the actual port
        // toward where the child sits, instead of crossing back over siblings.
        auto portRank = [](int p) -> int
        {
            if (p == 2) { return 0; }   // left
            if (p == 3) { return 2; }   // right
            return 1;                   // down / default centre
        };
        auto children = children_of;    // local, sortable copy
        for (auto& entry : children)
        {
            std::sort(entry.second.begin(), entry.second.end(),
                      [&](int a, int b) { return portRank(routing.parent_port[a]) < portRank(routing.parent_port[b]); });
        }

        std::vector<float> col(N, 0.0f);
        std::vector<int>   row(N, 0);
        float next_col = 0.0f;
        for (int r : roots)
        {
            layoutNode(topo, children, r, 1, next_col, col, row);  // master is row 0
        }
        int max_row = 1;
        for (int r : row) { if (r > max_row) { max_row = r; } }
        float n_cols = next_col;
        if (n_cols < 1.0f) { n_cols = 1.0f; }

        constexpr float BOX    = 78.0f;   // ESC square
        constexpr float CELL_W = 150.0f;  // box + horizontal room for branch cables
        constexpr float CELL_H = 130.0f;  // box + vertical room for the cable run
        constexpr float PAD    = 24.0f;
        float total_w = n_cols * CELL_W + 2.0f * PAD;
        float total_h = (max_row + 1) * CELL_H + 2.0f * PAD;

        float avail_h  = ImGui::GetContentRegionAvail().y;
        float canvas_h = avail_h - 90.0f;
        if (canvas_h < 200.0f)          { canvas_h = 200.0f; }
        if (total_h + 12.0f < canvas_h) { canvas_h = total_h + 12.0f; }

        ImGui::BeginChild("##topo_canvas", ImVec2(0.0f, canvas_h),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // Box top-left from grid (col centred within its cell), and the centre
        // of a given port's face.
        auto boxTL = [&](float c, int r) -> ImVec2
        {
            return ImVec2(origin.x + PAD + c * CELL_W + (CELL_W - BOX) * 0.5f,
                          origin.y + PAD + r * CELL_H);
        };
        auto portPt = [&](ImVec2 tl, int p) -> ImVec2
        {
            if (p == 0) { return ImVec2(tl.x + BOX * 0.5f, tl.y); }
            if (p == 1) { return ImVec2(tl.x + BOX * 0.5f, tl.y + BOX); }
            if (p == 2) { return ImVec2(tl.x, tl.y + BOX * 0.5f); }
            return ImVec2(tl.x + BOX, tl.y + BOX * 0.5f);
        };
        auto portStub = [&](ImVec2 tl, int p) -> ImVec2
        {
            ImVec2 q = portPt(tl, p);
            if (p == 0) { q.y -= 14.0f; }
            if (p == 1) { q.y += 14.0f; }
            if (p == 2) { q.x -= 14.0f; }
            if (p == 3) { q.x += 14.0f; }
            return q;
        };

        float master_col = (n_cols - 1.0f) * 0.5f;
        ImVec2 master_tl = boxTL(master_col, 0);

        // Cables first, so boxes paint over the endpoints. A cable is red when
        // the child's upstream port is not communicating (broken / redundant).
        auto cableColor = [&](int child) -> ImU32
        {
            if (topo.nodes[child].port_com[upstream(child)]) { return ImGui::GetColorU32(ImVec4(0.45f, 0.72f, 0.55f, 1.0f)); }
            return ImGui::GetColorU32(COLOR_RED);
        };
        // Collect each cable's polyline + its SIM slave endpoints so a right-click
        // can hit-test it and break/heal the wire. Master end = -1 (the tap link is
        // not a sim node, so it can't be broken).
        struct Cable
        {
            int    a = -1;   // sim slave index (-1 = master)
            int    b = -1;
            ImVec2 p[4];
        };
        std::vector<Cable> cables;

        // Each cable leaves the parent's actual port face, runs to the child's
        // entry-port face via short stubs. Children are placed on the side
        // their port faces (above), so these stay short and don't cross.
        auto drawCable = [&](ImVec2 from_face, ImVec2 from_stub, ImVec2 to_stub, ImVec2 to_face, ImU32 c)
        {
            dl->AddLine(from_face, from_stub, c, 2.0f);
            dl->AddLine(from_stub, to_stub, c, 2.0f);
            dl->AddLine(to_stub, to_face, c, 2.0f);
        };
        // Red X at a broken cable's midpoint so the break LOCATION is unmistakable.
        ImU32 const red = ImGui::GetColorU32(COLOR_RED);
        auto markBreak = [&](ImVec2 a, ImVec2 b)
        {
            ImVec2 m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            float r = 6.0f;
            dl->AddCircleFilled(m, r + 2.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.7f)));
            dl->AddLine(ImVec2(m.x - r, m.y - r), ImVec2(m.x + r, m.y + r), red, 2.5f);
            dl->AddLine(ImVec2(m.x - r, m.y + r), ImVec2(m.x + r, m.y - r), red, 2.5f);
        };

        for (int r : roots)   // master -> root upstream port
        {
            ImVec2 rtl = boxTL(col[r], row[r]);
            int up = upstream(r);
            ImVec2 mf{master_tl.x + BOX * 0.5f, master_tl.y + BOX};
            ImVec2 ms{master_tl.x + BOX * 0.5f, master_tl.y + BOX + 14.0f};
            drawCable(mf, ms, portStub(rtl, up), portPt(rtl, up), cableColor(r));
            cables.push_back(Cable{-1, label(topo.nodes[r].index), {mf, ms, portStub(rtl, up), portPt(rtl, up)}});
            if (not topo.nodes[r].port_com[up]) { markBreak(ms, portStub(rtl, up)); }
        }
        for (int i = 0; i < N; ++i)
        {
            auto it = children.find(topo.nodes[i].address);
            if (it == children.end()) { continue; }
            ImVec2 ptl = boxTL(col[i], row[i]);
            for (int child : it->second)
            {
                ImVec2 ctl = boxTL(col[child], row[child]);
                int pp = routing.parent_port[child];
                int cp = upstream(child);
                int sa = label(topo.nodes[i].index);
                int sb = label(topo.nodes[child].index);
                bool down = (not topo.nodes[child].port_com[cp]) or linkBroken(sa, sb);
                ImU32 c = cableColor(child);
                if (linkBroken(sa, sb))
                {
                    c = red;
                }
                drawCable(portPt(ptl, pp), portStub(ptl, pp), portStub(ctl, cp), portPt(ctl, cp), c);
                cables.push_back(Cable{sa, sb, {portPt(ptl, pp), portStub(ptl, pp), portStub(ctl, cp), portPt(ctl, cp)}});
                if (down) { markBreak(portStub(ptl, pp), portStub(ctl, cp)); }
            }
        }

        // Master box.
        dl->AddRectFilled(master_tl, ImVec2(master_tl.x + BOX, master_tl.y + BOX),
                          ImGui::GetColorU32(ImVec4(0.20f, 0.28f, 0.42f, 1.0f)), 5.0f);
        dl->AddRect(master_tl, ImVec2(master_tl.x + BOX, master_tl.y + BOX),
                    ImGui::GetColorU32(ImVec4(0.50f, 0.60f, 0.90f, 1.0f)), 5.0f, 0, 2.0f);
        ImVec2 msz = ImGui::CalcTextSize("Master");
        dl->AddText(ImVec2(master_tl.x + (BOX - msz.x) * 0.5f, master_tl.y + (BOX - msz.y) * 0.5f),
                    ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.95f, 1.0f)), "Master");

        for (int i = 0; i < N; ++i)
        {
            TopologyNode const& n = topo.nodes[i];
            ImVec2 tl = boxTL(col[i], row[i]);

            ImGui::SetCursorScreenPos(tl);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##node", ImVec2(BOX, BOX));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) { session_->select(n.index); }
            // Right-click: set this slave's EtherCAT state.
            if (ImGui::BeginPopupContextItem("##esm"))
            {
                session_->select(n.index);
                ImGui::TextDisabled("S%d \xe2\x86\x92 state", label(n.index));
                ImGui::Separator();
                auto& devs = session_->devices();
                if ((n.index >= 0) and (n.index < static_cast<int>(devs.size())))
                {
                    for (auto const& btn : STATE_BUTTONS)
                    {
                        if (ImGui::MenuItem(btn.label))
                        {
                            devs[n.index].requestState(static_cast<uint8_t>(btn.state));
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            ImU32 fill = ImGui::GetColorU32(ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
            if (n.index == session_->selected())
            {
                fill = ImGui::GetColorU32(ImVec4(0.24f, 0.27f, 0.34f, 1.0f));
            }
            float border_w = 2.0f;
            if (hovered) { border_w = 3.0f; }
            dl->AddRectFilled(tl, ImVec2(tl.x + BOX, tl.y + BOX), fill, 5.0f);
            dl->AddRect(tl, ImVec2(tl.x + BOX, tl.y + BOX),
                        ImGui::GetColorU32(slaveStateColor(n.index)), 5.0f, 0, border_w);

            char head[24];
            snprintf(head, sizeof(head), "S%d", label(n.index));
            char addr[16];
            snprintf(addr, sizeof(addr), "%u", n.address);
            ImVec2 hsz = ImGui::CalcTextSize(head);
            ImVec2 asz = ImGui::CalcTextSize(addr);
            float line_h = ImGui::GetTextLineHeight();
            float top    = tl.y + (BOX - (line_h * 2.0f + 3.0f)) * 0.5f;
            dl->AddText(ImVec2(tl.x + (BOX - hsz.x) * 0.5f, top),
                        ImGui::GetColorU32(ImVec4(0.90f, 0.90f, 0.92f, 1.0f)), head);
            dl->AddText(ImVec2(tl.x + (BOX - asz.x) * 0.5f, top + line_h + 3.0f),
                        ImGui::GetColorU32(ImVec4(0.62f, 0.64f, 0.68f, 1.0f)), addr);

            // Port markers on the four faces, coloured by link state, labelled.
            for (int p = 0; p < 4; ++p)
            {
                ImVec2 c = portPt(tl, p);
                ImU32 pc = ImGui::GetColorU32(ImVec4(0.32f, 0.32f, 0.36f, 1.0f));   // off
                if (n.port_com[p])       { pc = ImGui::GetColorU32(COLOR_GREEN); }
                else if (n.port_loop[p]) { pc = ImGui::GetColorU32(ImVec4(0.48f, 0.48f, 0.54f, 1.0f)); }
                dl->AddRectFilled(ImVec2(c.x - 6.0f, c.y - 6.0f), ImVec2(c.x + 6.0f, c.y + 6.0f), pc, 2.0f);
                dl->AddRect(ImVec2(c.x - 6.0f, c.y - 6.0f), ImVec2(c.x + 6.0f, c.y + 6.0f),
                            ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)), 2.0f);
                char pn[2] = {static_cast<char>('0' + p), '\0'};
                dl->AddText(ImVec2(c.x - 3.5f, c.y - 7.0f),
                            ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.05f, 1.0f)), pn);
            }

            if (hovered)
            {
                ImGui::SetTooltip("S%d  @%u  %s\nopen ports: %d  (click to select)",
                                  label(n.index), n.address, n.name.c_str(), n.open_ports);
            }
        }

        // Right-click a cable (not a box) to break/heal that wire. Sim-only: the
        // emulator breaks the link, then the master discovers the change (redundancy
        // activates, or downstream goes dark) -- exactly like a real cable fault.
        if (sim_->running and ImGui::IsWindowHovered() and (not ImGui::IsAnyItemHovered())
            and ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImVec2 m = ImGui::GetMousePos();
            auto segDist2 = [](ImVec2 p, ImVec2 a, ImVec2 b) -> float
            {
                float vx = b.x - a.x, vy = b.y - a.y;
                float wx = p.x - a.x, wy = p.y - a.y;
                float len2 = vx * vx + vy * vy;
                float t = 0.0f;
                if (len2 > 0.0f)
                {
                    t = (wx * vx + wy * vy) / len2;
                }
                if (t < 0.0f) { t = 0.0f; }
                if (t > 1.0f) { t = 1.0f; }
                float dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
                return dx * dx + dy * dy;
            };
            int best = -1;
            float best_d2 = 100.0f;   // (10 px)^2 pick radius
            for (int ci = 0; ci < static_cast<int>(cables.size()); ++ci)
            {
                for (int s = 0; s < 3; ++s)
                {
                    float d2 = segDist2(m, cables[ci].p[s], cables[ci].p[s + 1]);
                    if (d2 < best_d2)
                    {
                        best_d2 = d2;
                        best = ci;
                    }
                }
            }
            if (best >= 0)
            {
                link_ctx_a_      = cables[best].a;
                link_ctx_b_      = cables[best].b;
                link_ctx_master_ = (cables[best].a < 0);
                ImGui::OpenPopup("##link_ctx");
            }
        }
        if (ImGui::BeginPopup("##link_ctx"))
        {
            if (link_ctx_master_)
            {
                ImGui::TextDisabled("Master cable");
                ImGui::Separator();
                ImGui::TextDisabled("cannot be cut (it is the tap link,\nnot a simulated wire)");
            }
            else
            {
                ImGui::TextDisabled("Link S%d \xe2\x80\x94 S%d", link_ctx_a_, link_ctx_b_);
                ImGui::Separator();
                if (linkBroken(link_ctx_a_, link_ctx_b_))
                {
                    if (ImGui::MenuItem("Heal link") and sim_->set_link_broken)
                    {
                        sim_->set_link_broken(link_ctx_a_, link_ctx_b_, false);
                    }
                }
                else
                {
                    if (ImGui::MenuItem("Break link") and sim_->set_link_broken)
                    {
                        sim_->set_link_broken(link_ctx_a_, link_ctx_b_, true);
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(total_w, total_h));   // reserve the scroll region
        ImGui::EndChild();
    }

    // Dump the configured (sim-editor) tree and the discovered (master-POV)
    // tree to the clipboard + /tmp/kickui_topology.txt, for side-by-side
    // comparison when the discovered wiring looks wrong.
    void TopologyView::exportText(TopologyInfo const& topo)
    {
        std::string out = "== Configured (sim editor) ==\n";
        {
            std::vector<int> ports = scene_->assignedPorts();
            for (int i = 0; i < static_cast<int>(scene_->slaves.size()); ++i)
            {
                SimSlave const& s = scene_->slaves[i];
                char line[320];   // sized for the 256-byte config path
                if (s.parent < 0)
                {
                    std::snprintf(line, sizeof(line), "S%d  cfg=%s  parent=master  port=0\n", i, s.config);
                }
                else
                {
                    int port = ports[i];
                    if (port < 0) { port = 1; }
                    std::snprintf(line, sizeof(line), "S%d  cfg=%s  parent=S%d  port=%d\n",
                                  i, s.config, s.parent, port);
                }
                out += line;
            }
        }

        out += "\n== Discovered (master POV) ==\n";
        int const N = static_cast<int>(topo.nodes.size());
        PortRouting routing = computePortRouting(topo);
        std::unordered_map<uint16_t, int> addr_to_index;   // for labelling the parent by its S#
        for (int i = 0; i < N; ++i)
        {
            addr_to_index[topo.nodes[i].address] = i;
        }
        for (int i = 0; i < N; ++i)
        {
            TopologyNode const& n = topo.nodes[i];
            auto pc = [&](int p) -> char const*
            {
                if (n.port_com[p])
                {
                    return "up";
                }
                if (n.port_loop[p])
                {
                    return "loop";
                }
                return "off";
            };
            char ports[80];
            std::snprintf(ports, sizeof(ports), "P0:%s P1:%s P2:%s P3:%s", pc(0), pc(1), pc(2), pc(3));
            char line[256];
            if (n.parent_address == n.address)
            {
                std::snprintf(line, sizeof(line), "S%d @%u  parent=master  %s  open=%d\n",
                              label(n.index), n.address, ports, n.open_ports);
            }
            else
            {
                int parent_label = label(addr_to_index[n.parent_address]);
                std::snprintf(line, sizeof(line), "S%d @%u  parent=S%d port=%d  %s  open=%d\n",
                              label(n.index), n.address, parent_label, routing.parent_port[i], ports, n.open_ports);
            }
            out += line;
        }
        if (not topo.valid and not topo.error.empty())
        {
            out += "(tree incomplete: " + topo.error + ")\n";
        }

        ImGui::SetClipboardText(out.c_str());
        std::ofstream f("/tmp/kickui_topology.txt");
        if (f) { f << out; }
        export_msg_ = "copied to clipboard + /tmp/kickui_topology.txt";
    }

    void TopologyView::render(BusSession& session, SimScene const& scene,
                              std::function<int(int)> const& label_fn, bool bus_lost, SimHooks const& sim)
    {
        session_  = &session;
        scene_    = &scene;
        label_fn_ = &label_fn;
        bus_lost_ = bus_lost;
        sim_      = &sim;

        TopologyInfo topo = session.topology();

        if (ImGui::SmallButton("Refresh topology"))
        {
            session.refreshTopology();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Export topology"))
        {
            exportText(topo);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("master POV \xe2\x80\x94 DL_STATUS port links");
        if (not export_msg_.empty())
        {
            ImGui::TextDisabled("%s", export_msg_.c_str());
        }

        if (session.redundancyEnabled())
        {
            renderRedundancyStatus(session.redundancyActive());
        }

        // Where it broke: a red cable in the graph below is a link whose child
        // does not communicate on its entry port. List them (and any sim-injected
        // breaks) so the location is explicit, not just visual.
        PortRouting routing = computePortRouting(topo);
        std::string broken;
        for (int i = 0; i < static_cast<int>(topo.nodes.size()); ++i)
        {
            // Discovered but no communicating port = isolated/break.
            if ((routing.upstream[i] < 0) and (topo.nodes[i].index >= 0))
            {
                if (not broken.empty()) { broken += ", "; }
                broken += "S" + std::to_string(label(topo.nodes[i].index));
            }
        }
        if ((sim.broken_links != nullptr) and (not sim.broken_links->empty()))
        {
            std::string inj;
            for (auto const& pr : *sim.broken_links)
            {
                if (not inj.empty()) { inj += ", "; }
                inj += "S" + std::to_string(pr.first) + "\xe2\x80\x94S" + std::to_string(pr.second);
            }
            ImGui::TextColored(COLOR_RED, "Broken link(s): %s", inj.c_str());
        }
        if (not broken.empty())
        {
            ImGui::TextColored(COLOR_RED, "No link on: %s  (break upstream \xe2\x80\x94 see red cable below)", broken.c_str());
        }

        if (topo.nodes.empty())
        {
            ImGui::TextDisabled("No slaves discovered.");
            return;
        }
        if (not topo.valid and not topo.error.empty())
        {
            ImGui::TextColored(COLOR_YELLOW, "Tree incomplete: %s", topo.error.c_str());
        }

        // children_of[parent_address] = node indices whose parent is that
        // address; a root is a node whose parent is itself (master-connected).
        std::unordered_map<uint16_t, std::vector<int>> children_of;
        std::vector<int> roots;
        for (int i = 0; i < static_cast<int>(topo.nodes.size()); ++i)
        {
            TopologyNode const& node = topo.nodes[i];
            if (node.parent_address == node.address)
            {
                roots.push_back(i);
            }
            else
            {
                children_of[node.parent_address].push_back(i);
            }
        }

        ImGui::Separator();
        renderEsmLegend();
        renderGraph(topo, children_of, roots);

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Tree (text)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::TreeNodeEx("Master", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (int r : roots)
                {
                    drawTreeNode(topo, children_of, r);
                }
                ImGui::TreePop();
            }
        }
    }
}
