#include "config.h"
#include "declarations.h"
#include "gpio_chip_driver.h"
#include "gpio_counter.h"
#include "gpio_line.h"
#include "types.h"
#include <gtest/gtest.h>

class TFakeGpioChipDriver: public TGpioChipDriver
{
    using TGpioLines = std::vector<PGpioLine>;
    uint8_t LineReadVal;

public:
    TFakeGpioChipDriver(const PGpioLine& line, uint8_t fakeGpioLineVal): TGpioChipDriver()
    {
        LineReadVal = fakeGpioLineVal;
        AddFakeGpioLine(line);
        AutoDetectInterruptEdges();
    }

private:
    void AddFakeGpioLine(const PGpioLine& line)
    {
        Lines[line->GetFd()].push_back(line);
    }
    void ReadLinesValues(const TGpioLines&)
    {
        for (const auto& fdLines: Lines) {
            for (auto line: fdLines.second) {
                line->SetCachedValue(LineReadVal);
            }
        }
    }
    void ReListenLine(PGpioLine)
    {}
};

class TFakeGpioLine: public TGpioLine
{
public:
    TFakeGpioLine(const TGpioLineConfig& config): TGpioLine(config)
    {}
    bool IsHandled() const
    {
        return true;
    }
    bool IsOutput() const
    {
        return false;
    }
    std::string DescribeShort() const
    {
        return "Mocked gpio line";
    }
};

class TGpioCounterGetEdgeTest: public testing::Test
{
protected:
    TGpioLineConfig fakeGpioLineConfig;

    void SetUp()
    {
        fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(0);
        fakeGpioLineConfig.Offset = 0;
        fakeGpioLineConfig.Name = "testline";
        fakeGpioLineConfig.Type = "water_meter";
    }
};

TEST_F(TGpioCounterGetEdgeTest, counter_edge_both)
{
    auto assumedEdge = EGpioEdge::BOTH;
    fakeGpioLineConfig.InterruptEdge = assumedEdge;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    auto fakeGpioChipDriver = std::make_shared<TFakeGpioChipDriver>(fakeGpioLine, 0);

    ASSERT_EQ(fakeGpioLine->GetInterruptEdge(), assumedEdge);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetInterruptEdge(), assumedEdge);
}

TEST_F(TGpioCounterGetEdgeTest, counter_edge_rising)
{
    auto assumedEdge = EGpioEdge::RISING;
    fakeGpioLineConfig.InterruptEdge = assumedEdge;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    auto fakeGpioChipDriver = std::make_shared<TFakeGpioChipDriver>(fakeGpioLine, 0);

    ASSERT_EQ(fakeGpioLine->GetInterruptEdge(), assumedEdge);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetInterruptEdge(), assumedEdge);
}

TEST_F(TGpioCounterGetEdgeTest, counter_edge_falling)
{
    auto assumedEdge = EGpioEdge::FALLING;
    fakeGpioLineConfig.InterruptEdge = assumedEdge;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    auto fakeGpioChipDriver = std::make_shared<TFakeGpioChipDriver>(fakeGpioLine, 0);

    ASSERT_EQ(fakeGpioLine->GetInterruptEdge(), assumedEdge);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetInterruptEdge(), assumedEdge);
}

TEST_F(TGpioCounterGetEdgeTest, counter_edge_auto_falling)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::AUTO;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    const auto fakeGpioChipDriver = std::make_shared<TFakeGpioChipDriver>(fakeGpioLine, 1);

    ASSERT_EQ(fakeGpioLine->GetInterruptEdge(), EGpioEdge::FALLING);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetInterruptEdge(), EGpioEdge::FALLING);
}

TEST_F(TGpioCounterGetEdgeTest, counter_edge_auto_rising)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::AUTO;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    const auto fakeGpioChipDriver = std::make_shared<TFakeGpioChipDriver>(fakeGpioLine, 0);

    ASSERT_EQ(fakeGpioLine->GetInterruptEdge(), EGpioEdge::RISING);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetInterruptEdge(), EGpioEdge::RISING);
}
