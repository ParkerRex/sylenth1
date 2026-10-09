#include "FxChain.h"

#include <algorithm>
#include <cmath>

namespace synth
{
namespace
{
constexpr float twoPi = 6.28318530717958647692f;
constexpr float defaultTempoBpm = 128.0f;
constexpr float minTempoBpm = 20.0f;
constexpr float maxTempoBpm = 300.0f;
constexpr float maxTempoSyncedDelaySeconds = 18.0f;
constexpr float delayTailFloor = 0.001f;

float finiteOrZero(float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}

float mix(float dry, float wet, float amount) noexcept
{
    amount = clampUnit(finiteOrZero(amount));
    return dry + (wet - dry) * amount;
}

float saturate(float sample, float drive) noexcept
{
    const auto gain = 1.0f + clampUnit(drive) * 12.0f;
    const auto normalizer = softSaturate(gain);
    if (normalizer <= 0.0f)
        return sample;

    return softSaturate(sample * gain) / normalizer;
}

float clipDistort(float sample, float drive) noexcept
{
    const auto gain = 1.0f + clampUnit(drive) * 10.0f;
    return std::clamp(sample * gain, -1.0f, 1.0f);
}

float foldDistort(float sample, float drive) noexcept
{
    auto folded = std::clamp(sample, -1024.0f, 1024.0f) * (1.0f + clampUnit(drive) * 14.0f) + 1.0f;
    folded -= static_cast<float>(static_cast<int>(folded * 0.25f)) * 4.0f;
    if (folded < 0.0f)
        folded += 4.0f;
    else if (folded >= 4.0f)
        folded -= 4.0f;
    return 1.0f - std::abs(folded - 2.0f);
}

float dbToGain(float db) noexcept
{
    return decibelsToGain(std::clamp(db, -48.0f, 24.0f));
}

int secondsToSamples(double sampleRate, float seconds) noexcept
{
    return std::max(1, static_cast<int>(std::max(0.0f, seconds) * static_cast<float>(sampleRate) + 0.5f));
}
} // namespace

float delayDivisionBeats(DelaySyncDivision division) noexcept
{
    switch (division)
    {
        case DelaySyncDivision::Sixteenth: return 0.25f;
        case DelaySyncDivision::Eighth: return 0.5f;
        case DelaySyncDivision::DottedEighth: return 0.75f;
        case DelaySyncDivision::Quarter: return 1.0f;
        case DelaySyncDivision::Half: return 2.0f;
        case DelaySyncDivision::ThirtySecond: return 0.125f;
        case DelaySyncDivision::ThirtySecondTriplet: return 1.0f / 12.0f;
        case DelaySyncDivision::DottedThirtySecond: return 0.1875f;
        case DelaySyncDivision::SixteenthTriplet: return 1.0f / 6.0f;
        case DelaySyncDivision::DottedSixteenth: return 0.375f;
        case DelaySyncDivision::EighthTriplet: return 1.0f / 3.0f;
        case DelaySyncDivision::QuarterTriplet: return 2.0f / 3.0f;
        case DelaySyncDivision::DottedQuarter: return 1.5f;
        case DelaySyncDivision::HalfTriplet: return 4.0f / 3.0f;
        case DelaySyncDivision::DottedHalf: return 3.0f;
        case DelaySyncDivision::Whole: return 4.0f;
        case DelaySyncDivision::WholeTriplet: return 8.0f / 3.0f;
        case DelaySyncDivision::DottedWhole: return 6.0f;
    }

    return 0.5f;
}

int tempoSyncedDelaySamples(double sampleRate, float tempoBpm, DelaySyncDivision division) noexcept
{
    return secondsToSamples(sampleRate, tempoSyncedDelaySeconds(tempoBpm, division));
}

float tempoSyncedDelaySeconds(float tempoBpm, DelaySyncDivision division) noexcept
{
    const auto safeTempo = std::clamp(std::isfinite(tempoBpm) ? tempoBpm : defaultTempoBpm,
                                      minTempoBpm, maxTempoBpm);
    return (60.0f / safeTempo) * delayDivisionBeats(division);
}

float tempoSyncedEffectRateHz(float tempoBpm, DelaySyncDivision division) noexcept
{
    return 1.0f / tempoSyncedDelaySeconds(tempoBpm, division);
}

float fxTailLengthSeconds(const FxParameters& parameters) noexcept
{
    SynthParameters fullParameters;
    fullParameters.fx = parameters;
    return fxTailLengthSeconds(fullParameters);
}

float fxTailLengthSeconds(const SynthParameters& parameters) noexcept
{
    if (!parameters.fx.enabled)
        return 0.0f;
    const auto& fx = parameters.fx;
    auto delayTail = 0.0f;
    if (fx.delayEnabled && fx.delayMix > 0.0f)
    {
        const auto synced = parameters.sync && fx.delaySync;
        const auto left = synced ? tempoSyncedDelaySeconds(parameters.tempoBpm, fx.delaySyncDivision)
                                 : std::clamp(finiteOrZero(fx.delayTimeLeftMs), 1.0f, 6000.0f) * 0.001f;
        const auto right = synced ? tempoSyncedDelaySeconds(parameters.tempoBpm, fx.delayRightSyncDivision)
                                  : std::clamp(finiteOrZero(fx.delayTimeRightMs), 1.0f, 6000.0f) * 0.001f;
        const auto feedback = std::clamp(finiteOrZero(fx.delayFeedback), 0.0f, 0.95f);
        const auto repeats = feedback > 0.0f ? std::max(1.0f, std::ceil(std::log(delayTailFloor) / std::log(feedback))) : 1.0f;
        delayTail = (std::max(left, right) + (fx.delaySmear > 0.0f ? 0.22f : 0.0f)) * repeats;
    }
    auto reverbTail = 0.0f;
    if (fx.reverbEnabled && fx.reverbMix > 0.0f)
    {
        const auto size = clampUnit(finiteOrZero(fx.reverbDecay));
        const auto loop = 0.0477f * (0.5f + 3.5f * size) + 0.004f;
        const auto feedback = 0.52f + size * 0.34f;
        reverbTail = std::clamp(finiteOrZero(fx.reverbPreDelayMs), 0.0f, 500.0f) * 0.001f
            + loop * std::ceil(std::log(delayTailFloor) / std::log(feedback)) + 0.22f;
    }
    auto chorusTail = 0.0f;
    if (fx.chorusEnabled && fx.chorusMix > 0.0f)
    {
        const auto time = (std::clamp(finiteOrZero(fx.chorusDelayMs), 1.0f, 50.0f)
                           + std::clamp(finiteOrZero(fx.chorusDepthMs), 0.0f, 24.0f))
            * 0.001f;
        const auto feedback = std::clamp(std::abs(finiteOrZero(fx.chorusFeedback)), 0.0f, 0.95f);
        chorusTail = time * (feedback > 0.0f ? std::max(1.0f, std::ceil(std::log(delayTailFloor) / std::log(feedback))) : 1.0f);
    }
    // Serial effects can excite each other's tails; bounds therefore add.
    const auto phaserTail = fx.phaserEnabled && fx.phaserMix > 0.0f ? 2.0f : 0.0f;
    const auto distortionTail = fx.saturationEnabled && fx.saturationMix > 0.0f ? 0.002f : 0.0f;
    const auto eqTail = fx.eqEnabled && (fx.eqLowGainDb != 0.0f || fx.eqHighGainDb != 0.0f) ? 0.5f : 0.0f;
    return delayTail + reverbTail + chorusTail + phaserTail + distortionTail + eqTail;
}

void FxChain::DelayBuffer::prepare(int sampleCount)
{
    samples.assign(static_cast<std::size_t>(std::max(2, sampleCount)), 0.0f);
    writeIndex = 0;
    validSamples = 0;
}

void FxChain::DelayBuffer::reset() noexcept
{
    writeIndex = 0;
    validSamples = 0;
}

float FxChain::DelayBuffer::readAtWriteHead() const noexcept
{
    if (samples.empty())
        return 0.0f;

    if (validSamples < static_cast<int>(samples.size()))
        return 0.0f;

    return samples[static_cast<std::size_t>(writeIndex)];
}

float FxChain::DelayBuffer::read(int delaySamples) const noexcept
{
    const auto size = static_cast<int>(samples.size());
    if (size <= 1)
        return 0.0f;

    delaySamples = std::clamp(delaySamples, 1, size - 1);
    if (delaySamples > validSamples)
        return 0.0f;

    auto index = writeIndex - delaySamples;
    while (index < 0)
        index += size;

    return samples[static_cast<std::size_t>(index)];
}

float FxChain::DelayBuffer::readInterpolated(float delaySamples) const noexcept
{
    const auto size = static_cast<int>(samples.size());
    if (size <= 1)
        return 0.0f;

    delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(size - 2));
    const auto floorDelay = static_cast<int>(std::floor(delaySamples));
    const auto fraction = delaySamples - static_cast<float>(floorDelay);
    const auto first = read(floorDelay);
    const auto second = read(floorDelay + 1);
    return first + (second - first) * fraction;
}

