#include "gpio_counter.h"
#include "config.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[gpio counter] "

const auto WATT_METER = "watt_meter";
const auto WATER_METER = "water_meter";
const auto MICROSECONDS_DELAY = 200000;
const auto CURRENT_TIME_INTERVAL = 1;
const auto NULL_TIME_INTERVAL = 100;

TGpioCounter::TGpioCounter(const TGpioLineConfig & config)
    : Type(config.Type)
    , Multiplier(config.Multiplier)

{
    if (Type == WATT_METER) {
        Topic2 = "_current";
        Value_Topic2 = "power";
        Topic1 = "_total";
        Value_Topic1 = "power_consumption";
        ConvertingMultiplier = 1000;// convert kW to W (1/h)
        DecimalPlacesCurrent = (DecimalPlacesCurrent == -1) ? 2 : DecimalPlacesCurrent;
        DecimalPlacesTotal = (DecimalPlacesTotal == -1) ? 3 : DecimalPlacesTotal;
    } else if (Type == WATER_METER) {
        Topic2 = "_current";
        Value_Topic2 = "water_flow";
        Topic1 = "_total";
        Value_Topic1 = "water_consumption";
        ConvertingMultiplier = 1.0 / 3600; // convert 1/h to 1/s
        DecimalPlacesCurrent = (DecimalPlacesCurrent == -1) ? 3 : DecimalPlacesCurrent;
        DecimalPlacesTotal = (DecimalPlacesTotal == -1) ? 2 : DecimalPlacesTotal;
    } else {
        LOG(Error) << "Uknown gpio type";
        exit(EXIT_FAILURE);
    }
}

TGpioCounter::~TGpioCounter()
{

}

void TGpioCounter::HandleInterrupt(EGpioEdge edge)
{
    if (edge == InterruptEdge) {

    }
}
