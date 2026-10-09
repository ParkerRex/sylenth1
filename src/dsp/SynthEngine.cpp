#include "SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace synth
{
namespace
{
float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

bool directChordConfigChanged(const ChordParameters& before, const ChordParameters& after) noexcept
{
    if (before.enabled != after.enabled || before.voiceCount != after.voiceCount)
        return true;

    for (std::size_t index = 0; index < before.voices.size(); ++index)
    {
        const auto& beforeVoice = before.voices[index];
        const auto& afterVoice = after.voices[index];
        if (beforeVoice.enabled != afterVoice.enabled
            || beforeVoice.pitchSemitones != afterVoice.pitchSemitones
            || std::abs(beforeVoice.velocity - afterVoice.velocity) > 0.000001f)
        {
            return true;
        }
    }

    return false;
}

} // namespace

void SynthEngine::prepare(double newSampleRate, int newMaxBlockSize)
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlockSize = std::max(1, newMaxBlockSize);
    masterSmoothingSamples = std::max(1, static_cast<int>(sampleRate * 0.003));
    voices.prepare(sampleRate);
    fx.prepare(sampleRate, maxBlockSize);
    reset();
}

void SynthEngine::reset() noexcept
{
    masterCurrentGain = masterTargetGain;
    masterSmoothingRemaining = 0;
    performance = {};
    parameters.performance = performance;
    sustainPedalDown = false;
    arpeggiator.reset();
    resetInputNoteTracking();
    resetDirectChordTracking();
    voices.panic();
    fx.reset();
}

void SynthEngine::noteOn(int midiNote, float velocity) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(inputHeldNotes.size()))
        return;

    const auto noteIndex = static_cast<std::size_t>(midiNote);
    inputHeldNotes[noteIndex] = true;
    inputHeldVelocities[noteIndex] = std::clamp(velocity, 0.0f, 1.0f);
    inputHeldOrder[noteIndex] = ++inputOrderCounter;

    if (parameters.arp.enabled)
    {
        arpeggiator.noteOn(midiNote, velocity);
        return;
    }

    triggerDirectNoteOn(midiNote, velocity);
}

void SynthEngine::noteOff(int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(inputHeldNotes.size()))
        return;

    const auto noteIndex = static_cast<std::size_t>(midiNote);
    inputHeldNotes[noteIndex] = false;
    inputHeldVelocities[noteIndex] = 0.0f;
    inputHeldOrder[noteIndex] = 0;

    if (parameters.arp.enabled)
    {
        arpeggiator.noteOff(midiNote, parameters.arp.hold);
        return;
    }

    triggerDirectNoteOff(midiNote);
}

void SynthEngine::setSustainPedal(bool down) noexcept
{
    sustainPedalDown = down;
    voices.setSustainPedal(down);
}

void SynthEngine::setPitchBend(float normalizedBipolar) noexcept
{
    performance.pitchBend = std::clamp(finiteOr(normalizedBipolar, 0.0f), -1.0f, 1.0f);
    parameters.performance = performance;
}

void SynthEngine::setModWheel(float normalized) noexcept
{
    performance.modWheel = std::clamp(finiteOr(normalized, 0.0f), 0.0f, 1.0f);
    parameters.performance = performance;
}

void SynthEngine::setAftertouch(float normalized) noexcept
{
    performance.aftertouch = std::clamp(finiteOr(normalized, 0.0f), 0.0f, 1.0f);
    parameters.performance = performance;
}

void SynthEngine::allNotesOff() noexcept
{
    arpeggiator.reset();
    resetInputNoteTracking();
    resetDirectChordTracking();
    voices.allNotesOff();
}

void SynthEngine::panic() noexcept
{
    arpeggiator.reset();
    resetInputNoteTracking();
    resetDirectChordTracking();
    sustainPedalDown = false;
    voices.panic();
    fx.reset();
}

