#include "Lfo.h"

#include <algorithm>
#include <cmath>

namespace synth
{
namespace
{
constexpr float twoPi = 6.28318530717958647692f;
}

void Lfo::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    updatePhaseIncrement();
    resetPhase();
}

void Lfo::setRateHz(float newRateHz) noexcept
{
    const auto clampedRate = std::clamp(newRateHz, 0.01f, 40.0f);
    if (std::abs(clampedRate - rateHz) <= 0.000001f)
        return;

    rateHz = clampedRate;
    updatePhaseIncrement();
}

void Lfo::setShape(LfoShape newShape) noexcept
{
    if (shape == newShape)
        return;

    shape = newShape;
}

void Lfo::setSteps(const float* stepValues, int count, float smooth) noexcept
{
    stepCount = std::clamp(count, 1, lfoMaxSteps);
    stepSmooth = std::isfinite(smooth) ? std::clamp(smooth, 0.0f, 1.0f) : 0.0f;
    if (stepValues == nullptr)
        return;

    for (int i = 0; i < stepCount; ++i)
    {
        const auto raw = stepValues[i];
        steps[static_cast<std::size_t>(i)] = std::isfinite(raw) ? std::clamp(raw, -1.0f, 1.0f) : 0.0f;
    }
}

void Lfo::setPhaseDegrees(float degrees) noexcept
{
    const auto newPhaseOffset = std::fmod(std::max(0.0f, degrees) / 360.0f, 1.0f);
    if (std::abs(newPhaseOffset - phaseOffset) <= 0.000001f)
        return;

    phaseOffset = newPhaseOffset;
}

void Lfo::resetPhase() noexcept
{
    phase = phaseOffset;
    value = valueForPhase(phase);
}

float Lfo::process() noexcept
{
    const auto previousPhase = phase;
    phase += phaseIncrement;
    if (phase >= 1.0f)
        phase -= std::floor(phase);

    if (shape == LfoShape::SampleHold && phase < previousPhase)
    {
        randomState = randomState * 1664525u + 1013904223u;
        heldRandom = (static_cast<float>((randomState >> 8) & 0x00ffffffu) / 8388607.5f) - 1.0f;
    }

    value = valueForPhase(phase);
    return value;
}

float Lfo::valueForPhase(float phaseValue) noexcept
{
    phaseValue -= std::floor(phaseValue);

    switch (shape)
    {
        case LfoShape::Sine:
            return std::sin(twoPi * phaseValue);
        case LfoShape::Triangle:
            return 1.0f - 4.0f * std::abs(phaseValue - 0.5f);
        case LfoShape::SawUp:
            return 2.0f * phaseValue - 1.0f;
        case LfoShape::SawDown:
            return 1.0f - 2.0f * phaseValue;
        case LfoShape::Square:
            return phaseValue < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold:
            return heldRandom;
        case LfoShape::Step:
        {
            const auto count = static_cast<float>(stepCount);
            const auto position = phaseValue * count;
            const auto index = std::min(static_cast<int>(position), stepCount - 1);
            const auto current = steps[static_cast<std::size_t>(index)];
            if (stepSmooth <= 0.0001f)
                return current;

            // Crossfade only inside the trailing `stepSmooth` fraction of the
            // step so low smooth values keep the gated stepper character.
            const auto fraction = position - static_cast<float>(index);
            const auto blendStart = 1.0f - stepSmooth;
            if (fraction <= blendStart)
                return current;

            const auto nextIndex = index + 1 < stepCount ? index + 1 : 0;
            const auto next = steps[static_cast<std::size_t>(nextIndex)];
            const auto blend = (fraction - blendStart) / stepSmooth;
            return current + (next - current) * blend;
        }
    }

    return 0.0f;
}

void Lfo::updatePhaseIncrement() noexcept
{
    phaseIncrement = rateHz / static_cast<float>(sampleRate);
}
} // namespace synth
