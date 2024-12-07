#include "kickcat/SlaveFSM.h"
#include "AbstractESC2.h"

namespace kickcat::FSM
{
    Init::Init(AbstractESC2& esc)
        : AbstractState(esc, "Init")
    {
    }

    void Init::routine()
    {
        printf("init routine\n");
    }

    uint8_t Init::transition()
    {
        uint32_t al_control;
        esc_.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
        if ((al_control & kickcat::State::MASK_STATE) == kickcat::State::PRE_OP)
        {
            return kickcat::State::PRE_OP;
        }
        return kickcat::State::INIT;
    }

    void Init::onEntry(uint8_t oldState)
    {
        uint32_t al_status = kickcat::State::INIT;
        esc_.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    }

    void Init::onExit(uint8_t newState)
    {
    }

    PreOP::PreOP(AbstractESC2& esc)
        : AbstractState(esc, "PreOP")
    {
    }

    void PreOP::routine()
    {
        printf("preop routine\n");
    }

    uint8_t PreOP::transition()
    {
        return kickcat::State::OPERATIONAL;
    }

    void PreOP::onEntry(uint8_t oldState)
    {
        uint32_t al_status = kickcat::State::PRE_OP;
        esc_.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    }

    void PreOP::onExit(uint8_t newState)
    {
    }
}
