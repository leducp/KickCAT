#ifndef KICKCAT_UNIT_MOCKS_ESC_H
#define KICKCAT_UNIT_MOCKS_ESC_H

#include <gmock/gmock.h>

#include "kickcat/AbstractESC.h"

namespace kickcat
{
    class MockESC : public AbstractESC
    {
    public:
        MockESC() = default;
        virtual ~MockESC() = default;

        MOCK_METHOD(int32_t, init, (), (override));
        MOCK_METHOD(int32_t, read, (uint16_t address, void* data, uint16_t size), (override));
        MOCK_METHOD(int32_t, write, (uint16_t address, void const* data, uint16_t size), (override));
    };
}

#endif
