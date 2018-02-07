#include "gpio_driver.h"
#include "gpio_chip.h"
#include "config.h"
#include "utils.h"

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const string & configFileName)
{
    PConfig config = nullptr

    try {
        config = make_shared<TGpioDriverConfig>(configFileName);
    } catch (const exception & e) {
        LOG(Warn) << "unable to read config in new format: '" << e.what() << "'";
    }

    if (!config) {
        LOG(Info) << "trying to read config in deprecated format...";
        try {
            config = make_shared<THandlerConfig>(configFileName);
        } catch (const exception & e) {
            LOG(Warn) << "unable to read config in deprecated format: '" << e.what() << "'";
        }
    }

    if (!config) {
        wb_throw(TGpioDriverException, "unable to read config");
    }

    if (config->IsOldFormat()) {

    }
}

PConfig TGpioDriver::ToNewFormat(const PConfig & config)
{
    auto oldConfig = dynamic_pointer_cast<THandlerConfig>(config);
    auto newConfig = make_shared<TGpioDriverConfig>();

    assert(oldConfig);

    struct TRange
    {
        PGpioChip Chip;
        uint32_t  Begin, End;

        TRange(uint32_t begin, uint32_t end)
            : Chip(nullptr)
            , Begin(begin)
            , End(end)
        {}

        TRange(const PGpioChip & chip, uint32_t positionOffset)
            : Chip(chip)
            , Begin(positionOffset)
            , End(positionOffset + chip->GetLineCount())
        {}

        bool InRange(uint32_t gpio) const
        {
            return Begin <= gpio && gpio < End;
        }
    };

    struct TRangeCompare
    {
        bool operator(const TRange & a, const TRange & b) const {
            return a.End <= b.Begin;
        }
    };

    set<TRange, TRangeCompare> ranges;

    {   // init all available chips
        const auto & paths = EnumerateGpioChipsSorted();
        vector<PGpioChip> availableChips;
        availableChips.reserve(paths.size());

        for (const auto & path: paths) {
            const auto & chip = make_shared<TGpioChip>(path);

            uint32_t positionOffset = 0;

            if (!ranges.empty()) {
                positionOffset = (--ranges.end())->End;
            }

            ranges.insert(TRange(chip, positionOffset));

            availableChips.push_back(chip);
        }

        map<string, size_t> addedChips;

        for (const auto desc: oldConfig->Gpios) {
            TRange searchRange {desc.Gpio, desc.Gpio};

            auto itRange = ranges.find(searchRange);

            if (itRange == ranges.end()) {
                wb_throw(TGpioDriverException, "GPIO index is out of range");
            }

            const auto & range = *itRange;

            const auto & insRes = addedChips.insert(range->Chip->GetPath(), 0);
            if (insRes.second) {
                TGpioChipConfig chipConfig;

                chipConfig.Path = insRes.first->first;

                insRes.first->second = newConfig->Chips.size();
                newConfig->Chips.push_back(move(chipConfig));

                // TODO: continue
            }
        }
    }


}
