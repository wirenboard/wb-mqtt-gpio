#pragma once

#include <memory>
#include <chrono>
#include <stdint.h>


struct TGpioDriverConfig;
struct TGpioLineConfig;

class TGpioChip;
class TGpioLine;
class TGpioCounter;

using TTimePoint        = std::chrono::steady_clock::time_point;
using TTimeIntervalUs   = std::chrono::microseconds;

using PGpioChip         = std::shared_ptr<TGpioChip>;
using PWGpioChip        = std::weak_ptr<TGpioChip>;
using PGpioLine         = std::shared_ptr<TGpioLine>;
using PUGpioCounter     = std::unique_ptr<TGpioCounter>;
using PUGpioLineConfig  = std::unique_ptr<TGpioLineConfig>;
