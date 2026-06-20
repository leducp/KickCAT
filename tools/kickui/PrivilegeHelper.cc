#include "PrivilegeHelper.h"

#include <cstdlib>

#ifdef __linux__
#include <climits>
#include <unistd.h>
#endif

namespace kickcat::kickui
{
#ifdef __linux__
    namespace
    {
        // Wrap a `setcap cap_net_raw` in whatever graphical privilege-prompt is
        // available; empty if none found (caller shows the manual command).
        std::string buildSetcapCommand(std::string const& exe_path)
        {
            std::string setcap = "/usr/sbin/setcap cap_net_raw,cap_net_admin+ep " + exe_path;

            if (std::system("which pkexec >/dev/null 2>&1") == 0)
            {
                return "pkexec " + setcap;
            }

            constexpr char const* KDESU_PATHS[] =
            {
                "/usr/lib/x86_64-linux-gnu/libexec/kf6/kdesu",
                "/usr/lib/x86_64-linux-gnu/libexec/kf5/kdesu",
                "/usr/lib/libexec/kf6/kdesu",
                "/usr/lib/libexec/kf5/kdesu",
            };
            for (auto const* kdesu : KDESU_PATHS)
            {
                if (access(kdesu, X_OK) == 0)
                {
                    return std::string(kdesu) + " -c \"" + setcap + "\"";
                }
            }

            return {};
        }
    }
#endif

    PrivilegeHelper::~PrivilegeHelper()
    {
        if (thread_)
        {
            thread_->join();   // don't abandon a running setcap worker
        }
    }

    void PrivilegeHelper::ensureCommand()
    {
        if (command_ready_)
        {
            return;
        }
#ifdef __linux__
        char exe_buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
        if (n > 0)
        {
            exe_buf[n] = '\0';
            exe_path_  = exe_buf;
            command_   = buildSetcapCommand(exe_path_);
        }
#endif
        command_ready_ = true;
    }

    void PrivilegeHelper::grant()
    {
        if (running_ or command_.empty())
        {
            return;
        }
        done_    = false;
        ok_      = false;
        running_ = true;
        std::string cmd = command_;
        thread_.emplace("kickui-setcap", [this, cmd]()
        {
            bool ok = (std::system(cmd.c_str()) == 0);
            ok_   = ok;
            done_ = true;
        }, 0);
        try
        {
            thread_->start();
        }
        catch (std::exception const& e)
        {
            // start() failed: drop the never-started Thread so the destructor and
            // reap() don't join() it (which throws), and clear the flag so the
            // Grant button stays usable.
            thread_.reset();
            running_ = false;
            error_   = std::string("Could not start privilege helper: ") + e.what();
        }
    }

    void PrivilegeHelper::reap()
    {
        if (running_ and done_)
        {
            thread_->join();
            thread_.reset();
            running_ = false;
            if (ok_)
            {
                granted_ = true;
                error_.clear();
            }
            else
            {
                error_ = "Failed. Run manually:\n"
                    "  sudo /usr/sbin/setcap cap_net_raw,cap_net_admin+ep " + exe_path_;
            }
        }
    }
}