void FxChain::DelayBuffer::write(float value) noexcept
{
    if (samples.empty())
        return;

    samples[static_cast<std::size_t>(writeIndex)] = std::abs(value) < 1.0e-20f ? 0.0f : finiteOrZero(value);
    writeIndex = (writeIndex + 1) % static_cast<int>(samples.size());
    validSamples = std::min(validSamples + 1, static_cast<int>(samples.size()));
}

void FxChain::prepare(double newSampleRate, int newMaxBlockSize)
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0
        ? std::clamp(newSampleRate, 8000.0, 384000.0)
        : 44100.0;
    smoothingCoefficient = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate)));
    maxBlockSize = std::max(1, newMaxBlockSize);
    compressorAttackCoefficient = 1.0f / (0.004f * static_cast<float>(sampleRate));
    compressorReleaseCoefficient = 1.0f / (0.080f * static_cast<float>(sampleRate));

    const auto delaySamples = secondsToSamples(sampleRate, maxTempoSyncedDelaySeconds) + 2;
    delayLeft.prepare(delaySamples);
    delayRight.prepare(delaySamples);

    const auto chorusSamples = secondsToSamples(sampleRate, 0.16f);
    chorusLeft.prepare(chorusSamples);
    chorusRight.prepare(chorusSamples);
    chorusSecondLeft.prepare(chorusSamples);
    chorusSecondRight.prepare(chorusSamples);
    reverbPreDelayLeft.prepare(secondsToSamples(sampleRate, 0.501f));
    reverbPreDelayRight.prepare(secondsToSamples(sampleRate, 0.501f));
    for (std::size_t i = 0; i < reverbDiffusers.size(); ++i)
        reverbDiffusers[i].prepare(secondsToSamples(sampleRate, 0.003f + 0.0017f * static_cast<float>(i)));
    for (std::size_t i = 0; i < delayDiffusers.size(); ++i)
        delayDiffusers[i].prepare(secondsToSamples(sampleRate, 0.007f + 0.004f * static_cast<float>(i)));

    const std::array<float, 8> combSeconds {
        0.0297f, 0.0371f, 0.0411f, 0.0437f,
        0.0311f, 0.0399f, 0.0449f, 0.0477f
    };

    for (std::size_t i = 0; i < reverbCombs.size(); ++i)
        reverbCombs[i].prepare(secondsToSamples(sampleRate, combSeconds[i] * 4.0f) + 2);

    distortionReconstruction.prepare(sampleRate, 4);
    phaserReconstruction.prepare(sampleRate, 2);
    chorusReconstruction.prepare(sampleRate, 2);
    reset();
}

