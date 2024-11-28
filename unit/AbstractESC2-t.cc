#include <gtest/gtest.h>
#include "kickcat/AbstractESC2.h"

using namespace kickcat;

class TestESC : public AbstractESC2
{
    using AbstractESC2::AbstractESC2;
    virtual int32_t read(uint16_t address, void* data, uint16_t size) {};
    virtual int32_t write(uint16_t address, void const* data, uint16_t size) {};
};


uint32_t alStatus_{};
uint32_t alStatusCode_{};
uint32_t alControl_{};

class MockPDI : public PDIProxy
{
    void setALStatus(uint32_t alStatus) override
    {
        alStatus_ = alStatus;
    };
    void setALStatusCode(uint32_t alStatusCode) override
    {
        alStatusCode_ = alStatusCode;
    };
    uint32_t getALControl() override
    {
        return alControl_;
    };
};

class AbstractESC2Test : public testing::Test
{
public:
    MockPDI pdi;
    TestESC esc{pdi};
};


TEST_F(AbstractESC2Test, initToPreop)
{
    esc.init();
    ASSERT_EQ(alStatus_, State::INIT);

    alControl_ = State::PRE_OP;
    esc.routine();

    ASSERT_EQ(alStatus_, State::PRE_OP);
}

