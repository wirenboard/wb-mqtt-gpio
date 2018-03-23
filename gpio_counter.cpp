#include "gpio_counter.h"
#include "config.h"
#include "log.h"
#include "exceptions.h"

#include <wblib/utils.h>

#define LOG(logger) ::logger.Log() << "[gpio counter] "

using namespace std;

const auto WATT_METER = "watt_meter";
const auto WATER_METER = "water_meter";
const auto DELAY_US = TTimeIntervalUs(200000);
const auto CURRENT_TIME_INTERVAL = 1;
const auto NULL_TIME_INTERVAL = 100;

TGpioCounter::TGpioCounter(const TGpioLineConfig & config)
    : Multiplier(config.Multiplier)
    , Total(0)
    , Current(0)
    , InitialTotal(0)
    , DecimalPlacesTotal(config.DecimalPlacesTotal)
    , DecimalPlacesCurrent(config.DecimalPlacesCurrent)
    , PrintedNULL(false)
    , Counts(0)
    , InterruptEdge(config.InterruptEdge)
{
    IdPostfixTotal = "_total";
    IdPostfixCurrent = "_current";

    if (config.Type == WATT_METER) {
        Type1 = "power_consumption";
        Type2 = "power";
        ConvertingMultiplier = 1000;// convert kW to W (1/h)
        DecimalPlacesCurrent = (DecimalPlacesCurrent == -1) ? 2 : DecimalPlacesCurrent;
        DecimalPlacesTotal = (DecimalPlacesTotal == -1) ? 3 : DecimalPlacesTotal;
    } else if (config.Type == WATER_METER) {
        Type1 = "water_consumption";
        Type2 = "water_flow";
        ConvertingMultiplier = 1.0 / 3600; // convert 1/h to 1/s
        DecimalPlacesCurrent = (DecimalPlacesCurrent == -1) ? 3 : DecimalPlacesCurrent;
        DecimalPlacesTotal = (DecimalPlacesTotal == -1) ? 2 : DecimalPlacesTotal;
    } else {
        LOG(Error) << "Uknown gpio type";
        wb_throw(TGpioDriverException, "unknown GPIO type: '" + config.Type + "'");
    }

    LOG(Debug) << "New counter of type '" << config.Type << "' for " << config.Name << " edge: " << GpioEdgeToString(config.InterruptEdge);
}

TGpioCounter::~TGpioCounter()
{

}

void TGpioCounter::HandleInterrupt(EGpioEdge edge, TTimeIntervalUs interval)
{
    if (edge != InterruptEdge)
        return;

    if (Counts > 0 && interval < DELAY_US) {
        return;
    }
    Counts++;

    if (interval == TTimeIntervalUs::zero()) {
        Current = -1;
    } else {
        Current = 3600.0 * 1000000 * ConvertingMultiplier / (interval.count() * Multiplier); // convert microseconds to seconds, hours to seconds
    }
    Total = (float) Counts / Multiplier + InitialTotal;
}

float TGpioCounter::GetCurrent() const
{
    return Current;
}

float TGpioCounter::GetTotal() const
{
    return Total;
}

uint64_t TGpioCounter::GetCounts() const
{
    return Counts;
}

vector<TGpioCounter::TMetadataPair> TGpioCounter::GetIdsAndTypes(const string & baseId) const
{
    return {
        { baseId + IdPostfixTotal,   Type1 },
        { baseId + IdPostfixCurrent, Type2 }
    };
}

vector<TGpioCounter::TValuePair> TGpioCounter::GetIdsAndValues(const string & baseId) const
{
    return {
        { baseId + IdPostfixTotal,   Total },
        { baseId + IdPostfixCurrent, Current }
    };
}

void TGpioCounter::SetInterruptEdge(EGpioEdge edge)
{
    InterruptEdge = edge;
}

EGpioEdge TGpioCounter::GetInterruptEdge() const
{
    return InterruptEdge;
}