void SynthEngine::setParameters(const SynthParameters& newParameters) noexcept
{
    const SynthParameters defaults;
    const auto arpWasEnabled = parameters.arp.enabled;
    const auto arpHoldWasEnabled = parameters.arp.hold;
    const auto priorChord = parameters.chord;
    parameters = newParameters;
    parameters.performance = performance;
    parameters.voiceMode = static_cast<VoiceMode>(std::clamp(static_cast<int>(parameters.voiceMode), 0, 3));
    parameters.polyphony = std::clamp(parameters.polyphony, 1, 32);
    parameters.unisonCount = std::clamp(parameters.unisonCount, 1, 8);
    parameters.glideMs = std::clamp(finiteOr(parameters.glideMs, defaults.glideMs), 0.0f, 2000.0f);
    parameters.velocityGlideMs = std::clamp(finiteOr(parameters.velocityGlideMs, defaults.velocityGlideMs), 0.0f, 2000.0f);
    for (std::size_t layerIndex = 0; layerIndex < parameters.layers.size(); ++layerIndex)
    {
        auto& layer = parameters.layers[layerIndex];
        const auto& defaultLayer = defaults.layers[layerIndex];
        layer.levelDb = std::clamp(finiteOr(layer.levelDb, defaultLayer.levelDb), -48.0f, 12.0f);
        layer.pan = std::clamp(finiteOr(layer.pan, defaultLayer.pan), -1.0f, 1.0f);

        for (std::size_t oscillatorIndex = 0; oscillatorIndex < layer.oscillators.size(); ++oscillatorIndex)
        {
            auto& oscillator = layer.oscillators[oscillatorIndex];
            const auto& defaultOscillator = defaultLayer.oscillators[oscillatorIndex];
            oscillator.voices = std::clamp(oscillator.voices, 0, 8);
            oscillator.waveform = static_cast<OscillatorSlotWaveform>(
                std::clamp(static_cast<int>(oscillator.waveform), 0, 7));
            oscillator.octave = std::clamp(oscillator.octave, -4, 4);
            oscillator.note = std::clamp(oscillator.note, -12, 12);
            oscillator.fineCents = std::clamp(finiteOr(oscillator.fineCents, defaultOscillator.fineCents),
                                              -100.0f, 100.0f);
            oscillator.level = std::clamp(finiteOr(oscillator.level, defaultOscillator.level), 0.0f, 1.0f);
            oscillator.phaseDegrees = std::clamp(finiteOr(oscillator.phaseDegrees,
                                                          defaultOscillator.phaseDegrees),
                                                 0.0f, 360.0f);
            oscillator.detune = std::clamp(finiteOr(oscillator.detune, defaultOscillator.detune), 0.0f, 1.0f);
            oscillator.stereo = std::clamp(finiteOr(oscillator.stereo, defaultOscillator.stereo), 0.0f, 1.0f);
            oscillator.pan = std::clamp(finiteOr(oscillator.pan, defaultOscillator.pan), -1.0f, 1.0f);
        }
    }
    parameters.osc.pitchSemitones = std::clamp(finiteOr(parameters.osc.pitchSemitones, defaults.osc.pitchSemitones), -48.0f, 48.0f);
    parameters.osc.fineCents = std::clamp(finiteOr(parameters.osc.fineCents, defaults.osc.fineCents), -100.0f, 100.0f);
    parameters.osc.stackCount = std::clamp(parameters.osc.stackCount, 1, 8);
    parameters.osc.stackDetune = std::clamp(finiteOr(parameters.osc.stackDetune, defaults.osc.stackDetune), 0.0f, 1.0f);
    parameters.osc.sawLevel = std::clamp(finiteOr(parameters.osc.sawLevel, defaults.osc.sawLevel), 0.0f, 1.0f);
    parameters.osc.pulseLevel = std::clamp(finiteOr(parameters.osc.pulseLevel, defaults.osc.pulseLevel), 0.0f, 1.0f);
    parameters.osc.noiseLevel = std::clamp(finiteOr(parameters.osc.noiseLevel, defaults.osc.noiseLevel), 0.0f, 1.0f);
    parameters.osc.pulseWidth = std::clamp(finiteOr(parameters.osc.pulseWidth, defaults.osc.pulseWidth), 0.05f, 0.95f);
    parameters.osc.subLevel = std::clamp(finiteOr(parameters.osc.subLevel, defaults.osc.subLevel), 0.0f, 1.0f);
    parameters.osc.subPulseWidth = std::clamp(finiteOr(parameters.osc.subPulseWidth, defaults.osc.subPulseWidth), 0.05f, 0.95f);
    parameters.osc.syncAmount = std::clamp(finiteOr(parameters.osc.syncAmount, defaults.osc.syncAmount), 0.0f, 1.0f);
    parameters.osc.subWave = static_cast<SubWave>(std::clamp(static_cast<int>(parameters.osc.subWave), 0, 3));
    parameters.osc.subOctave = std::clamp(parameters.osc.subOctave, 1, 3);
    parameters.osc.phaseReset = std::clamp(parameters.osc.phaseReset, 0, 3);
    parameters.filter.mode = static_cast<FilterMode>(std::clamp(static_cast<int>(parameters.filter.mode), 0, 8));
    parameters.amp.drive = std::clamp(finiteOr(parameters.amp.drive, defaults.amp.drive), 0.0f, 1.0f);
    parameters.amp.levelDb = std::clamp(finiteOr(parameters.amp.levelDb, defaults.amp.levelDb), -48.0f, 12.0f);
    parameters.amp.pan = std::clamp(finiteOr(parameters.amp.pan, defaults.amp.pan), -1.0f, 1.0f);
    parameters.amp.panSpread = std::clamp(finiteOr(parameters.amp.panSpread, defaults.amp.panSpread), 0.0f, 1.0f);
    parameters.amp.unisonSpread = std::clamp(finiteOr(parameters.amp.unisonSpread, defaults.amp.unisonSpread), 0.0f, 1.0f);
    parameters.amp.analog = std::clamp(finiteOr(parameters.amp.analog, defaults.amp.analog), 0.0f, 1.0f);
    parameters.ampEnv.attackMs = std::clamp(finiteOr(parameters.ampEnv.attackMs, defaults.ampEnv.attackMs), 0.0f, 10000.0f);
    parameters.ampEnv.decayMs = std::clamp(finiteOr(parameters.ampEnv.decayMs, defaults.ampEnv.decayMs), 1.0f, 10000.0f);
    parameters.ampEnv.sustain = std::clamp(finiteOr(parameters.ampEnv.sustain, defaults.ampEnv.sustain), 0.0f, 1.0f);
    parameters.ampEnv.releaseMs = std::clamp(finiteOr(parameters.ampEnv.releaseMs, defaults.ampEnv.releaseMs), 1.0f, 10000.0f);
    parameters.modEnv.attackMs = std::clamp(finiteOr(parameters.modEnv.attackMs, defaults.modEnv.attackMs), 0.0f, 10000.0f);
    parameters.modEnv.decayMs = std::clamp(finiteOr(parameters.modEnv.decayMs, defaults.modEnv.decayMs), 0.0f, 10000.0f);
    parameters.modEnv.sustain = std::clamp(finiteOr(parameters.modEnv.sustain, defaults.modEnv.sustain), 0.0f, 1.0f);
    parameters.modEnv.releaseMs = std::clamp(finiteOr(parameters.modEnv.releaseMs, defaults.modEnv.releaseMs), 0.0f, 10000.0f);
    parameters.lfo.shape = static_cast<LfoShapeChoice>(
        std::clamp(static_cast<int>(parameters.lfo.shape), 0, static_cast<int>(LfoShapeChoice::Step)));
    parameters.lfo.rateMode = static_cast<LfoRateMode>(std::clamp(static_cast<int>(parameters.lfo.rateMode), 0, 1));
    parameters.lfo.rateHz = std::clamp(finiteOr(parameters.lfo.rateHz, defaults.lfo.rateHz), 0.01f, 40.0f);
    parameters.lfo.syncDivision = std::clamp(parameters.lfo.syncDivision, 0, 17);
    parameters.lfo.phaseDegrees = std::clamp(finiteOr(parameters.lfo.phaseDegrees, defaults.lfo.phaseDegrees), 0.0f, 360.0f);
    parameters.lfo.gateMode = static_cast<LfoGateMode>(std::clamp(static_cast<int>(parameters.lfo.gateMode), 0, 3));
    parameters.lfo.swing = std::clamp(finiteOr(parameters.lfo.swing, defaults.lfo.swing), 0.0f, 1.0f);
    parameters.lfo.stepCount = std::clamp(parameters.lfo.stepCount, 1, lfoStepSlotCount);
    parameters.lfo.stepSmooth = std::clamp(finiteOr(parameters.lfo.stepSmooth, defaults.lfo.stepSmooth), 0.0f, 1.0f);
    for (std::size_t step = 0; step < parameters.lfo.steps.size(); ++step)
        parameters.lfo.steps[step] = std::clamp(finiteOr(parameters.lfo.steps[step], 0.0f), -1.0f, 1.0f);
    parameters.ramp.mode = static_cast<RampMode>(std::clamp(static_cast<int>(parameters.ramp.mode), 0, 2));
    parameters.ramp.delayMs = std::clamp(finiteOr(parameters.ramp.delayMs, defaults.ramp.delayMs), 0.0f, 10000.0f);
    parameters.ramp.riseMs = std::clamp(finiteOr(parameters.ramp.riseMs, defaults.ramp.riseMs), 1.0f, 10000.0f);
    parameters.ramp.curve = static_cast<RampCurve>(std::clamp(static_cast<int>(parameters.ramp.curve), 0, 2));
    parameters.arp.mode = static_cast<ArpMode>(std::clamp(static_cast<int>(parameters.arp.mode), 0, 9));
    parameters.arp.rate = static_cast<ArpRateDivision>(std::clamp(static_cast<int>(parameters.arp.rate), 0, 17));
    parameters.arp.gate = std::clamp(finiteOr(parameters.arp.gate, defaults.arp.gate), 0.02f, 1.0f);
    parameters.arp.octaves = std::clamp(parameters.arp.octaves, 1, 4);
    parameters.arp.swing = std::clamp(finiteOr(parameters.arp.swing, defaults.arp.swing), 0.0f, 0.75f);
    parameters.arp.stepCount = std::clamp(parameters.arp.stepCount, 1, arpStepCount);
    for (auto& step : parameters.arp.steps)
    {
        step.pitchSemitones = std::clamp(step.pitchSemitones, -24, 24);
        step.velocity = std::clamp(finiteOr(step.velocity, 1.0f), 0.0f, 1.0f);
        step.gate = std::clamp(finiteOr(step.gate, 1.0f), 0.02f, 1.0f);
    }
    parameters.chord.voiceCount = std::clamp(parameters.chord.voiceCount, 1, chordVoiceCount);
    for (auto& voice : parameters.chord.voices)
    {
        voice.pitchSemitones = std::clamp(voice.pitchSemitones, -24, 24);
        voice.velocity = std::clamp(finiteOr(voice.velocity, 1.0f), 0.0f, 1.0f);
    }
    parameters.direct.filterKeytrack = std::clamp(finiteOr(parameters.direct.filterKeytrack, defaults.direct.filterKeytrack), -1.0f, 1.0f);
    parameters.direct.filterLfoSemitones = std::clamp(finiteOr(parameters.direct.filterLfoSemitones, defaults.direct.filterLfoSemitones), -72.0f, 72.0f);
    parameters.direct.filterModEnvSemitones = std::clamp(finiteOr(parameters.direct.filterModEnvSemitones, defaults.direct.filterModEnvSemitones), -72.0f, 72.0f);
    parameters.direct.oscKeytrackSemitones = std::clamp(finiteOr(parameters.direct.oscKeytrackSemitones, defaults.direct.oscKeytrackSemitones), -48.0f, 48.0f);
    parameters.direct.oscLfoSemitones = std::clamp(finiteOr(parameters.direct.oscLfoSemitones, defaults.direct.oscLfoSemitones), -48.0f, 48.0f);
    parameters.direct.oscModEnvSemitones = std::clamp(finiteOr(parameters.direct.oscModEnvSemitones, defaults.direct.oscModEnvSemitones), -48.0f, 48.0f);
    parameters.direct.pulseKeytrack = std::clamp(finiteOr(parameters.direct.pulseKeytrack, defaults.direct.pulseKeytrack), -1.0f, 1.0f);
    parameters.direct.pulseLfo = std::clamp(finiteOr(parameters.direct.pulseLfo, defaults.direct.pulseLfo), -1.0f, 1.0f);
    parameters.direct.pulseModEnv = std::clamp(finiteOr(parameters.direct.pulseModEnv, defaults.direct.pulseModEnv), -1.0f, 1.0f);
    for (auto& slot : parameters.transMod.slots)
    {
        slot.source = static_cast<ModSource>(std::clamp(static_cast<int>(slot.source), 0, 23));
        slot.scaler = static_cast<ModSource>(std::clamp(static_cast<int>(slot.scaler), 0, 23));
        slot.depth = std::clamp(finiteOr(slot.depth, 0.0f), -1.0f, 1.0f);
        slot.oscPitchSemitones = std::clamp(finiteOr(slot.oscPitchSemitones, 0.0f), -48.0f, 48.0f);
        slot.pulseWidth = std::clamp(finiteOr(slot.pulseWidth, 0.0f), -1.0f, 1.0f);
        slot.filterCutoffSemitones = std::clamp(finiteOr(slot.filterCutoffSemitones, 0.0f), -72.0f, 72.0f);
        slot.ampLevelDb = std::clamp(finiteOr(slot.ampLevelDb, 0.0f), -24.0f, 24.0f);
        slot.pan = std::clamp(finiteOr(slot.pan, 0.0f), -1.0f, 1.0f);
    }
    const auto sanitizeEnvelope = [](EnvelopeParameters& envelope) noexcept {
        envelope.attackMs = std::clamp(finiteOr(envelope.attackMs, 0.0f), 0.0f, 10000.0f);
        envelope.decayMs = std::clamp(finiteOr(envelope.decayMs, 300.0f), 0.0f, 10000.0f);
        envelope.sustain = std::clamp(finiteOr(envelope.sustain, 0.0f), 0.0f, 1.0f);
        envelope.releaseMs = std::clamp(finiteOr(envelope.releaseMs, 200.0f), 0.0f, 10000.0f);
    };
    for (auto& layer : parameters.layers)
    {
        sanitizeEnvelope(layer.ampEnv);
        layer.input = static_cast<FilterInput>(std::clamp(static_cast<int>(layer.input), 0, 3));
        auto& filter = layer.filter;
        filter.mode = static_cast<FilterMode>(std::clamp(static_cast<int>(filter.mode), 0, 8));
        filter.cutoffSemitones = std::clamp(finiteOr(filter.cutoffSemitones, 96.0f), 0.0f, 136.0f);
        filter.resonance = std::clamp(finiteOr(filter.resonance, 0.0f), 0.0f, 1.0f);
        filter.drive = std::clamp(finiteOr(filter.drive, 0.0f), 0.0f, 1.0f);
        filter.keytrack = std::clamp(finiteOr(filter.keytrack, 0.0f), -1.0f, 2.0f);
        filter.oversampling = std::clamp(filter.oversampling, 0, 3);
    }
    sanitizeEnvelope(parameters.modEnv2);
    auto& secondLfo = parameters.lfo2;
    secondLfo.shape = static_cast<LfoShapeChoice>(std::clamp(static_cast<int>(secondLfo.shape), 0, 7));
    secondLfo.rateMode = static_cast<LfoRateMode>(std::clamp(static_cast<int>(secondLfo.rateMode), 0, 1));
    secondLfo.rateHz = std::clamp(finiteOr(secondLfo.rateHz, 2.0f), 0.01f, 40.0f);
    secondLfo.syncDivision = std::clamp(secondLfo.syncDivision, 0, 17);
    secondLfo.gateMode = static_cast<LfoGateMode>(std::clamp(static_cast<int>(secondLfo.gateMode), 0, 3));
    secondLfo.phaseDegrees = std::clamp(finiteOr(secondLfo.phaseDegrees, 0.0f), 0.0f, 360.0f);
    secondLfo.stepCount = std::clamp(secondLfo.stepCount, 1, lfoStepSlotCount);
    secondLfo.stepSmooth = std::clamp(finiteOr(secondLfo.stepSmooth, 0.0f), 0.0f, 1.0f);
    for (auto& step : secondLfo.steps) step = std::clamp(finiteOr(step, 0.0f), -1.0f, 1.0f);
    for (auto* oscillator : { &parameters.lfo, &parameters.lfo2 })
    {
        oscillator->gain = std::clamp(finiteOr(oscillator->gain, 1.0f), 0.0f, 1.0f);
        oscillator->offset = std::clamp(finiteOr(oscillator->offset, 0.0f), -1.0f, 1.0f);
    }
    parameters.filterControl.cutoffSemitones = std::clamp(finiteOr(parameters.filterControl.cutoffSemitones, 0.0f), -136.0f, 136.0f);
    parameters.filterControl.resonance = std::clamp(finiteOr(parameters.filterControl.resonance, 0.0f), -1.0f, 1.0f);
    parameters.filterControl.drive = std::clamp(finiteOr(parameters.filterControl.drive, 0.0f), 0.0f, 1.0f);
    parameters.filterControl.keytrack = std::clamp(finiteOr(parameters.filterControl.keytrack, 0.0f), -1.0f, 2.0f);
    parameters.master.levelDb = std::clamp(finiteOr(parameters.master.levelDb, 0.0f), -96.0f, 12.0f);
    const auto targetGain = parameters.master.levelDb <= -48.0f ? 0.0f : decibelsToGain(parameters.master.levelDb);
    if (!masterGainInitialized)
    {
        masterCurrentGain = targetGain;
        masterGainInitialized = true;
    }
    if (masterTargetGain != targetGain) masterSmoothingRemaining = masterSmoothingSamples;
    masterTargetGain = targetGain;
    parameters.portamentoMode = static_cast<PortamentoMode>(std::clamp(static_cast<int>(parameters.portamentoMode), 0, 1));
    parameters.pitchBendRange = std::clamp(finiteOr(parameters.pitchBendRange, 2.0f), 0.0f, 24.0f);
    parameters.arp.wrap = std::clamp(parameters.arp.wrap, 0, 128);
    parameters.arp.timeMs = std::clamp(finiteOr(parameters.arp.timeMs, 125.0f), 1.0f, 6000.0f);
    parameters.arp.velocityMode = static_cast<ArpVelocityMode>(std::clamp(static_cast<int>(parameters.arp.velocityMode), 0, 4));
    for (auto& slot : parameters.transMod.slots)
    {
        for (std::size_t index = 0; index < slot.nativeDepths.size(); ++index)
        {
            auto limit = 1.0f;
            if (index < 20)
            {
                switch (index % 5)
                {
                    case 0: limit = 48.0f; break;
                    case 1: limit = 24.0f; break;
                    case 4: limit = 180.0f; break;
                    default: break;
                }
            }
            else if (index == 20 || index == 23) limit = 72.0f;
            else if (index == 26 || index == 28) limit = 24.0f;
            else if (index == 30 || index == 33) limit = 40.0f;
            else if (index == static_cast<std::size_t>(NativeModDestination::PhaserCenterFrequency)) limit = 20000.0f;
            slot.nativeDepths[index] = std::clamp(finiteOr(slot.nativeDepths[index], 0.0f), -limit, limit);
        }
    }
    parameters.transMod.activeSlotCount = 0;
    for (const auto& slot : parameters.transMod.slots)
    {
        if (!slot.enabled || slot.source == ModSource::None)
            continue;

        if (parameters.transMod.activeSlotCount >= transModSlotCount)
            break;

        parameters.transMod.activeSlots[static_cast<std::size_t>(parameters.transMod.activeSlotCount++)] = slot;
    }
    parameters.transMod.activeSlotCacheValid = true;
    parameters.macro.motion = std::clamp(finiteOr(parameters.macro.motion, defaults.macro.motion), 0.0f, 1.0f);
    parameters.macro.width = std::clamp(finiteOr(parameters.macro.width, defaults.macro.width), 0.0f, 1.0f);
    parameters.macro.drive = std::clamp(finiteOr(parameters.macro.drive, defaults.macro.drive), 0.0f, 1.0f);
    parameters.macro.space = std::clamp(finiteOr(parameters.macro.space, defaults.macro.space), 0.0f, 1.0f);
    parameters.fx.distortionMode = static_cast<DistortionMode>(std::clamp(static_cast<int>(parameters.fx.distortionMode), 0, 4));
    parameters.fx.saturationMix = std::clamp(finiteOr(parameters.fx.saturationMix, defaults.fx.saturationMix), 0.0f, 1.0f);
    parameters.fx.saturationDrive = std::clamp(finiteOr(parameters.fx.saturationDrive, defaults.fx.saturationDrive), 0.0f, 1.0f);
    parameters.fx.phaserMix = std::clamp(finiteOr(parameters.fx.phaserMix, defaults.fx.phaserMix), 0.0f, 1.0f);
    parameters.fx.phaserRateHz = std::clamp(finiteOr(parameters.fx.phaserRateHz, defaults.fx.phaserRateHz), 0.02f, 8.0f);
    parameters.fx.phaserDepth = std::clamp(finiteOr(parameters.fx.phaserDepth, defaults.fx.phaserDepth), 0.0f, 1.0f);
    parameters.fx.phaserFeedback = std::clamp(finiteOr(parameters.fx.phaserFeedback, defaults.fx.phaserFeedback), 0.0f, 0.95f);
    parameters.fx.delayMix = std::clamp(finiteOr(parameters.fx.delayMix, defaults.fx.delayMix), 0.0f, 1.0f);
    parameters.fx.delaySyncDivision = static_cast<DelaySyncDivision>(std::clamp(static_cast<int>(parameters.fx.delaySyncDivision), 0, 17));
    parameters.fx.delayFeedback = std::clamp(finiteOr(parameters.fx.delayFeedback, defaults.fx.delayFeedback), 0.0f, 0.86f);
    parameters.fx.reverbMix = std::clamp(finiteOr(parameters.fx.reverbMix, defaults.fx.reverbMix), 0.0f, 1.0f);
    parameters.fx.reverbDecay = std::clamp(finiteOr(parameters.fx.reverbDecay, defaults.fx.reverbDecay), 0.0f, 1.0f);
    parameters.fx.chorusMix = std::clamp(finiteOr(parameters.fx.chorusMix, defaults.fx.chorusMix), 0.0f, 1.0f);
    parameters.fx.chorusRateHz = std::clamp(finiteOr(parameters.fx.chorusRateHz, defaults.fx.chorusRateHz), 0.02f, 8.0f);
    parameters.fx.chorusDepthMs = std::clamp(finiteOr(parameters.fx.chorusDepthMs, defaults.fx.chorusDepthMs), 0.1f, 24.0f);
    parameters.fx.eqLowGainDb = std::clamp(finiteOr(parameters.fx.eqLowGainDb, defaults.fx.eqLowGainDb), -12.0f, 12.0f);
    parameters.fx.eqHighGainDb = std::clamp(finiteOr(parameters.fx.eqHighGainDb, defaults.fx.eqHighGainDb), -12.0f, 12.0f);
    parameters.fx.compressorThresholdDb = std::clamp(finiteOr(parameters.fx.compressorThresholdDb, defaults.fx.compressorThresholdDb), -36.0f, 0.0f);
    parameters.fx.compressorRatio = std::clamp(finiteOr(parameters.fx.compressorRatio, defaults.fx.compressorRatio), 1.0f, 8.0f);
    parameters.fx.compressorMakeupDb = std::clamp(finiteOr(parameters.fx.compressorMakeupDb, defaults.fx.compressorMakeupDb), -12.0f, 12.0f);
    parameters.fx.compressorMix = std::clamp(finiteOr(parameters.fx.compressorMix, defaults.fx.compressorMix), 0.0f, 1.0f);
    parameters.quality.realtimeMode = static_cast<QualityMode>(std::clamp(static_cast<int>(parameters.quality.realtimeMode), 0, 2));
    parameters.quality.offlineMode = static_cast<QualityMode>(std::clamp(static_cast<int>(parameters.quality.offlineMode), 0, 2));
    parameters.quality.activeMode = static_cast<QualityMode>(std::clamp(static_cast<int>(parameters.quality.activeMode), 0, 2));
    parameters.tempoBpm = std::clamp(finiteOr(parameters.tempoBpm, defaults.tempoBpm), 20.0f, 300.0f);

    const auto chordConfigChanged = directChordConfigChanged(priorChord, parameters.chord);
    if (arpWasEnabled != parameters.arp.enabled)
    {
        arpeggiator.reset();
        resetDirectChordTracking();
        resetVoicesForHeldInputRebuild();
        if (parameters.arp.enabled)
            rebuildArpeggiatorFromInputNotes();
        else
            triggerDirectNotesFromInputNotes();
    }
    else if (chordConfigChanged)
    {
        resetDirectChordTracking();
        resetVoicesForHeldInputRebuild();
        if (parameters.arp.enabled)
        {
            arpeggiator.reset();
            rebuildArpeggiatorFromInputNotes();
        }
        else
        {
            triggerDirectNotesFromInputNotes();
        }
    }
    else if (parameters.arp.enabled && arpHoldWasEnabled && !parameters.arp.hold)
    {
        arpeggiator.reset();
        resetVoicesForHeldInputRebuild();
        rebuildArpeggiatorFromInputNotes();
    }

    voices.syncVoiceLimit(parameters);
}

