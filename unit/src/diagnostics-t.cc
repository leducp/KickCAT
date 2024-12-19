#include "kickcat/Diagnostics.h"
#include "kickcat/Error.h"
#include <gtest/gtest.h>

using namespace kickcat;


TEST(Diagnostics, get_topology)
{
    std::unordered_map<uint16_t, uint16_t> expected_map;
    std::vector<uint16_t> parents;
    std::vector<Slave> slaves;

    for (uint16_t i = 0; i < 5; ++i)
    {
        Slave slave;
        slave.address = i;
        slaves.push_back(slave);
    }

    // Case 1 : line ( 0 - 1 - 2 - 3 - 4)
    parents = {0, 0, 1, 2, 3};

    slaves[0].dl_status.PL_port0 = 1;
    slaves[0].dl_status.PL_port1 = 1;
    slaves[0].dl_status.PL_port2 = 0;
    slaves[0].dl_status.PL_port3 = 0;

    slaves[1].dl_status.PL_port0 = 1;
    slaves[1].dl_status.PL_port1 = 1;
    slaves[1].dl_status.PL_port2 = 0;
    slaves[1].dl_status.PL_port3 = 0;

    slaves[2].dl_status.PL_port0 = 1;
    slaves[2].dl_status.PL_port1 = 1;
    slaves[2].dl_status.PL_port2 = 0;
    slaves[2].dl_status.PL_port3 = 0;

    slaves[3].dl_status.PL_port0 = 1;
    slaves[3].dl_status.PL_port1 = 1;
    slaves[3].dl_status.PL_port2 = 0;
    slaves[3].dl_status.PL_port3 = 0;

    slaves[4].dl_status.PL_port0 = 1;
    slaves[4].dl_status.PL_port1 = 0;
    slaves[4].dl_status.PL_port2 = 0;
    slaves[4].dl_status.PL_port3 = 0;

    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }

    
    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 2 : simple branch ( 0 - 1 - 2 - 3 )
    //                              |
    //                              4
    parents = {0, 0, 1, 2, 1};

    slaves[0].dl_status.PL_port0 = 1;
    slaves[0].dl_status.PL_port1 = 1;
    slaves[0].dl_status.PL_port2 = 0;
    slaves[0].dl_status.PL_port3 = 0;

    slaves[1].dl_status.PL_port0 = 1;
    slaves[1].dl_status.PL_port1 = 1;
    slaves[1].dl_status.PL_port2 = 1;
    slaves[1].dl_status.PL_port3 = 0;

    slaves[2].dl_status.PL_port0 = 1;
    slaves[2].dl_status.PL_port1 = 1;
    slaves[2].dl_status.PL_port2 = 0;
    slaves[2].dl_status.PL_port3 = 0;

    slaves[3].dl_status.PL_port0 = 1;
    slaves[3].dl_status.PL_port1 = 0;
    slaves[3].dl_status.PL_port2 = 0;
    slaves[3].dl_status.PL_port3 = 0;

    slaves[4].dl_status.PL_port0 = 1;
    slaves[4].dl_status.PL_port1 = 0;
    slaves[4].dl_status.PL_port2 = 0;
    slaves[4].dl_status.PL_port3 = 0;

    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }

    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 3 : multiple branches ( 0 - 1 - 2 )
    //                              |   |
    //                              4   3
    parents = {0, 0, 1, 1, 0};

    slaves[0].dl_status.PL_port0 = 1;
    slaves[0].dl_status.PL_port1 = 1;
    slaves[0].dl_status.PL_port2 = 1;
    slaves[0].dl_status.PL_port3 = 0;

    slaves[1].dl_status.PL_port0 = 1;
    slaves[1].dl_status.PL_port1 = 1;
    slaves[1].dl_status.PL_port2 = 1;
    slaves[1].dl_status.PL_port3 = 0;

    slaves[2].dl_status.PL_port0 = 1;
    slaves[2].dl_status.PL_port1 = 0;
    slaves[2].dl_status.PL_port2 = 0;
    slaves[2].dl_status.PL_port3 = 0;

    slaves[3].dl_status.PL_port0 = 1;
    slaves[3].dl_status.PL_port1 = 0;
    slaves[3].dl_status.PL_port2 = 0;
    slaves[3].dl_status.PL_port3 = 0;

    slaves[4].dl_status.PL_port0 = 1;
    slaves[4].dl_status.PL_port1 = 0;
    slaves[4].dl_status.PL_port2 = 0;
    slaves[4].dl_status.PL_port3 = 0;

    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }
    
    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 4 : invalid topology [lone slave] ( 0 - 1 - 2 - 3    4 )

    slaves[4].dl_status.PL_port0 = 0;
    slaves[4].dl_status.PL_port1 = 0;
    slaves[4].dl_status.PL_port2 = 0;
    slaves[4].dl_status.PL_port3 = 0;

    ASSERT_THROW(getTopology(slaves), Error);
}
