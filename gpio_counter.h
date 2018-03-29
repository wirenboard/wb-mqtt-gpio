#pragma once

#include "declarations.h"
#include "types.h"

#include <vector>

class TGpioCounter
{
    using TMetadataPair = std::pair<std::string, std::string>;
    using TValuePair    = std::pair<std::string, std::string>;

    float           Multiplier,
                    ConvertingMultiplier,   // multiplier that converts value to appropriate measuring unit according to meter type
                    InitialTotal;

    TValue<float>   Total,
                    Current;

    const char  *   TotalType,
                *   CurrentType;

    int             DecimalPlacesTotal,
                    DecimalPlacesCurrent;

    uint64_t        Counts;
    EGpioEdge       InterruptEdge;
    TTimeIntervalUs PreviousInterval;

public:
    explicit TGpioCounter(const TGpioLineConfig & config);
    ~TGpioCounter();

    /* if occured interrupt (hardware or simulated) */
    void HandleInterrupt(EGpioEdge, const TTimeIntervalUs & interval);
    void Update(const TTimeIntervalUs &);

    float GetCurrent() const;
    float GetTotal() const;
    uint64_t GetCounts() const;
    bool IsChanged() const;
    void ResetIsChanged();
    std::vector<TMetadataPair> GetIdsAndTypes(const std::string & baseId) const;
    std::vector<TValuePair>    GetIdsAndValues(const std::string & baseId) const;

    void SetInterruptEdge(EGpioEdge);
    EGpioEdge GetInterruptEdge() const;

    void SetInitialValues(float total); // set total when starting

private:
    void UpdateCurrent(const TTimeIntervalUs &);
    void UpdateTotal();
};