void FxChain::reset() noexcept
{
    delayLeft.reset();
    delayRight.reset();
    chorusLeft.reset();
    chorusRight.reset();
    for (auto& comb : reverbCombs)
        comb.reset();
    reverbDampingStates.fill(0.0f);
    resetPhaser();
    compressorEnvelope = 0.0f;
    chorusPhase = 0.0f;
    reverbWasActive = false;
    chorusSecondLeft.reset();
    chorusSecondRight.reset();
    reverbPreDelayLeft.reset();
    reverbPreDelayRight.reset();
    for (auto& diffuser : reverbDiffusers)
        diffuser.reset();
    for (auto& diffuser : delayDiffusers)
        diffuser.reset();
    for (auto& shelf : eqShelves)
        shelf.reset();
    eqSettings = { -1.0f, -1.0f, -100.0f, -100.0f };
    previousDistortionInput = previousPhaserInput = previousChorusInput = decimated = {};
    decimationCounter = 0;
    phaserStages = {};
    phaserFeedback = {};
    delayLowState = delayHighState = {};
    cachedDelayLowCut = cachedDelayHighCut = cachedAttackMs = cachedReleaseMs = -1.0f;
    smoothedDelayLeft = smoothedDelayRight = 0.0f;
    controlsInitialized = globalWasActive = delayWasActive = chorusWasActive = false;
    distortionReconstruction.reset();
    phaserReconstruction.reset();
    chorusReconstruction.reset();
}

FxStereoFrame FxChain::process(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    input.left = finiteOrZero(input.left);
    input.right = finiteOrZero(input.right);
    if (!parameters.fx.enabled)
    {
        if (globalWasActive)
            reset();
        return input;
    }
    globalWasActive = true;
    input.left = std::clamp(input.left, -64.0f, 64.0f);
    input.right = std::clamp(input.right, -64.0f, 64.0f);
    const std::array<float, 8> targets {
        parameters.fx.saturationEnabled ? parameters.fx.saturationMix : 0.0f,
        parameters.fx.phaserEnabled ? parameters.fx.phaserMix : 0.0f,
        parameters.fx.chorusEnabled ? parameters.fx.chorusMix : 0.0f,
        parameters.fx.delayEnabled ? parameters.fx.delayMix : 0.0f,
        parameters.fx.reverbEnabled ? parameters.fx.reverbMix : 0.0f,
        parameters.fx.compressorEnabled ? parameters.fx.compressorMix : 0.0f,
        parameters.fx.saturationDrive,
        parameters.fx.eqEnabled ? 1.0f : 0.0f
    };
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        const auto target = clampUnit(finiteOrZero(targets[i]));
        if (!controlsInitialized)
            mixStates[i] = target;
        else
            smooth(target, mixStates[i]);
    }
    controlsInitialized = true;
    auto output = processSaturation(input, parameters);
    output = processPhaser(output, parameters);
    output = processChorus(output, parameters);
    output = processEq(output, parameters);
    output = processReverb(output, parameters);
    output = processDelay(output, parameters);
    output = processCompressor(output, parameters);
    return { std::clamp(finiteOrZero(output.left), -1.0f, 1.0f),
             std::clamp(finiteOrZero(output.right), -1.0f, 1.0f) };
}

