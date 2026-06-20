#ifndef KICKCAT_TOOLS_KICKUI_PRIVILEGE_HELPER_H
#define KICKCAT_TOOLS_KICKUI_PRIVILEGE_HELPER_H

#include <atomic>
#include <optional>
#include <string>

#include "kickcat/OS/Thread.h"

namespace kickcat::kickui
{
    // Grants CAP_NET_RAW to this executable: resolves a graphical privilege
    // prompt (pkexec/kdesu) once, then runs the blocking, password-prompting
    // setcap command on a worker thread so the render loop keeps drawing.
    // Linux-only; on other platforms command() stays empty.
    class PrivilegeHelper
    {
    public:
        ~PrivilegeHelper();   // joins a running worker

        // Resolve the executable path and the prompt command once; the probes
        // (which/access) fork a shell, so they must not run per frame.
        void ensureCommand();
        void grant();         // start the worker (no-op while running / no command)
        void reap();          // join a finished worker; call once per frame

        bool running() const { return running_; }
        bool granted() const { return granted_; }
        void resetGranted()  { granted_ = false; }
        std::string const& exePath() const { return exe_path_; }
        std::string const& command() const { return command_; }
        std::string const& error()   const { return error_; }
        void setError(std::string e)       { error_ = std::move(e); }
        void clearError()                  { error_.clear(); }

    private:
        std::string           exe_path_;
        std::string           command_;
        bool                  command_ready_ = false;
        bool                  granted_       = false;
        std::string           error_;
        std::optional<Thread> thread_;
        std::atomic<bool>     running_{false};
        std::atomic<bool>     done_{false};
        std::atomic<bool>     ok_{false};
    };
}

#endif
