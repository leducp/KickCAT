#include <gmock/gmock.h>
#include "kickcat/AbstractSocket.h"


namespace kickcat
{
    class MockSocket : public AbstractSocket
    {
    public:
        MOCK_METHOD(void,    open,  (std::string const& interface, microseconds timeout), (override));
        MOCK_METHOD(void,    setTimeout,  (microseconds timeout), (override));
        MOCK_METHOD(void,    close, (), (noexcept override));
        MOCK_METHOD(int32_t, read,  (uint8_t* frame, int32_t frame_size), (override));
        MOCK_METHOD(int32_t, write, (uint8_t const* frame, int32_t frame_size), (override));
    };
}
