#pragma once

#include <memory>
#include <stdint.h>

class TGpioChip;
class TGpioLine;

using PGpioChip = std::shared_ptr<TGpioChip>;
using PWGpioChip = std::weak_ptr<TGpioChip>;
using PGpioLine = std::shared_ptr<TGpioLine>;