void SynthEngine::advanceIdleModulators(int numSamples) noexcept
{
    if (hasPendingArpeggiatorEvents()) return;
    voices.advanceIdleModulators(numSamples, parameters);
}

RenderStats SynthEngine::process(float* left, float* right, int numSamples) noexcept
{
    RenderStats stats;
    stats.samplesRendered = std::max(0, numSamples);
    voices.syncActiveVoiceModulators(parameters);

    const auto emitSample = [&stats, left, right, this](int index, StereoFrame frame) noexcept {
        parameters.fx.phaserCenterOffsetHz = std::clamp(finiteOr(frame.phaserCenterOffsetHz, 0.0f), -20000.0f, 20000.0f);
        const auto fxFrame = fx.process({ frame.left, frame.right }, parameters);
        if (masterSmoothingRemaining > 0)
        {
            masterCurrentGain += (masterTargetGain - masterCurrentGain) / static_cast<float>(masterSmoothingRemaining);
            if (--masterSmoothingRemaining == 0) masterCurrentGain = masterTargetGain;
        }
        const auto masterGain = masterCurrentGain;
        auto l = fxFrame.left * masterGain;
        auto r = fxFrame.right * masterGain;

        if (!std::isfinite(l))
        {
            ++stats.invalidSamples;
            l = 0.0f;
        }

        if (!std::isfinite(r))
        {
            ++stats.invalidSamples;
            r = 0.0f;
        }

        constexpr auto outputLimit = 32.0f;
        l = std::clamp(l, -outputLimit, outputLimit);
        r = std::clamp(r, -outputLimit, outputLimit);

        if (left != nullptr)
            left[index] = l;

        if (right != nullptr)
            right[index] = r;

        stats.peak = std::max(stats.peak, std::max(std::abs(l), std::abs(r)));
    };

    if (parameters.arp.enabled)
    {
        // Arp events fire per sample, so the arp path keeps the per-sample loop.
        for (int i = 0; i < stats.samplesRendered; ++i)
        {
            processArpEvent(arpeggiator.processSample(parameters, sampleRate));
            parameters.performance.stepVelocity = arpeggiator.getStepVelocity();
            emitSample(i, voices.renderSample(parameters));
        }
    }
    else
    {
        float blockLeft[renderBlockMaxSamples];
        float blockRight[renderBlockMaxSamples];
        float phaserCenterOffsetsHz[renderBlockMaxSamples];
        for (int start = 0; start < stats.samplesRendered; start += renderBlockMaxSamples)
        {
            const auto blockSamples = std::min(renderBlockMaxSamples, stats.samplesRendered - start);
            voices.renderBlock(parameters, blockLeft, blockRight, blockSamples, phaserCenterOffsetsHz);
            for (int i = 0; i < blockSamples; ++i)
                emitSample(start + i, StereoFrame { blockLeft[i], blockRight[i], phaserCenterOffsetsHz[i] });
        }
    }

    stats.activeVoices = voices.activeVoiceCount();

    return stats;
}

