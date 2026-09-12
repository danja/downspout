#include "magneto_engine.hpp"

#include "magneto_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace downspout::magneto {
namespace {

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum) noexcept
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] const ParamSpec& spec(const ParamId id) noexcept
{
    return kParameterSpecs[static_cast<std::size_t>(id)];
}

// Clamp to the declared range, snapping integer parameters to whole values.
[[nodiscard]] float clampToSpec(const ParamId id, const float value) noexcept
{
    const ParamSpec& s = spec(id);
    if (!std::isfinite(value))
        return s.defaultValue;
    const float clamped = clampf(value, s.minimum, s.maximum);
    return s.integer ? std::round(clamped) : clamped;
}

[[nodiscard]] int intParam(const float value) noexcept
{
    return static_cast<int>(std::lround(value));
}

// Equal-power pan: -1 is hard left, +1 hard right.
void panGains(const float position, float& leftGain, float& rightGain) noexcept
{
    const float theta = (clampf(position, -1.0f, 1.0f) + 1.0f) * (kPi * 0.25f);
    leftGain = std::cos(theta);
    rightGain = std::sin(theta);
}

void resetEngineFilters(EngineState& state) noexcept
{
    state.noiseLp.reset();
    state.blockLp1.reset();
    state.blockLp2.reset();
    state.dcIntake.reset();
    state.dcBlock.reset();
    state.dcExhaust.reset();
}

void clearAcoustics(EngineState& state) noexcept
{
    for (auto& cylinder : state.cylinders)
        cylinder.reset();
    state.exhaust.reset();
    resetEngineFilters(state);
}

