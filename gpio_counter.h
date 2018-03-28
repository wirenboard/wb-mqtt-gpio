#pragma once

#include "declarations.h"
#include "types.h"

#include <vector>

class TGpioCounter
{
    using TMetadataPair = std::pair<std::string, std::string>;
    using TValuePair    = std::pair<std::string, std::string>;

    float           Multiplier,
                    ConvertingMultiplier;// multiplier that converts value to appropriate measuring unit according to meter type
    float           Total,
                    Current,
                    InitialTotal;
    const char  *   IdPostfixTotal,
                *   IdPostfixCurrent,
                *   Type1,
                *   Type2;
    int             DecimalPlacesTotal,
                    DecimalPlacesCurrent;
    bool            PrintedNULL;
    bool            Changed;

    uint64_t        Counts;
    EGpioEdge       InterruptEdge;

public:
    explicit TGpioCounter(const TGpioLineConfig & config);
    ~TGpioCounter();

    void HandleInterrupt(EGpioEdge, TTimeIntervalUs interval);
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

    // vector<TMetadataPair> MetaType();
    // vector<TMetadataPair> GpioPublish();
    // int InterruptUp();                  // if user didn't specify interrupt edge, method would figure it out
    // TMetadataPair CheckTimeInterval();
};
