#include <gtest/gtest.h>
#include <cstdio>

#include "protocol.h"
#include "Error.h"


using namespace kickcat;

TEST(yolo, yala)
{
    printf("yolo %ld\n", sizeof(DatagramHeader));

    DatagramHeader header{};

    header.len = 0xfff;
    //header.C = 1;
    //header.M = 1;
    uint8_t* raw = (uint8_t*)&header;

    for (int i=0; i<sizeof(DatagramHeader); ++i)
    {
        printf("0x%02x ", raw[i]);
    }
    printf("\n");

    try
    {
        THROW_ERROR("yeah");
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }

}
