#include "declarations.h"
#include "gpio_chip.h"
#include "gpio_counter.h"
#include "gpio_line.h"
#include "types.h"
#include <gtest/gtest.h>

class TDebounceTest: public testing::Test
{
protected:
    int debounceTimeoutUs;
    TGpioLineConfig fakeGpioLineConfig;
    void InitGpioLine(const PGpioLine& gpioLine, uint8_t gpioValue);
    void HandleGpioEvent(const PGpioLine& gpioLine, uint8_t gpioValue, TTimePoint ts);

    void SetUp()
    {
        debounceTimeoutUs = 50000;

        fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(debounceTimeoutUs);
        fakeGpioLineConfig.Offset = 0;
        fakeGpioLineConfig.Name = "testline";
        fakeGpioLineConfig.Type = "water_meter";
        fakeGpioLineConfig.InterruptEdge = EGpioEdge::BOTH;
    }
};

void TDebounceTest::InitGpioLine(const PGpioLine& gpioLine, uint8_t gpioValue)
{
    gpioLine->SetCachedValue(gpioValue);
    gpioLine->SetCachedValueUnfiltered(gpioValue);

    ASSERT_EQ(gpioLine->GetValue(), gpioValue);
    ASSERT_EQ(gpioLine->GetValueUnfiltered(), gpioValue);
}

void TDebounceTest::HandleGpioEvent(const PGpioLine& gpioLine, uint8_t gpioValue, TTimePoint ts)
{
    gpioLine->HandleInterrupt(ts);
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

TEST_F(TDebounceTest, count_debounce_not_firing)
{
    uint32_t betweenEventsUs = 1000000;
    auto now = std::chrono::steady_clock::now();
    const auto fakeGpioLine = std::make_shared<TGpioLine>(fakeGpioLineConfig);

    InitGpioLine(fakeGpioLine, 0);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), 0);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetTotal(), 0);

    HandleGpioEvent(fakeGpioLine, 1, now);
    ASSERT_TRUE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs + 1)));
    HandleGpioEvent(fakeGpioLine, 0, (now + std::chrono::microseconds(betweenEventsUs)));
    ASSERT_TRUE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(betweenEventsUs + debounceTimeoutUs + 1)));

    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), 3600);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetTotal(), 2);
}

TEST_F(TDebounceTest, count_debounce_is_firing)
{
    uint32_t betweenEventsUs = 1000000;
    auto now = std::chrono::steady_clock::now();
    const auto fakeGpioLine = std::make_shared<TGpioLine>(fakeGpioLineConfig);

    InitGpioLine(fakeGpioLine, 0);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), 0);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetTotal(), 0);

    HandleGpioEvent(fakeGpioLine, 1, now);
    ASSERT_FALSE(fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(debounceTimeoutUs - 1)));
    HandleGpioEvent(fakeGpioLine, 0, now + std::chrono::microseconds(betweenEventsUs));
    ASSERT_FALSE(
        fakeGpioLine->UpdateIfStable(now + std::chrono::microseconds(betweenEventsUs + debounceTimeoutUs - 1)));

    ASSERT_EQ(fakeGpioLine->GetCounter()->GetTotal(), 0);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), 0);
}

// Single-edge counters count a transition only when the settled level is a real
// change in the configured direction that held for the whole debounce window.
// A short spike that returns within the window — even one momentarily read as
// high — is not a transition once the line settles, so it must not be counted.
// (The opposite edge that reveals the return is visible because both kernel
// edges are subscribed, see TGpioChipDriver::TryListenLine.)
class TDebounceEdgeTest: public TDebounceTest
{
protected:
    // Drive an edge (cache the new level + stamp its time) and then let the line
    // settle: the value is committed once it has held DebounceTimeout unchanged.
    void Settle(const PGpioLine& line, uint8_t level, TTimePoint edgeTs)
    {
        HandleGpioEvent(line, level, edgeTs);
        line->UpdateIfStable(edgeTs + std::chrono::microseconds(debounceTimeoutUs + 1));
    }
};

TEST_F(TDebounceEdgeTest, rising_counts_held_pulse)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::RISING;
    auto now = std::chrono::steady_clock::now();
    const auto line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    InitGpioLine(line, 0); // rest low

    Settle(line, 1, now); // rises and holds -> one count
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);

    Settle(line, 0, now + std::chrono::microseconds(1000000)); // returns low: not a rising edge
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);
}

TEST_F(TDebounceEdgeTest, rising_ignores_short_spike_caught_high)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::RISING;
    auto now = std::chrono::steady_clock::now();
    const auto line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    InitGpioLine(line, 0); // rest low

    // Spike: rises (read high), then returns low well within the debounce window.
    HandleGpioEvent(line, 1, now);
    auto tReturn = now + std::chrono::microseconds(debounceTimeoutUs / 2);
    HandleGpioEvent(line, 0, tReturn);

    // Once settled the level equals rest -> no transition -> no count.
    ASSERT_TRUE(line->UpdateIfStable(tReturn + std::chrono::microseconds(debounceTimeoutUs + 1)));
    ASSERT_EQ(line->GetCounter()->GetTotal(), 0);
}

TEST_F(TDebounceEdgeTest, rising_ignores_falling_edge_bounce)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::RISING;
    auto now = std::chrono::steady_clock::now();
    const auto line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    InitGpioLine(line, 0);

    Settle(line, 1, now); // a real pulse
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);

    // Pulse ends with contact bounce on the falling edge: 1->0->1->0 within a
    // short window. The spurious rising sub-edge must not add a second count.
    auto t = now + std::chrono::microseconds(1000000);
    HandleGpioEvent(line, 0, t);
    HandleGpioEvent(line, 1, t + std::chrono::microseconds(50));
    HandleGpioEvent(line, 0, t + std::chrono::microseconds(100));
    ASSERT_TRUE(line->UpdateIfStable(t + std::chrono::microseconds(100 + debounceTimeoutUs + 1)));
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);
}

TEST_F(TDebounceEdgeTest, falling_counts_held_pulse)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::FALLING;
    auto now = std::chrono::steady_clock::now();
    const auto line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    InitGpioLine(line, 1); // rest high

    Settle(line, 0, now); // drops and holds -> one count
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);

    Settle(line, 1, now + std::chrono::microseconds(1000000)); // returns high: not a falling edge
    ASSERT_EQ(line->GetCounter()->GetTotal(), 1);
}

TEST_F(TDebounceEdgeTest, falling_ignores_short_spike_caught_low)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::FALLING;
    auto now = std::chrono::steady_clock::now();
    const auto line = std::make_shared<TGpioLine>(fakeGpioLineConfig);
    InitGpioLine(line, 1); // rest high

    HandleGpioEvent(line, 0, now); // dips low (read low)
    auto tReturn = now + std::chrono::microseconds(debounceTimeoutUs / 2);
    HandleGpioEvent(line, 1, tReturn); // returns high before debounce elapses

    ASSERT_TRUE(line->UpdateIfStable(tReturn + std::chrono::microseconds(debounceTimeoutUs + 1)));
    ASSERT_EQ(line->GetCounter()->GetTotal(), 0);
}
