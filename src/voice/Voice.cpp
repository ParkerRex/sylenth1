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
        default: return 1.0f;
    }
}

float effectiveLfoRateHz(const SynthParameters& parameters) noexcept
{
    if (parameters.lfo.rateMode == LfoRateMode::Hz)
        return parameters.lfo.rateHz;

    const auto beats = std::max(0.01f, syncDivisionBeats(parameters.lfo.syncDivision));
    return std::clamp(parameters.tempoBpm, 20.0f, 300.0f) / (60.0f * beats);
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

bool isBlockConstantModSource(ModSource source) noexcept
{
    switch (source)
    {
        case ModSource::None:
        case ModSource::Velocity:
        case ModSource::PitchBend:
        case ModSource::ModWheel:
        case ModSource::Aftertouch:
        case ModSource::VoiceUni:
        case ModSource::VoiceBi:
        case ModSource::UnisonUni:
        case ModSource::UnisonBi:
        case ModSource::RandomOnNote:
        case ModSource::Macro1:
        case ModSource::Macro2:
        case ModSource::Macro3:
        case ModSource::Macro4:
            return true;
        case ModSource::Lfo:
        case ModSource::Ramp:
        case ModSource::ModEnv:
        case ModSource::AmpEnv:
        case ModSource::Keytrack:
        case ModSource::VelocityGlide:
            return false;
    }

    return false;
}

bool sameEnvelopeParameters(const EnvelopeParameters& before, const EnvelopeParameters& after) noexcept
{
    return std::abs(before.attackMs - after.attackMs) <= 0.000001f
        && std::abs(before.decayMs - after.decayMs) <= 0.000001f
        && std::abs(before.sustain - after.sustain) <= 0.000001f
        && std::abs(before.releaseMs - after.releaseMs) <= 0.000001f;
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

bool isLegacyOscillatorSlot(int layerIndex, int oscillatorIndex) noexcept
{
    return layerIndex == 0 && oscillatorIndex == 0;
}

bool hasSoloLayer(const SynthParameters& parameters) noexcept
{
    for (const auto& layer : parameters.layers)
    {
        if (layer.enabled && layer.solo)
            return true;
    }

    return false;
}

bool shouldRenderLayer(const LayerParameters& layer, bool soloActive) noexcept
{
    if (!layer.enabled || layer.mute)
        return false;

    return !soloActive || layer.solo;
}

bool shouldRenderOscillatorSlot(const LayerOscillatorParameters& oscillator) noexcept
{
    return oscillator.enabled && oscillator.voices > 0 && oscillator.level > 0.0f;
}

OscillatorParameters toSlotOscillatorParameters(const LayerOscillatorParameters& slot,
                                                const SynthParameters& parameters) noexcept
{
    OscillatorParameters oscillator;
    oscillator.pitchSemitones = static_cast<float>(slot.octave * 12 + slot.note);
    oscillator.fineCents = slot.fineCents;
    oscillator.stackCount = clampIntFast(slot.voices, 1, 8);
    oscillator.stackDetune = slot.detune;
    oscillator.pulseWidth = parameters.osc.pulseWidth;
    oscillator.subWave = parameters.osc.subWave;
    oscillator.subOctave = 1;
    oscillator.subPulseWidth = parameters.osc.subPulseWidth;

    switch (slot.waveform)
    {
        case OscillatorSlotWaveform::Saw:
            oscillator.sawLevel = 1.0f;
            break;
        case OscillatorSlotWaveform::Pulse:
            oscillator.sawLevel = 0.0f;
            oscillator.pulseLevel = 1.0f;
            break;
        case OscillatorSlotWaveform::Noise:
            oscillator.sawLevel = 0.0f;
            oscillator.noiseLevel = 1.0f;
            break;
        case OscillatorSlotWaveform::Sub:
            oscillator.sawLevel = 0.0f;
            oscillator.subLevel = 1.0f;
            break;
    }

    return oscillator;
}
} // namespace

void Voice::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    ampEnvelope.prepare(sampleRate);
    ampEnvelope.setSettings({ 2.0f, 300.0f, 0.0f, 180.0f });

    modEnvelope.prepare(sampleRate);
    modEnvelope.setSettings({ 1.0f, 400.0f, 0.0f, 160.0f });

    lfo.prepare(sampleRate);
    lfo.setShape(LfoShape::SawDown);
    lfo.setRateHz(2.0f);

    oscillator.prepare(sampleRate);
    for (auto& layerOscillator : layerOscillators)
        layerOscillator.prepare(sampleRate);
    ramp.prepare(sampleRate);
    filter.prepare(sampleRate);
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
                   bool allowGlide, bool retriggerModulators) noexcept
{
    const auto wasActive = state != VoiceState::Idle && midiNote >= 0;
    const auto shouldGlide = wasActive && allowGlide;
    const auto shouldRetrigger = !wasActive || retriggerModulators;
    const auto previousNote = wasActive ? currentMidiNote : static_cast<float>(note);
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

    randomOnNote = randomValue;
    stopFadeSamples = 0;
    stopFadeTotalSamples = 0;
    setAllocationIndices(voiceIndex, voiceCount, newUnisonIndex, newUnisonCount);
    syncModulatorConfig(parameters);
    if (shouldRetrigger)
    {
        ampEnvelope.noteOn();
        modEnvelope.noteOn();
        if (parameters.lfo.gateMode == LfoGateMode::Poly || parameters.lfo.gateMode == LfoGateMode::PolyOn)
            lfo.resetPhase();
        if (parameters.osc.phaseReset == 3)
            oscillator.reset(randomOnNote);
        else if (parameters.osc.phaseReset == 1 || parameters.osc.phaseReset == 2)
            oscillator.reset(0.0f);
        resetLayerOscillators(parameters, randomOnNote);
        ramp.noteOn();
        filter.reset();
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
    ampEnvelope.noteOff();
    modEnvelope.noteOff();
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
    ampEnvelope.reset();
    modEnvelope.reset();
    lfo.resetPhase();
    ramp.reset();
    for (auto& layerOscillator : layerOscillators)
        layerOscillator.reset(0.0f);
    filter.reset();
    lastDirectSums = {};
    lastTransModSums = {};
    lastRampValue = 0.0f;
    modulatorConfigInitialized = false;
    preparedTransModCount = -1;
}

void Voice::process(int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        ampEnvelope.process();
        modEnvelope.process();
        lfo.process();
    }

    if (state == VoiceState::Releasing && !ampEnvelope.isActive() && stopFadeSamples <= 0)
        reset();
}

