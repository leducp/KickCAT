#include "kickcat/Diagnostics.h"
#include "kickcat/Error.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace kickcat;

void setOpenPorts(Slave& slave, std::vector<int> ports)
{
    std::memset(&slave.dl_status, 0, sizeof(DLStatus));
    for (int p : ports)
    {
        if (p == 0) { slave.dl_status.COM_port0 = 1; }
        if (p == 1) { slave.dl_status.COM_port1 = 1; }
        if (p == 2) { slave.dl_status.COM_port2 = 1; }
        if (p == 3) { slave.dl_status.COM_port3 = 1; }
    }
}

TEST(Diagnostics, get_topology)
{
    std::unordered_map<uint16_t, uint16_t> expected_map;
    std::vector<uint16_t> parents;
    std::vector<Slave> slaves(5);

    for (uint16_t i = 0; i < 5; ++i)
    {
        slaves[i].address = i;
    }

    // Case 1 : line ( 0 - 1 - 2 - 3 - 4)
    parents = {0, 0, 1, 2, 3};

    setOpenPorts(slaves[0], {0, 1});
    setOpenPorts(slaves[1], {0, 1});
    setOpenPorts(slaves[2], {0, 1});
    setOpenPorts(slaves[3], {0, 1});
    setOpenPorts(slaves[4], {0});

    expected_map.clear();
    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }
    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 2 : simple branch ( 0 - 1 - 2 - 3 )
    //                              |
    //                              4
    parents = {0, 0, 1, 2, 1};

    setOpenPorts(slaves[0], {0, 1});
    setOpenPorts(slaves[1], {0, 1, 2});
    setOpenPorts(slaves[2], {0, 1});
    setOpenPorts(slaves[3], {0});
    setOpenPorts(slaves[4], {0});

    expected_map.clear();
    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }
    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 3 : multiple branches ( 0 - 1 - 2 )
    //                              |   |
    //                              4   3
    parents = {0, 0, 1, 1, 0};

    setOpenPorts(slaves[0], {0, 1, 2});
    setOpenPorts(slaves[1], {0, 1, 2});
    setOpenPorts(slaves[2], {0});
    setOpenPorts(slaves[3], {0});
    setOpenPorts(slaves[4], {0});

    expected_map.clear();
    for (uint16_t i = 0; i < 5; ++i)
    {
        expected_map[i] = parents.at(i);
    }
    ASSERT_EQ(expected_map, getTopology(slaves));

    // Case 4 : invalid topology [lone slave] ( 0 - 1 - 2 - 3    4 )
    setOpenPorts(slaves[4], {});
    ASSERT_THROW(getTopology(slaves), Error);
}
