#include "gpio_counter.h"
#include "config.h"
#include "log.h"
#include "exceptions.h"
#include "utils.h"

#include <wblib/utils.h>

#define LOG(logger) ::logger.Log() << "[gpio counter] "

using namespace std;

const auto WATT_METER            = "watt_meter";
const auto WATER_METER           = "water_meter";
const auto ID_POSTFIX_TOTAL      = "_total";
const auto ID_POSTFIX_CURRENT    = "_current";
const auto DELAY_US              = TTimeIntervalUs(200000);
const auto CURRENT_TIME_INTERVAL = 1;
const auto NULL_TIME_INTERVAL    = 100;

TGpioCounter::TGpioCounter(const TGpioLineConfig & config)
    : Multiplier(config.Multiplier)
    , InitialTotal(0)
    , Total(0)
    , Current(0)
    , DecimalPlacesTotal(config.DecimalPlacesTotal)
    , DecimalPlacesCurrent(config.DecimalPlacesCurrent)
    , Counts(0)
    , InterruptEdge(config.InterruptEdge)
    , PreviousInterval(TTimeIntervalUs::zero())
{
    if (config.Type == WATT_METER) {
        TotalType   = "power_consumption";
        CurrentType = "power";
        ConvertingMultiplier = 1000;// convert kW to W (1/h)
        DecimalPlacesCurrent = (DecimalPlacesCurrent == -1) ? 2 : DecimalPlacesCurrent;
        DecimalPlacesTotal = (DecimalPlacesTotal == -1) ? 3 : DecimalPlacesTotal;
    } else if (config.Type == WATER_METER) {
        TotalType   = "water_consumption";
        CurrentType = "water_flow";
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
{}

bool TGpioCounter::HandleInterrupt(EGpioEdge edge, const TTimeIntervalUs & interval)
{
    if (edge != InterruptEdge)
        return false;

    if (Counts > 0 && interval < DELAY_US) {
        return false;
    }

    ++Counts;
    PreviousInterval = interval;

    if (interval == TTimeIntervalUs::zero()) {
        Current.Set(-1);
    } else {
        UpdateCurrent(interval);
    }
    UpdateTotal();

    return true;
}

void TGpioCounter::Update(const TTimeIntervalUs & interval)
{
    if (interval > NULL_TIME_INTERVAL * PreviousInterval) {
        Current.Set(0);
    } else if (interval > CURRENT_TIME_INTERVAL * PreviousInterval) {
        UpdateCurrent(interval);
    }
}

float TGpioCounter::GetCurrent() const
{
    return Current.Get();
}

float TGpioCounter::GetTotal() const
{
    return Total.Get();
}

uint64_t TGpioCounter::GetCounts() const
{
    return Counts;
}

bool TGpioCounter::IsChanged() const
{
    return Total.IsChanged() || Current.IsChanged();
}

void TGpioCounter::ResetIsChanged()
{
    Total.ResetChanged();
    Current.ResetChanged();
}

vector<TGpioCounter::TMetadataPair> TGpioCounter::GetIdsAndTypes(const string & baseId) const
{
    return {
        { baseId + ID_POSTFIX_TOTAL,   TotalType },
        { baseId + ID_POSTFIX_CURRENT, CurrentType }
    };
}

vector<TGpioCounter::TValuePair> TGpioCounter::GetIdsAndValues(const string & baseId) const
{
    vector<TGpioCounter::TValuePair> idsAndValues;

    if (Total.IsChanged())
        idsAndValues.push_back({ baseId + ID_POSTFIX_TOTAL, SetDecimalPlaces(Total.Get(), DecimalPlacesTotal) });
    if (Current.IsChanged())
        idsAndValues.push_back({ baseId + ID_POSTFIX_CURRENT, SetDecimalPlaces(Current.Get(), DecimalPlacesCurrent) });

    return idsAndValues;
}

void TGpioCounter::SetInterruptEdge(EGpioEdge edge)
{
    InterruptEdge = edge;
}

EGpioEdge TGpioCounter::GetInterruptEdge() const
{
    return InterruptEdge;
}

void TGpioCounter::SetInitialValues(float total)
{
    InitialTotal = total;
}

void TGpioCounter::UpdateCurrent(const TTimeIntervalUs & interval)
{
    Current.Set(3600.0 * 1000000 * ConvertingMultiplier / (interval.count() * Multiplier)); // convert microseconds to seconds, hours to seconds
}

void TGpioCounter::UpdateTotal()
{
    Total.Set((float) Counts / Multiplier + InitialTotal);
}