void SynthEngine::triggerDirectNoteOn(int midiNote, float velocity) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(directChordInputs.size()))
        return;

    releaseTrackedDirectChordInput(midiNote);

    if (!parameters.chord.enabled)
    {
        voices.noteOn(midiNote, velocity, parameters);
        return;
    }

    std::array<DirectChordOutputNote, chordVoiceCount> outputNotes {};
    const auto noteCount = buildDirectChordOutputNotes(midiNote, velocity, outputNotes);
    if (noteCount <= 0)
        return;

    auto& inputState = directChordInputs[static_cast<std::size_t>(midiNote)];
    inputState.active = true;
    inputState.noteCount = noteCount;

    for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
    {
        const auto& outputNote = outputNotes[static_cast<std::size_t>(noteIndex)];
        inputState.outputNotes[static_cast<std::size_t>(noteIndex)] = outputNote.note;

        auto& refCount = directChordOutputRefCounts[static_cast<std::size_t>(outputNote.note)];
        if (refCount <= 0 || !hasActiveVoiceForNote(outputNote.note))
            voices.noteOn(outputNote.note, outputNote.velocity, parameters);
        ++refCount;
    }
}

void SynthEngine::triggerDirectNoteOff(int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(directChordInputs.size()))
        return;

    if (releaseTrackedDirectChordInput(midiNote))
        return;

    voices.noteOff(midiNote, parameters);
}

