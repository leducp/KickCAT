#ifndef KICKCAT_LUA_PROTOCOL_H
#define KICKCAT_LUA_PROTOCOL_H

#include "kickcat/protocol.h"

namespace kickcat
{
    constexpr char const* const COMMAND[] =
    {
        "NOP",
        "APRD",
        "APWR",
        "APRW",
        "FPRD",
        "FPWR",
        "FPRW",
        "BRD",
        "BWR",
        "BRW",
        "LRD",
        "LWR",
        "LRW",
        "ARMW",
        "FRMW",
        nullptr
    };

    constexpr char const* const ECAT_EVENT[] =
    {
        "DC_LATCH",
        "reserved",
        "DL_STATUS",
        "AL_STATUS",
        "SM0_STATUS",
        "SM1_STATUS",
        "SM2_STATUS",
        "SM3_STATUS",
        "SM4_STATUS",
        "SM5_STATUS",
        "SM6_STATUS",
        "SM7_STATUS",
        nullptr
    };
}

#endif