#pragma once

#include <memory>

class TGpioChip;
class TGpioLine;

using PGpioChipDriver = std::shared_ptr<TGpioChip>;
using PWGpioChipDriver = std::weak_ptr<TGpioChip>;
using PGpioChipLine = std::shared_ptr<TGpioLine>;
