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

        MOCK_METHOD(void, open, (std::string const&, uint8_t, uint8_t, uint32_t), (override));
        MOCK_METHOD(void, close, (), (override));

        MOCK_METHOD(void, transfer, (uint8_t const*, uint8_t*, uint32_t), (override));
        MOCK_METHOD(void, enableChipSelect, (), (override));
        MOCK_METHOD(void, disableChipSelect, (), (override));

        MOCK_METHOD(void, setBaudRate, (uint32_t), (override));
        MOCK_METHOD(void, setMode, (uint8_t, uint8_t), (override));
    };
}

#endif
