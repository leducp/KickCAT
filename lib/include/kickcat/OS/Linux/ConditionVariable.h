#ifndef KICKCAT_OS_LINUX_CONDITION_VARIABLE_H
#define KICKCAT_OS_LINUX_CONDITION_VARIABLE_H

#include <functional>
#include "Mutex.h"

namespace kickcat
{
    class ConditionVariable
    {
    public:
        /// \param cond    nullptr for a private cond, a pointer to a shared segment otherwise.
        ConditionVariable(pthread_cond_t& cond) : ConditionVariable(&cond) {};
        ConditionVariable(pthread_cond_t* cond = nullptr);
        ~ConditionVariable();

        /// \brief Initialise the condition variable. Automatically called by the constructor
        ///        if the condition variable is private.
        void init();


        /// Wait for a signal
        /// \warning Mutex is assumed to be already locked
        void wait(Mutex& lock, std::function<bool(void)> stopWaiting);

        /// Send a signal to waiting threads
        /// \warning the corresponding lock is assumed to be already locked
        void signal();

    private:
        pthread_cond_t* pcond_;
        pthread_cond_t cond_;
    };
}

#endif
