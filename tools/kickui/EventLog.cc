#include "EventLog.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

#include "Theme.h"

namespace kickcat::kickui
{
    std::string EventLog::nowHHMMSS()
    {
        std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        return buf;
    }

    void EventLog::log(std::string text, bool severe)
    {
        entries_.push_back(Entry{nowHHMMSS(), std::move(text), severe});
        while (entries_.size() > 500)
        {
            entries_.pop_front();
            if (acked_ > 0) { --acked_; }
        }
    }

    size_t EventLog::unacked() const
    {
        if (entries_.size() < acked_) { return 0; }
        return entries_.size() - acked_;
    }

    bool EventLog::unackedSevere() const
    {
        for (size_t i = acked_; i < entries_.size(); ++i)
        {
            if (entries_[i].severe) { return true; }
        }
        return false;
    }

    void EventLog::clear()
    {
        entries_.clear();
        acked_ = 0;
    }

    void EventLog::update(std::shared_ptr<BusSnapshot const> const& snap, bool connected, bool bus_lost,
                          std::function<int(int)> const& slave_label)
    {
        if (not connected or (snap == nullptr))
        {
            have_prev_ = false;   // don't log stale deltas across (re)connect
            return;
        }
        int const n = static_cast<int>(snap->slaves.size());

        if (bus_lost and not prev_bus_lost_) { log("Bus communication lost", true); }
        prev_bus_lost_ = bus_lost;

        bool red = snap->redundancy_active;
        if (red and not prev_redundancy_)     { log(REDUNDANCY_ACTIVE_TEXT, false); }
        if (not red and prev_redundancy_)     { log("Cable redundancy cleared \xe2\x80\x94 ring intact", false); }
        prev_redundancy_ = red;

        if (have_prev_ and (static_cast<int>(prev_al_.size()) == n))
        {
            for (int i = 0; i < n; ++i)
            {
                uint8_t a = snap->slaves[i].al_status;
                if (a != prev_al_[i])
                {
                    char line[160];
                    bool sev = (a & AL_ERROR_BIT) != 0;
                    if (sev)
                    {
                        std::snprintf(line, sizeof(line), "S%d  %s \xe2\x86\x92 %s+ERR  (0x%04X %s)",
                                      slave_label(i), stateLabel(prev_al_[i]), stateLabel(a),
                                      snap->slaves[i].stats.al_status_code,
                                      ALStatus_to_string(snap->slaves[i].stats.al_status_code));
                    }
                    else
                    {
                        std::snprintf(line, sizeof(line), "S%d  %s \xe2\x86\x92 %s",
                                      slave_label(i), stateLabel(prev_al_[i]), stateLabel(a));
                    }
                    log(line, sev);
                }
                for (int p = 0; p < 4; ++p)
                {
                    uint64_t lost = snap->slaves[i].stats.lost_total[p];
                    if ((i < static_cast<int>(prev_lost_.size())) and (lost > prev_lost_[i][p]))
                    {
                        char line[96];
                        std::snprintf(line, sizeof(line), "S%d  port %d: link lost (total %llu)",
                                      slave_label(i), p, static_cast<unsigned long long>(lost));
                        log(line, true);
                    }
                }
            }
        }

        prev_al_.resize(n);
        prev_lost_.resize(n);
        for (int i = 0; i < n; ++i)
        {
            prev_al_[i] = snap->slaves[i].al_status;
            for (int p = 0; p < 4; ++p) { prev_lost_[i][p] = snap->slaves[i].stats.lost_total[p]; }
        }
        have_prev_ = true;
    }
}
