#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC2_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC2_H_

#include <cstdint>
#include <vector>

#include <cstdarg>
#include "kickcat/Error.h"
#include "kickcat/SlaveFSM.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    class AbstractESC2
    {
    public:
        AbstractESC2()          = default;
        virtual ~AbstractESC2() = default;

        hresult init();
        void routine();

        virtual int32_t read(uint16_t address, void* data, uint16_t size)        = 0;
        virtual int32_t write(uint16_t address, void const* data, uint16_t size) = 0;

    private:
        void clear_error();
        FSM::Init init_{*this};
        FSM::PreOP preOp_{*this};
        std::map<uint8_t, FSM::AbstractState*> states{{kickcat::State::INIT, &init_},
                                                      {kickcat::State::PRE_OP, &preOp_}};


        FSM::StateMachine stateMachine{states, kickcat::State::INIT};
    };

}
#endif

