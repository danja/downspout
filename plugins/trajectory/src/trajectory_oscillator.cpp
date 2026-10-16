#include "trajectory_oscillator.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::trajectory {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kEpsilon = 1.0e-6;
constexpr double kNudge = 1.0e-4;

[[nodiscard]] double cross(const Vec2& a, const Vec2& b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] Vec2 normalized(const Vec2& v) noexcept
{
    const double magnitude = std::hypot(v.x, v.y);
    if (magnitude < kEpsilon)
        return {};
    return {v.x / magnitude, v.y / magnitude};
}

}  // namespace

Oscillator::Oscillator()
{
    rebuild();
    start(0.0, 0.0);
}

void Oscillator::setSampleRate(const double sampleRate) noexcept
{
    sampleRate_ = std::max(sampleRate, 1.0);
    setFrequency(frequency_);
}

void Oscillator::setFrequency(const double hertz) noexcept
{
    if (!std::isfinite(hertz) || hertz <= 0.0)
        return;

    const double previous = std::hypot(velocity_.x, velocity_.y);
    frequency_ = hertz;
    speed_ = std::min(hertz * kSpeedPerHertz / sampleRate_, kMaxSpeed);

    // Keep the current heading: the point may already have bounced away from the
    // launch direction, and a pitch change should not undo that.
    if (previous > kEpsilon * 1.0e-3)
    {
        const double scale = speed_ / previous;
        velocity_ = {velocity_.x * scale, velocity_.y * scale};
    }
    else
    {
        updateVelocity();
    }
}

void Oscillator::setSides(const int sides) noexcept
{
    const int clamped = std::min(std::max(sides, kMinSides), kMaxSides);
    if (clamped == sides_)
        return;
    sides_ = clamped;
    rebuild();
    if (!inside(position_))
        reseat();
}

void Oscillator::setLaunchAngle(const double radians) noexcept
{
    if (std::fabs(radians - launchAngle_) <= kEpsilon)
        return;
    launchAngle_ = radians;
    updateVelocity();
}

void Oscillator::setBounceJitter(const double radians) noexcept
{
    jitter_ = std::max(radians, 0.0);
}

void Oscillator::seedNoise(const std::uint32_t seed) noexcept
{
    noise_ = seed != 0u ? seed : 0x9e3779b9u;
}

void Oscillator::start(const double startPosition, const double launchAngle) noexcept
{
    launchAngle_ = launchAngle;
    Vec2 hit {};
    if (rayIntersection({std::cos(startPosition), std::sin(startPosition)}, hit))
        position_ = {hit.x * 0.995, hit.y * 0.995};
    else
        position_ = {};
    updateVelocity();
}

float Oscillator::process(const float mixX, const float mixY) noexcept
{
    Vec2 position = position_;
    Vec2 velocity = velocity_;

    for (int bounce = 0; bounce < 2; ++bounce)
    {
        const Vec2 next {position.x + velocity.x, position.y + velocity.y};
        if (inside(next))
        {
            position = next;
            break;
        }

        // The edge the step went furthest past.
        const Edge* worst = nullptr;
        double worstDistance = 0.0;
        for (int i = 0; i < sides_; ++i)
        {
            const Edge& edge = edges_[static_cast<std::size_t>(i)];
            const double distance = (next.x - edge.start.x) * edge.normal.x +
                                    (next.y - edge.start.y) * edge.normal.y;
            if (distance > worstDistance)
            {
                worstDistance = distance;
                worst = &edge;
            }
        }
        if (worst == nullptr)
        {
            position = next;
            break;
        }

        const double along = velocity.x * worst->normal.x + velocity.y * worst->normal.y;
        velocity = {velocity.x - 2.0 * along * worst->normal.x, velocity.y - 2.0 * along * worst->normal.y};

        if (jitter_ > 0.0)
        {
            const double angle = nextNoise() * jitter_;
            const double c = std::cos(angle);
            const double s = std::sin(angle);
            velocity = {velocity.x * c - velocity.y * s, velocity.x * s + velocity.y * c};
        }

        position = {next.x - worst->normal.x * (worstDistance + kNudge),
                    next.y - worst->normal.y * (worstDistance + kNudge)};
    }

    position_ = position;
    velocity_ = velocity;

    // A non-finite or runaway point can only come from a pathological parameter
    // sequence; put it back rather than let it poison the output.
    if (!std::isfinite(position_.x) || !std::isfinite(position_.y) || !inside(position_))
    {
        reseat();
        updateVelocity();
    }

    return static_cast<float>(position_.x * mixX + position_.y * mixY);
}

