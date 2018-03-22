#pragma once

#include "declarations.h"

class TGpioCounter
{
    string Type;
    float Multiplier;
    float ConvertingMultiplier;// multiplier that converts value to appropriate measuring unit according to meter type
    float Total;
    float Power;
    string Topic1;
    string Topic2;
    string Value_Topic1;
    string Value_Topic2;
    int DecimalPlacesTotal;
    int DecimalPlacesCurrent;
    float InitialTotal;
    bool PrintedNULL;

    EGpioEdge InterruptEdge;

public:
    explicit TGpioCounter(const TGpioLineConfig & config);
    ~TGpioCounter();

    void HandleInterrupt(EGpioEdge);

    vector<TPublishPair> MetaType();
    vector<TPublishPair> GpioPublish();
    int InterruptUp();                  // if user didn't specify interrupt edge, method would figure it out
    void SetInitialValues(float total); // set total when starting
    TPublishPair CheckTimeInterval();

private:
    string SetDecimalPlaces(float value, int decimal_points);
};