StereoFrame Voice::renderSample(const SynthParameters& parameters, const float* monoLfoValue) noexcept
{
    if (state == VoiceState::Idle)
        return {};

    if (!modulatorConfigInitialized)
        syncModulatorConfig(parameters);

    const auto amp = ampEnvelope.process();
    const auto mod = modEnvelope.process();
    const auto lfoValue = monoLfoValue != nullptr ? *monoLfoValue : lfo.process();
    const auto rampValue = ramp.process(parameters.ramp, parameters.tempoBpm);
    const auto effectiveNote = processGlide(parameters);
    const auto glidedVelocity = processVelocityGlide(parameters);

    if (state == VoiceState::Releasing && !ampEnvelope.isActive() && stopFadeSamples <= 0)
    {
        reset();
        return {};
    }

    const auto macroMotion = parameters.macro.motion - 0.5f;
    const auto keytrackSource = (effectiveNote - 60.0f) / 12.0f;
    lastDirectSums.oscPitchSemitones = keytrackSource * parameters.direct.oscKeytrackSemitones
        + lfoValue * parameters.direct.oscLfoSemitones
        + mod * parameters.direct.oscModEnvSemitones
        + macroMotion * 2.0f;
    lastDirectSums.pulseWidth = keytrackSource * parameters.direct.pulseKeytrack
        + lfoValue * parameters.direct.pulseLfo
        + mod * parameters.direct.pulseModEnv;
    lastDirectSums.filterCutoffSemitones = (effectiveNote - 60.0f) * parameters.direct.filterKeytrack
        + lfoValue * parameters.direct.filterLfoSemitones
        + mod * parameters.direct.filterModEnvSemitones
        + macroMotion * 12.0f;
    lastDirectSums.ampLevelDb = 0.0f;
    lastDirectSums.pan = 0.0f;

    if (preparedTransModCount == 0)
        lastTransModSums = ModulationSums {};
    else if (preparedTransModCount > 0)
        lastTransModSums = evaluatePreparedTransMod(parameters, lfoValue, rampValue, mod, amp,
                                                    effectiveNote, glidedVelocity);
    else
        lastTransModSums = parameters.transMod.activeSlotCacheValid && parameters.transMod.activeSlotCount <= 0
            ? ModulationSums {}
            : evaluateTransMod(parameters, lfoValue, rampValue, mod, amp, effectiveNote, glidedVelocity);
    lastRampValue = rampValue;

    const auto oscPitchMod = lastDirectSums.oscPitchSemitones
        + lastTransModSums.oscPitchSemitones;
    const auto pulseMod = lastDirectSums.pulseWidth
        + lastTransModSums.pulseWidth;
    const auto cutoffMod = lastDirectSums.filterCutoffSemitones
        + lastTransModSums.filterCutoffSemitones;

    const auto unisonBi = cachedUnisonBi;
    const auto analogPitchMod = randomOnNote * parameters.amp.analog * 0.07f
        + unisonBi * parameters.amp.analog * 0.12f;
    const auto layerMix = renderLayerOscillators(parameters, effectiveNote, oscPitchMod,
                                                 pulseMod, analogPitchMod, unisonBi);
    auto sample = layerMix.sample;
    sample = filter.processPrepared(sample, effectiveNote, parameters, cutoffMod);

    const auto ampDrive = clampUnitFast(parameters.amp.drive + parameters.macro.drive * 0.35f);
    if (!cachedAmpDriveValid || std::abs(cachedAmpDrive - ampDrive) > 0.000001f)
    {
        cachedAmpDrive = ampDrive;
        cachedAmpDriveGain = 1.0f + ampDrive * 7.0f;
        cachedAmpDriveNormalizer = softSaturate(cachedAmpDriveGain);
        cachedAmpDriveValid = true;
    }

    if (ampDrive > 0.0f)
    {
        sample = cachedAmpDriveNormalizer > 0.0f
            ? softSaturate(sample * cachedAmpDriveGain) / cachedAmpDriveNormalizer
            : sample;
    }
    sample *= amp * (0.35f + 0.65f * glidedVelocity);
    const auto ampGainDb = parameters.amp.levelDb + lastTransModSums.ampLevelDb;
    if (!cachedAmpGainValid || std::abs(cachedAmpGainDb - ampGainDb) > 0.000001f)
    {
        cachedAmpGainDb = ampGainDb;
        cachedAmpGain = decibelsToGain(ampGainDb);
        cachedAmpGainValid = true;
    }
    sample *= cachedAmpGain;

    const auto voiceSpread = randomOnNote * parameters.amp.panSpread * 0.85f
        + unisonBi * parameters.amp.unisonSpread * 0.7f
        + randomOnNote * parameters.macro.width * 0.25f;
    const auto pan = clampFast(parameters.amp.pan + voiceSpread + layerMix.pan + lastTransModSums.pan, -1.0f, 1.0f);
    if (!cachedPanValid || std::abs(cachedPan - pan) > 0.000001f)
    {
        cachedPan = pan;
        const auto angle = (pan + 1.0f) * 0.5f * halfPi;
        cachedPanLeftGain = std::cos(angle);
        cachedPanRightGain = std::sin(angle);
        cachedPanValid = true;
    }

    auto output = StereoFrame { sanitize(sample * cachedPanLeftGain), sanitize(sample * cachedPanRightGain) };
    if (stopFadeSamples > 0)
    {
        const auto fade = static_cast<float>(stopFadeSamples)
            / static_cast<float>(std::max(1, stopFadeTotalSamples));
        output.left *= fade;
        output.right *= fade;
        --stopFadeSamples;
        if (stopFadeSamples <= 0)
            reset();
    }

    return output;
}