FxStereoFrame FxChain::processSaturation(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[0] <= 0.0f)
        return input;
    const auto drive = mixStates[6];
    auto distort = [&](float sample) noexcept {
        switch (parameters.fx.distortionMode)
        {
            case DistortionMode::Soft: return saturate(sample, drive);
            case DistortionMode::Clip: return clipDistort(sample, drive);
            case DistortionMode::Fold: return foldDistort(sample, drive);
            case DistortionMode::Decimate: return sample;
            case DistortionMode::Bitcrush: {
                const auto bits = std::clamp(16 - static_cast<int>(drive * 14.0f), 2, 16);
                const auto levels = static_cast<float>(1 << bits);
                const auto value = std::clamp(sample, -1.0f, 1.0f) * levels;
                return static_cast<float>(static_cast<int>(value + (value < 0.0f ? -0.5f : 0.5f))) / levels;
            }
        }
        return sample;
    };
    FxStereoFrame wet;
    for (int phase = 1; phase <= 4; ++phase)
    {
        const auto fraction = static_cast<float>(phase) * 0.25f;
        auto subframe = FxStereoFrame {
            previousDistortionInput.left + (input.left - previousDistortionInput.left) * fraction,
            previousDistortionInput.right + (input.right - previousDistortionInput.right) * fraction
        };
        if (parameters.fx.distortionMode == DistortionMode::Decimate)
        {
            if (decimationCounter-- <= 0)
            {
                decimated = subframe;
                decimationCounter = static_cast<int>(drive * drive * 252.0f);
            }
            subframe = decimated;
        }
        else
            subframe = { distort(subframe.left), distort(subframe.right) };
        wet = distortionReconstruction.process(subframe);
    }
    previousDistortionInput = input;
    return { mix(input.left, wet.left, mixStates[0]), mix(input.right, wet.right, mixStates[0]) };
}

FxStereoFrame FxChain::processPhaser(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[1] <= 0.0f)
    {
        if (phaserWasActive)
            resetPhaser();
        return input;
    }
    auto rateParameters = parameters.fx;
    rateParameters.phaserRateHz = parameters.sync
        ? tempoSyncedEffectRateHz(parameters.tempoBpm, parameters.fx.phaserSyncDivision)
        : std::clamp(finiteOrZero(parameters.fx.phaserRateHz), 0.02f, 8.0f);
    return processPhaser(input, rateParameters);
}

FxStereoFrame FxChain::processChorus(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[2] <= 0.0f)
    {
        if (chorusWasActive)
            resetChorus();
        return input;
    }
    auto rateParameters = parameters.fx;
    rateParameters.chorusRateHz = parameters.sync
        ? tempoSyncedEffectRateHz(parameters.tempoBpm, parameters.fx.chorusSyncDivision)
        : std::clamp(finiteOrZero(parameters.fx.chorusRateHz), 0.02f, 8.0f);
    return processChorus(input, rateParameters);
}

FxStereoFrame FxChain::processEq(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[7] <= 0.0f)
    {
        for (auto& shelf : eqShelves)
            shelf.reset();
        return input;
    }
    return processEq(input, parameters.fx);
}

FxStereoFrame FxChain::processReverb(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[4] <= 0.0f)
    {
        if (reverbWasActive)
            resetReverb();
        return input;
    }
    return processReverb(input, parameters.fx);
}

FxStereoFrame FxChain::processCompressor(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    if (mixStates[5] <= 0.0f)
    {
        compressorEnvelope = 0.0f;
        return input;
    }
    updateCompressorCoefficients(parameters.fx);
    const auto peak = std::max(std::abs(input.left), std::abs(input.right));
    const auto coefficient = peak > compressorEnvelope ? compressorAttackCoefficient : compressorReleaseCoefficient;
    compressorEnvelope += (peak - compressorEnvelope) * coefficient;
    const auto safeEnvelope = std::max(compressorEnvelope, 1.0e-6f);
    const auto envelopeDb = gainToDecibels(safeEnvelope);
    const auto threshold = std::clamp(finiteOrZero(parameters.fx.compressorThresholdDb), -36.0f, 0.0f);
    const auto ratio = std::clamp(finiteOrZero(parameters.fx.compressorRatio), 1.0f, 100.0f);
    const auto reductionDb = envelopeDb > threshold ? (threshold - envelopeDb) * (1.0f - 1.0f / ratio) : 0.0f;
    const auto gain = dbToGain(reductionDb + finiteOrZero(parameters.fx.compressorMakeupDb));
    return { mix(input.left, input.left * gain, mixStates[5]),
             mix(input.right, input.right * gain, mixStates[5]) };
}

void FxChain::resetPhaser() noexcept
{
    phaserPhase = 0.0f;
    phaserWasActive = false;
    phaserStages = {};
    phaserFeedback = {};
    previousPhaserInput = {};
    phaserReconstruction.reset();
}