// Recompute everything that does not need to move at audio rate.
void controlTick(EngineState& st, const Parameters& p, const TransportSnapshot& transport) noexcept
{
    const auto sampleRate = static_cast<float>(st.sampleRate);

    if (p.seed != st.appliedSeed)
    {
        st.appliedSeed = p.seed;
        const auto seed = static_cast<std::uint32_t>(std::lround(p.seed));
        // Two independent streams so changing the turbulence level cannot
        // perturb backfire timing.
        st.rngNoise.seed(seed * 2654435761u + 1u);
        st.rngBackfire.seed(seed * 40503u + 0x9E3779B9u);
    }

    st.activeCylinders = std::clamp(intParam(p.cylinders), 1, kMaxCylinders);
    st.invActive = 1.0f / static_cast<float>(st.activeCylinders);
    st.invSqrtActive = 1.0f / std::sqrt(static_cast<float>(st.activeCylinders));
    cylinderPhaseOffsets(st.activeCylinders, p.asymmetry, st.phaseOffset);

    // Engine speed: target, then flywheel slew.
    const float target = resolveTargetRpm(p, transport);
    const float tauSeconds = std::max(p.inertia, 1.0f) * 0.001f;
    const float inertiaCoefficient =
        1.0f - std::exp(-static_cast<float>(kControlUpdatePeriod) / (tauSeconds * sampleRate));
    st.rpmPrevious = st.rpm;
    st.rpm += inertiaCoefficient * (target - st.rpm);
    st.decelerating = st.rpm < st.rpmPrevious - 1.0e-3f;
    st.phaseIncrement = cycleFrequencyHz(st.rpm) / sampleRate;

    st.throttle = clampf(p.throttle, 0.0f, 1.0f);
    st.blockDrive = 0.35f + 0.65f * st.throttle;
    st.ignitionAmp = kIgnitionBaseAmp
                   * (kIgnitionIdleFloor + (1.0f - kIgnitionIdleFloor) * st.throttle)
                   * st.invSqrtActive;

    // Geometry, smoothed so host automation cannot zipper the delay lengths.
    st.geometryCoefficient =
        1.0f - std::exp(-static_cast<float>(kControlUpdatePeriod) / (kGeometrySmoothingMs * 0.001f * sampleRate));
    const float gc = st.geometryCoefficient;
    st.invCompression = 1.0f / std::max(p.compression, 1.0f);

    st.chamberDelay.process(tubeDelaySamples(chamberLengthMetres(p.displacement), sampleRate), gc);
    st.intakeDelay.process(tubeDelaySamples(p.intakeLen, sampleRate), gc);
    st.extractorDelay.process(tubeDelaySamples(p.extractorLen, sampleRate), gc);
    st.pipeDelay.process(tubeDelaySamples(p.pipeLen, sampleRate), gc);
    st.mufflerDelay.process(tubeDelaySamples(p.mufflerLen, sampleRate), gc);
    st.outletDelay.process(tubeDelaySamples(p.outletLen, sampleRate), gc);

    for (int m = 0; m < kMufflerElements; ++m)
    {
        st.mufflerTargets[static_cast<std::size_t>(m)] =
            nearestPrimeDelay(st.mufflerDelay.value * kMufflerRatio[static_cast<std::size_t>(m)]);
    }

    st.damping = onePoleCoefficient(6000.0f, sampleRate);
    st.noiseCoefficient = onePoleCoefficient(800.0f + 3200.0f * st.throttle, sampleRate);
    st.blockCoefficient = onePoleCoefficient(240.0f, sampleRate);
    st.lampDecay = std::exp(-1.0f / (0.15f * sampleRate));

    for (int k = 0; k < st.activeCylinders; ++k)
    {
        CylinderState& cylinder = st.cylinders[static_cast<std::size_t>(k)];
        const float spreadK = kRunnerSpread[static_cast<std::size_t>(k)];

        cylinder.chamber.gain = lossGainForRoundTrip(kChamberRoundTrip, st.chamberDelay.value);
        cylinder.chamber.damping = st.damping;
        cylinder.intake.gain = lossGainForRoundTrip(kRunnerRoundTrip, st.intakeDelay.value * spreadK);
        cylinder.intake.damping = st.damping;
        cylinder.extractor.gain = lossGainForRoundTrip(kRunnerRoundTrip, st.extractorDelay.value * spreadK);
        cylinder.extractor.damping = st.damping;
    }

    st.exhaust.pipe.gain = lossGainForRoundTrip(kPipeRoundTrip, st.pipeDelay.value);
    st.exhaust.pipe.damping = st.damping;
    for (int m = 0; m < kMufflerElements; ++m)
    {
        Waveguide<kMufflerLineLen>& element = st.exhaust.muffler[static_cast<std::size_t>(m)];
        element.gain = lossGainForRoundTrip(kMufflerRoundTrip, st.mufflerTargets[static_cast<std::size_t>(m)]);
        element.damping = st.damping;
    }
    st.exhaust.outlet.gain = lossGainForRoundTrip(kOutletRoundTrip, st.outletDelay.value);
    st.exhaust.outlet.damping = st.damping;

    st.mufflerBeta = -clampf(p.mufflerAction, 0.0f, kMaxReflection);

    // Output mix.
    st.intakeGain = clampf(p.intakeGain, 0.0f, 1.0f);
    st.blockGain = clampf(p.blockGain, 0.0f, 1.0f);
    st.exhaustGain = clampf(p.outletGain, 0.0f, 1.0f);
    st.levelGain = clampf(p.level, 0.0f, 1.0f);
    st.widthAmount = clampf(p.width, 0.0f, 1.0f);
    st.mix = kListenTable[static_cast<std::size_t>(std::clamp(intParam(p.listen), 0, 3))];

    // Fold the position weights and the per-tap makeup into the pan gains so
    // the audio loop is three multiply-adds per channel.
    const float width = st.mix.width * st.widthAmount;
    panGains(st.mix.pIntake * width, st.panIntakeL, st.panIntakeR);
    panGains(st.mix.pBlock * width, st.panBlockL, st.panBlockR);
    panGains(st.mix.pExhaust * width, st.panExhaustL, st.panExhaustR);

    const float intakeWeight = st.mix.wIntake * kIntakeMakeup;
    const float blockWeight = st.mix.wBlock * kBlockMakeup;
    const float exhaustWeight = st.mix.wExhaust * kExhaustMakeup;
    st.panIntakeL *= intakeWeight;
    st.panIntakeR *= intakeWeight;
    st.panBlockL *= blockWeight;
    st.panBlockR *= blockWeight;
    st.panExhaustL *= exhaustWeight;
    st.panExhaustR *= exhaustWeight;
}

}  // namespace