bool Voice::renderBlock(const SynthParameters& parameters, float* accumLeft, float* accumRight,
                        float* normalizationWeights, const float* monoLfoValues, int numSamples) noexcept
{
    if (state == VoiceState::Idle)
        return false;

    if (numSamples <= 0)
        return true;

    if (!modulatorConfigInitialized)
        syncModulatorConfig(parameters);

    numSamples = std::min(numSamples, renderBlockMaxSamples);

    float noteArr[renderBlockMaxSamples];
    float oscPitchModArr[renderBlockMaxSamples];
    float pulseModArr[renderBlockMaxSamples];
    float cutoffModArr[renderBlockMaxSamples];
    float ampFactorArr[renderBlockMaxSamples];
    float transAmpDbArr[renderBlockMaxSamples];
    float transPanArr[renderBlockMaxSamples];
    float fadeArr[renderBlockMaxSamples];
    float mixBuf[renderBlockMaxSamples];
    float mixPanArr[renderBlockMaxSamples];

    const auto macroMotion = parameters.macro.motion - 0.5f;
    const auto unisonBi = cachedUnisonBi;
    const auto analogPitchMod = randomOnNote * parameters.amp.analog * 0.07f
        + unisonBi * parameters.amp.analog * 0.12f;

    // Pass A: modulators, glide, TransMod, and stop-fade lifecycle, in exact
    // per-sample order. Death by release end contributes weight but no audio for
    // its sample; death by stop-fade end contributes both. reset() is deferred
    // until after the audio passes because they consume member state.
    auto rendered = 0;
    auto died = false;
    for (int i = 0; i < numSamples; ++i)
    {
        normalizationWeights[i] += isStopFading() ? normalizationPowerWeight() : 1.0f;

        const auto amp = ampEnvelope.process();
        const auto mod = modEnvelope.process();
        const auto lfoValue = monoLfoValues != nullptr ? monoLfoValues[i] : lfo.process();
        const auto rampValue = ramp.process(parameters.ramp, parameters.tempoBpm);
        const auto effectiveNote = processGlide(parameters);
        const auto glidedVelocity = processVelocityGlide(parameters);

        if (state == VoiceState::Releasing && !ampEnvelope.isActive() && stopFadeSamples <= 0)
        {
            died = true;
            break;
        }

        const auto keytrackSource = (effectiveNote - 60.0f) / 12.0f;
        lastDirectSums.oscPitchSemitones = keytrackSource * parameters.direct.oscKeytrackSemitones
            + lfoValue * parameters.direct.oscLfoSemitones
            + mod * parameters.direct.oscModEnvSemitones
            + macroMotion * 2.0f;
        lastDirectSums.pulseWidth = keytrackSource * parameters.direct.pulseKeytrack
            + lfoValue * parameters.direct.pulseLfo
            + mod * parameters.direct.pulseModEnv;
        lastDirectSums.filterCutoffSemitones = (effectiveNote - 60.0f) * parameters.direct.filterKeytrack
            + lfoValue * parameters.direct.filterLfoSemitones
            + mod * parameters.direct.filterModEnvSemitones
            + macroMotion * 12.0f;
        lastDirectSums.ampLevelDb = 0.0f;
        lastDirectSums.pan = 0.0f;

        if (preparedTransModCount == 0)
            lastTransModSums = ModulationSums {};
        else if (preparedTransModCount > 0)
            lastTransModSums = evaluatePreparedTransMod(parameters, lfoValue, rampValue, mod, amp,
                                                        effectiveNote, glidedVelocity);
        else
            lastTransModSums = parameters.transMod.activeSlotCacheValid && parameters.transMod.activeSlotCount <= 0
                ? ModulationSums {}
                : evaluateTransMod(parameters, lfoValue, rampValue, mod, amp, effectiveNote, glidedVelocity);
        lastRampValue = rampValue;

        noteArr[i] = effectiveNote;
        oscPitchModArr[i] = lastDirectSums.oscPitchSemitones + lastTransModSums.oscPitchSemitones;
        pulseModArr[i] = lastDirectSums.pulseWidth + lastTransModSums.pulseWidth;
        cutoffModArr[i] = lastDirectSums.filterCutoffSemitones + lastTransModSums.filterCutoffSemitones;
        ampFactorArr[i] = amp * (0.35f + 0.65f * glidedVelocity);
        transAmpDbArr[i] = lastTransModSums.ampLevelDb;
        transPanArr[i] = lastTransModSums.pan;

        if (stopFadeSamples > 0)
        {
            fadeArr[i] = static_cast<float>(stopFadeSamples)
                / static_cast<float>(std::max(1, stopFadeTotalSamples));
            --stopFadeSamples;
            rendered = i + 1;
            if (stopFadeSamples <= 0)
            {
                died = true;
                break;
            }
        }
        else
        {
            fadeArr[i] = 1.0f;
            rendered = i + 1;
        }
    }

    if (rendered > 0)
    {
        // Pass B: oscillator mix across the block, slot-major so each stack's
        // phase state stays hot. Accumulation order per sample matches the
        // per-sample slot loop.
        if (parameters.oscillatorRender.cacheValid)
        {
            const auto activeSlotCount = clampIntFast(parameters.oscillatorRender.activeSlotCount,
                                                      0, preparedOscillatorSlotCount);
            if (activeSlotCount <= 0)
            {
                for (int i = 0; i < rendered; ++i)
                {
                    mixBuf[i] = 0.0f;
                    mixPanArr[i] = 0.0f;
                }
            }
            else if (activeSlotCount == 1)
            {
                const auto& slot = parameters.oscillatorRender.activeSlots[0];
                auto& oscillatorStack = slot.legacy
                    ? oscillator
                    : layerOscillators[static_cast<std::size_t>(slot.oscillatorStateIndex)];
                if (slot.sawStackOnly)
                {
                    oscillatorStack.renderSawStackBlock(noteArr, oscPitchModArr, analogPitchMod,
                                                        slot.oscillator, slot.sawStackGain, mixBuf, rendered);
                }
                else
                {
                    for (int i = 0; i < rendered; ++i)
                        mixBuf[i] = oscillatorStack.renderSample(noteArr[i], slot.oscillator,
                                                                 oscPitchModArr[i] + analogPitchMod,
                                                                 pulseModArr[i]);
                }

                const auto slotPan = clampFast(slot.pan + unisonBi * slot.stereo * 0.65f, -1.0f, 1.0f);
                for (int i = 0; i < rendered; ++i)
                {
                    auto slotSample = mixBuf[i];
                    if (slot.invert)
                        slotSample = -slotSample;

                    mixBuf[i] = sanitize(slotSample * slot.gain);
                    mixPanArr[i] = slotPan;
                }
            }
            else
            {
                for (int i = 0; i < rendered; ++i)
                    mixBuf[i] = 0.0f;

                auto panWeight = 0.0f;
                auto weightedPan = 0.0f;
                float slotBuf[renderBlockMaxSamples];
                for (int slotIndex = 0; slotIndex < activeSlotCount; ++slotIndex)
                {
                    const auto& slot =
                        parameters.oscillatorRender.activeSlots[static_cast<std::size_t>(slotIndex)];
                    auto& oscillatorStack = slot.legacy
                        ? oscillator
                        : layerOscillators[static_cast<std::size_t>(slot.oscillatorStateIndex)];
                    if (slot.sawStackOnly)
                    {
                        oscillatorStack.renderSawStackBlock(noteArr, oscPitchModArr, analogPitchMod,
                                                            slot.oscillator, slot.sawStackGain,
                                                            slotBuf, rendered);
                    }
                    else
                    {
                        for (int i = 0; i < rendered; ++i)
                            slotBuf[i] = oscillatorStack.renderSample(noteArr[i], slot.oscillator,
                                                                      oscPitchModArr[i] + analogPitchMod,
                                                                      pulseModArr[i]);
                    }

                    for (int i = 0; i < rendered; ++i)
                    {
                        auto slotSample = slotBuf[i];
                        if (slot.invert)
                            slotSample = -slotSample;

                        mixBuf[i] += slotSample * slot.gain;
                    }

                    weightedPan += slot.weightedPanBase + unisonBi * slot.stereo * 0.65f * slot.gain;
                    panWeight += slot.panWeight;
                }

                const auto slotScale = inverseSqrtForCount(activeSlotCount);
                const auto mixPan = panWeight > 0.0f
                    ? clampFast(weightedPan / panWeight, -1.0f, 1.0f)
                    : 0.0f;
                for (int i = 0; i < rendered; ++i)
                {
                    mixBuf[i] = sanitize(mixBuf[i] * slotScale);
                    mixPanArr[i] = mixPan;
                }
            }
        }
        else
        {
            for (int i = 0; i < rendered; ++i)
            {
                const auto layerMix = renderLayerOscillators(parameters, noteArr[i], oscPitchModArr[i],
                                                             pulseModArr[i], analogPitchMod, unisonBi);
                mixBuf[i] = layerMix.sample;
                mixPanArr[i] = layerMix.pan;
            }
        }

        // Pass C: filter across the block.
        filter.processPreparedBlock(mixBuf, noteArr, parameters, cutoffModArr, rendered);

        // Pass D: drive, gain, pan, fade, and accumulation, with the same
        // per-sample cache behavior as renderSample.
        const auto ampDrive = clampUnitFast(parameters.amp.drive + parameters.macro.drive * 0.35f);
        if (!cachedAmpDriveValid || std::abs(cachedAmpDrive - ampDrive) > 0.000001f)
        {
            cachedAmpDrive = ampDrive;
            cachedAmpDriveGain = 1.0f + ampDrive * 7.0f;
            cachedAmpDriveNormalizer = softSaturate(cachedAmpDriveGain);
            cachedAmpDriveValid = true;
        }

        const auto applyDrive = ampDrive > 0.0f && cachedAmpDriveNormalizer > 0.0f;
        const auto voiceSpread = randomOnNote * parameters.amp.panSpread * 0.85f
            + unisonBi * parameters.amp.unisonSpread * 0.7f
            + randomOnNote * parameters.macro.width * 0.25f;
        const auto panBase = parameters.amp.pan + voiceSpread;
        for (int i = 0; i < rendered; ++i)
        {
            auto sample = mixBuf[i];
            if (applyDrive)
                sample = softSaturate(sample * cachedAmpDriveGain) / cachedAmpDriveNormalizer;

            sample *= ampFactorArr[i];
            const auto ampGainDb = parameters.amp.levelDb + transAmpDbArr[i];
            if (!cachedAmpGainValid || std::abs(cachedAmpGainDb - ampGainDb) > 0.000001f)
            {
                cachedAmpGainDb = ampGainDb;
                cachedAmpGain = decibelsToGain(ampGainDb);
                cachedAmpGainValid = true;
            }
            sample *= cachedAmpGain;

            const auto pan = clampFast(panBase + mixPanArr[i] + transPanArr[i], -1.0f, 1.0f);
            if (!cachedPanValid || std::abs(cachedPan - pan) > 0.000001f)
            {
                cachedPan = pan;
                const auto angle = (pan + 1.0f) * 0.5f * halfPi;
                cachedPanLeftGain = std::cos(angle);
                cachedPanRightGain = std::sin(angle);
                cachedPanValid = true;
            }

            const auto fade = fadeArr[i];
            accumLeft[i] += sanitize(sample * cachedPanLeftGain) * fade;
            accumRight[i] += sanitize(sample * cachedPanRightGain) * fade;
        }
    }

    if (died)
        reset();

    return state != VoiceState::Idle;
}