void FxChain::resetReverb() noexcept
{
    for (auto& comb : reverbCombs)
        comb.reset();
    reverbDampingStates.fill(0.0f);
    reverbPreDelayLeft.reset();
    reverbPreDelayRight.reset();
    for (auto& diffuser : reverbDiffusers)
        diffuser.reset();
    reverbWasActive = false;
}

void FxChain::resetChorus() noexcept
{
    chorusLeft.reset();
    chorusRight.reset();
    chorusSecondLeft.reset();
    chorusSecondRight.reset();
    chorusReconstruction.reset();
    chorusWasActive = false;
    previousChorusInput = {};
}

void FxChain::updateCompressorCoefficients(const FxParameters& parameters) noexcept
{
    const auto attack = std::clamp(finiteOrZero(parameters.compressorAttackMs), 0.1f, 500.0f);
    const auto release = std::clamp(finiteOrZero(parameters.compressorReleaseMs), 1.0f, 2000.0f);
    if (attack != cachedAttackMs)
    {
        cachedAttackMs = attack;
        compressorAttackCoefficient = 1.0f - std::exp(-1.0f / (attack * 0.001f * static_cast<float>(sampleRate)));
    }
    if (release != cachedReleaseMs)
    {
        cachedReleaseMs = release;
        compressorReleaseCoefficient = 1.0f - std::exp(-1.0f / (release * 0.001f * static_cast<float>(sampleRate)));
    }
}

void FxChain::Biquad::setLowpass(double rate, float frequency, float resonance) noexcept
{
    const auto omega = twoPi * frequency / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto alpha = std::sin(omega) / (2.0f * resonance);
    const auto normalization = 1.0f / (1.0f + alpha);
    coefficients = { (1.0f - cosine) * 0.5f * normalization,
                     (1.0f - cosine) * normalization,
                     (1.0f - cosine) * 0.5f * normalization,
                     -2.0f * cosine * normalization, (1.0f - alpha) * normalization };
}

void FxChain::ReconstructionFilter::prepare(double rate, int factor) noexcept
{
    filters[0].setLowpass(rate * factor, static_cast<float>(rate) * 0.45f, 0.5411961f);
    filters[1].coefficients = filters[0].coefficients;
    filters[2].setLowpass(rate * factor, static_cast<float>(rate) * 0.45f, 1.3065630f);
    filters[3].coefficients = filters[2].coefficients;
    reset();
}

void FxChain::ReconstructionFilter::reset() noexcept
{
    for (auto& filter : filters)
        filter.reset();
}

FxStereoFrame FxChain::ReconstructionFilter::process(FxStereoFrame input) noexcept
{
    return { filters[2].process(filters[0].process(input.left)),
             filters[3].process(filters[1].process(input.right)) };
}

float FxChain::smooth(float target, float& state) const noexcept
{
    target = finiteOrZero(target);
    state += (target - state) * smoothingCoefficient;
    if (std::abs(target - state) < 1.0e-6f)
        state = target;
    return state;
}

FxStereoFrame FxChain::applyWidth(FxStereoFrame input, float width) noexcept
{
    const auto mid = (input.left + input.right) * 0.5f;
    const auto side = (input.left - input.right) * 0.5f * clampUnit(finiteOrZero(width));
    return { mid + side, mid - side };
}

float FxChain::Biquad::process(float input) noexcept
{
    const auto output = coefficients[0] * input + firstState;
    firstState = finiteOrZero(coefficients[1] * input - coefficients[3] * output + secondState);
    secondState = finiteOrZero(coefficients[2] * input - coefficients[4] * output);
    if (std::abs(firstState) < 1.0e-20f)
        firstState = 0.0f;
    if (std::abs(secondState) < 1.0e-20f)
        secondState = 0.0f;
    return finiteOrZero(output);
}

void FxChain::Biquad::reset() noexcept
{
    firstState = secondState = 0.0f;
}

void FxChain::Biquad::setShelf(double rate, float frequency, float gainDb, bool high) noexcept
{
    // Audio EQ Cookbook shelf, slope 1. Both channels share the same coefficients.
    const auto amplitude = std::pow(10.0f, gainDb / 40.0f);
    const auto omega = twoPi * std::clamp(frequency, 20.0f, static_cast<float>(rate) * 0.45f) / static_cast<float>(rate);
    const auto cosine = std::cos(omega);
    const auto beta = std::sin(omega) * std::sqrt(2.0f * amplitude);
    const auto plus = amplitude + 1.0f;
    const auto minus = amplitude - 1.0f;
    const auto sign = high ? 1.0f : -1.0f;
    const auto divisor = plus - sign * minus * cosine + beta;
    coefficients = {
        amplitude * (plus + sign * minus * cosine + beta) / divisor,
        -sign * 2.0f * amplitude * (minus + sign * plus * cosine) / divisor,
        amplitude * (plus + sign * minus * cosine - beta) / divisor,
        sign * 2.0f * (minus - sign * plus * cosine) / divisor,
        (plus - sign * minus * cosine - beta) / divisor
    };
}

