// KickOS ConditionVariable backend: not implemented yet. Every operation throws so the full
// library links for KickOS while the real primitive is written. Construction does not throw.
#include "OS/ConditionVariable.h"
#include "Error.h"

// Throw-only placeholders: -Wmissing-noreturn is expected until the real backend lands.
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

namespace kickcat
{
    ConditionVariable::ConditionVariable(os_cond* cond)
        : pcond_(cond)
        , cond_(0)
    {
        if (cond == nullptr)
        {
            pcond_ = &cond_;
        }
    }

    ConditionVariable::~ConditionVariable() = default;

    void ConditionVariable::init()
    {
        THROW_ERROR("ConditionVariable::init() not implemented on KickOS");
    }

    void ConditionVariable::wait(Mutex&, std::function<bool(void)>)
    {
        THROW_ERROR("ConditionVariable::wait() not implemented on KickOS");
    }

    int ConditionVariable::wait_until(Mutex&, nanoseconds, std::function<bool(void)>)
    {
        THROW_ERROR("ConditionVariable::wait_until() not implemented on KickOS");
    }

    void ConditionVariable::signal()
    {
        THROW_ERROR("ConditionVariable::signal() not implemented on KickOS");
    }
}