void Voice::syncModulators(const SynthParameters& parameters) noexcept
{
    if (state != VoiceState::Idle)
        syncModulatorConfig(parameters);
}

void Voice::syncModulatorConfig(const SynthParameters& parameters) noexcept
{
    if (!modulatorConfigInitialized || !sameEnvelopeParameters(cachedAmpEnv, parameters.ampEnv))
    {
        ampEnvelope.setSettings(toEnvelopeSettings(parameters.ampEnv));
        cachedAmpEnv = parameters.ampEnv;
    }

    if (!modulatorConfigInitialized || !sameEnvelopeParameters(cachedModEnv, parameters.modEnv))
    {
        modEnvelope.setSettings(toEnvelopeSettings(parameters.modEnv));
        cachedModEnv = parameters.modEnv;
    }

    const auto lfoConfigChanged = !modulatorConfigInitialized
        || cachedLfoShape != parameters.lfo.shape
        || cachedLfoRateMode != parameters.lfo.rateMode
        || cachedLfoSyncDivision != parameters.lfo.syncDivision
        || std::abs(cachedLfoRateHz - parameters.lfo.rateHz) > 0.000001f
        || std::abs(cachedLfoPhaseDegrees - parameters.lfo.phaseDegrees) > 0.000001f
        || std::abs(cachedTempoBpm - parameters.tempoBpm) > 0.000001f;

    if (lfoConfigChanged)
    {
        lfo.setShape(toLfoShape(parameters.lfo.shape));
        lfo.setRateHz(effectiveLfoRateHz(parameters));
        lfo.setPhaseDegrees(parameters.lfo.phaseDegrees);
        cachedLfoShape = parameters.lfo.shape;
        cachedLfoRateMode = parameters.lfo.rateMode;
        cachedLfoSyncDivision = parameters.lfo.syncDivision;
        cachedLfoRateHz = parameters.lfo.rateHz;
        cachedLfoPhaseDegrees = parameters.lfo.phaseDegrees;
        cachedTempoBpm = parameters.tempoBpm;
    }

    // Step-table updates bypass the config-change gate: copying 16 floats is
    // cheaper than comparing them, and live step edits must not reset phase.
    lfo.setSteps(parameters.lfo.steps.data(), parameters.lfo.stepCount, parameters.lfo.stepSmooth);

    filter.prepareBlock(parameters);
    prepareTransModSlots(parameters);
    modulatorConfigInitialized = true;
}

