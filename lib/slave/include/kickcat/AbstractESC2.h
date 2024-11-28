#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC2_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC2_H_

#include <cstdint>
#include <vector>

#include <cstdarg>
#include "kickcat/Error.h"
#include "kickcat/FSM.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    class PDIProxy
    {
    public:
        // TODO: by default this 3 function should use the read function to get the reg
        // but can be redefine fore tests
        virtual void setALStatus(uint32_t alStatus)         = 0;
        virtual void setALStatusCode(uint32_t alStatusCode) = 0;
        virtual uint32_t getALControl()                     = 0;
        //        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;
        //        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;
    };

    class AbstractESC2
    {
    public:
        AbstractESC2(PDIProxy& pdi);
        virtual ~AbstractESC2() = default;

        hresult init();
        void routine();

    private:
        void clear_error();

        PDIProxy& pdi_;
        FSM::State init_{"Init"};
        FSM::State preop_{"PreOP"};
        FSM::State safeop_{"SafeOP"};
        FSM::State op_{"OP"};
        FSM::StateMachine stateMachine{};  // TODO: change to reference

        uint32_t al_status_{};
        uint32_t al_status_code_{};
    };

}
#endif