void SynthEngine::processArpEvent(const ArpGeneratedEvent& event) noexcept
{
    const auto releaseNotes = [this, &event]() noexcept {
        if (event.noteOffCount > 0)
        {
            for (int index = 0; index < std::min(event.noteOffCount, static_cast<int>(event.noteOffNumbers.size())); ++index)
                voices.noteOff(event.noteOffNumbers[static_cast<std::size_t>(index)], parameters);
        }
        else if (event.noteOff)
            voices.noteOff(event.noteOffNumber, parameters);
    };
    const auto triggerNotes = [this, &event]() noexcept {
        if (event.noteOnCount > 0)
        {
            for (int index = 0; index < std::min(event.noteOnCount, static_cast<int>(event.noteOnNumbers.size())); ++index)
                voices.noteOn(event.noteOnNumbers[static_cast<std::size_t>(index)], event.velocities[static_cast<std::size_t>(index)], parameters);
        }
        else if (event.noteOn)
            voices.noteOn(event.noteOnNumber, event.velocity, parameters);
    };
    if (event.noteOnBeforeNoteOff)
    {
        triggerNotes();
        releaseNotes();
    }
    else
    {
        releaseNotes();
        triggerNotes();
    }
}

int SynthEngine::buildDirectChordOutputNotes(
    int midiNote,
    float velocity,
    std::array<DirectChordOutputNote, chordVoiceCount>& outputNotes) const noexcept
{
    auto noteCount = 0;
    auto enabledVoiceCount = 0;
    const auto voiceLimit = std::clamp(parameters.chord.voiceCount, 1, chordVoiceCount);
    const auto inputVelocity = std::clamp(velocity, 0.0f, 1.0f);

    auto appendOutputNote = [&outputNotes, &noteCount](int note, float outputVelocity) noexcept {
        if (outputVelocity <= 0.0f)
            return;

        for (int index = 0; index < noteCount; ++index)
        {
            auto& existing = outputNotes[static_cast<std::size_t>(index)];
            if (existing.note == note)
            {
                existing.velocity = std::max(existing.velocity, outputVelocity);
                return;
            }
        }

        if (noteCount >= chordVoiceCount)
            return;

        auto& outputNote = outputNotes[static_cast<std::size_t>(noteCount++)];
        outputNote.note = note;
        outputNote.velocity = outputVelocity;
    };

    for (int voiceIndex = 0; voiceIndex < voiceLimit; ++voiceIndex)
    {
        const auto& chordVoice = parameters.chord.voices[static_cast<std::size_t>(voiceIndex)];
        if (!chordVoice.enabled)
            continue;

        ++enabledVoiceCount;
        appendOutputNote(std::clamp(midiNote + chordVoice.pitchSemitones, 0, 127),
                         std::clamp(inputVelocity * chordVoice.velocity, 0.0f, 1.0f));
    }

    if (enabledVoiceCount == 0)
        appendOutputNote(midiNote, inputVelocity);

    return noteCount;
}

