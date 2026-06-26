#pragma once

#include "basilico_transport.hpp"

namespace downspout::basilico {

struct WobbleConfig {
    bool sync = false;
    int division = 3;
    int shape = 0;
    float freeRateHz = 0.5f;
    float startDegrees = 0.0f;
};

struct WobbleFrame {
    float bipolar = 0.0f;
    float unipolar = 0.5f;
    bool syncActive = false;
};

class WobbleModulator {
public:
    void reset();
    void setTransport(const TransportSnapshot& transport);
    WobbleFrame process(const WobbleConfig& config, float sampleRate);

private:
    double beatPosition_ = 0.0;
    double beatsPerBar_ = 4.0;
    double bpm_ = 120.0;
    bool transportUsable_ = false;
    float freePhase_ = 0.0f;
    float smoothedBipolar_ = 0.0f;
};

float basilicoWobbleDivisionBeats(int division, double beatsPerBar);
float basilicoWobbleShapeValue(int shape, float phase);

} // namespace downspout::basilico
