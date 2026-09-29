#include "config.h"
#include <gtest/gtest.h>
#include <sstream>
#include <vector>

namespace
{
    Json::Value ParseJson(const std::string& str)
    {
        Json::Value res;
        std::istringstream in(str);
        Json::CharReaderBuilder readerBuilder;
        Json::String errs;
        EXPECT_TRUE(Json::parseFromStream(readerBuilder, in, &res, &errs)) << errs;
        return res;
    }
} // namespace

class TConfigTest: public testing::Test
{
protected:
    std::string testRootDir;
    std::string schemaFile;

    void SetUp()
    {
        char* d = getenv("TEST_DIR_ABS");
        if (d != NULL) {
            testRootDir = d;
            testRootDir += '/';
        }
        testRootDir += "config_test_data";

        schemaFile = testRootDir + "/../../wb-mqtt-gpio.schema.json";
    }
};

TEST_F(TConfigTest, no_file)
{
    ASSERT_THROW(LoadConfig("fake.conf", "", "", schemaFile);, std::runtime_error);
    ASSERT_THROW(LoadConfig("", "a1", "", schemaFile), std::runtime_error);
    ASSERT_THROW(LoadConfig(testRootDir + "/bad/bad1.conf", "", "", ""), std::runtime_error);
}

TEST_F(TConfigTest, bad_config)
{
    for (size_t i = 1; i <= 9; ++i) {
        ASSERT_THROW(LoadConfig(testRootDir + "/bad/bad" + std::to_string(i) + ".conf", "", "", schemaFile),
                     std::runtime_error)
            << "bad" << i << ".conf";
    }
    ASSERT_THROW(LoadConfig("", testRootDir + "/bad/bad1.conf", "", schemaFile), std::runtime_error);
}

TEST_F(TConfigTest, bad_titles_config)
{
    // bad10 - non-string title, bad11 - unknown type, bad12 - empty title
    for (size_t i = 10; i <= 12; ++i) {
        ASSERT_THROW(LoadConfig(testRootDir + "/bad/bad" + std::to_string(i) + ".conf", "", "", schemaFile),
                     std::runtime_error)
            << "bad" << i << ".conf";
    }
}

TEST_F(TConfigTest, good_config)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good1/wb-mqtt-gpio.conf", "", "", schemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip2");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A1_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].LoadPreviousState, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::RISING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 100.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 15);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "watt_meter");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DebounceTimeout, std::chrono::microseconds(20000));
}

TEST_F(TConfigTest, optional_config)
{
    TGpioDriverConfig cfg =
        LoadConfig(testRootDir + "/good1/wb-mqtt-gpio.conf", testRootDir + "/good1/optional.conf", "", schemaFile);
    ASSERT_EQ(cfg.DeviceName, "I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip22");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A2_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 32);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Output);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].LoadPreviousState, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::FALLING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 1002.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 152);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "water_meter");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DebounceTimeout, std::chrono::microseconds(10000));
}

TEST_F(TConfigTest, full_main_config)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good2/wb-mqtt-gpio.conf",
                                       "",
                                       testRootDir + "/good2/wb-mqtt-gpio.conf.d",
                                       schemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 2);
    ASSERT_EQ(cfg.Chips[1].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[1].Path, "/dev/gpiochip2");
    ASSERT_EQ(cfg.Chips[1].Lines[0].Name, "A1_OUT");
    ASSERT_EQ(cfg.Chips[1].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[1].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[1].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[1].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[1].Lines[0].LoadPreviousState, true);
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
    ASSERT_EQ(cfg.Chips[0].Lines[0].LoadPreviousState, true);
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
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good3/wb-mqtt-gpio.conf",
                                       "",
                                       testRootDir + "/good3/wb-mqtt-gpio.conf.d",
                                       schemaFile);
    ASSERT_EQ(cfg.DeviceName, "Discrete I/O");
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip22");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A2_OUT");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesCurrent, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].DecimalPlacesTotal, 3);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Output);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InitialState, true);
    ASSERT_EQ(cfg.Chips[0].Lines[0].LoadPreviousState, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].InterruptEdge, EGpioEdge::RISING);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenDrain, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsOpenSource, false);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Multiplier, 100.0);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 152);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Type, "watt_meter");
    ASSERT_EQ(cfg.Chips[0].Lines[0].DebounceTimeout, std::chrono::microseconds(30000));
}

