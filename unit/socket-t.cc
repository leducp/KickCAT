#include <gtest/gtest.h>
#include "kickcat/SocketNull.h"
#include "Mocks.h"

using namespace kickcat;

TEST(SoketNull, it_does_nothing)
{
    SocketNull socket;
    socket.open("");
    socket.close();
    ASSERT_EQ(0,  socket.read(nullptr, 0));
    ASSERT_EQ(42, socket.write(nullptr, 42));
}


TEST(DiagSocket, index_rolling)
{
    MockDiagSocket socket;

    for (int i = 1; i < 65536; ++i)
    {
        uint16_t index = socket.nextIndex();
        ASSERT_LT(index, mailbox::GATEWAY_MAX_REQUEST);
    }
}