float resolveTargetRpm(const Parameters& params, const TransportSnapshot& transport) noexcept
{
    const ParamSpec& rpmSpec = spec(ParamId::rpm);

    float target = params.rpm;
    if (intParam(params.rpmSource) == 1)
    {
        if (transport.valid && transport.playing && transport.bpm > 1.0)
        {
            const auto index = static_cast<std::size_t>(
                std::clamp(intParam(params.syncRatio), 0, static_cast<int>(kSyncRatioValues.size()) - 1));
            // One engine cycle is two crank revolutions, so RPM = 120 * f and
            // f = ratio * bpm / 60.
            target = static_cast<float>(2.0 * transport.bpm * static_cast<double>(kSyncRatioValues[index]));
        }
        else
        {
            target = params.idleRpm;
        }
    }
    return clampf(target, rpmSpec.minimum, rpmSpec.maximum);
}

Parameters clampParameters(const Parameters& raw)
{
    Parameters p = raw;

    p.cylinders = clampToSpec(ParamId::cylinders, p.cylinders);
    p.displacement = clampToSpec(ParamId::displacement, p.displacement);
    p.compression = clampToSpec(ParamId::compression, p.compression);
    p.ignition = clampToSpec(ParamId::ignition, p.ignition);
    p.asymmetry = clampToSpec(ParamId::asymmetry, p.asymmetry);
    p.blockGain = clampToSpec(ParamId::blockGain, p.blockGain);

    p.intakeLen = clampToSpec(ParamId::intakeLen, p.intakeLen);
    p.intakeGain = clampToSpec(ParamId::intakeGain, p.intakeGain);
    p.turbulence = clampToSpec(ParamId::turbulence, p.turbulence);

    p.extractorLen = clampToSpec(ParamId::extractorLen, p.extractorLen);
    p.pipeLen = clampToSpec(ParamId::pipeLen, p.pipeLen);
    p.mufflerLen = clampToSpec(ParamId::mufflerLen, p.mufflerLen);
    p.mufflerAction = clampToSpec(ParamId::mufflerAction, p.mufflerAction);
    p.outletLen = clampToSpec(ParamId::outletLen, p.outletLen);
    p.outletGain = clampToSpec(ParamId::outletGain, p.outletGain);
    p.backfire = clampToSpec(ParamId::backfire, p.backfire);

    p.rpm = clampToSpec(ParamId::rpm, p.rpm);
    p.throttle = clampToSpec(ParamId::throttle, p.throttle);
    p.rpmSource = clampToSpec(ParamId::rpmSource, p.rpmSource);
    p.syncRatio = clampToSpec(ParamId::syncRatio, p.syncRatio);
    p.idleRpm = clampToSpec(ParamId::idleRpm, p.idleRpm);
    p.inertia = clampToSpec(ParamId::inertia, p.inertia);
    p.seed = clampToSpec(ParamId::seed, p.seed);
    p.midiCh = clampToSpec(ParamId::midiCh, p.midiCh);

    p.listen = clampToSpec(ParamId::listen, p.listen);
    p.width = clampToSpec(ParamId::width, p.width);
    p.level = clampToSpec(ParamId::level, p.level);

    return p;
}