TEST_F(TConfigTest, system_channel_title_override)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good5/wb-mqtt-gpio.conf",
                                       "",
                                       testRootDir + "/good5/wb-mqtt-gpio.conf.d",
                                       schemaFile);
    ASSERT_EQ(cfg.Chips.size(), 1);
    ASSERT_EQ(cfg.Chips[0].Lines.size(), 1);
    // Only name and titles are set in the main config: the rest comes from the system config
    ASSERT_EQ(cfg.Chips[0].Path, "/dev/gpiochip3");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Name, "A1_IN");
    ASSERT_EQ(cfg.Chips[0].Lines[0].Offset, 7);
    ASSERT_EQ(cfg.Chips[0].Lines[0].Direction, EGpioDirection::Input);
    ASSERT_EQ(cfg.Chips[0].Lines[0].IsActiveLow, true);
    // Set in both configs: the main config wins
    ASSERT_EQ(cfg.Chips[0].Lines[0].Title, "Leak sensor");
    ASSERT_EQ(cfg.Chips[0].Lines[0].TitleTotal, "User total");
    // Set only in the system config: kept
    ASSERT_EQ(cfg.Chips[0].Lines[0].TitleCurrent, "System current");
}

TEST_F(TConfigTest, good_config_debug_option)
{
    TGpioDriverConfig cfg = LoadConfig(testRootDir + "/good1/wb-mqtt-gpio.conf", "", "", schemaFile);
    ASSERT_EQ(cfg.Debug, true);
}

TEST_F(TConfigTest, confed_default_titles)
{
    auto json = BuildJsonForConfed(testRootDir + "/confed/wb-mqtt-gpio.conf",
                                   testRootDir + "/confed/wb-mqtt-gpio.conf.d",
                                   schemaFile);
    // "title" is filled for all channels, counter titles only for inputs (direction may come from system config)
    auto expected = ParseJson(R"({
        "device_name": "Discrete I/O",
        "channels": [
            {
                "name": "A1_IN",
                "direction": "input",
                "title": "System A1",
                "title_total": "A1_IN_total",
                "title_current": "A1_IN_current"
            },
            {
                "name": "A2_OUT",
                "title": "Lamp"
            },
            {
                "name": "A3_IN",
                "type": "water_meter",
                "title": "A3_IN",
                "title_total": "Water",
                "title_current": "A3_IN_current"
            },
            {
                "name": "A4_IN",
                "direction": "input",
                "title": "A4_IN",
                "title_total": "A4_IN_total",
                "title_current": "A4_IN_current"
            },
            {
                "name": "C1",
                "gpio": { "chip": "/dev/gpiochip1", "offset": 5 },
                "direction": "input",
                "title": "C1",
                "title_total": "C1_total",
                "title_current": "C1_current"
            }
        ]
    })");
    ASSERT_EQ(json, expected) << json.toStyledString();
}

TEST_F(TConfigTest, confed_titles_save)
{
    auto confed = ParseJson(R"({
        "device_name": "Discrete I/O",
        "channels": [
            { "name": "A1_IN", "direction": "input", "title": "A1_IN", "title_total": "", "title_current": "Current" },
            { "name": "A2_OUT", "title": "A2_OUT", "title_total": "A2_OUT_total" },
            { "name": "A3_IN", "title": "System A1" },
            { "name": "C1", "gpio": 5, "direction": "input", "title": "C1", "title_total": "Total" }
        ]
    })");
    auto config = BuildConfigFromConfed(confed, testRootDir + "/confed/wb-mqtt-gpio.conf.d", schemaFile);
    // A title differs from the default one -> saved, even if it equals the control id;
    // equals the default one or empty -> not saved
    auto expected = ParseJson(R"({
        "device_name": "Discrete I/O",
        "channels": [
            { "name": "A1_IN", "title": "A1_IN", "title_current": "Current" },
            { "name": "A3_IN", "title": "System A1" },
            { "name": "C1", "gpio": 5, "direction": "input", "title_total": "Total" }
        ]
    })");
    ASSERT_EQ(config, expected) << config.toStyledString();
}
