#pragma once

#include <memory>
#include <stdint.h>

class TGpioLineConfig;
class TGpioChip;
class TGpioLine;
class TGpioCounter;

using PGpioChip     = std::shared_ptr<TGpioChip>;
using PWGpioChip    = std::weak_ptr<TGpioChip>;
using PGpioLine     = std::shared_ptr<TGpioLine>;
using PUGpioCounter = std::unique_ptr<TGpioCounter>;
