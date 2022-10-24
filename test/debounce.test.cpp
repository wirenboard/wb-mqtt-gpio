#include "gpio_line.h"
#include "gpio_counter.h"
#include "gpio_chip.h"
#include "declarations.h"
#include "types.h"
#include <gtest/gtest.h>

class TDebounceTest : public testing::Test
{
protected:
    const PGpioLine & InitFakeGpioLine(int debounceTimeoutUs, uint8_t initialGpioState);
    void HandleGpioEvent(const PGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts);
};

const PGpioLine & TDebounceTest::InitFakeGpioLine(int debounceTimeoutUs, uint8_t initialGpioState)
{
    TGpioLineConfig fakeGpioLineConfig;
    fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(debounceTimeoutUs);
    fakeGpioLineConfig.Offset = 0;
    fakeGpioLineConfig.Name = "testline";
    const auto & line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    line->SetCachedValue(initialGpioState);
    line->SetCachedValueUnfiltered(initialGpioState);
    return line;
}

void TDebounceTest::HandleGpioEvent(const PGpioLine & gpioLine, uint8_t gpioValue, TTimePoint ts)
{
    gpioLine->HandleInterrupt(EGpioEdge::BOTH, ts);
    gpioLine->SetCachedValueUnfiltered(gpioValue);
}

TEST_F(TDebounceTest, value_is_stable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    int debounceTimeoutUs = 50000;
    auto now = std::chrono::steady_clock::now();
    const auto & fakeGpioLine = InitFakeGpioLine(debounceTimeoutUs, initialGpioState);

    ASSERT_EQ(fakeGpioLine->GetValue(), initialGpioState);
    ASSERT_EQ(fakeGpioLine->GetValueUnfiltered(), initialGpioState);

    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);
    ASSERT_EQ(fakeGpioLine->GetValueUnfiltered(), assumedGpioState);

    ASSERT_TRUE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs + 1)));
    ASSERT_EQ(fakeGpioLine->GetValue(), assumedGpioState);
}

TEST_F(TDebounceTest, value_is_unstable)
{
    uint8_t initialGpioState = 0, assumedGpioState = 1;
    int debounceTimeoutUs = 50000;
    auto now = std::chrono::steady_clock::now();
    const auto & fakeGpioLine = InitFakeGpioLine(debounceTimeoutUs, initialGpioState);

    ASSERT_EQ(fakeGpioLine->GetValue(), initialGpioState);
    ASSERT_EQ(fakeGpioLine->GetValueUnfiltered(), initialGpioState);

    HandleGpioEvent(fakeGpioLine, assumedGpioState, now);
    ASSERT_EQ(fakeGpioLine->GetValueUnfiltered(), assumedGpioState);

    ASSERT_FALSE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs - 1)));
    ASSERT_EQ(fakeGpioLine->GetValue(), initialGpioState);
}
