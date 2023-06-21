#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <wblib/control.h>
#include <wblib/control_args.h>
#include <wblib/device_args.h>
#include <wblib/devices.h>
#include <wblib/driver_args.h>
#include <wblib/mqtt.h>
#include <wblib/promise.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>
#include <wblib/transaction.h>
#include <wblib/utils.h>

#include "config.h"
#include "gpio_driver.h"
#include "utils.h"

using namespace WBMQTT;
using namespace WBMQTT::Testing;
using namespace std;

class TInitialStateTest: public virtual TLoggedFixture
{
protected:
    PFakeMqttBroker MqttBroker;
    std::string testRootDir;
    std::string schemaFile;

    void SetUp()
    {
        TLoggedFixture::SetUp();

        char* d = getenv("TEST_DIR_ABS");
        if (d != NULL) {
            testRootDir = d;
            testRootDir += '/';
        }
        testRootDir += "config_test_data";

        schemaFile = testRootDir + "/../../wb-mqtt-gpio.schema.json";

        MqttBroker = WBMQTT::Testing::NewFakeMqttBroker(*this);
    }
};

TEST_F(TInitialStateTest, InitialStateTest)
{
    const auto DB_FILE = "/tmp/test.db";
    std::filesystem::remove(DB_FILE);
    std::filesystem::copy_file(testRootDir + "/test.db", DB_FILE);

    TPublishParameters publishParameters;
    publishParameters.Policy = WBMQTT::TPublishParameters::PublishAll;

    auto mqttDriver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
                                            .SetBackend(NewDriverBackend(MqttBroker->MakeClient("test")))
                                            .SetId("test")
                                            .SetUseStorage(true)
                                            .SetReownUnknownDevices(true)
                                            .SetStoragePath(DB_FILE),
                                        publishParameters);

    mqttDriver->StartLoop();
    mqttDriver->WaitForReady();

    auto tx = mqttDriver->BeginTx();
    auto device =
        tx->CreateDevice(
              TLocalDeviceArgs{}.SetId("wb-gpio").SetTitle("test").SetIsVirtual(true).SetDoLoadPrevious(false))
            .GetValue();

    TGpioLineConfig lineConfig;
    ::testing::MockFunction<void(uint8_t)> mockSetValue;
    {
        lineConfig.Name = "test";
        lineConfig.InitialState = 1;
        lineConfig.LoadPreviousState = false;

        EXPECT_CALL(mockSetValue, Call(1));
        auto future = CreateOutputControl(device, tx, nullptr, lineConfig, mockSetValue.AsStdFunction());
        future.Wait();
    }

    {
        lineConfig.Name = "test2";
        lineConfig.InitialState = 0;
        lineConfig.LoadPreviousState = false;

        EXPECT_CALL(mockSetValue, Call(0));
        auto future = CreateOutputControl(device, tx, nullptr, lineConfig, mockSetValue.AsStdFunction());
        future.Wait();
    }

    {
        lineConfig.Name = "A1_OUT";
        lineConfig.InitialState = 0;
        lineConfig.LoadPreviousState = true;

        EXPECT_CALL(mockSetValue, Call(1));
        auto future = CreateOutputControl(device, tx, nullptr, lineConfig, mockSetValue.AsStdFunction());
        future.Wait();
    }

    {
        lineConfig.Name = "A2_OUT";
        lineConfig.InitialState = 1;
        lineConfig.LoadPreviousState = true;

        EXPECT_CALL(mockSetValue, Call(0));
        auto future = CreateOutputControl(device, tx, nullptr, lineConfig, mockSetValue.AsStdFunction());
        future.Wait();
    }
}
