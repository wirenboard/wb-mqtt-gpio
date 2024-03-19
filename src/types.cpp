#include "types.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[types] "

using namespace std;

void EnumerateGpioEdge(const std::string& edge, EGpioEdge& enumEdge)
{
    if (edge == "rising")
        enumEdge = EGpioEdge::RISING;
    else if (edge == "falling")
        enumEdge = EGpioEdge::FALLING;
    else if (edge == "both")
        enumEdge = EGpioEdge::BOTH;
    else if (!edge.empty())
        LOG(Warn) << "Unable to determine edge from '" << edge
                  << "': needs to be either 'rising', 'falling' or 'both'. Using: '" << GpioEdgeToString(enumEdge)
                  << "'";
}

string GpioEdgeToString(EGpioEdge edge)
{
    switch (edge) {
        case EGpioEdge::RISING:
            return "rising";
        case EGpioEdge::FALLING:
            return "falling";
        case EGpioEdge::BOTH:
            return "both";
        case EGpioEdge::AUTO:
            return "auto"; // for counter
        default:
            return "<unknown (" + to_string((int)edge) + ")>";
    }
}
