#include "kickcat/AbstractESC2.h"
#include "Error.h"
#include "kickcat/OS/Time.h"

using namespace kickcat;
using namespace kickcat::FSM;



hresult AbstractESC2::init()
{
    stateMachine.start();

    return hresult::OK;
}

void AbstractESC2::routine()
{
    stateMachine.play();
}
