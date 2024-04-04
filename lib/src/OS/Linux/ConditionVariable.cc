#include "Error.h"
#include "OS/Linux/ConditionVariable.h"

namespace kickcat
{
    ConditionVariable::ConditionVariable(pthread_cond_t* cond)
        : pcond_(cond)
        , cond_(PTHREAD_COND_INITIALIZER)
    {
        if (cond == nullptr)
        {
            pcond_ = &cond_;
            init();
        }
    }


    ConditionVariable::~ConditionVariable()
    {
        if (pcond_ == &cond_)
        {
            int rc = pthread_cond_destroy(pcond_);
            if (rc != 0)
            {
                THROW_SYSTEM_ERROR_CODE("pthread_cond_destroy()", rc);
            }
        }
    }


    void ConditionVariable::init()
    {
        pthread_condattr_t attr;
        int rc = pthread_condattr_init(&attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_condattr_init()", rc);
        }

        if (&cond_ != pcond_)
        {
            rc = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            if (rc != 0)
            {
                THROW_SYSTEM_ERROR_CODE("pthread_condattr_setpshared()", rc);
            }
        }

        rc = pthread_cond_init(pcond_, &attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_cond_init()", rc);
        }

        rc = pthread_condattr_destroy(&attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_condattr_destroy()", rc);
        }
    }


    void ConditionVariable::wait(Mutex& mutex, std::function<bool(void)> stopWaiting)
    {
        while (not stopWaiting())
        {
            int rc = pthread_cond_wait(pcond_, mutex.pmutex_);
            if (rc != 0)
            {
                THROW_SYSTEM_ERROR_CODE("pthread_cond_wait()", rc);
            }
        }
    }


    void ConditionVariable::signal()
    {
        int rc = pthread_cond_signal(pcond_);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_cond_signal()", rc);
        }
    }
}
