#include "energise_quantise.hpp"

#include "energise_rng.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::energise {

void quantiseEvents(std::vector<DrumEvent>& events,
                    const GrooveMap& groove,
                    const RenderSettings& settings,
                    const std::uint32_t seed,
                    const double captureSeconds)
{
    const double step = gridStepSeconds(groove);
    const double swingShift = settings.swing * step * 0.5;
    Rng rng {};
    rng.seed(hashSeed(seed, 0x5A17u));

    for (DrumEvent& event : events) {
        const double index = std::round((event.sourceTime - groove.phaseSec) / step);
        double target = groove.phaseSec + index * step;
        if (settings.swing > 0.0f && static_cast<long long>(index) % 2 != 0)
            target += swingShift;
        const auto cls = static_cast<std::size_t>(event.primary);
        target += static_cast<double>(settings.classOffsetsMs[cls]) * 0.001;

        const double strength = event.ghost ? settings.ghostStrength : settings.quantiseStrength;
        double quantised = event.sourceTime + strength * (target - event.sourceTime);
        if (settings.humanise > 0.0f)
            quantised += static_cast<double>(rng.nextSigned()) * settings.humanise * step * 0.25;
        event.quantisedTime = std::clamp(quantised, 0.0, std::max(0.0, captureSeconds));
    }
}

}  // namespace downspout::energise
