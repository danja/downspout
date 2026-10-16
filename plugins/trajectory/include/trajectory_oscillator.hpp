#pragma once

#include "trajectory_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::trajectory {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// A point bouncing inside a regular polygon of unit circumradius. The audio
// output is a mix of the point's x and y coordinates. Ported from
// experiments/trajectory/src/audio/modules/TrajectoryOscillator.js in the flues
// repository; the geometry, the reflection rule and the two-bounce step limit are
// the same. Fixed storage only, so it is safe to run on the audio thread.
class Oscillator {
public:
    Oscillator();

    void setSampleRate(double sampleRate) noexcept;
    void setFrequency(double hertz) noexcept;

    // Rebuild the polygon. A point left outside the new shape is pulled back in.
    void setSides(int sides) noexcept;

    // Redirect the moving point without moving it. The Flues original re-seats the
    // point on every change; here a live change (a CC from Drift, say) only turns
    // the velocity, so it does not click. A new note re-seats via start().
    void setLaunchAngle(double radians) noexcept;
    void setBounceJitter(double radians) noexcept;
    void seedNoise(std::uint32_t seed) noexcept;

    // Note-on: place the point on the boundary at startPosition (radians from the
    // centre) and launch it along launchAngle (radians).
    void start(double startPosition, double launchAngle) noexcept;

    // Advance one sample and return x * mixX + y * mixY.
    [[nodiscard]] float process(float mixX, float mixY) noexcept;

    [[nodiscard]] Vec2 position() const noexcept { return position_; }
    [[nodiscard]] int sides() const noexcept { return sides_; }
    [[nodiscard]] bool inside(const Vec2& point) const noexcept;

    // Largest step per sample. The reflection rule resolves at most two bounces a
    // sample, so a step much longer than the polygon would escape it.
    static constexpr double kMaxSpeed = 0.5;
    static constexpr double kSpeedPerHertz = 4.0;  // speed = frequency * 4 / sampleRate

private:
    struct Edge {
        Vec2 start;
        Vec2 end;
        Vec2 normal;  // outward, unit length
    };

    void rebuild() noexcept;
    void updateVelocity() noexcept;
    void reseat() noexcept;
    [[nodiscard]] bool rayIntersection(const Vec2& direction, Vec2& out) const noexcept;
    [[nodiscard]] double nextNoise() noexcept;

    double sampleRate_ = 48000.0;
    double frequency_ = 440.0;
    double speed_ = 0.0;
    int sides_ = 6;
    double launchAngle_ = 0.0;
    double jitter_ = 0.0;
    std::uint32_t noise_ = 0x9e3779b9u;
    std::array<Edge, static_cast<std::size_t>(kMaxSides)> edges_ {};
    Vec2 position_ {};
    Vec2 velocity_ {};
};

}  // namespace downspout::trajectory
