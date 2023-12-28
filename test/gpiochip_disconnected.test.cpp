#include "declarations.h"
#include "gpio_chip.h"
#include "gpio_chip_driver.h"
#include "gpio_line.h"
#include "types.h"
#include <gtest/gtest.h>

class TDisconnectedChipTest: public testing::Test
{};

TEST_F(TDisconnectedChipTest, init_disconnected_chip)
{
    auto dummyPath = "/dev/DUMMY_PATH";
    auto chip = std::make_shared<TGpioChip>(dummyPath);
    ASSERT_EQ(chip->GetPath(), dummyPath);
    ASSERT_FALSE(chip->IsValid());
}

class TGpioChipDisconnectedTest: public testing::Test
{
protected:
    TGpioChipConfig fakeGpioChipConfig{"disconnected_1"};
    TGpioLineConfig fakeGpioLineConfig;

    void SetUp()
    {
        fakeGpioLineConfig.DebounceTimeout = std::chrono::microseconds(0);
        fakeGpioLineConfig.Offset = 0;
        fakeGpioLineConfig.Name = "testline";
        fakeGpioLineConfig.Direction = EGpioDirection::Input;

        fakeGpioChipConfig.Lines.push_back(fakeGpioLineConfig);
    }
};

TEST_F(TGpioChipDisconnectedTest, add_disconnected_line)
{
    const auto chip = std::make_shared<TGpioChip>("disconnected_1");
    auto fakeGpioChipDriver = std::make_shared<TGpioChipDriver>(fakeGpioChipConfig);
    auto fakeGpioLine = std::make_shared<TGpioLine>(chip, fakeGpioLineConfig);
    fakeGpioLine->SetFd(0);
    ASSERT_EQ(fakeGpioLine->GetFd(), 0);
    fakeGpioChipDriver->AddDisconnectedLine(fakeGpioLine);
    ASSERT_EQ(fakeGpioLine->GetFd(), -2);
    fakeGpioChipDriver->AddDisconnectedLine(fakeGpioLine);
    ASSERT_EQ(fakeGpioLine->GetFd(), -3);
}