void Voice::resetLayerOscillators(const SynthParameters& parameters, float fallbackPhase) noexcept
{
    const auto fallbackNormalizedPhase = (clampFast(fallbackPhase, -1.0f, 1.0f) + 1.0f) * 0.5f;
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
    {
        const auto& layer = parameters.layers[static_cast<std::size_t>(layerIndex)];
        for (int oscillatorIndex = 0; oscillatorIndex < oscillatorSlotsPerLayer; ++oscillatorIndex)
        {
            if (isLegacyOscillatorSlot(layerIndex, oscillatorIndex))
                continue;

            const auto& slot = layer.oscillators[static_cast<std::size_t>(oscillatorIndex)];
            if (!slot.retrigger)
                continue;

            const auto phase = std::isfinite(slot.phaseDegrees)
                ? slot.phaseDegrees / 360.0f
                : fallbackNormalizedPhase;
            layerOscillators[static_cast<std::size_t>(layerOscillatorIndex(layerIndex, oscillatorIndex))]
                .resetToPhase(phase);
        }
    }
}

Voice::LayerOscillatorMix Voice::renderLayerOscillators(const SynthParameters& parameters, float effectiveNote,
                                                        float pitchModSemitones, float pulseWidthMod,
                                                        float analogPitchMod, float unisonBi) noexcept
{
    LayerOscillatorMix mix;
    if (parameters.oscillatorRender.cacheValid)
    {
        const auto activeSlotCount = clampIntFast(parameters.oscillatorRender.activeSlotCount,
                                                  0, preparedOscillatorSlotCount);
        if (activeSlotCount <= 0)
            return {};

        if (activeSlotCount == 1)
        {
            const auto& slot = parameters.oscillatorRender.activeSlots[0];
            auto& oscillatorStack = slot.legacy
                ? oscillator
                : layerOscillators[static_cast<std::size_t>(slot.oscillatorStateIndex)];
            const auto effectivePitchMod = pitchModSemitones + analogPitchMod;
            auto slotSample = slot.sawStackOnly
                ? oscillatorStack.renderSawStack(effectiveNote, slot.oscillator, effectivePitchMod, slot.sawStackGain)
                : oscillatorStack.renderSample(effectiveNote, slot.oscillator, effectivePitchMod, pulseWidthMod);

            if (slot.invert)
                slotSample = -slotSample;

            mix.sample = sanitize(slotSample * slot.gain);
            mix.pan = clampFast(slot.pan + unisonBi * slot.stereo * 0.65f, -1.0f, 1.0f);
            return mix;
        }

        auto panWeight = 0.0f;
        auto weightedPan = 0.0f;
        for (int slotIndex = 0; slotIndex < activeSlotCount; ++slotIndex)
        {
            const auto& slot = parameters.oscillatorRender.activeSlots[static_cast<std::size_t>(slotIndex)];
            auto& oscillatorStack = slot.legacy
                ? oscillator
                : layerOscillators[static_cast<std::size_t>(slot.oscillatorStateIndex)];
            const auto effectivePitchMod = pitchModSemitones + analogPitchMod;
            auto slotSample = slot.sawStackOnly
                ? oscillatorStack.renderSawStack(effectiveNote, slot.oscillator, effectivePitchMod, slot.sawStackGain)
                : oscillatorStack.renderSample(effectiveNote, slot.oscillator, effectivePitchMod, pulseWidthMod);

            if (slot.invert)
                slotSample = -slotSample;

            mix.sample += slotSample * slot.gain;
            weightedPan += slot.weightedPanBase + unisonBi * slot.stereo * 0.65f * slot.gain;
            panWeight += slot.panWeight;
        }

        mix.sample *= inverseSqrtForCount(activeSlotCount);
        mix.sample = sanitize(mix.sample);
        mix.pan = panWeight > 0.0f ? clampFast(weightedPan / panWeight, -1.0f, 1.0f) : 0.0f;
        return mix;
    }

    const auto soloActive = hasSoloLayer(parameters);
    if (layerCount == 2 && oscillatorSlotsPerLayer == 2)
    {
        const auto& layerA = parameters.layers[0];
        const auto& layerB = parameters.layers[1];
        const auto& legacySlot = layerA.oscillators[0];
        if (shouldRenderLayer(layerA, soloActive)
            && shouldRenderOscillatorSlot(legacySlot)
            && !shouldRenderOscillatorSlot(layerA.oscillators[1])
            && !shouldRenderLayer(layerB, soloActive))
        {
            const auto gain = decibelsToGain(layerA.levelDb) * clampUnitFast(legacySlot.level);
            mix.sample = oscillator.renderSample(effectiveNote, parameters,
                                                 pitchModSemitones + analogPitchMod,
                                                 pulseWidthMod) * gain;
            if (legacySlot.invert)
                mix.sample = -mix.sample;

            mix.sample = sanitize(mix.sample);
            mix.pan = clampFast(layerA.pan + legacySlot.pan + unisonBi * legacySlot.stereo * 0.65f,
                                -1.0f, 1.0f);
            return mix;
        }
    }

    auto activeSlotCount = 0;
    auto panWeight = 0.0f;
    auto weightedPan = 0.0f;
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
    {
        const auto& layer = parameters.layers[static_cast<std::size_t>(layerIndex)];
        if (!shouldRenderLayer(layer, soloActive))
            continue;

        const auto layerGain = decibelsToGain(layer.levelDb);
        for (int oscillatorIndex = 0; oscillatorIndex < oscillatorSlotsPerLayer; ++oscillatorIndex)
        {
            const auto& slot = layer.oscillators[static_cast<std::size_t>(oscillatorIndex)];
            if (!shouldRenderOscillatorSlot(slot))
                continue;

            ++activeSlotCount;
            const auto gain = layerGain * clampUnitFast(slot.level);
            auto slotSample = 0.0f;
            if (isLegacyOscillatorSlot(layerIndex, oscillatorIndex))
            {
                slotSample = oscillator.renderSample(effectiveNote, parameters,
                                                     pitchModSemitones + analogPitchMod,
                                                     pulseWidthMod);
            }
            else
            {
                const auto oscillatorParameters = toSlotOscillatorParameters(slot, parameters);
                slotSample = layerOscillators[static_cast<std::size_t>(layerOscillatorIndex(layerIndex, oscillatorIndex))]
                    .renderSample(effectiveNote, oscillatorParameters,
                                  pitchModSemitones + analogPitchMod,
                                  pulseWidthMod);
            }

            if (slot.invert)
                slotSample = -slotSample;

            mix.sample += slotSample * gain;
            const auto slotPan = clampFast(layer.pan + slot.pan + unisonBi * slot.stereo * 0.65f, -1.0f, 1.0f);
            weightedPan += slotPan * gain;
            panWeight += gain;
        }
    }

    if (activeSlotCount <= 0)
        return {};

    mix.sample *= inverseSqrtForCount(activeSlotCount);
    mix.sample = sanitize(mix.sample);
    mix.pan = panWeight > 0.0f ? clampFast(weightedPan / panWeight, -1.0f, 1.0f) : 0.0f;
    return mix;
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

    (void) parameters;
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
    (void) parameters;
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
            return clampFast(lfoValue, -1.0f, 1.0f);
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

void Voice::prepareTransModSlots(const SynthParameters& parameters) noexcept
{
    if (!parameters.transMod.activeSlotCacheValid)
    {
        preparedTransModCount = -1;
        return;
    }

    const auto activeSlotCount = clampIntFast(parameters.transMod.activeSlotCount, 0, transModSlotCount);
    preparedTransModCount = activeSlotCount;
    for (int slotIndex = 0; slotIndex < activeSlotCount; ++slotIndex)
    {
        const auto& slot = parameters.transMod.activeSlots[static_cast<std::size_t>(slotIndex)];
        auto& prepared = preparedTransModSlots[static_cast<std::size_t>(slotIndex)];
        prepared.source = slot.source;
        prepared.scaler = slot.scaler;
        prepared.sourceConstant = isBlockConstantModSource(slot.source);
        prepared.sourceValue = prepared.sourceConstant
            ? evalModSource(parameters, slot.source, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, 0.0f)
            : 0.0f;
        prepared.scalerConstant = slot.scaler == ModSource::None || isBlockConstantModSource(slot.scaler);
        prepared.scalerValue = slot.scaler == ModSource::None
            ? 1.0f
            : (prepared.scalerConstant
                   ? evalModSource(parameters, slot.scaler, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, 0.0f)
                   : 1.0f);
        prepared.oscPitchSemitones = slot.oscPitchSemitones;
        prepared.pulseWidth = slot.pulseWidth;
        prepared.filterCutoffSemitones = slot.filterCutoffSemitones + slot.depth * 72.0f;
        prepared.ampLevelDb = slot.ampLevelDb;
        prepared.pan = slot.pan;
    }
}

Voice::ModulationSums Voice::evaluatePreparedTransMod(const SynthParameters& parameters, float lfoValue,
                                                      float rampValue, float modEnvValue, float ampEnvValue,
                                                      float effectiveNote, float velocityGlideValue) const noexcept
{
    ModulationSums sums;
    for (int slotIndex = 0; slotIndex < preparedTransModCount; ++slotIndex)
    {
        const auto& slot = preparedTransModSlots[static_cast<std::size_t>(slotIndex)];
        const auto source = slot.sourceConstant
            ? slot.sourceValue
            : evalModSource(parameters, slot.source, lfoValue, rampValue, modEnvValue, ampEnvValue,
                            effectiveNote, velocityGlideValue);
        const auto scaler = slot.scalerConstant
            ? slot.scalerValue
            : evalModSource(parameters, slot.scaler, lfoValue, rampValue, modEnvValue, ampEnvValue,
                            effectiveNote, velocityGlideValue);
        const auto amount = source * scaler;
        if (!std::isfinite(amount))
            continue;

        sums.oscPitchSemitones += amount * slot.oscPitchSemitones;
        sums.pulseWidth += amount * slot.pulseWidth;
        sums.filterCutoffSemitones += amount * slot.filterCutoffSemitones;
        sums.ampLevelDb += amount * slot.ampLevelDb;
        sums.pan += amount * slot.pan;
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
        ampEnvelope.getValue(),
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
