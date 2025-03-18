#include "gpio_counter.h"
#include "config.h"
#include "exceptions.h"
#include "log.h"
#include "utils.h"

#include <wblib/utils.h>

#include <cassert>
#include <cmath>

#define LOG(logger) ::logger.Log() << "[gpio counter] "

using namespace std;

const auto WATT_METER = "watt_meter";
const auto WATER_METER = "water_meter";
const auto ID_POSTFIX_TOTAL = "_total";
const auto ID_POSTFIX_CURRENT = "_current";
const auto CURRENT_TIME_INTERVAL = 1;
const auto NULL_TIME_INTERVAL = 100;
const auto COUNTER_UPDATE_INTERVAL_US = 200000;

TGpioCounter::TGpioCounter(const TGpioLineConfig& config)
    : Multiplier(config.Multiplier),
      InitialTotal(0),
      Total(0),
      Current(0),
      DecimalPlacesTotal(config.DecimalPlacesTotal),
      DecimalPlacesCurrent(config.DecimalPlacesCurrent),
      Counts(0),
      InterruptEdge(config.InterruptEdge),
      PreviousInterval(TTimeIntervalUs::zero())
{
    if (config.Type == WATT_METER) {
        TotalType = "power_consumption";
        CurrentType = "power";
        ConvertingMultiplier = 1000; // convert kW to W (1/h)
    } else if (config.Type == WATER_METER) {
        TotalType = "water_consumption";
        CurrentType = "water_flow";
        ConvertingMultiplier = 1.0; // 1/h
    } else {
        LOG(Error) << "Unknown gpio type";
        wb_throw(TGpioDriverException, "unknown GPIO type: '" + config.Type + "'");
    }

    LOG(Debug) << "New counter of type '" << config.Type << "' for " << config.Name
               << " edge: " << GpioEdgeToString(config.InterruptEdge);
}

TGpioCounter::~TGpioCounter()
{}

void TGpioCounter::HandleInterrupt(EGpioEdge edge, const TTimeIntervalUs& interval)
{
    assert(edge == InterruptEdge);

    PreviousInterval = interval;

    if (interval == TTimeIntervalUs::zero()) {
        Current.Set(-1);
    } else {
        UpdateCurrent(interval);
    }

    std::unique_lock<std::mutex> lk(AccessMutex);
    ++Counts;
    UpdateTotal();
}

void TGpioCounter::Update(const TTimeIntervalUs& interval)
{
    if (interval > NULL_TIME_INTERVAL * PreviousInterval) {
        Current.Set(0);
    } else if (interval > CURRENT_TIME_INTERVAL * PreviousInterval) {
        // more info at https://wirenboard.com/wiki/Frequency_registers
        auto divider = std::pow(2, (interval.count() / COUNTER_UPDATE_INTERVAL_US));
        Current.Set(Current.Get() / divider);
    }
}

float TGpioCounter::GetCurrent() const
{
    return Current.Get();
}

float TGpioCounter::GetTotal() const
{
    std::unique_lock<std::mutex> lk(AccessMutex);
    return Total.Get();
}

uint64_t TGpioCounter::GetCounts() const
{
    std::unique_lock<std::mutex> lk(AccessMutex);
    return Counts;
}

vector<TGpioCounter::TMetadataPair> TGpioCounter::GetIdsAndTypes(const string& baseId) const
{
    return {{baseId + ID_POSTFIX_TOTAL, TotalType}, {baseId + ID_POSTFIX_CURRENT, CurrentType}};
}

vector<TGpioCounter::TValuePair> TGpioCounter::GetIdsAndValues(const string& baseId) const
{
    vector<TGpioCounter::TValuePair> idsAndValues;

    idsAndValues.push_back({baseId + ID_POSTFIX_TOTAL, GetRoundedTotal()});
    idsAndValues.push_back({baseId + ID_POSTFIX_CURRENT, Utils::SetDecimalPlaces(Current.Get(), DecimalPlacesCurrent)});

    return idsAndValues;
}

std::string TGpioCounter::GetRoundedTotal() const
{
    return Utils::SetDecimalPlaces(GetTotal(), DecimalPlacesTotal);
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
    std::unique_lock<std::mutex> lk(AccessMutex);
    InitialTotal = total;
    Counts = 0;
    Total.Set(total);
}

void TGpioCounter::UpdateCurrent(const TTimeIntervalUs& interval)
{
    Current.Set(3600.0 * 1000000 * ConvertingMultiplier /
                (interval.count() * Multiplier)); // convert microseconds to seconds, hours to seconds
}

void TGpioCounter::UpdateTotal()
{
    Total.Set((float)Counts / Multiplier + InitialTotal);
}
