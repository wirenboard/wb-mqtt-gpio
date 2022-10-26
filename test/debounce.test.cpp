#include "gpio_line.h"
#include "gpio_counter.h"
#include "gpio_chip.h"
#include "declarations.h"
#include "types.h"
#include <gtest/gtest.h>

class TDebounceTest : public testing::Test
{
protected:
    int debounceTimeoutUs;
    TGpioLineConfig fakeGpioLineConfig;
    void InitGpioLine(const PGpioLine & gpioLine, uint8_t gpioValue);
    void HandleGpioEvent(const PGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts);

    void SetUp()
    {
        debounceTimeoutUs = 50000;

        fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(debounceTimeoutUs);
        fakeGpioLineConfig.Offset = 0;
        fakeGpioLineConfig.Name = "testline";
    }
};

void TDebounceTest::InitGpioLine(const PGpioLine & gpioLine, uint8_t gpioValue)
{
    gpioLine->SetCachedValue(gpioValue);
    gpioLine->SetCachedValueUnfiltered(gpioValue);

    ASSERT_EQ(gpioLine->GetValue(), gpioValue);
    ASSERT_EQ(gpioLine->GetValueUnfiltered(), gpioValue);

}

void TDebounceTest::HandleGpioEvent(const PGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts)
{
    gpioLine->HandleInterrupt(EGpioEdge::BOTH, ts);
    gpioLine->SetCachedValueUnfiltered(gpioValue);

    ASSERT_EQ(gpioLine->GetValueUnfiltered(), gpioValue);
}

TEST_F(TDebounceTest, value_is_stable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    auto now = std::chrono::steady_clock::now();
    const auto fakeGpioLine = std::make_shared<TGpioLine>(fakeGpioLineConfig);

    InitGpioLine(fakeGpioLine, initialGpioState);
    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);

    ASSERT_TRUE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs + 1)));
    ASSERT_EQ(fakeGpioLine->GetValue(), assumedGpioState);
}

TEST_F(TDebounceTest, value_is_unstable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    auto now = std::chrono::steady_clock::now();
    const auto fakeGpioLine = std::make_shared<TGpioLine>(fakeGpioLineConfig);

    InitGpioLine(fakeGpioLine, initialGpioState);
    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);

    ASSERT_FALSE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs - 1)));
    ASSERT_EQ(fakeGpioLine->GetValue(), initialGpioState);
}
