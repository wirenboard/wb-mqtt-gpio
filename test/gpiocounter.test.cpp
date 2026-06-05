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

TEST_F(TGpioCounterGetEdgeTest, counter_update_current)
{
    fakeGpioLineConfig.InterruptEdge = EGpioEdge::RISING;
    const auto fakeGpioLine = std::make_shared<TFakeGpioLine>(fakeGpioLineConfig);
    auto interval = std::chrono::microseconds(100000);
    auto assumedCurrent = (3600.0 * 1000000 * 1.0 / (interval.count() * fakeGpioLineConfig.Multiplier));

    fakeGpioLine->GetCounter()->HandleInterrupt(EGpioEdge::RISING, interval);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetTotal(), 1);
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), assumedCurrent);

    fakeGpioLine->GetCounter()->Update(std::chrono::microseconds(200000));
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), assumedCurrent / 2);

    fakeGpioLine->GetCounter()->Update(std::chrono::microseconds(200000));
    ASSERT_EQ(fakeGpioLine->GetCounter()->GetCurrent(), assumedCurrent / 4);
}

// Regression harness for the "all inputs disappearing" bug. A counter line on a
// chip without interrupt support shares a single polled fd with all the other
// inputs. AutoDetectInterruptEdges() must NOT call ReListenLine() for such a
// line, otherwise the shared fd is erased (and closed), killing every input
// behind it.
class TFakePolledChipDriver: public TGpioChipDriver
{
    using TGpioLines = std::vector<PGpioLine>;
    uint8_t LineReadVal;

public:
    int ReListenCalls = 0;

    explicit TFakePolledChipDriver(uint8_t fakeGpioLineVal): TGpioChipDriver()
    {
        LineReadVal = fakeGpioLineVal;
    }

    // Put the line onto a shared polling fd, mimicking InitLinesPolling().
    // NB: we set only the map key, NOT line->SetFd(): SetFd() calls UpdateInfo()
    // which ioctl()s AccessChip(), and these fake lines have no chip — in a
    // release (NDEBUG) build the assert in AccessChip() is a no-op, so it would
    // dereference a null shared_ptr and segfault. TFakeGpioLine::IsHandled()
    // already returns true unconditionally, so the fd on the line is not needed.
    void AddPolledLine(const PGpioLine& line, int sharedFd)
    {
        Lines[sharedFd].push_back(line);
    }

    void Detect()
    {
        AutoDetectInterruptEdges();
    }

    size_t LinesOnFd(int fd) const
    {
        const auto it = Lines.find(fd);
        return it == Lines.end() ? 0 : it->second.size();
    }

private:
    void ReadLinesValues(const TGpioLines&) override
    {
        for (const auto& fdLines: Lines) {
            for (const auto& line: fdLines.second) {
                line->SetCachedValue(LineReadVal);
            }
        }
    }
    void ReListenLine(PGpioLine) override
    {
        ++ReListenCalls;
    }
};

class TGpioCounterPollingTest: public testing::Test
{
protected:
    TGpioLineConfig MakeLineConfig(uint32_t offset, const std::string& type)
    {
        TGpioLineConfig config;
        config.DebounceTimeout = std::chrono::microseconds(0);
        config.Offset = offset;
        config.Name = "line" + std::to_string(offset);
        config.Type = type;
        config.InterruptEdge = EGpioEdge::AUTO;
        return config;
    }
};

// A polled counter line (no interrupt support) sharing an fd with a plain input
// must not trigger ReListenLine, and the shared fd must keep all its lines.
TEST_F(TGpioCounterPollingTest, polled_counter_keeps_shared_fd)
{
    // Deliberately out-of-range fd: the base dtor close()s every fd in Lines.
    const int sharedFd = 100042;

    auto counterLine = std::make_shared<TFakeGpioLine>(MakeLineConfig(0, "water_meter"));
    counterLine->SetInterruptSupport(EInterruptSupport::NO);

    auto plainInput = std::make_shared<TFakeGpioLine>(MakeLineConfig(1, ""));
    plainInput->SetInterruptSupport(EInterruptSupport::NO);

    auto driver = std::make_shared<TFakePolledChipDriver>(1);
    driver->AddPolledLine(counterLine, sharedFd);
    driver->AddPolledLine(plainInput, sharedFd);

    driver->Detect();

    // Edge must still be auto-detected via polling (read value 1 => falling).
    ASSERT_EQ(counterLine->GetCounter()->GetInterruptEdge(), EGpioEdge::FALLING);
    // But no re-listen must happen for a polled line ...
    ASSERT_EQ(driver->ReListenCalls, 0);
    // ... and the shared fd must keep both lines.
    ASSERT_EQ(driver->LinesOnFd(sharedFd), 2u);
}

// A counter line that does support interrupts must still be re-listened so the
// freshly detected edge takes effect.
TEST_F(TGpioCounterPollingTest, interrupt_counter_is_relistened)
{
    const int ownFd = 100007;

    auto counterLine = std::make_shared<TFakeGpioLine>(MakeLineConfig(0, "water_meter"));
    counterLine->SetInterruptSupport(EInterruptSupport::YES);

    auto driver = std::make_shared<TFakePolledChipDriver>(0);
    driver->AddPolledLine(counterLine, ownFd);

    driver->Detect();

    // Read value 0 => rising.
    ASSERT_EQ(counterLine->GetCounter()->GetInterruptEdge(), EGpioEdge::RISING);
    ASSERT_EQ(driver->ReListenCalls, 1);
}
