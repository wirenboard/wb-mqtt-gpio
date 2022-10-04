#include "gpio_line.h"
#include "gpio_counter.h"
#include "gpio_chip.h"
#include "declarations.h"
#include "types.h"
#include <gtest/gtest.h>

class TDebounceTest : public testing::Test
{
protected:
    TGpioLine InitFakeGpioLine(int debounceTimeuotUs, uint8_t initialGpioState);
    void HandleGpioEvent(TGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts);
};

TGpioLine TDebounceTest::InitFakeGpioLine(int debounceTimeuotUs, uint8_t initialGpioState)
{
    TGpioLineConfig fakeGpioLineConfig;
    fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(debounceTimeuotUs);
    fakeGpioLineConfig.Offset = 0;
    fakeGpioLineConfig.Name = "testline";
    auto line = TGpioLine(fakeGpioLineConfig);
    line.SetCachedValue(initialGpioState);
    line.SetCachedValueUnfiltered(initialGpioState);
    return line;
}

void TDebounceTest::HandleGpioEvent(TGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts)
{
    gpioLine.HandleInterrupt(EGpioEdge::BOTH, ts);
    gpioLine.SetCachedValueUnfiltered(gpioValue);
}

TEST_F(TDebounceTest, value_is_stable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    int debounceTimeuotUs = 50000;
    auto now = std::chrono::steady_clock::now();
    TGpioLine fakeGpioLine = InitFakeGpioLine(debounceTimeuotUs, initialGpioState);

    ASSERT_EQ(fakeGpioLine.GetValue(), initialGpioState);
    ASSERT_EQ(fakeGpioLine.GetValueUnfiltered(), initialGpioState);

    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);
    ASSERT_EQ(fakeGpioLine.GetValueUnfiltered(), assumedGpioState);

    ASSERT_TRUE(fakeGpioLine.UpdateIfStable(now + std::chrono::microseconds(debounceTimeuotUs + 1)));
    ASSERT_EQ(fakeGpioLine.GetValue(), assumedGpioState);
}

TEST_F(TDebounceTest, value_is_unstable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    int debounceTimeuotUs = 50000;
    auto now = std::chrono::steady_clock::now();
    TGpioLine fakeGpioLine = InitFakeGpioLine(debounceTimeuotUs, initialGpioState);

    ASSERT_EQ(fakeGpioLine.GetValue(), initialGpioState);
    ASSERT_EQ(fakeGpioLine.GetValueUnfiltered(), initialGpioState);

    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);
    ASSERT_EQ(fakeGpioLine.GetValueUnfiltered(), assumedGpioState);

    ASSERT_FALSE(fakeGpioLine.UpdateIfStable(now + std::chrono::microseconds(debounceTimeuotUs - 1)));
    ASSERT_EQ(fakeGpioLine.GetValue(), initialGpioState);
}