FxStereoFrame FxChain::processPhaser(FxStereoFrame input, const FxParameters& parameters) noexcept
{
    const auto center = std::clamp(finiteOrZero(parameters.phaserCenterHz)
                                      + finiteOrZero(parameters.phaserCenterOffsetHz),
                                  20.0f, 20000.0f);
    if (!phaserWasActive)
        smoothedPhaserCenter = center;
    phaserWasActive = true;
    smooth(center, smoothedPhaserCenter);
    const auto rate = std::clamp(finiteOrZero(parameters.phaserRateHz), 0.02f, 60.0f);
    phaserPhase += rate / static_cast<float>(sampleRate);
    phaserPhase -= std::floor(phaserPhase);
    const auto modulation = std::sin(phaserPhase * twoPi) * clampUnit(finiteOrZero(parameters.phaserDepth)) * 3.0f;
    const auto frequency = smoothedPhaserCenter * std::exp2(modulation);
    const auto offset = clampUnit(finiteOrZero(parameters.phaserLrOffset));
    const auto spread = clampUnit(finiteOrZero(parameters.phaserSpread));
    const auto feedback = std::clamp(finiteOrZero(parameters.phaserFeedback), -0.95f, 0.95f) * 0.85f;
    if (spread != cachedPhaserSpread || offset != cachedPhaserOffset)
    {
        cachedPhaserSpread = spread;
        cachedPhaserOffset = offset;
        for (std::size_t channel = 0; channel < phaserFrequencyMultipliers.size(); ++channel)
            for (std::size_t stage = 0; stage < phaserFrequencyMultipliers[channel].size(); ++stage)
            {
                const auto distance = static_cast<float>(stage) - 2.5f;
                const auto channelOffset = channel == 0 ? -offset * 0.5f : offset * 0.5f;
                phaserFrequencyMultipliers[channel][stage] = std::exp2(channelOffset + distance * spread);
            }
    }
    auto processChannel = [&](float value, std::size_t channel, float& previous) noexcept {
        auto output = value + previous * feedback;
        auto& states = phaserStages[channel];
        for (std::size_t stage = 0; stage < states.size(); ++stage)
        {
            const auto stageFrequency = std::clamp(frequency * phaserFrequencyMultipliers[channel][stage],
                                                   10.0f, static_cast<float>(sampleRate) * 0.45f);
            const auto bilinear = 3.14159265358979323846f * stageFrequency / (2.0f * static_cast<float>(sampleRate));
            const auto coefficient = (1.0f - bilinear) / (1.0f + bilinear);
            const auto next = -coefficient * output + states[stage];
            states[stage] = finiteOrZero(output + coefficient * next);
            if (std::abs(states[stage]) < 1.0e-20f)
                states[stage] = 0.0f;
            output = next;
        }
        previous = std::clamp(finiteOrZero(output), -2.0f, 2.0f);
        return output;
    };
    FxStereoFrame wet;
    for (int subsample = 1; subsample <= 2; ++subsample)
    {
        const auto fraction = static_cast<float>(subsample) * 0.5f;
        const auto left = previousPhaserInput.left + (input.left - previousPhaserInput.left) * fraction;
        const auto right = previousPhaserInput.right + (input.right - previousPhaserInput.right) * fraction;
        wet = phaserReconstruction.process({ processChannel(left, 0, phaserFeedback.left),
                                             processChannel(right, 1, phaserFeedback.right) });
    }
    previousPhaserInput = input;
    wet = applyWidth(wet, parameters.phaserWidth);
    return { mix(input.left, wet.left, mixStates[1]), mix(input.right, wet.right, mixStates[1]) };
}

