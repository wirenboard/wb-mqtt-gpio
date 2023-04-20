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

using namespace WBMQTT;
using namespace WBMQTT::Testing;
using namespace std;

class TInitialStateTest: public virtual TLoggedFixture
{
protected:
    PFakeMqttBroker MqttBroker;
    PDeviceDriver MqttDriver;
    PDriverTx Tx;
    PLocalDevice LocalDevice;

    void SetUp()
    {
        TLoggedFixture::SetUp();

        MqttBroker = WBMQTT::Testing::NewFakeMqttBroker(*this);
        auto mqttClient = MqttBroker->MakeClient("test");
        auto driverBackend = NewDriverBackend(mqttClient);
        auto MqttDriver = NewDriver(TDriverArgs{}
                                        .SetId("em-test")
                                        .SetBackend(driverBackend)
                                        .SetIsTesting(true)
                                        .SetReownUnknownDevices(true)
                                        .SetUseStorage(false));

        MqttDriver->StartLoop();
        Tx = MqttDriver->BeginTx();
        LocalDevice = Tx->CreateDevice(TLocalDeviceArgs{}
                                           .SetId("InitialStateTest")
                                           .SetTitle("InitialStateTestDevice")
                                           .SetIsVirtual(true)
                                           .SetDoLoadPrevious(false))
                          .GetValue();
    }

    void TearDown()
    {
        TLoggedFixture::TearDown();
        MqttDriver->StopLoop();
    }
};

TEST_F(TInitialStateTest, InitialStateTest)
{
    MqttBroker->Publish("test", {TMqttMessage{"/devices/InitialStateTest/controls/TestControl", "1", 0, true}});
    // MqttBroker->WaitForPublish();
    auto futureControl = LocalDevice->CreateControl(Tx,
                                                    TControlArgs{}
                                                        .SetId("TestControl")
                                                        .SetType("switch")
                                                        .SetReadonly(false)
                                                        .SetRawValue("1")
                                                        .SetDoLoadPrevious(false));
    bool result = futureControl.GetValue()->GetValue().As<bool>();
}
