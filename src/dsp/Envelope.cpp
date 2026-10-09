#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace synth
{
void Envelope::prepare(double newSampleRate) noexcept
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0 ? newSampleRate : 44100.0;
    updateCachedRates();
    reset();
}

void Envelope::setSettings(EnvelopeSettings newSettings) noexcept
{
    newSettings.attackMs = std::max(0.0f, newSettings.attackMs);
    newSettings.decayMs = std::max(0.0f, newSettings.decayMs);
    newSettings.sustain = std::clamp(newSettings.sustain, 0.0f, 1.0f);
    newSettings.releaseMs = std::max(0.0f, newSettings.releaseMs);
    if (std::abs(newSettings.attackMs - settings.attackMs) <= 0.000001f
        && std::abs(newSettings.decayMs - settings.decayMs) <= 0.000001f
        && std::abs(newSettings.sustain - settings.sustain) <= 0.000001f
        && std::abs(newSettings.releaseMs - settings.releaseMs) <= 0.000001f)
    {
        return;
    }

    settings = newSettings;
    updateCachedRates();
}

void Envelope::reset() noexcept
{
    stage = EnvelopeStage::Idle;
    value = 0.0f;
    releaseStart = 0.0f;
    releaseSamplesElapsed = 0.0f;
}

void Envelope::noteOn() noexcept
{
    releaseSamplesElapsed = 0.0f;
    stage = settings.attackMs <= 0.0f ? EnvelopeStage::Decay : EnvelopeStage::Attack;
    if (settings.attackMs <= 0.0f)
        value = 1.0f;
}

void Envelope::noteOff() noexcept
{
    if (stage == EnvelopeStage::Idle)
        return;

    releaseStart = value;
    releaseSamplesElapsed = 0.0f;
    stage = settings.releaseMs <= 0.0f ? EnvelopeStage::Idle : EnvelopeStage::Release;
    if (stage == EnvelopeStage::Idle)
        value = 0.0f;
}

float Envelope::process() noexcept
{
    if (stage == EnvelopeStage::Idle)
        return value;

    if (stage == EnvelopeStage::Attack)
    {
        value += attackIncrement;
        if (value >= 1.0f)
        {
            value = 1.0f;
            stage = EnvelopeStage::Decay;
        }
    }
    else if (stage == EnvelopeStage::Decay)
    {
        if (settings.decayMs <= 0.0f)
        {
            value = settings.sustain;
            stage = EnvelopeStage::Sustain;
        }
        else
        {
            value -= decayDecrement;
            if (value <= settings.sustain)
            {
                value = settings.sustain;
                stage = EnvelopeStage::Sustain;
            }
        }
    }
    else if (stage == EnvelopeStage::Release)
    {
        releaseSamplesElapsed += 1.0f;
        const auto progress = std::min(1.0f, releaseSamplesElapsed / releaseSamples);
        value = releaseStart * (1.0f - progress);
        if (progress >= 1.0f)
        {
            value = 0.0f;
            stage = EnvelopeStage::Idle;
        }
    }

    return value;
}

float Envelope::samplesForMs(float ms) const noexcept
{
    return std::max(1.0f, static_cast<float>(sampleRate) * ms / 1000.0f);
}

void Envelope::updateCachedRates() noexcept
{
    const auto attackSamples = samplesForMs(settings.attackMs);
    const auto decaySamples = samplesForMs(settings.decayMs);
    releaseSamples = samplesForMs(settings.releaseMs);
    attackIncrement = settings.attackMs <= 0.0f ? 1.0f : 1.0f / attackSamples;
    decayDecrement = settings.decayMs <= 0.0f ? 1.0f : (1.0f - settings.sustain) / decaySamples;
}
} // namespace synth