FxStereoFrame FxChain::processChorus(FxStereoFrame input, const FxParameters& parameters) noexcept
{
    const auto delay = std::clamp(finiteOrZero(parameters.chorusDelayMs), 1.0f, 50.0f);
    if (!chorusWasActive)
    {
        chorusLeft.reset();
        chorusRight.reset();
        chorusSecondLeft.reset();
        chorusSecondRight.reset();
        smoothedChorusDelay = delay;
    }
    chorusWasActive = true;
    smooth(delay, smoothedChorusDelay);
    const auto rate = std::clamp(finiteOrZero(parameters.chorusRateHz), 0.02f, 60.0f);
    const auto depth = std::clamp(finiteOrZero(parameters.chorusDepthMs), 0.0f, 24.0f);
    const auto feedback = std::clamp(finiteOrZero(parameters.chorusFeedback), -0.95f, 0.95f);
    FxStereoFrame wet;
    for (int subsample = 0; subsample < 2; ++subsample)
    {
        chorusPhase += rate / (2.0f * static_cast<float>(sampleRate));
        chorusPhase -= std::floor(chorusPhase);
        const auto phase = chorusPhase * twoPi;
        const auto scale = 0.002f * static_cast<float>(sampleRate);
        const auto firstLeft = chorusLeft.readInterpolated((smoothedChorusDelay + std::sin(phase) * depth) * scale);
        const auto firstRight = chorusRight.readInterpolated((smoothedChorusDelay + std::sin(phase + 1.57079632679f) * depth) * scale);
        const auto fraction = static_cast<float>(subsample + 1) * 0.5f;
        const auto left = previousChorusInput.left + (input.left - previousChorusInput.left) * fraction;
        const auto right = previousChorusInput.right + (input.right - previousChorusInput.right) * fraction;
        chorusLeft.write(left + firstLeft * feedback);
        chorusRight.write(right + firstRight * feedback);
        wet = { firstLeft, firstRight };
        if (parameters.chorusDualMode)
        {
            const auto secondLeft = chorusSecondLeft.readInterpolated((smoothedChorusDelay + std::sin(phase + 3.14159265359f) * depth) * scale);
            const auto secondRight = chorusSecondRight.readInterpolated((smoothedChorusDelay + std::sin(phase + 4.71238898038f) * depth) * scale);
            chorusSecondLeft.write(left + secondLeft * feedback);
            chorusSecondRight.write(right + secondRight * feedback);
            wet.left = 0.5f * (firstLeft + secondLeft);
            wet.right = 0.5f * (firstRight + secondRight);
        }
        else
        {
            chorusSecondLeft.reset();
            chorusSecondRight.reset();
        }
        wet = chorusReconstruction.process(wet);
    }
    previousChorusInput = input;
    wet = applyWidth(wet, parameters.chorusWidth);
    return { mix(input.left, wet.left, mixStates[2]), mix(input.right, wet.right, mixStates[2]) };
}

FxStereoFrame FxChain::processEq(FxStereoFrame input, const FxParameters& parameters) noexcept
{
    const std::array<float, 4> settings {
        std::clamp(finiteOrZero(parameters.eqLowFrequencyHz), 20.0f, 2000.0f),
        std::clamp(finiteOrZero(parameters.eqHighFrequencyHz), 1000.0f, 20000.0f),
        std::clamp(finiteOrZero(parameters.eqLowGainDb), -12.0f, 12.0f),
        std::clamp(finiteOrZero(parameters.eqHighGainDb), -12.0f, 12.0f)
    };
    if (settings != eqSettings)
    {
        eqSettings = settings;
        eqShelves[0].setShelf(sampleRate, settings[0], settings[2], false);
        eqShelves[1].coefficients = eqShelves[0].coefficients;
        eqShelves[2].setShelf(sampleRate, settings[1], settings[3], true);
        eqShelves[3].coefficients = eqShelves[2].coefficients;
        eqNormalization = dbToGain(-std::max({ 0.0f, settings[2], settings[3] }));
    }
    if (settings[2] == 0.0f && settings[3] == 0.0f)
        return input;
    return {
        mix(input.left, eqShelves[2].process(eqShelves[0].process(input.left)) * eqNormalization, mixStates[7]),
        mix(input.right, eqShelves[3].process(eqShelves[1].process(input.right)) * eqNormalization, mixStates[7])
    };
}

