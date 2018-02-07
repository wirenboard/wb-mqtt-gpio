#pragma once

#include <memory>

class TConfig;
class TGpioChip;
class TGpioLine;

using PConfig = std::shared_ptr<TConfig>;
using PGpioChip = std::shared_ptr<TGpioChip>;
using PWGpioChip = std::weak_ptr<TGpioChip>;
using PGpioLine = std::shared_ptr<TGpioLine>;
