#include "declarations.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "types.h"
#include <gtest/gtest.h>

class TLineErrorTest: public testing::Test
{
protected:
    TGpioLineConfig fakeGpioLineConfig;

    void SetUp()
    {
        int debounceTimeoutUs = 0;

        fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(debounceTimeoutUs);
        fakeGpioLineConfig.Offset = 0;
        fakeGpioLineConfig.Name = "testline";
    }
};

TEST_F(TLineErrorTest, initial_error)
{
    const auto fakeGpioLine = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    ASSERT_EQ(fakeGpioLine->GetError(), 0);
}

TEST_F(TLineErrorTest, set_clear_error)
{
    const auto chip = std::make_shared<TGpioChip>("disconnected_1");
    const auto fakeGpioLine = std::make_shared<TGpioLine>(chip, fakeGpioLineConfig);
    ASSERT_EQ(fakeGpioLine->GetError(), 0);
    fakeGpioLine->IoctlGetGpiohandleData();
    ASSERT_NE(fakeGpioLine->GetError(), 0);
    fakeGpioLine->ClearError();
    ASSERT_EQ(fakeGpioLine->GetError(), 0);
}

TEST_F(TLineErrorTest, does_need_reinit)
{
    const auto chip = std::make_shared<TGpioChip>("disconnected_1");
    const auto fakeGpioLine = std::make_shared<TGpioLine>(chip, fakeGpioLineConfig);

    fakeGpioLine->AccessChip()->SetLabel("mcp23017");

    ASSERT_FALSE(fakeGpioLine->GetNeedsReinit());
    fakeGpioLine->ClearError();
    ASSERT_TRUE(fakeGpioLine->GetNeedsReinit());
}
