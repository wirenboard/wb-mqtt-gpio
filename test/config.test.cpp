#include "config.h"
#include <gtest/gtest.h>
#include <vector>

class TConfigTest : public testing::Test
{
protected:
    std::string testRootDir;
    std::string shemaFile;

    void SetUp()
    {
        char* d = getenv("TEST_DIR_ABS");
        if (d != NULL) {
            testRootDir = d;
            testRootDir += '/';
        }
        testRootDir += "config_test_data";

        shemaFile = testRootDir + "/../../wb-mqtt-gpio.schema.json";
    }
};

TEST_F(TConfigTest, no_file)
{
    ASSERT_THROW(LoadConfig("fake.conf", "", shemaFile);, std::runtime_error);
    ASSERT_THROW(LoadConfig("", "a1", shemaFile), std::runtime_error);
    ASSERT_THROW(LoadConfig(testRootDir + "/bad/bad1.conf", "", ""), std::runtime_error);
}

TEST_F(TConfigTest, bad_config)
{
    for(size_t i=1; i<=9; ++i) {
        ASSERT_THROW(LoadConfig(testRootDir + "/bad/bad" + std::to_string(i) + ".conf", "", shemaFile), std::runtime_error) << "bad" << i << ".conf";
    }
    ASSERT_THROW(LoadConfig("", testRootDir + "/bad/bad1.conf", shemaFile), std::runtime_error);
}

TEST_F(TConfigTest, good_config)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good1/wb-mqtt-gpio.conf", "", shemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip2");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A1_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::RISING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 100.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 15);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "watt_meter");
}


TEST_F(TConfigTest, optional_config)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good1/wb-mqtt-gpio.conf", testRootDir + "/good1/optional.conf", shemaFile);
    ASSERT_EQ(cfg.DeviceName, "I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip22");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A2_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Output);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::FALLING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 1002.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 152);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "water_meter");
}

TEST_F(TConfigTest, full_main_config)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good2/wb-mqtt-gpio.conf", "", shemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 2);
    ASSERT_EQ(cfg.Chips[1].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[1].Path, "/dev/gpiochip2");
    ASSERT_EQ(cfg.Chips[1].Lines[0].Name, "A1_OUT");
    ASSERT_EQ(cfg.Chips[1].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[1].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[1].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[1].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[1].Lines[0].InterruptEdge, EGpioEdge::RISING);
    ASSERT_EQ(cfg.Chips[1].Lines[0].IsActiveLow, true);
    ASSERT_EQ(cfg.Chips[1].Lines[0].IsOpenDrain, true);
    ASSERT_EQ(cfg.Chips[1].Lines[0].IsOpenSource, true);
    ASSERT_EQ(cfg.Chips[1].Lines[0].Multiplier, 100.0);
    ASSERT_EQ(cfg.Chips[1].Lines[0].Offset, 15);
    ASSERT_EQ(cfg.Chips[1].Lines[0].Type, "watt_meter");

    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip22");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A2_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Output);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::FALLING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 1002.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 152);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "water_meter");
}

TEST_F(TConfigTest, line_override)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good3/wb-mqtt-gpio.conf", "", shemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip2");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A2_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::RISING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 100.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 15);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "watt_meter");
}