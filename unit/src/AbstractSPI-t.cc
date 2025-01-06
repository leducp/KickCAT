#include "mocks/SPI.h"

using namespace kickcat;

TEST(SPI, read_write)
{
    uint8_t buffer[256];
    MockSPI spi;

    EXPECT_CALL(spi, transfer(nullptr, buffer, sizeof(buffer))).Times(1);
    EXPECT_CALL(spi, transfer(buffer, nullptr, sizeof(buffer))).Times(1);

    spi.read (buffer, sizeof(buffer));
    spi.write(buffer, sizeof(buffer));
}
