
#ifndef SLAVE_STACK_INCLUDE_KICKCAT_SLAVE_H_
#define SLAVE_STACK_INCLUDE_KICKCAT_SLAVE_H_

#include "kickcat/AbstractESC.h"

#include <vector>

namespace kickcat
{
    struct SyncManagerConfig
    {
        uint8_t index;
        uint16_t start_address;
        uint16_t length;
        uint8_t  control;
    };


    class Slave
    {
    public:
        Slave(AbstractESC& esc);




    private:
        AbstractESC& esc_;

        std::vector<SyncManagerConfig> sm_configs_;


    };



}
#endif