FxStereoFrame FxChain::processDelay(FxStereoFrame input, const SynthParameters& parameters) noexcept
{
    const auto& fx = parameters.fx;
    if (mixStates[3] <= 0.0f)
    {
        if (delayWasActive)
        {
            delayLeft.reset();
            delayRight.reset();
            delayLowState = delayHighState = {};
            for (auto& diffuser : delayDiffusers)
                diffuser.reset();
        }
        delayWasActive = false;
        return input;
    }
    const auto synced = parameters.sync && fx.delaySync;
    const auto leftTime = synced ? tempoSyncedDelaySeconds(parameters.tempoBpm, fx.delaySyncDivision)
                                 : std::clamp(finiteOrZero(fx.delayTimeLeftMs), 1.0f, 6000.0f) * 0.001f;
    const auto rightTime = synced ? tempoSyncedDelaySeconds(parameters.tempoBpm, fx.delayRightSyncDivision)
                                  : std::clamp(finiteOrZero(fx.delayTimeRightMs), 1.0f, 6000.0f) * 0.001f;
    const auto leftSamples = static_cast<float>(secondsToSamples(sampleRate, leftTime));
    const auto rightSamples = static_cast<float>(secondsToSamples(sampleRate, rightTime));
    if (!delayWasActive)
    {
        smoothedDelayLeft = leftSamples;
        smoothedDelayRight = rightSamples;
    }
    delayWasActive = true;
    smooth(leftSamples, smoothedDelayLeft);
    smooth(rightSamples, smoothedDelayRight);
    auto delayed = FxStereoFrame {
        delayLeft.readInterpolated(smoothedDelayLeft), delayRight.readInterpolated(smoothedDelayRight)
    };
    const auto lowCut = std::clamp(finiteOrZero(fx.delayLowCutHz), 20.0f, 2000.0f);
    const auto highCut = std::clamp(finiteOrZero(fx.delayHighCutHz), 200.0f, 20000.0f);
    if (lowCut != cachedDelayLowCut)
    {
        cachedDelayLowCut = lowCut;
        delayLowCoefficient = 1.0f - std::exp(-twoPi * lowCut / static_cast<float>(sampleRate));
    }
    if (highCut != cachedDelayHighCut)
    {
        cachedDelayHighCut = highCut;
        delayHighCoefficient = 1.0f - std::exp(-twoPi * highCut / static_cast<float>(sampleRate));
    }
    auto filter = [&](float value, float& low, float& high, DelayBuffer& diffuser) noexcept {
        if (lowCut > 20.0f)
        {
            low += delayLowCoefficient * (value - low);
            value -= low;
        }
        if (highCut < std::min(20000.0f, static_cast<float>(sampleRate) * 0.45f))
        {
            high += delayHighCoefficient * (value - high);
            value = high;
        }
        const auto smear = clampUnit(finiteOrZero(fx.delaySmear)) * 0.7f;
        if (smear > 0.0f)
        {
            const auto previous = diffuser.readAtWriteHead();
            const auto output = previous - value * smear;
            diffuser.write(value + output * smear);
            value = output;
        }
        return finiteOrZero(value);
    };
    delayed.left = filter(delayed.left, delayLowState.left, delayHighState.left, delayDiffusers[0]);
    delayed.right = filter(delayed.right, delayLowState.right, delayHighState.right, delayDiffusers[1]);
    const auto feedback = std::clamp(finiteOrZero(fx.delayFeedback), 0.0f, 0.95f);
    const auto spread = clampUnit(finiteOrZero(fx.delaySpread));
    const auto cross = fx.delayPingPong ? spread : 0.0f;
    delayLeft.write(input.left + (delayed.left * (1.0f - cross) + delayed.right * cross) * feedback);
    delayRight.write(input.right + (delayed.right * (1.0f - cross) + delayed.left * cross) * feedback);
    delayed = applyWidth(delayed, fx.delayWidth);
    return { mix(input.left, delayed.left, mixStates[3]), mix(input.right, delayed.right, mixStates[3]) };
}

FxStereoFrame FxChain::processReverb(FxStereoFrame input, const FxParameters& parameters) noexcept
{
    const auto size = clampUnit(finiteOrZero(parameters.reverbDecay));
    const auto predelay = std::clamp(finiteOrZero(parameters.reverbPreDelayMs), 0.0f, 500.0f);
    if (!reverbWasActive)
    {
        smoothedReverbSize = size;
        smoothedReverbPreDelay = predelay;
    }
    reverbWasActive = true;
    smooth(size, smoothedReverbSize);
    smooth(predelay, smoothedReverbPreDelay);
    const auto preSamples = smoothedReverbPreDelay * 0.001f * static_cast<float>(sampleRate);
    const auto excitation = preSamples < 1.0f ? input : FxStereoFrame { reverbPreDelayLeft.readInterpolated(preSamples), reverbPreDelayRight.readInterpolated(preSamples) };
    reverbPreDelayLeft.write(input.left);
    reverbPreDelayRight.write(input.right);
    const auto damping = clampUnit(finiteOrZero(parameters.reverbDamp)) * 0.95f;
    const auto feedback = 0.52f + smoothedReverbSize * 0.34f;
    constexpr std::array<float, 8> times {
        0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0311f, 0.0399f, 0.0449f, 0.0477f
    };
    const auto mono = 0.5f * (excitation.left + excitation.right);
    FxStereoFrame wet;
    for (std::size_t i = 0; i < reverbCombs.size(); ++i)
    {
        const auto delay = times[i] * (0.5f + 3.5f * smoothedReverbSize) * static_cast<float>(sampleRate);
        const auto previous = reverbCombs[i].readInterpolated(delay);
        auto& state = reverbDampingStates[i];
        state = finiteOrZero(previous * (1.0f - damping) + state * damping);
        if (std::abs(state) < 1.0e-20f)
            state = 0.0f;
        reverbCombs[i].write(mono + (i < 4 ? excitation.left : excitation.right) * 0.35f + state * feedback);
        if (i < 4)
            wet.left += previous;
        else
            wet.right += previous;
    }
    wet.left *= 0.125f;
    wet.right *= 0.125f;
    for (std::size_t i = 0; i < reverbDiffusers.size(); ++i)
    {
        auto& value = i < 2 ? wet.left : wet.right;
        const auto previous = reverbDiffusers[i].readAtWriteHead();
        const auto output = previous - value * 0.5f;
        reverbDiffusers[i].write(value + output * 0.5f);
        value = output;
    }
    wet = applyWidth(wet, parameters.reverbWidth);
    return { mix(input.left, wet.left, mixStates[4]), mix(input.right, wet.right, mixStates[4]) };
}

} // namespace synth
