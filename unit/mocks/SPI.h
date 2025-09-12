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
        MOCK_METHOD(void, transfer, (uint8_t const*, uint8_t*, uint32_t), (override));
        MOCK_METHOD(void, enableChipSelect, (), (override));
        MOCK_METHOD(void, disableChipSelect, (), (override));

        MOCK_METHOD(void, setBaudRate, (uint32_t baud), (override));
        MOCK_METHOD(uint32_t, getBaudRate, (), (const, override));

        MOCK_METHOD(void, setMode, (uint8_t mode), (override));
        MOCK_METHOD(uint8_t, getMode, (), (const, override));

        MOCK_METHOD(void, setDevice, (const std::string& devicePath), (override));
        MOCK_METHOD(std::string, getDevice, (), (const, override));

        MOCK_METHOD(void, setChipSelect, (uint32_t cs), (override));
        MOCK_METHOD(uint32_t, getChipSelect, (), (const, override));
    };
}

#endif