bool SynthEngine::releaseTrackedDirectChordInput(int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(directChordInputs.size()))
        return false;

    auto& inputState = directChordInputs[static_cast<std::size_t>(midiNote)];
    if (!inputState.active)
        return false;

    for (int noteIndex = 0; noteIndex < inputState.noteCount; ++noteIndex)
    {
        const auto outputNote = inputState.outputNotes[static_cast<std::size_t>(noteIndex)];
        if (outputNote < 0 || outputNote >= static_cast<int>(directChordOutputRefCounts.size()))
            continue;

        auto& refCount = directChordOutputRefCounts[static_cast<std::size_t>(outputNote)];
        if (refCount <= 0)
            continue;

        --refCount;
        if (refCount == 0)
            voices.noteOff(outputNote, parameters);
    }

    inputState = {};
    return true;
}

bool SynthEngine::hasActiveVoiceForNote(int midiNote) const noexcept
{
    for (int voiceIndex = 0; voiceIndex < 32; ++voiceIndex)
    {
        const auto* voice = voices.getVoice(voiceIndex);
        if (voice != nullptr && voice->isActive() && voice->getMidiNote() == midiNote)
            return true;
    }

    return false;
}

void SynthEngine::resetDirectChordTracking() noexcept
{
    for (auto& inputState : directChordInputs)
        inputState = {};
    directChordOutputRefCounts.fill(0);
}

