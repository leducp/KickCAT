#include "kickcat/AbstractESC2.h"
#include "Error.h"
#include "kickcat/OS/Time.h"

using namespace kickcat;
using namespace kickcat::FSM;


AbstractESC2::AbstractESC2(PDIProxy& pdi)
    : pdi_{pdi}
{
    init_.configure([&](FSM::State& currentState) {},
                    [&](FSM::State& currentState)
                    {
                        if ((pdi.getALControl() & State::MASK_STATE) == State::PRE_OP)
                        {
                            return &preop_;
                        }
                        return static_cast<FSM::State*>(nullptr);
                    },
                    [&](FSM::State& oldState, FSM::State& newState) { al_status_ = State::INIT; },
                    [&](FSM::State& oldState, FSM::State& newState) {});


    preop_.configure([&](FSM::State& state) {},
                     [&](FSM::State& state)
                     {
                         if ((pdi.getALControl() & State::MASK_STATE) == State::INIT)
                         {
                             return &init_;
                         }

                         return static_cast<FSM::State*>(nullptr);
                     },
                     [&](FSM::State& oldState, FSM::State& newState) { al_status_ = State::PRE_OP; },
                     [&](FSM::State& oldState, FSM::State& newState) {});
}

hresult AbstractESC2::init()
{
    stateMachine.init(&init_);

    pdi_.setALStatusCode(al_status_code_);
    pdi_.setALStatus(al_status_);

    return hresult::OK;
}

void AbstractESC2::routine()
{
    // TODO: ? Should we get the al status ?

    stateMachine.routine();

    pdi_.setALStatusCode(al_status_code_);
    pdi_.setALStatus(al_status_);
}
void AbstractESC2::clear_error()
{
    al_status_code_ = StatusCode::NO_ERROR;
    al_status_ &= ~AL_STATUS_ERR_IND;
}
