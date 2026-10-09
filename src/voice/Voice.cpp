#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace synth
{
namespace
{
constexpr float halfPi = 1.57079632679489661923f;

LfoShape toLfoShape(LfoShapeChoice choice) noexcept
{
    switch (choice)
    {
        case LfoShapeChoice::Sine: return LfoShape::Sine;
        case LfoShapeChoice::Triangle: return LfoShape::Triangle;
        case LfoShapeChoice::SawUp: return LfoShape::SawUp;
        case LfoShapeChoice::SawDown: return LfoShape::SawDown;
        case LfoShapeChoice::Square: return LfoShape::Square;
        case LfoShapeChoice::SampleHold:
            return LfoShape::SampleHold;
        case LfoShapeChoice::Noise:
            return LfoShape::SampleHold;
        case LfoShapeChoice::Step: return LfoShape::Step;
    }

    return LfoShape::SawDown;
}

EnvelopeSettings toEnvelopeSettings(EnvelopeParameters parameters) noexcept
{
    return { parameters.attackMs, parameters.decayMs, parameters.sustain, parameters.releaseMs };
}

float syncDivisionBeats(int division) noexcept
{
    switch (division)
    {
        case 0: return 0.25f;
        case 1: return 0.5f;
        case 2: return 0.75f;
        case 3: return 1.0f;
        case 4: return 2.0f;
        case 5: return 4.0f;
        case 6: return 1.0f / 12.0f;
        case 7: return 0.1875f;
        case 8: return 1.0f / 6.0f;
        case 9: return 0.375f;
        case 10: return 1.0f / 3.0f;
        case 11: return 2.0f / 3.0f;
        case 12: return 1.5f;
        case 13: return 4.0f / 3.0f;
        case 14: return 3.0f;
        case 15: return 0.125f;
        case 16: return 8.0f / 3.0f;
        case 17: return 6.0f;
        default: return 1.0f;
    }
}

float clampFast(float value, float minimum, float maximum) noexcept
{
    if (!std::isfinite(value))
        return minimum;

    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

float clampUnitFast(float value) noexcept
{
    return clampFast(value, 0.0f, 1.0f);
}

int clampIntFast(int value, int minimum, int maximum) noexcept
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

float bipolarIndex(int index, int count) noexcept
{
    if (count <= 1)
        return 0.0f;

    return (2.0f * static_cast<float>(index) / static_cast<float>(count - 1)) - 1.0f;
}

float unipolarIndex(int index, int count) noexcept
{
    if (count <= 1)
        return 0.0f;

    return clampUnitFast(static_cast<float>(index) / static_cast<float>(count - 1));
}

float sanitize(float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}

int layerOscillatorIndex(int layerIndex, int oscillatorIndex) noexcept
{
    return layerIndex * oscillatorSlotsPerLayer + oscillatorIndex;
}

} // namespace

void Voice::prepare(double newSampleRate)
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0 ? newSampleRate : 44100.0;
    for (auto& envelope : partAmpEnvelopes) envelope.prepare(sampleRate);
    for (auto& part : partFilters)
        for (auto& channel : part) channel.prepare(sampleRate);
    modEnvelope.prepare(sampleRate);
    modEnvelope2.prepare(sampleRate);
    lfo.prepare(sampleRate);
    lfo2.prepare(sampleRate);
    for (auto& stack : layerOscillators) stack.prepare(sampleRate);
    ramp.prepare(sampleRate);
}

void Voice::setVoiceIndex(int index, int total) noexcept
{
    setAllocationIndices(index, total, unisonIndex, unisonCount);
}

void Voice::setAllocationIndices(int index, int total, int newUnisonIndex, int newUnisonCount) noexcept
{
    voiceIndex = std::max(0, index);
    voiceCount = std::max(1, total);
    unisonIndex = std::max(0, newUnisonIndex);
    unisonCount = std::max(1, newUnisonCount);
    cachedVoiceUni = unipolarIndex(voiceIndex, voiceCount);
    cachedVoiceBi = bipolarIndex(voiceIndex, voiceCount);
    cachedUnisonUni = unipolarIndex(unisonIndex, unisonCount);
    cachedUnisonBi = bipolarIndex(unisonIndex, unisonCount);
}

void Voice::noteOn(int note, float normalizedVelocity, float randomValue,
                   int newUnisonIndex, int newUnisonCount, const SynthParameters& parameters,
                   bool allowGlide, bool retriggerModulators, float glideFromNote) noexcept
{
    normalizedVelocity = clampUnitFast(normalizedVelocity);
    const auto wasActive = state != VoiceState::Idle && midiNote >= 0;
    const auto shouldGlide = allowGlide && (wasActive || glideFromNote >= 0.0f);
    const auto shouldRetrigger = !wasActive || retriggerModulators;
    const auto previousNote = wasActive ? currentMidiNote : (glideFromNote >= 0.0f ? glideFromNote : static_cast<float>(note));
    const auto previousVelocity = wasActive ? currentVelocity : normalizedVelocity;

    state = VoiceState::Held;
    midiNote = note;
    velocity = std::clamp(normalizedVelocity, 0.0f, 1.0f);
    startVelocity = previousVelocity;
    currentVelocity = previousVelocity;
    const auto velocityGlideMs = std::clamp(parameters.velocityGlideMs, 0.0f, 2000.0f);
    velocityGlideTotalSamples = shouldGlide && velocityGlideMs > 0.0f
        ? std::max(1, static_cast<int>(std::round(velocityGlideMs * static_cast<float>(sampleRate) * 0.001f)))
        : 0;
    velocityGlideSamples = 0;
    if (velocityGlideTotalSamples == 0)
        currentVelocity = velocity;

    startMidiNote = previousNote;
    targetMidiNote = static_cast<float>(note);
    currentMidiNote = previousNote;
    const auto glideMs = std::clamp(parameters.glideMs, 0.0f, 2000.0f);
    glideTotalSamples = shouldGlide && glideMs > 0.0f
        ? std::max(1, static_cast<int>(std::round(glideMs * static_cast<float>(sampleRate) * 0.001f)))
        : 0;
    glideSamples = 0;
    if (glideTotalSamples == 0)
        currentMidiNote = targetMidiNote;

    randomOnNote = clampFast(randomValue, -1.0f, 1.0f);
    stopFadeSamples = 0;
    stopFadeTotalSamples = 0;
    setAllocationIndices(voiceIndex, voiceCount, newUnisonIndex, newUnisonCount);
    syncModulatorConfig(parameters);
    if (shouldRetrigger)
    {
        {
            for (auto& envelope : partAmpEnvelopes) envelope.noteOn();
            modEnvelope2.noteOn();
            if (!parameters.lfo.free) lfo.resetPhase();
            if (!parameters.lfo2.free) lfo2.resetPhase();
            for (auto& part : partFilters)
                for (auto& channel : part) channel.reset();
        }
        modEnvelope.noteOn();
        if (!parameters.lfo.free && (parameters.lfo.gateMode == LfoGateMode::Poly || parameters.lfo.gateMode == LfoGateMode::PolyOn))
            lfo.resetPhase();
        resetLayerOscillators(parameters, randomOnNote);
        ramp.noteOn();
    }
}

void Voice::noteOff() noexcept
{
    if (state == VoiceState::Idle)
        return;

    if (stopFadeSamples > 0)
        return;

    stopFadeSamples = 0;
    stopFadeTotalSamples = 0;
    state = VoiceState::Releasing;
    modEnvelope.noteOff();
    modEnvelope2.noteOff();
    for (auto& envelope : partAmpEnvelopes) envelope.noteOff();
}

void Voice::stopWithFade(int fadeSamples) noexcept
{
    if (state == VoiceState::Idle)
        return;

    if (stopFadeSamples > 0)
        return;

    stopFadeTotalSamples = std::max(1, fadeSamples);
    stopFadeSamples = stopFadeTotalSamples;
    state = VoiceState::Releasing;
}

float Voice::normalizationPowerWeight() const noexcept
{
    if (state == VoiceState::Idle)
        return 0.0f;

    if (stopFadeSamples <= 0 || stopFadeTotalSamples <= 0)
        return 1.0f;

    const auto fade = static_cast<float>(stopFadeSamples)
        / static_cast<float>(std::max(1, stopFadeTotalSamples));
    return fade * fade;
}

void Voice::reset() noexcept
{
    state = VoiceState::Idle;
    midiNote = -1;
    velocity = 0.0f;
    currentVelocity = 0.0f;
    startVelocity = 0.0f;
    velocityGlideSamples = 0;
    velocityGlideTotalSamples = 0;
    currentMidiNote = 0.0f;
    startMidiNote = 0.0f;
    targetMidiNote = 0.0f;
    glideSamples = 0;
    glideTotalSamples = 0;
    stopFadeSamples = 0;
    stopFadeTotalSamples = 0;
    randomOnNote = 0.0f;
    modEnvelope.reset();
    lfo.resetPhase();
    ramp.reset();
    for (auto& envelope : partAmpEnvelopes) envelope.reset();
    for (auto& part : partFilters)
        for (auto& channel : part) channel.reset();
    modEnvelope2.reset();
    lfo2.resetPhase();
    nativeModulation.fill(0.0f);
    nativeModEnv2Value = nativeAmpEnv2Value = nativeLfo2Value = 0.0f;
    nativeLfo1PhaseOffset = nativeLfo2PhaseOffset = 0.0f;
    lastDirectSums = {};
    lastTransModSums = {};
    lastRampValue = 0.0f;
    modulatorConfigInitialized = false;
}

void Voice::process(int numSamples) noexcept
{
    for (int index = 0; index < numSamples; ++index)
    {
        modEnvelope.process();
        lfo.process();
        {
            for (auto& envelope : partAmpEnvelopes) envelope.process();
            modEnvelope2.process();
            lfo2.process();
        }
    }
    const auto envelopeActive = partAmpEnvelopes[0].isActive() || partAmpEnvelopes[1].isActive();
    if (state == VoiceState::Releasing && !envelopeActive && stopFadeSamples <= 0) reset();
}

StereoFrame Voice::renderSample(const SynthParameters& parameters, const LfoFrame* monoLfoValue,
                                const LfoFrame* monoLfo2Value) noexcept
{
    if (!isActive()) return {};
    if (!modulatorConfigInitialized) syncModulatorConfig(parameters);
    return renderNative(parameters, monoLfoValue, monoLfo2Value);
}

void Voice::syncModulatorConfig(const SynthParameters& parameters) noexcept
{
    for (std::size_t part = 0; part < partAmpEnvelopes.size(); ++part)
    {
        partAmpEnvelopes[part].setSettings(toEnvelopeSettings(parameters.layers[part].ampEnv));
        auto settings = parameters.layers[part].filter;
        settings.warmDrive = parameters.filterControl.warmDrive;
        settings.selfOscillation = true;
        settings.nativeTopology = true;
        settings.cutoffSemitones += parameters.filterControl.cutoffSemitones;
        settings.keytrack = clampFast(settings.keytrack + parameters.filterControl.keytrack, -1.0f, 2.0f);
        settings.resonance = clampUnitFast(settings.resonance + parameters.filterControl.resonance);
        settings.drive = clampUnitFast(settings.drive + parameters.filterControl.drive);
        preparedPartFilters[part] = settings;
        for (auto& channel : partFilters[part]) channel.prepareBlock(settings);
    }
    modEnvelope.setSettings(toEnvelopeSettings(parameters.modEnv));
    lfo.setPhaseDegrees(parameters.lfo.phaseDegrees);
    lfo.setSteps(parameters.lfo.steps.data(), parameters.lfo.stepCount, parameters.lfo.stepSmooth);
    modEnvelope2.setSettings(toEnvelopeSettings(parameters.modEnv2));
    const auto rate = [&parameters](const LfoParameters& settings, float tempo) noexcept {
        return settings.free || !parameters.sync ? settings.rateHz
                                                 : tempo / (60.0f * syncDivisionBeats(settings.syncDivision));
    };
    lfo.setShape(parameters.lfo.shape == LfoShapeChoice::Noise ? LfoShape::Noise : toLfoShape(parameters.lfo.shape));
    lfo.setRateHz(rate(parameters.lfo, parameters.tempoBpm));
    lfo2.setRateHz(rate(parameters.lfo2, parameters.tempoBpm));
    lfo2.setShape(parameters.lfo2.shape == LfoShapeChoice::Noise ? LfoShape::Noise : toLfoShape(parameters.lfo2.shape));
    lfo2.setPhaseDegrees(parameters.lfo2.phaseDegrees);
    lfo2.setSteps(parameters.lfo2.steps.data(), parameters.lfo2.stepCount, parameters.lfo2.stepSmooth);
    modulatorConfigInitialized = true;
}

StereoFrame Voice::renderNative(const SynthParameters& parameters, const LfoFrame* monoLfoValue,
                                const LfoFrame* monoLfo2Value) noexcept
{
    if (!isActive()) return {};
    const auto note = processGlide(parameters);
    const auto glidedVelocity = processVelocityGlide(parameters);
    std::array<float, layerCount> ampValues {};
    auto anyEnvelopeActive = false;
    for (std::size_t part = 0; part < partAmpEnvelopes.size(); ++part)
    {
        ampValues[part] = partAmpEnvelopes[part].process();
        anyEnvelopeActive = anyEnvelopeActive || partAmpEnvelopes[part].isActive();
    }
    if (state == VoiceState::Releasing && !anyEnvelopeActive && stopFadeSamples <= 0)
    {
        reset();
        return {};
    }
    const auto mod = modEnvelope.process();
    nativeModEnv2Value = modEnvelope2.process();
    nativeAmpEnv2Value = ampValues[1];
    const auto destination = [this](NativeModDestination id) noexcept {
        return nativeModulation[static_cast<std::size_t>(id)];
    };
    const auto rate = [&parameters](const LfoParameters& settings, float tempo) noexcept {
        return settings.free || !parameters.sync ? settings.rateHz
                                                 : tempo / (60.0f * syncDivisionBeats(settings.syncDivision));
    };
    const auto baseRate1 = clampFast(rate(parameters.lfo, parameters.tempoBpm), 0.01f, 100.0f);
    const auto baseRate2 = clampFast(rate(parameters.lfo2, parameters.tempoBpm), 0.01f, 100.0f);
    const auto effectiveRate1 = clampFast(baseRate1 + destination(NativeModDestination::Lfo1Rate), 0.01f, 100.0f);
    const auto effectiveRate2 = clampFast(baseRate2 + destination(NativeModDestination::Lfo2Rate), 0.01f, 100.0f);
    lfo.setRateHz(effectiveRate1);
    lfo2.setRateHz(effectiveRate2);
    if (monoLfoValue != nullptr)
    {
        nativeLfo1PhaseOffset += (effectiveRate1 - baseRate1) / static_cast<float>(sampleRate);
        nativeLfo1PhaseOffset -= std::floor(nativeLfo1PhaseOffset);
        lfo.setNormalizedPhase(monoLfoValue->phase + nativeLfo1PhaseOffset);
    }
    if (monoLfo2Value != nullptr)
    {
        nativeLfo2PhaseOffset += (effectiveRate2 - baseRate2) / static_cast<float>(sampleRate);
        nativeLfo2PhaseOffset -= std::floor(nativeLfo2PhaseOffset);
        lfo2.setNormalizedPhase(monoLfo2Value->phase + nativeLfo2PhaseOffset);
    }
    const auto lfoValue = clampFast((monoLfoValue == nullptr ? lfo.process() : (nativeLfo1PhaseOffset == 0.0f ? monoLfoValue->value : lfo.getValue()))
                                            * clampFast(parameters.lfo.gain + destination(NativeModDestination::Lfo1Gain), 0.0f, 2.0f)
                                        + parameters.lfo.offset + destination(NativeModDestination::Lfo1Offset),
                                    -2.0f, 2.0f);
    nativeLfo2Value = clampFast((monoLfo2Value == nullptr ? lfo2.process() : (nativeLfo2PhaseOffset == 0.0f ? monoLfo2Value->value : lfo2.getValue()))
                                        * clampFast(parameters.lfo2.gain + destination(NativeModDestination::Lfo2Gain), 0.0f, 2.0f)
                                    + parameters.lfo2.offset + destination(NativeModDestination::Lfo2Offset),
                                -2.0f, 2.0f);
    const auto rampValue = ramp.process(parameters.ramp, parameters.tempoBpm);
    lastRampValue = rampValue;
    lastTransModSums = evaluateTransMod(parameters, lfoValue, rampValue, mod, ampValues[0], note, glidedVelocity);
    nativeModulation.fill(0.0f);
    for (const auto& slot : parameters.transMod.slots)
    {
        if (!slot.enabled || slot.source == ModSource::None) continue;
        const auto source = evalModSource(parameters, slot.source, lfoValue, rampValue, mod, ampValues[0], note, glidedVelocity);
        const auto scaler = slot.scaler == ModSource::None ? 1.0f
                                                           : evalModSource(parameters, slot.scaler, lfoValue, rampValue, mod, ampValues[0], note, glidedVelocity);
        const auto amount = source * scaler;
        if (!std::isfinite(amount)) continue;
        for (std::size_t index = 0; index < nativeModulation.size(); ++index)
            nativeModulation[index] += amount * slot.nativeDepths[index];
    }
    const auto keytrack = (note - 60.0f) / 12.0f;
    const auto pitch = keytrack * parameters.direct.oscKeytrackSemitones
        + lfoValue * parameters.direct.oscLfoSemitones + mod * parameters.direct.oscModEnvSemitones
        + lastTransModSums.oscPitchSemitones
        + parameters.performance.pitchBend * parameters.pitchBendRange;
    const auto cutoff = (note - 60.0f) * parameters.direct.filterKeytrack
        + lfoValue * parameters.direct.filterLfoSemitones + mod * parameters.direct.filterModEnvSemitones
        + lastTransModSums.filterCutoffSemitones;
    lastDirectSums.oscPitchSemitones = pitch - lastTransModSums.oscPitchSemitones;
    lastDirectSums.filterCutoffSemitones = cutoff - lastTransModSums.filterCutoffSemitones;
    std::array<StereoFrame, layerCount> sourceParts {};
    for (int partIndex = 0; partIndex < layerCount; ++partIndex)
    {
        const auto part = static_cast<std::size_t>(partIndex);
        for (int oscillatorIndex = 0; oscillatorIndex < oscillatorSlotsPerLayer; ++oscillatorIndex)
        {
            const auto index = partIndex * oscillatorSlotsPerLayer + oscillatorIndex;
            const auto offset = static_cast<std::size_t>(index * 5);
            auto oscillatorParameters = parameters.layers[part].oscillators[static_cast<std::size_t>(oscillatorIndex)];
            oscillatorParameters.level *= decibelsToGain(clampFast(nativeModulation[offset + 1], -24.0f, 24.0f));
            oscillatorParameters.pan = clampFast(oscillatorParameters.pan + nativeModulation[offset + 2], -1.0f, 1.0f);
            oscillatorParameters.detune = clampUnitFast(oscillatorParameters.detune + nativeModulation[offset + 3]);
            const auto frame = layerOscillators[static_cast<std::size_t>(index)].renderNative(note, oscillatorParameters,
                                                                                              pitch + clampFast(nativeModulation[offset], -96.0f, 96.0f), nativeModulation[offset + 4]);
            sourceParts[part].left += frame.left;
            sourceParts[part].right += frame.right;
        }
    }
    StereoFrame output;
    const auto solo = parameters.layers[0].solo || parameters.layers[1].solo;
    for (int partIndex = 0; partIndex < layerCount; ++partIndex)
    {
        const auto part = static_cast<std::size_t>(partIndex);
        const auto& layer = parameters.layers[part];
        if (layer.mute || (solo && !layer.solo)) continue;
        StereoFrame input;
        if (layer.input == FilterInput::A || layer.input == FilterInput::AB) input = sourceParts[0];
        if (layer.input == FilterInput::B || layer.input == FilterInput::AB)
        {
            input.left += sourceParts[1].left;
            input.right += sourceParts[1].right;
        }
        const auto filterOffset = static_cast<std::size_t>(20 + partIndex * 3);
        auto settings = preparedPartFilters[part];
        settings.resonance = clampUnitFast(settings.resonance + nativeModulation[filterOffset + 1]
                                           + destination(NativeModDestination::FilterResonanceAB));
        settings.drive = clampUnitFast(settings.drive + nativeModulation[filterOffset + 2]);
        for (auto& channel : partFilters[part]) channel.prepareBlock(settings);
        const auto cutoffModulation = cutoff + clampFast(nativeModulation[filterOffset], -136.0f, 136.0f);
        input.left = partFilters[part][0].processPrepared(input.left, note, settings, cutoffModulation);
        input.right = partFilters[part][1].processPrepared(input.right, note, settings, cutoffModulation);
        const auto layerOffset = static_cast<std::size_t>(26 + partIndex * 2);
        const auto levelDb = layer.levelDb + clampFast(nativeModulation[layerOffset], -24.0f, 24.0f)
            + lastTransModSums.ampLevelDb;
        const auto gain = ampValues[part] * (0.35f + 0.65f * glidedVelocity)
            * (levelDb <= -48.0f ? 0.0f : decibelsToGain(levelDb));
        const auto pan = clampFast(layer.pan + nativeModulation[layerOffset + 1] + lastTransModSums.pan, -1.0f, 1.0f);
        // A stereo balance preserves the independently rendered channels at center.
        output.left += input.left * gain * (pan >= 1.0f ? 0.0f : (pan > 0.0f ? std::cos(pan * halfPi) : 1.0f));
        output.right += input.right * gain * (pan <= -1.0f ? 0.0f : (pan < 0.0f ? std::cos(-pan * halfPi) : 1.0f));
    }
    output.phaserCenterOffsetHz = clampFast(destination(NativeModDestination::PhaserCenterFrequency), -20000.0f, 20000.0f);
    output.left = sanitize(output.left);
    output.right = sanitize(output.right);
    if (stopFadeSamples > 0)
    {
        const auto fade = static_cast<float>(stopFadeSamples) / static_cast<float>(std::max(1, stopFadeTotalSamples));
        output.left *= fade;
        output.right *= fade;
        if (--stopFadeSamples <= 0) reset();
    }
    return output;
}

void Voice::syncModulators(const SynthParameters& parameters) noexcept
{
    if (state != VoiceState::Idle)
        syncModulatorConfig(parameters);
}

void Voice::resetLayerOscillators(const SynthParameters& parameters, float fallbackPhase) noexcept
{
    for (int part = 0; part < layerCount; ++part)
    {
        for (int index = 0; index < oscillatorSlotsPerLayer; ++index)
        {
            const auto& slot = parameters.layers[static_cast<std::size_t>(part)].oscillators[static_cast<std::size_t>(index)];
            if (!slot.retrigger) continue;
            const auto phase = std::isfinite(slot.phaseDegrees) ? slot.phaseDegrees / 360.0f : fallbackPhase;
            layerOscillators[static_cast<std::size_t>(layerOscillatorIndex(part, index))].resetNativePhase(phase);
        }
    }
}

float Voice::processGlide(const SynthParameters& parameters) noexcept
{
    if (glideTotalSamples <= 0)
    {
        currentMidiNote = targetMidiNote;
        return currentMidiNote;
    }

    const auto phase = glideTotalSamples <= 1
        ? 1.0f
        : clampUnitFast(static_cast<float>(glideSamples) / static_cast<float>(glideTotalSamples - 1));
    currentMidiNote = startMidiNote + (targetMidiNote - startMidiNote) * phase;
    if (glideSamples < glideTotalSamples - 1)
        ++glideSamples;
    else
        currentMidiNote = targetMidiNote;

    if (!std::isfinite(currentMidiNote))
        currentMidiNote = static_cast<float>(midiNote);

    (void)parameters;
    return currentMidiNote;
}

float Voice::processVelocityGlide(const SynthParameters& parameters) noexcept
{
    if (velocityGlideTotalSamples <= 0)
    {
        currentVelocity = velocity;
        return currentVelocity;
    }

    const auto phase = velocityGlideTotalSamples <= 1
        ? 1.0f
        : clampUnitFast(static_cast<float>(velocityGlideSamples) / static_cast<float>(velocityGlideTotalSamples - 1));
    currentVelocity = startVelocity + (velocity - startVelocity) * phase;
    if (velocityGlideSamples < velocityGlideTotalSamples - 1)
        ++velocityGlideSamples;
    else
        currentVelocity = velocity;

    if (!std::isfinite(currentVelocity))
        currentVelocity = velocity;

    currentVelocity = clampUnitFast(currentVelocity);
    (void)parameters;
    return currentVelocity;
}

float Voice::evalModSource(const SynthParameters& parameters, ModSource source, float lfoValue, float rampValue,
                           float modEnvValue, float ampEnvValue, float effectiveNote,
                           float velocityGlideValue) const noexcept
{
    switch (source)
    {
        case ModSource::None:
            return 0.0f;
        case ModSource::Lfo:
            return clampFast(lfoValue, -2.0f, 2.0f);
        case ModSource::Lfo2:
            return nativeLfo2Value;
        case ModSource::ModEnv2:
            return nativeModEnv2Value;
        case ModSource::AmpEnv2:
            return nativeAmpEnv2Value;
        case ModSource::StepVelocity:
            return parameters.performance.stepVelocity;
        case ModSource::Ramp:
            return clampUnitFast(rampValue);
        case ModSource::ModEnv:
            return clampUnitFast(modEnvValue);
        case ModSource::AmpEnv:
            return clampUnitFast(ampEnvValue);
        case ModSource::Keytrack:
            return clampFast((effectiveNote - 60.0f) / 36.0f, -1.0f, 1.0f);
        case ModSource::Velocity:
            return clampUnitFast(velocity);
        case ModSource::VelocityGlide:
            return clampUnitFast(velocityGlideValue);
        case ModSource::PitchBend:
            return clampFast(parameters.performance.pitchBend, -1.0f, 1.0f);
        case ModSource::ModWheel:
            return clampUnitFast(parameters.performance.modWheel);
        case ModSource::Aftertouch:
            return clampUnitFast(parameters.performance.aftertouch);
        case ModSource::VoiceUni:
            return cachedVoiceUni;
        case ModSource::VoiceBi:
            return cachedVoiceBi;
        case ModSource::UnisonUni:
            return cachedUnisonUni;
        case ModSource::UnisonBi:
            return cachedUnisonBi;
        case ModSource::RandomOnNote:
            return clampFast(randomOnNote, -1.0f, 1.0f);
        case ModSource::Macro1:
            return clampUnitFast(parameters.macro.motion);
        case ModSource::Macro2:
            return clampUnitFast(parameters.macro.width);
        case ModSource::Macro3:
            return clampUnitFast(parameters.macro.drive);
        case ModSource::Macro4:
            return clampUnitFast(parameters.macro.space);
    }

    return 0.0f;
}

Voice::ModulationSums Voice::evaluateTransMod(const SynthParameters& parameters, float lfoValue,
                                              float rampValue, float modEnvValue, float ampEnvValue,
                                              float effectiveNote, float velocityGlideValue) const noexcept
{
    ModulationSums sums;
    auto accumulateSlot = [&sums, &parameters, lfoValue, rampValue, modEnvValue, ampEnvValue, effectiveNote,
                           velocityGlideValue, this](const TransModSlotParameters& slot) noexcept {
        if (!slot.enabled || slot.source == ModSource::None)
            return;

        const auto source = evalModSource(parameters, slot.source, lfoValue, rampValue, modEnvValue, ampEnvValue,
                                          effectiveNote, velocityGlideValue);
        const auto scaler = slot.scaler == ModSource::None
            ? 1.0f
            : evalModSource(parameters, slot.scaler, lfoValue, rampValue, modEnvValue, ampEnvValue,
                            effectiveNote, velocityGlideValue);
        const auto amount = source * scaler;
        if (!std::isfinite(amount))
            return;

        sums.oscPitchSemitones += amount * clampFast(slot.oscPitchSemitones, -48.0f, 48.0f);
        sums.pulseWidth += amount * clampFast(slot.pulseWidth, -1.0f, 1.0f);
        sums.filterCutoffSemitones += amount
            * (clampFast(slot.filterCutoffSemitones, -72.0f, 72.0f) + clampFast(slot.depth, -1.0f, 1.0f) * 72.0f);
        sums.ampLevelDb += amount * clampFast(slot.ampLevelDb, -48.0f, 48.0f);
        sums.pan += amount * clampFast(slot.pan, -1.0f, 1.0f);
    };

    if (parameters.transMod.activeSlotCacheValid)
    {
        const auto activeSlotCount = clampIntFast(parameters.transMod.activeSlotCount, 0, transModSlotCount);
        if (activeSlotCount <= 0)
            return {};

        for (int slotIndex = 0; slotIndex < activeSlotCount; ++slotIndex)
        {
            const auto& slot = parameters.transMod.activeSlots[static_cast<std::size_t>(slotIndex)];
            const auto source = evalModSource(parameters, slot.source, lfoValue, rampValue, modEnvValue,
                                              ampEnvValue, effectiveNote, velocityGlideValue);
            const auto scaler = slot.scaler == ModSource::None
                ? 1.0f
                : evalModSource(parameters, slot.scaler, lfoValue, rampValue, modEnvValue,
                                ampEnvValue, effectiveNote, velocityGlideValue);
            const auto amount = source * scaler;
            if (!std::isfinite(amount))
                continue;

            sums.oscPitchSemitones += amount * slot.oscPitchSemitones;
            sums.pulseWidth += amount * slot.pulseWidth;
            sums.filterCutoffSemitones += amount * (slot.filterCutoffSemitones + slot.depth * 72.0f);
            sums.ampLevelDb += amount * slot.ampLevelDb;
            sums.pan += amount * slot.pan;
        }
    }
    else
    {
        for (const auto& slot : parameters.transMod.slots)
            accumulateSlot(slot);
    }

    sums.oscPitchSemitones = clampFast(sanitize(sums.oscPitchSemitones), -96.0f, 96.0f);
    sums.pulseWidth = clampFast(sanitize(sums.pulseWidth), -1.0f, 1.0f);
    sums.filterCutoffSemitones = clampFast(sanitize(sums.filterCutoffSemitones), -136.0f, 136.0f);
    sums.ampLevelDb = clampFast(sanitize(sums.ampLevelDb), -24.0f, 24.0f);
    sums.pan = clampFast(sanitize(sums.pan), -2.0f, 2.0f);
    return sums;
}

VoiceSnapshot Voice::snapshot() const noexcept
{
    return {
        state,
        midiNote,
        velocity,
        partAmpEnvelopes[0].getValue(),
        modEnvelope.getValue(),
        lfo.getValue(),
        lastRampValue,
        randomOnNote,
        currentMidiNote,
        currentVelocity,
        cachedVoiceUni,
        cachedVoiceBi,
        cachedUnisonUni,
        cachedUnisonBi,
        lastDirectSums.oscPitchSemitones,
        lastDirectSums.pulseWidth,
        lastDirectSums.filterCutoffSemitones,
        lastTransModSums.oscPitchSemitones,
        lastTransModSums.pulseWidth,
        lastTransModSums.filterCutoffSemitones,
        lastTransModSums.ampLevelDb,
        lastTransModSums.pan,
        lfo.getPhase()
    };
}
} // namespace synth