void SynthEngine::resetInputNoteTracking() noexcept
{
    inputHeldNotes.fill(false);
    inputHeldVelocities.fill(0.0f);
    inputHeldOrder.fill(0);
    inputOrderCounter = 0;
}

void SynthEngine::rebuildArpeggiatorFromInputNotes() noexcept
{
    std::array<bool, 128> restoredNotes {};

    for (int restoredCount = 0; restoredCount < static_cast<int>(inputHeldNotes.size()); ++restoredCount)
    {
        auto nextNote = -1;
        auto nextOrder = std::numeric_limits<std::uint64_t>::max();
        for (int note = 0; note < static_cast<int>(inputHeldNotes.size()); ++note)
        {
            const auto noteIndex = static_cast<std::size_t>(note);
            if (!inputHeldNotes[noteIndex] || restoredNotes[noteIndex] || inputHeldOrder[noteIndex] == 0)
                continue;

            if (inputHeldOrder[noteIndex] < nextOrder)
            {
                nextOrder = inputHeldOrder[noteIndex];
                nextNote = note;
            }
        }

        if (nextNote < 0)
            return;

        const auto noteIndex = static_cast<std::size_t>(nextNote);
        restoredNotes[noteIndex] = true;
        arpeggiator.noteOn(nextNote, inputHeldVelocities[noteIndex]);
    }
}

void SynthEngine::triggerDirectNotesFromInputNotes() noexcept
{
    for (int note = 0; note < static_cast<int>(inputHeldNotes.size()); ++note)
    {
        const auto noteIndex = static_cast<std::size_t>(note);
        if (inputHeldNotes[noteIndex])
            triggerDirectNoteOn(note, inputHeldVelocities[noteIndex]);
    }
}

void SynthEngine::resetVoicesForHeldInputRebuild() noexcept
{
    voices.stopAllWithFade(64);
    if (sustainPedalDown)
        voices.setSustainPedal(true);
}
} // namespace synth
