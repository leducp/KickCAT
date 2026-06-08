#ifndef KICKCAT_SLAVE_SLAVE_H_
#define KICKCAT_SLAVE_SLAVE_H_


#include "kickcat/PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/Mailbox.h"
#include "kickcat/ESMStates.h"

#include <tuple>
#include <type_traits>

namespace kickcat::slave
{
    class Slave final
    {
    public:
        Slave(AbstractESC* esc, PDO* pdo);

        void setMailbox(mailbox::response::Mailbox* mbx);

        // Object dictionary for PDO mapping and bind(), owned by the application and injected
        // here (a mailboxless terminal has one too); it must outlive the slave.
        void setDictionary(CoE::Dictionary* dictionary);

        void start();
        void routine();
        State state();
        void validateOutputData();

        template<typename T>
        void bind(uint16_t idx, T*& ptr, uint8_t subindex = 0)
        {
            if (dictionary_)
            {
                auto [obj, entry] = kickcat::CoE::findObject(*dictionary_, idx, subindex);
                if (entry)
                {
                    ptr = static_cast<std::remove_reference_t<decltype(*ptr)> *>(entry->data);
                }
            }
        }

    private:
        AbstractESC* esc_;
        mailbox::response::Mailbox* mbx_{nullptr};
        CoE::Dictionary* dictionary_{nullptr};
        PDO* pdo_;

        ESM::Init init_{*esc_, *pdo_};
        ESM::PreOP preOp_{*esc_, *pdo_};
        ESM::SafeOP safeOP_{*esc_, *pdo_};
        ESM::OP OP_{*esc_, *pdo_};
        ESM::StateMachine stateMachine_{*esc_, {{&init_, &preOp_, &safeOP_, &OP_}}};
    };
}

#endif
