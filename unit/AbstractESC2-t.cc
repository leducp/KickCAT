#include <gtest/gtest.h>
#include <cstring>
#include "kickcat/AbstractESC2.h"

using namespace kickcat;

uint32_t alStatus_{};
uint32_t alStatusCode_{};
uint32_t alControl_{};

class TestESC : public AbstractESC2
{
    using AbstractESC2::AbstractESC2;
    int32_t read(uint16_t address, void* data, uint16_t size)
    {
        std::memcpy(data, &alControl_, size);
        return size;
    };
    int32_t write(uint16_t address, void const* data, uint16_t size)
    {
        std::memcpy(&alStatus_, data, size);
        return size;
    };
};


class AbstractESC2Test : public testing::Test
{
public:
    TestESC esc{};
};

TEST_F(AbstractESC2Test, initToPreop)
{
    esc.init();
    ASSERT_EQ(alStatus_, State::INIT);

    esc.routine();
    ASSERT_EQ(alStatus_, State::INIT);

    alControl_ = State::PRE_OP;
    esc.routine();
    ASSERT_EQ(alStatus_, (uint32_t)State::PRE_OP);

    esc.routine();
}