void activate(EngineState& state, const double sampleRate)
{
    state.sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    const auto sr = static_cast<float>(state.sampleRate);

    clearAcoustics(state);

    state.phasor.reset();
    state.phaseOffset.fill(0.0f);
    cylinderPhaseOffsets(state.activeCylinders, 0.0f, state.phaseOffset);

    state.rngNoise.seed(2654435761u + 1u);
    state.rngBackfire.seed(40503u + 0x9E3779B9u);
    state.appliedSeed = 0.0f;

    const Parameters defaults {};
    state.rpm = defaults.idleRpm;
    state.rpmPrevious = state.rpm;
    state.phaseIncrement = cycleFrequencyHz(state.rpm) / sr;
    state.decelerating = false;
    state.throttle = 0.0f;
    state.ignitionAmp = 0.0f;

    // Seed the geometry smoothers at their default targets so the first block
    // does not have to ramp the whole exhaust system into place.
    state.chamberDelay.reset(tubeDelaySamples(chamberLengthMetres(defaults.displacement), sr));
    state.intakeDelay.reset(tubeDelaySamples(defaults.intakeLen, sr));
    state.extractorDelay.reset(tubeDelaySamples(defaults.extractorLen, sr));
    state.pipeDelay.reset(tubeDelaySamples(defaults.pipeLen, sr));
    state.mufflerDelay.reset(tubeDelaySamples(defaults.mufflerLen, sr));
    state.outletDelay.reset(tubeDelaySamples(defaults.outletLen, sr));

    for (int m = 0; m < kMufflerElements; ++m)
    {
        state.mufflerTargets[static_cast<std::size_t>(m)] =
            nearestPrimeDelay(state.mufflerDelay.value * kMufflerRatio[static_cast<std::size_t>(m)]);
    }

    for (auto& cylinder : state.cylinders)
    {
        cylinder.chamber.delay = state.chamberDelay.value;
        cylinder.intake.delay = state.intakeDelay.value;
        cylinder.extractor.delay = state.extractorDelay.value;
    }
    state.exhaust.pipe.delay = state.pipeDelay.value;
    for (int m = 0; m < kMufflerElements; ++m)
        state.exhaust.muffler[static_cast<std::size_t>(m)].delay = state.mufflerTargets[static_cast<std::size_t>(m)];
    state.exhaust.outlet.delay = state.outletDelay.value;

    state.backfireProbability = 0.0f;
    state.backfireActive = false;
    state.backfirePhase = 0.0f;
    state.backfireIncrement = 0.0f;
    state.backfireAmp = 0.0f;
    state.backfireLamp = 0.0f;

    state.cycleCount = 0;
    state.backfireCount = 0;
    state.backfireFirstFrame = 0;
    state.nonFinite = 0;
    state.controlCounter = 0;
}

float currentRpm(const EngineState& state) noexcept
{
    return state.rpm;
}

float backfireLamp(const EngineState& state) noexcept
{
    return state.backfireLamp;
}

