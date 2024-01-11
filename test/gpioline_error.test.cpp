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
    ASSERT_TRUE(fakeGpioLine->GetError().empty());
}

TEST_F(TLineErrorTest, set_clear_error)
{
    const auto chip = std::make_shared<TGpioChip>("disconnected_1");
    const auto fakeGpioLine = std::make_shared<TGpioLine>(chip, fakeGpioLineConfig);
    ASSERT_TRUE(fakeGpioLine->GetError().empty());
    fakeGpioLine->SetError("w");
    fakeGpioLine->SetError("r");
    fakeGpioLine->SetError("r");
    fakeGpioLine->SetError("p");
    fakeGpioLine->SetError("p");
    ASSERT_EQ(fakeGpioLine->GetError(), "wrp");
}