bool Oscillator::inside(const Vec2& point) const noexcept
{
    for (int i = 0; i < sides_; ++i)
    {
        const Edge& edge = edges_[static_cast<std::size_t>(i)];
        const Vec2 along {edge.end.x - edge.start.x, edge.end.y - edge.start.y};
        const Vec2 toPoint {point.x - edge.start.x, point.y - edge.start.y};
        if (cross(along, toPoint) < -kEpsilon)
            return false;
    }
    return true;
}

void Oscillator::rebuild() noexcept
{
    // Vertices sit on the unit circle, rotated by pi/N as in the Flues original. (Its
    // comment says this lays a side flat; it does for a square, while a hexagon ends
    // up with a vertex on top. The geometry is ported as is.)
    std::array<Vec2, static_cast<std::size_t>(kMaxSides)> vertices {};
    const double rotation = kTwoPi * 0.5 / static_cast<double>(sides_);
    for (int i = 0; i < sides_; ++i)
    {
        const double theta = kTwoPi * static_cast<double>(i) / static_cast<double>(sides_) + rotation;
        vertices[static_cast<std::size_t>(i)] = {std::cos(theta), std::sin(theta)};
    }

    for (int i = 0; i < sides_; ++i)
    {
        const Vec2& begin = vertices[static_cast<std::size_t>(i)];
        const Vec2& end = vertices[static_cast<std::size_t>((i + 1) % sides_)];
        const Vec2 along {end.x - begin.x, end.y - begin.y};
        edges_[static_cast<std::size_t>(i)] = {begin, end, normalized({along.y, -along.x})};
    }
}

void Oscillator::updateVelocity() noexcept
{
    velocity_ = {std::cos(launchAngle_) * speed_, std::sin(launchAngle_) * speed_};
}

void Oscillator::reseat() noexcept
{
    if (!std::isfinite(position_.x) || !std::isfinite(position_.y))
        position_ = {};
    for (int i = 0; i < 64 && !inside(position_); ++i)
        position_ = {position_.x * 0.9, position_.y * 0.9};
    if (!inside(position_))
        position_ = {};
}

bool Oscillator::rayIntersection(const Vec2& direction, Vec2& out) const noexcept
{
    bool found = false;
    double nearest = 0.0;

    for (int i = 0; i < sides_; ++i)
    {
        const Edge& edge = edges_[static_cast<std::size_t>(i)];
        const Vec2 segment {edge.end.x - edge.start.x, edge.end.y - edge.start.y};
        const double denominator = cross(direction, segment);
        if (std::fabs(denominator) < kEpsilon)
            continue;

        // The ray starts at the origin, so the offset to the edge start is the
        // edge start itself.
        const double t = cross(edge.start, segment) / denominator;
        const double u = cross(edge.start, direction) / denominator;
        if (t >= 0.0 && u >= 0.0 && u <= 1.0 && (!found || t < nearest))
        {
            nearest = t;
            out = {direction.x * t, direction.y * t};
            found = true;
        }
    }
    return found;
}

double Oscillator::nextNoise() noexcept
{
    noise_ ^= noise_ << 13u;
    noise_ ^= noise_ >> 17u;
    noise_ ^= noise_ << 5u;
    // Uniform in [-1, 1).
    return static_cast<double>(noise_) * (2.0 / 4294967296.0) - 1.0;
}

}  // namespace downspout::trajectory