void processBlock(EngineState& st,
                  const Parameters& params,
                  const TransportSnapshot& transport,
                  const std::uint32_t nframes,
                  const double sampleRate,
                  float* const outL,
                  float* const outR)
{
    if (outL == nullptr || outR == nullptr)
        return;

    if (sampleRate > 1000.0 && std::fabs(sampleRate - st.sampleRate) > 0.5)
        activate(st, sampleRate);

    const Parameters p = params;

    // Per-cylinder scratch, reused across the block.
    std::array<float, kMaxCylinders> valveIntake {};
    std::array<float, kMaxCylinders> valveExhaust {};
    std::array<float, kMaxCylinders> ignition {};
    std::array<float, kMufflerElements> mufflerNearIn {};
    std::array<float, kMufflerElements> mufflerFarIn {};

    for (std::uint32_t frame = 0; frame < nframes; ++frame)
    {
        if (--st.controlCounter <= 0)
        {
            st.controlCounter = kControlUpdatePeriod;
            controlTick(st, p, transport);
        }

        const int active = st.activeCylinders;
        const float phase = st.phasor.advance(st.phaseIncrement);
        const bool wrapped = st.phasor.wrapped;
        if (wrapped)
            ++st.cycleCount;

        // ── Read phase ─────────────────────────────────────────────────────
        float blockAccumulator = 0.0f;
        float intakeTap = 0.0f;
        float manifoldSum = 0.0f;

        for (int k = 0; k < active; ++k)
        {
            const auto index = static_cast<std::size_t>(k);
            CylinderState& cylinder = st.cylinders[index];

            const float x = wrapPhase(phase + st.phaseOffset[index]);
            valveIntake[index] = intakeValve(x);
            valveExhaust[index] = exhaustValve(x);
            ignition[index] = fuelIgnition(x, p.ignition);
            const float piston = pistonMotion(x);
            // Chassis vibration rises with combustion load. The paper sums bare
            // piston motion, which would make the block tap as loud at idle as
            // at full throttle; see docs/design.md.
            blockAccumulator += piston * st.blockDrive + ignition[index];

            const float spreadK = kRunnerSpread[index];
            cylinder.chamber.setDelayTarget(st.chamberDelay.value * chamberLengthScale(piston, p.compression));
            cylinder.intake.setDelayTarget(st.intakeDelay.value * spreadK);
            cylinder.extractor.setDelayTarget(st.extractorDelay.value * spreadK);

            cylinder.chamber.read();
            cylinder.intake.read();
            cylinder.extractor.read();

            intakeTap += cylinder.intake.radiatedFar(kIntakeFreeEnd);
            manifoldSum += cylinder.extractor.radiatedFar(kExtractorFreeEnd);
        }

        st.exhaust.pipe.setDelayTarget(st.pipeDelay.value);
        st.exhaust.pipe.read();
        for (int m = 0; m < kMufflerElements; ++m)
        {
            Waveguide<kMufflerLineLen>& element = st.exhaust.muffler[static_cast<std::size_t>(m)];
            element.setDelayTarget(st.mufflerTargets[static_cast<std::size_t>(m)]);
            element.read();
        }
        st.exhaust.outlet.setDelayTarget(st.outletDelay.value);
        st.exhaust.outlet.read();

        // ── Scatter phase ──────────────────────────────────────────────────
        const float pipeToManifold = st.exhaust.pipe.radiatedNear(kManifoldJunction);
        const float pipeToMuffler = st.exhaust.pipe.radiatedFar(kPipeJunction);
        const float outletToMuffler = st.exhaust.outlet.radiatedNear(kMufflerJunction);
        const float exhaustTap = st.exhaust.outlet.radiatedFar(kOutletFreeEnd);

        float mufflerToPipe = 0.0f;
        float mufflerToOutlet = 0.0f;
        for (int m = 0; m < kMufflerElements; ++m)
        {
            const auto index = static_cast<std::size_t>(m);
            const Waveguide<kMufflerLineLen>& element = st.exhaust.muffler[index];
            mufflerToPipe += element.radiatedNear(kMufflerJunction);
            mufflerToOutlet += element.radiatedFar(st.mufflerBeta);
            mufflerNearIn[index] = 0.5f * pipeToMuffler;
            mufflerFarIn[index] = 0.5f * outletToMuffler;
        }

        // ── Write phase ────────────────────────────────────────────────────
        const float returnToExtractors = st.invSqrtActive * pipeToManifold;
        const float noiseLevel = p.turbulence * (0.3f + 0.7f * st.throttle);

        for (int k = 0; k < active; ++k)
        {
            const auto index = static_cast<std::size_t>(k);
            CylinderState& cylinder = st.cylinders[index];

            const float openIntake = valveIntake[index];
            const float openExhaust = valveExhaust[index];
            const float openEither = std::min(1.0f, openIntake + openExhaust);

            const float headReflection = valveReflection(openEither);
            const float intakeReflection = valveReflection(openIntake);
            const float exhaustReflection = valveReflection(openExhaust);

            const float headRadiated = cylinder.chamber.radiatedFar(headReflection);
            const float share = openIntake + openExhaust + 1.0e-6f;
            const float toIntake = headRadiated * (openIntake / share);
            const float toExhaust = headRadiated * (openExhaust / share);

            const float fromIntake = cylinder.intake.radiatedNear(intakeReflection);
            const float fromExhaust = cylinder.extractor.radiatedNear(exhaustReflection);

            // Chamber: near end is the piston crown where combustion pushes,
            // far end is the head where both valves sit.
            cylinder.chamber.write(st.ignitionAmp * ignition[index],
                                   fromIntake + fromExhaust,
                                   kPistonEndReflection,
                                   headReflection);

            // Intake runner: near end at the valve, far end open to air. The
            // secondary noise source models aspiration turbulence.
            const float turbulence =
                st.noiseLp.process(st.rngNoise.bipolar(), st.noiseCoefficient) * openIntake * noiseLevel;
            cylinder.intake.write(toIntake + turbulence, 0.0f, intakeReflection, kIntakeFreeEnd);

            // Extractor: near end at the valve, far end into the manifold.
            cylinder.extractor.write(toExhaust, returnToExtractors, exhaustReflection, kExtractorFreeEnd);
        }

        st.exhaust.pipe.write(st.invSqrtActive * manifoldSum,
                              0.5f * mufflerToPipe,
                              kManifoldJunction,
                              kPipeJunction);

        // ── Backfire ───────────────────────────────────────────────────────
        if (wrapped)
        {
            if (!st.decelerating)
            {
                st.backfireProbability = clampf(p.backfire, 0.0f, 1.0f);
            }
            else if (p.backfire > 0.0f)
            {
                if (st.backfireProbability > st.rngBackfire.uniform())
                {
                    st.backfireActive = true;
                    st.backfirePhase = 0.0f;
                    const float width = 0.5f * p.ignition / std::max(st.phaseIncrement, 1.0e-9f);
                    st.backfireIncrement = 1.0f / std::max(width, 8.0f);
                    st.backfireAmp = kBackfireGain * (0.5f + 0.5f * clampf(p.backfire, 0.0f, 1.0f));
                    st.backfireLamp = 1.0f;
                    if (st.backfireCount == 0)
                        st.backfireFirstFrame = frame;
                    ++st.backfireCount;
                }
                st.backfireProbability *= st.backfireProbability;
            }
        }

        float backfireSignal = 0.0f;
        if (st.backfireActive)
        {
            backfireSignal = sinTurns(0.5f * st.backfirePhase) * st.backfireAmp;
            st.backfirePhase += st.backfireIncrement;
            if (st.backfirePhase >= 1.0f)
            {
                st.backfireActive = false;
                st.backfirePhase = 0.0f;
            }
        }
        st.backfireLamp *= st.lampDecay;

        for (int m = 0; m < kMufflerElements; ++m)
        {
            const auto index = static_cast<std::size_t>(m);
            st.exhaust.muffler[index].write(mufflerNearIn[index],
                                            mufflerFarIn[index],
                                            kMufflerJunction,
                                            st.mufflerBeta);
        }

        st.exhaust.outlet.write(0.5f * mufflerToOutlet + backfireSignal,
                                0.0f,
                                kMufflerJunction,
                                kOutletFreeEnd);

        // ── Output taps ────────────────────────────────────────────────────
        const float intakeSignal =
            softClip(st.dcIntake.process(intakeTap * st.invSqrtActive)) * st.intakeGain;

        const float blockFiltered =
            st.blockLp2.process(st.blockLp1.process(blockAccumulator * st.invSqrtActive, st.blockCoefficient),
                                st.blockCoefficient);
        const float blockSignal = softClip(st.dcBlock.process(blockFiltered)) * st.blockGain;

        const float exhaustSignal = softClip(st.dcExhaust.process(exhaustTap)) * st.exhaustGain;

        float left = intakeSignal * st.panIntakeL + blockSignal * st.panBlockL + exhaustSignal * st.panExhaustL;
        float right = intakeSignal * st.panIntakeR + blockSignal * st.panBlockR + exhaustSignal * st.panExhaustR;

        left = softClip(left) * st.levelGain;
        right = softClip(right) * st.levelGain;

        if (!std::isfinite(left))
        {
            left = 0.0f;
            ++st.nonFinite;
        }
        if (!std::isfinite(right))
        {
            right = 0.0f;
            ++st.nonFinite;
        }

        outL[frame] = left;
        outR[frame] = right;
    }

    // A network this large should never diverge, but if a host hands us a
    // pathological sample rate or a denormal storm gets through, clear rather
    // than keep feeding garbage to the output.
    if (st.nonFinite > kNonFinitePanicCount)
    {
        clearAcoustics(st);
        st.nonFinite = 0;
    }
}

}  // namespace downspout::magneto
