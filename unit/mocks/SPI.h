#ifndef KICKCAT_UNIT_MOCKS_SPI_H
#define KICKCAT_UNIT_MOCKS_SPI_H

#include <gmock/gmock.h>

#include "kickcat/AbstractSPI.h"

namespace kickcat
{
    class MockSPI : public AbstractSPI
    {
    public:
        MockSPI() = default;
        virtual ~MockSPI() = default;

        MOCK_METHOD(void, init, (), (override));
        MOCK_METHOD(void, transfer,  (uint8_t const*, uint8_t*, uint32_t), (override));
        MOCK_METHOD(void, enableChipSelect, (), (override));
        MOCK_METHOD(void, disableChipSelect, (), (override));
    };
}

#endif
