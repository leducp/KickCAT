#ifndef KICKCAT_TOOLS_KICKUI_EVENT_LOG_H
#define KICKCAT_TOOLS_KICKUI_EVENT_LOG_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "BusProtocol.h"

namespace kickcat::kickui
{
    // The diagnostics event log: derives discrete entries (per-slave AL state
    // transitions, lost-link rises, redundancy flips, bus loss) from successive
    // snapshots. Pure model -- the alert banner and the Diagnostics tab render it.
    class EventLog
    {
    public:
        struct Entry
        {
            std::string when;    // HH:MM:SS
            std::string text;
            bool        severe = false;   // error (red) vs notice (yellow)
        };

        // Diff this frame's snapshot against the previous one. slave_label maps a
        // scan index to the displayed S# (the shell owns that mapping).
        void update(std::shared_ptr<BusSnapshot const> const& snap, bool connected, bool bus_lost,
                    std::function<int(int)> const& slave_label);

        std::deque<Entry> const& entries() const { return entries_; }
        size_t unacked() const;
        bool   unackedSevere() const;   // at least one unacknowledged severe entry
        void   ackAll() { acked_ = entries_.size(); }
        void   clear();

    private:
        static std::string nowHHMMSS();
        void log(std::string text, bool severe);

        std::deque<Entry> entries_;
        size_t            acked_ = 0;       // entries the user has acknowledged
        bool              have_prev_ = false;
        std::vector<uint8_t> prev_al_;      // per-slave AL status, previous frame
        std::vector<std::array<uint64_t, 4>> prev_lost_;  // per-slave lost-link TOTALS (monotone)
        bool prev_redundancy_ = false;
        bool prev_bus_lost_   = false;
    };
}

#endif
