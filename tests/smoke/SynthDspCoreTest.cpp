#include "../../src/dsp/Filter.h"
#include "../../src/dsp/OscillatorStack.h"
#include "../../src/dsp/Ramp.h"
#include "../../src/dsp/SynthEngine.h"
#include "../../src/dsp/fx/FxChain.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
float estimateFrequency(const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    auto first = -1;
    auto last = -1;

    for (int i = 1; i < static_cast<int>(samples.size()); ++i)
    {
        if (samples[static_cast<std::size_t>(i - 1)] <= 0.0f && samples[static_cast<std::size_t>(i)] > 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }

    if (crossings < 2 || first < 0 || last <= first)
        return 0.0f;

    return static_cast<float>((static_cast<double>(crossings - 1) * sampleRate) / static_cast<double>(last - first));
}

float centsBetween(float actual, float expected)
{
    if (actual <= 0.0f || expected <= 0.0f)
        return 9999.0f;

    return 1200.0f * std::log2(actual / expected);
}

bool testOscillatorTuning()
{
    synth::OscillatorStack oscillator;
    oscillator.prepare(48000.0);
    oscillator.reset(0.0f);

    synth::SynthParameters parameters;
    parameters.osc.sawLevel = 1.0f;
    parameters.osc.pulseLevel = 0.0f;
    parameters.osc.subLevel = 0.0f;
    parameters.osc.noiseLevel = 0.0f;
    parameters.amp.analog = 0.0f;

    std::vector<float> samples(48000);
    for (auto& sample : samples)
        sample = oscillator.renderSample(60, parameters, 0.0f, 0.0f);

    const auto frequency = estimateFrequency(samples, 48000.0);
    return std::abs(centsBetween(frequency, synth::midiNoteToHz(60.0f))) < 5.0f;
}

bool testMidiNoteLookupCoversModulatedPitchRange()
{
    const auto nanPitch = synth::midiNoteToHz(std::numeric_limits<float>::quiet_NaN());
    const auto lowPitch = synth::midiNoteToHz(-120.0f);
    const auto midiZero = synth::midiNoteToHz(0.0f);
    const auto formerUpperEdge = synth::midiNoteToHz(136.0f);
    const auto highModulatedPitch = synth::midiNoteToHz(272.0f);
    const auto nextHighPitch = synth::midiNoteToHz(273.0f);

    return std::isfinite(nanPitch)
        && std::isfinite(lowPitch)
        && std::isfinite(highModulatedPitch)
        && lowPitch > 0.0f
        && lowPitch < midiZero
        && highModulatedPitch > formerUpperEdge
        && nextHighPitch > highModulatedPitch;
}

bool testDecibelGainLookupCoversAmpModulationRange()
{
    const auto nanGain = synth::decibelsToGain(std::numeric_limits<float>::quiet_NaN());
    const auto gainAt24Db = synth::decibelsToGain(24.0f);
    const auto gainAt36Db = synth::decibelsToGain(36.0f);
    const auto gainAt35AndHalfDb = synth::decibelsToGain(35.5f);

    return std::isfinite(nanGain)
        && std::isfinite(gainAt36Db)
        && gainAt24Db > 0.0f
        && gainAt36Db > gainAt24Db * 3.75f
        && gainAt36Db > gainAt35AndHalfDb;
}

bool testPulseWidth()
{
    for (const auto width : { 0.10f, 0.25f, 0.50f, 0.75f, 0.90f })
    {
        synth::OscillatorStack oscillator;
        oscillator.prepare(48000.0);
        oscillator.reset(0.0f);

        synth::SynthParameters parameters;
        parameters.osc.sawLevel = 0.0f;
        parameters.osc.pulseLevel = 1.0f;
        parameters.osc.pulseWidth = width;
        parameters.osc.subLevel = 0.0f;

        int positive = 0;
        constexpr int sampleCount = 48000;
        for (int i = 0; i < sampleCount; ++i)
        {
            if (oscillator.renderSample(48, parameters, 0.0f, 0.0f) > 0.0f)
                ++positive;
        }

        const auto duty = static_cast<float>(positive) / static_cast<float>(sampleCount);
        if (std::abs(duty - width) >= 0.04f)
            return false;
    }

    return true;
}

bool testSubOctave()
{
    for (int octave = 1; octave <= 3; ++octave)
    {
        synth::OscillatorStack oscillator;
        oscillator.prepare(48000.0);
        oscillator.reset(0.0f);

        synth::SynthParameters parameters;
        parameters.osc.sawLevel = 0.0f;
        parameters.osc.pulseLevel = 0.0f;
        parameters.osc.subLevel = 1.0f;
        parameters.osc.subWave = synth::SubWave::Saw;
        parameters.osc.subOctave = octave;

        std::vector<float> samples(48000);
        for (auto& sample : samples)
            sample = oscillator.renderSample(60, parameters, 0.0f, 0.0f);

        const auto frequency = estimateFrequency(samples, 48000.0);
        if (std::abs(centsBetween(frequency, synth::midiNoteToHz(static_cast<float>(60 - octave * 12)))) >= 5.0f)
            return false;
    }

    return true;
}

bool testOscillatorDetuneNoiseAndSync()
{
    for (int count = 1; count <= 5; ++count)
    {
        for (int index = 0; index < count; ++index)
        {
            const auto opposite = count - 1 - index;
            const auto sum = synth::OscillatorStack::detuneOffsetCents(index, count, 0.7f)
                + synth::OscillatorStack::detuneOffsetCents(opposite, count, 0.7f);
            if (std::abs(sum) > 0.0001f)
                return false;
        }
    }

    synth::OscillatorStack noiseA;
    synth::OscillatorStack noiseB;
    noiseA.prepare(48000.0);
    noiseB.prepare(48000.0);
    noiseA.reset(0.25f);
    noiseB.reset(0.25f);
    synth::SynthParameters noiseParameters;
    noiseParameters.osc.sawLevel = 0.0f;
    noiseParameters.osc.pulseLevel = 0.0f;
    noiseParameters.osc.subLevel = 0.0f;
    noiseParameters.osc.noiseLevel = 1.0f;
    for (int i = 0; i < 512; ++i)
    {
        if (std::abs(noiseA.renderSample(60, noiseParameters, 0.0f, 0.0f)
            - noiseB.renderSample(60, noiseParameters, 0.0f, 0.0f)) >= 1.0e-7f)
        {
            return false;
        }
    }

    synth::OscillatorStack sync;
    sync.prepare(48000.0);
    sync.reset(0.0f);
    synth::SynthParameters syncParameters;
    syncParameters.osc.syncAmount = 1.0f;
    for (int i = 0; i < 4096; ++i)
    {
        const auto sample = sync.renderSample(96, syncParameters, 0.0f, 0.0f);
        if (!std::isfinite(sample) || std::abs(sample) > 1.21f)
            return false;
    }

    return true;
}

bool testFilterMappingAndStability()
{
    if (std::abs(synth::Filter::cutoffSemitonesToHz(69.0f) - 440.0f) > 0.1f)
        return false;

    synth::SynthParameters parameters;
    parameters.filter.cutoffSemitones = 72.0f;
    parameters.filter.resonance = 0.85f;
    parameters.filter.drive = 0.6f;
    parameters.filter.oversampling = 2;

    synth::Filter filter;
    filter.prepare(48000.0);

    auto previousOutput = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const auto input = i == 0 ? 1.0f : 0.0f;
        const auto output = filter.process(input, 60, parameters, 0.0f);
        if (!std::isfinite(output) || std::abs(output) > 4.0f)
            return false;
        previousOutput = output;
    }

    return std::isfinite(previousOutput);
}

bool testEngineProducesAudio()
{
    synth::SynthEngine engine;
    engine.prepare(48000.0, 128);
    engine.noteOn(60, 1.0f);

    std::vector<float> left(4096);
    std::vector<float> right(4096);
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    return stats.activeVoices == 1 && stats.invalidSamples == 0 && stats.peak > 0.001f;
}

float renderMaxAbsDifference(const synth::SynthParameters& leftParameters,
                             const synth::SynthParameters& rightParameters,
                             int sampleCount)
{
    synth::SynthEngine leftEngine;
    synth::SynthEngine rightEngine;
    leftEngine.prepare(48000.0, 1);
    rightEngine.prepare(48000.0, 1);
    leftEngine.setParameters(leftParameters);
    rightEngine.setParameters(rightParameters);
    leftEngine.noteOn(60, 0.8f);
    rightEngine.noteOn(60, 0.8f);

    auto maxDifference = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        float leftA = 0.0f;
        float rightA = 0.0f;
        float leftB = 0.0f;
        float rightB = 0.0f;
        leftEngine.process(&leftA, &rightA, 1);
        rightEngine.process(&leftB, &rightB, 1);
        maxDifference = std::max(maxDifference, std::abs(leftA - leftB));
        maxDifference = std::max(maxDifference, std::abs(rightA - rightB));
    }

    return maxDifference;
}

float renderPeak(const synth::SynthParameters& parameters, int sampleCount)
{
    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);

    auto peak = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;
        engine.process(&left, &right, 1);
        peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
    }

    return peak;
}

synth::SynthParameters makeLayerRenderTestParameters()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.osc.sawLevel = 1.0f;
    parameters.osc.pulseLevel = 0.15f;
    parameters.osc.subLevel = 0.1f;
    parameters.ampEnv.releaseMs = 20.0f;
    return parameters;
}

bool testInactiveLayerStateDoesNotAffectRender()
{
    const auto baseParameters = makeLayerRenderTestParameters();
    auto mutatedLayerParameters = baseParameters;
    mutatedLayerParameters.layers[0].oscillators[1].enabled = false;
    mutatedLayerParameters.layers[0].oscillators[1].voices = 8;
    mutatedLayerParameters.layers[0].oscillators[1].waveform = synth::OscillatorSlotWaveform::Noise;
    mutatedLayerParameters.layers[0].oscillators[1].level = 0.0f;
    mutatedLayerParameters.layers[0].oscillators[1].invert = true;
    mutatedLayerParameters.layers[1].enabled = false;
    mutatedLayerParameters.layers[1].solo = true;
    mutatedLayerParameters.layers[1].levelDb = 12.0f;
    mutatedLayerParameters.layers[1].pan = 1.0f;
    for (auto& oscillator : mutatedLayerParameters.layers[1].oscillators)
    {
        oscillator.enabled = true;
        oscillator.voices = 8;
        oscillator.waveform = synth::OscillatorSlotWaveform::Sub;
        oscillator.octave = -4;
        oscillator.note = 12;
        oscillator.fineCents = 100.0f;
        oscillator.level = 1.0f;
        oscillator.phaseDegrees = 360.0f;
        oscillator.detune = 1.0f;
        oscillator.stereo = 1.0f;
        oscillator.pan = -1.0f;
        oscillator.retrigger = false;
        oscillator.invert = true;
    }

    return renderMaxAbsDifference(baseParameters, mutatedLayerParameters, 4096) <= 1.0e-7f;
}

bool testAdditionalOscillatorSlotAffectsRender()
{
    const auto baseParameters = makeLayerRenderTestParameters();
    auto layeredParameters = baseParameters;
    auto& oscillator = layeredParameters.layers[0].oscillators[1];
    oscillator.enabled = true;
    oscillator.voices = 4;
    oscillator.waveform = synth::OscillatorSlotWaveform::Pulse;
    oscillator.octave = 1;
    oscillator.level = 0.75f;
    oscillator.detune = 0.6f;
    oscillator.stereo = 0.5f;
    oscillator.pan = 0.4f;

    return renderMaxAbsDifference(baseParameters, layeredParameters, 4096) > 1.0e-4f;
}

bool testLayerBMuteAndSoloAffectRender()
{
    const auto baseParameters = makeLayerRenderTestParameters();
    auto layerBParameters = baseParameters;
    layerBParameters.layers[1].enabled = true;
    layerBParameters.layers[1].levelDb = -3.0f;
    layerBParameters.layers[1].pan = -0.5f;
    auto& oscillator = layerBParameters.layers[1].oscillators[0];
    oscillator.enabled = true;
    oscillator.voices = 3;
    oscillator.waveform = synth::OscillatorSlotWaveform::Sub;
    oscillator.octave = -1;
    oscillator.level = 0.9f;
    oscillator.detune = 0.25f;

    if (renderMaxAbsDifference(baseParameters, layerBParameters, 4096) <= 1.0e-4f)
        return false;

    auto mutedLayerBParameters = layerBParameters;
    mutedLayerBParameters.layers[1].mute = true;
    if (renderMaxAbsDifference(baseParameters, mutedLayerBParameters, 4096) > 1.0e-7f)
        return false;

    auto soloLayerBParameters = layerBParameters;
    soloLayerBParameters.layers[1].solo = true;
    if (renderMaxAbsDifference(baseParameters, soloLayerBParameters, 4096) <= 1.0e-4f)
        return false;

    auto mutedSoloLayerBParameters = soloLayerBParameters;
    mutedSoloLayerBParameters.layers[1].mute = true;
    return renderPeak(mutedSoloLayerBParameters, 1024) < 1.0e-7f;
}

synth::VoiceSnapshot firstActiveSnapshot(const synth::SynthEngine& engine)
{
    for (int i = 0; i < 32; ++i)
    {
        const auto* voice = engine.getVoice(i);
        if (voice == nullptr)
            continue;

        const auto snapshot = voice->snapshot();
        if (snapshot.state != synth::VoiceState::Idle)
            return snapshot;
    }

    return {};
}

int activeSnapshotCount(const synth::SynthEngine& engine)
{
    auto count = 0;
    for (int i = 0; i < 32; ++i)
    {
        const auto* voice = engine.getVoice(i);
        if (voice == nullptr)
            continue;

        if (voice->snapshot().state != synth::VoiceState::Idle)
            ++count;
    }

    return count;
}

std::vector<synth::VoiceSnapshot> heldSnapshots(const synth::SynthEngine& engine)
{
    std::vector<synth::VoiceSnapshot> snapshots;
    for (int i = 0; i < 32; ++i)
    {
        const auto* voice = engine.getVoice(i);
        if (voice == nullptr)
            continue;

        const auto snapshot = voice->snapshot();
        if (snapshot.state == synth::VoiceState::Held)
            snapshots.push_back(snapshot);
    }

    return snapshots;
}

int firstHeldNote(const synth::SynthEngine& engine)
{
    const auto snapshots = heldSnapshots(engine);
    return snapshots.empty() ? -1 : snapshots.front().midiNote;
}

float firstHeldVelocity(const synth::SynthEngine& engine)
{
    const auto snapshots = heldSnapshots(engine);
    return snapshots.empty() ? 0.0f : snapshots.front().velocity;
}

bool hasHeldNote(const synth::SynthEngine& engine, int midiNote)
{
    const auto snapshots = heldSnapshots(engine);
    return std::any_of(snapshots.begin(), snapshots.end(), [midiNote](const auto& snapshot) {
        return snapshot.midiNote == midiNote;
    });
}

void processSamples(synth::SynthEngine& engine, int sampleCount)
{
    float left = 0.0f;
    float right = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
        engine.process(&left, &right, 1);
}

bool testDirectChordExpansion()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.chord.enabled = true;
    parameters.chord.voiceCount = 3;
    parameters.chord.voices[0] = { true, 0, 1.0f };
    parameters.chord.voices[1] = { true, 4, 0.75f };
    parameters.chord.voices[2] = { true, 7, 0.5f };

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);

    auto hasRoot = false;
    auto hasThird = false;
    auto hasFifth = false;
    for (const auto& snapshot : heldSnapshots(engine))
    {
        hasRoot = hasRoot || (snapshot.midiNote == 60 && std::abs(snapshot.velocity - 0.8f) < 0.001f);
        hasThird = hasThird || (snapshot.midiNote == 64 && std::abs(snapshot.velocity - 0.6f) < 0.001f);
        hasFifth = hasFifth || (snapshot.midiNote == 67 && std::abs(snapshot.velocity - 0.4f) < 0.001f);
    }

    if (!hasRoot || !hasThird || !hasFifth)
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testDirectChordOverlapKeepsSharedOutputHeld()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.chord.enabled = true;
    parameters.chord.voiceCount = 2;
    parameters.chord.voices[0] = { true, 0, 1.0f };
    parameters.chord.voices[1] = { true, 4, 1.0f };

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    engine.noteOn(64, 0.7f);
    processSamples(engine, 1);

    if (!hasHeldNote(engine, 60) || !hasHeldNote(engine, 64) || !hasHeldNote(engine, 68))
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    if (hasHeldNote(engine, 60) || !hasHeldNote(engine, 64) || !hasHeldNote(engine, 68))
        return false;

    engine.noteOff(64);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testDirectChordParameterChangeReleasesOriginalOutputs()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.chord.enabled = true;
    parameters.chord.voiceCount = 2;
    parameters.chord.voices[0] = { true, 0, 1.0f };
    parameters.chord.voices[1] = { true, 7, 1.0f };

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);
    if (!hasHeldNote(engine, 60) || !hasHeldNote(engine, 67))
        return false;

    parameters.chord.enabled = false;
    engine.setParameters(parameters);
    processSamples(engine, 2);
    if (!hasHeldNote(engine, 60) || hasHeldNote(engine, 67))
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testDirectChordSustainParameterChangeClearsOriginalOutputs()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.chord.enabled = true;
    parameters.chord.voiceCount = 2;
    parameters.chord.voices[0] = { true, 0, 1.0f };
    parameters.chord.voices[1] = { true, 7, 1.0f };

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);
    if (!hasHeldNote(engine, 60) || !hasHeldNote(engine, 67))
        return false;

    engine.setSustainPedal(true);
    parameters.chord.enabled = false;
    engine.setParameters(parameters);
    processSamples(engine, 2);
    if (!hasHeldNote(engine, 60) || hasHeldNote(engine, 67))
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    if (!hasHeldNote(engine, 60))
        return false;

    engine.setSustainPedal(false);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testArpChordSustainParameterChangeClearsOriginalOutputs()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.attackMs = 0.0f;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.arp.enabled = true;
    parameters.arp.mode = synth::ArpMode::Up;
    parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
    parameters.arp.gate = 1.0f;
    parameters.arp.stepCount = 1;
    parameters.tempoBpm = 120.0f;
    parameters.chord.enabled = true;
    parameters.chord.voiceCount = 2;
    parameters.chord.voices[0] = { true, 0, 1.0f };
    parameters.chord.voices[1] = { true, 7, 1.0f };

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);
    if (firstHeldNote(engine) != 60)
        return false;

    processSamples(engine, 125);
    if (!hasHeldNote(engine, 67))
        return false;

    engine.setSustainPedal(true);
    parameters.chord.enabled = false;
    engine.setParameters(parameters);
    processSamples(engine, 1);
    if (!hasHeldNote(engine, 60) || hasHeldNote(engine, 67))
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    if (!hasHeldNote(engine, 60))
        return false;

    engine.setSustainPedal(false);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testArpUpModeTimingAndGate()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.attackMs = 0.0f;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.arp.enabled = true;
    parameters.arp.mode = synth::ArpMode::Up;
    parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
    parameters.arp.gate = 0.25f;
    parameters.arp.stepCount = 1;
    parameters.tempoBpm = 120.0f;

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(67, 0.9f);
    engine.noteOn(60, 0.9f);
    engine.noteOn(64, 0.9f);

    processSamples(engine, 1);
    if (firstHeldNote(engine) != 60)
        return false;

    processSamples(engine, 32);
    if (!heldSnapshots(engine).empty())
        return false;

    processSamples(engine, 92);
    if (firstHeldNote(engine) != -1)
        return false;

    processSamples(engine, 1);
    if (firstHeldNote(engine) != 64)
        return false;

    processSamples(engine, 125);
    return firstHeldNote(engine) == 67;
}

bool testArpOctaveWrapAndStepPitchVelocity()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.attackMs = 0.0f;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.arp.enabled = true;
    parameters.arp.mode = synth::ArpMode::Up;
    parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
    parameters.arp.gate = 1.0f;
    parameters.arp.octaves = 2;
    parameters.arp.stepCount = 2;
    parameters.arp.steps[1].pitchSemitones = 7;
    parameters.arp.steps[1].velocity = 0.5f;
    parameters.tempoBpm = 120.0f;

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    engine.noteOn(64, 0.8f);

    processSamples(engine, 1);
    if (firstHeldNote(engine) != 60 || std::abs(firstHeldVelocity(engine) - 0.8f) > 0.001f)
        return false;

    processSamples(engine, 125);
    if (firstHeldNote(engine) != 71 || std::abs(firstHeldVelocity(engine) - 0.4f) > 0.001f)
        return false;

    processSamples(engine, 125);
    if (firstHeldNote(engine) != 72)
        return false;

    processSamples(engine, 125);
    return firstHeldNote(engine) == 83;
}

bool testArpTieHoldAndPanic()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.attackMs = 0.0f;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.arp.enabled = true;
    parameters.arp.mode = synth::ArpMode::AsPlayed;
    parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
    parameters.arp.gate = 0.05f;
    parameters.arp.hold = true;
    parameters.arp.stepCount = 2;
    parameters.arp.steps[0].tie = true;
    parameters.tempoBpm = 120.0f;

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);
    if (firstHeldNote(engine) != 60)
        return false;

    processSamples(engine, 80);
    if (firstHeldNote(engine) != 60)
        return false;

    engine.noteOff(60);
    processSamples(engine, 45);
    if (firstHeldNote(engine) != 60)
        return false;

    engine.panic();
    processSamples(engine, 1);
    return activeSnapshotCount(engine) == 0;
}

bool testArpEnableDisableWhileNotesHeld()
{
    {
        synth::SynthParameters parameters;
        parameters.filter.enabled = false;
        parameters.ampEnv.attackMs = 0.0f;
        parameters.ampEnv.releaseMs = 1.0f;
        parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
        parameters.arp.gate = 1.0f;
        parameters.arp.stepCount = 1;
        parameters.tempoBpm = 120.0f;

        synth::SynthEngine engine;
        engine.prepare(1000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.8f);
        processSamples(engine, 1);
        if (!hasHeldNote(engine, 60))
            return false;

        parameters.arp.enabled = true;
        engine.setParameters(parameters);
        processSamples(engine, 1);
        if (!hasHeldNote(engine, 60))
            return false;

        engine.noteOff(60);
        processSamples(engine, 2);
        if (!heldSnapshots(engine).empty())
            return false;
    }

    {
        synth::SynthParameters parameters;
        parameters.filter.enabled = false;
        parameters.ampEnv.attackMs = 0.0f;
        parameters.ampEnv.releaseMs = 1.0f;
        parameters.arp.enabled = true;
        parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
        parameters.arp.gate = 1.0f;
        parameters.arp.stepCount = 1;
        parameters.tempoBpm = 120.0f;

        synth::SynthEngine engine;
        engine.prepare(1000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.8f);
        processSamples(engine, 1);
        if (!hasHeldNote(engine, 60))
            return false;

        parameters.arp.enabled = false;
        engine.setParameters(parameters);
        processSamples(engine, 1);
        if (!hasHeldNote(engine, 60))
            return false;

        engine.noteOff(60);
        processSamples(engine, 2);
        if (!heldSnapshots(engine).empty())
            return false;
    }

    return true;
}

bool testArpHoldDisableClearsReleasedInputNotes()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.ampEnv.attackMs = 0.0f;
    parameters.ampEnv.releaseMs = 1.0f;
    parameters.arp.enabled = true;
    parameters.arp.mode = synth::ArpMode::AsPlayed;
    parameters.arp.rate = synth::ArpRateDivision::Sixteenth;
    parameters.arp.gate = 1.0f;
    parameters.arp.hold = true;
    parameters.arp.stepCount = 1;
    parameters.tempoBpm = 120.0f;

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.8f);
    processSamples(engine, 1);
    if (!hasHeldNote(engine, 60))
        return false;

    engine.noteOff(60);
    processSamples(engine, 2);
    if (!hasHeldNote(engine, 60))
        return false;

    parameters.arp.hold = false;
    engine.setParameters(parameters);
    processSamples(engine, 2);
    return heldSnapshots(engine).empty();
}

bool testRampGlideAndVelocityGlide()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::MonoLegato;
    parameters.polyphony = 1;
    parameters.unisonCount = 1;
    parameters.glideMs = 100.0f;
    parameters.velocityGlideMs = 100.0f;
    parameters.ramp.enabled = true;
    parameters.ramp.riseMs = 100.0f;
    parameters.ramp.curve = synth::RampCurve::Linear;
    parameters.osc.sawLevel = 1.0f;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.25f);
    processSamples(engine, 2400);

    const auto halfwayRamp = firstActiveSnapshot(engine).ramp;
    if (halfwayRamp < 0.45f || halfwayRamp > 0.55f)
        return false;

    engine.noteOn(72, 1.0f);
    processSamples(engine, 16);
    const auto startGlide = firstActiveSnapshot(engine);
    if (startGlide.effectiveMidiNote <= 60.0f || startGlide.effectiveMidiNote >= 61.0f)
        return false;

    if (startGlide.velocityGlide <= 0.25f || startGlide.velocityGlide >= 0.35f)
        return false;

    processSamples(engine, 4800);
    const auto endGlide = firstActiveSnapshot(engine);
    return std::abs(endGlide.effectiveMidiNote - 72.0f) < 0.05f
        && std::abs(endGlide.velocityGlide - 1.0f) < 0.01f
        && endGlide.ramp > 0.98f;
}

bool testRampOneShotBoundary()
{
    synth::Ramp ramp;
    ramp.prepare(1000.0);
    ramp.noteOn();

    synth::RampParameters parameters;
    parameters.enabled = true;
    parameters.riseMs = 4.0f;
    parameters.curve = synth::RampCurve::Linear;

    const auto first = ramp.process(parameters, 120.0f);
    const auto second = ramp.process(parameters, 120.0f);
    const auto third = ramp.process(parameters, 120.0f);
    const auto fourth = ramp.process(parameters, 120.0f);

    return std::abs(first - 0.0f) < 0.0001f
        && std::abs(second - (1.0f / 3.0f)) < 0.0001f
        && std::abs(third - (2.0f / 3.0f)) < 0.0001f
        && std::abs(fourth - 1.0f) < 0.0001f;
}

bool testGlideOneSampleBoundary()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::MonoLegato;
    parameters.glideMs = 1.0f;
    parameters.velocityGlideMs = 1.0f;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(1000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.25f);
    processSamples(engine, 1);
    engine.noteOn(72, 1.0f);
    processSamples(engine, 1);

    const auto snapshot = firstActiveSnapshot(engine);
    return std::abs(snapshot.effectiveMidiNote - 72.0f) < 0.0001f
        && std::abs(snapshot.velocityGlide - 1.0f) < 0.0001f;
}

bool testMonoModeDoesNotLegatoGlide()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::Mono;
    parameters.glideMs = 100.0f;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.5f);
    processSamples(engine, 8);
    engine.noteOn(72, 1.0f);
    processSamples(engine, 1);

    const auto snapshot = firstActiveSnapshot(engine);
    return snapshot.midiNote == 72
        && std::abs(snapshot.effectiveMidiNote - 72.0f) < 0.0001f;
}

bool testMonoLegatoRetriggerPolicy()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::MonoLegato;
    parameters.retrigger = false;
    parameters.glideMs = 100.0f;
    parameters.ramp.enabled = true;
    parameters.ramp.riseMs = 100.0f;
    parameters.ramp.curve = synth::RampCurve::Linear;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 0.5f);
    processSamples(engine, 2400);
    const auto beforeLegato = firstActiveSnapshot(engine).ramp;

    engine.noteOn(64, 1.0f);
    processSamples(engine, 1);
    const auto afterLegato = firstActiveSnapshot(engine).ramp;

    return beforeLegato > 0.49f
        && beforeLegato < 0.51f
        && afterLegato > 0.49f;
}

bool testMonoLegatoReleaseResumesHeldNote()
{
    auto makeParameters = [] {
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::MonoLegato;
        parameters.retrigger = false;
        parameters.glideMs = 0.0f;
        parameters.filter.enabled = false;
        parameters.ampEnv.releaseMs = 1.0f;
        return parameters;
    };

    {
        auto parameters = makeParameters();
        synth::SynthEngine engine;
        engine.prepare(48000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.4f);
        processSamples(engine, 8);
        engine.noteOn(64, 1.0f);
        processSamples(engine, 8);
        if (firstActiveSnapshot(engine).midiNote != 64)
            return false;

        engine.noteOff(64);
        processSamples(engine, 8);
        const auto resumed = firstActiveSnapshot(engine);
        if (resumed.state != synth::VoiceState::Held
            || resumed.midiNote != 60
            || activeSnapshotCount(engine) != 1)
        {
            return false;
        }
    }

    {
        auto parameters = makeParameters();
        synth::SynthEngine engine;
        engine.prepare(48000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.4f);
        processSamples(engine, 8);
        engine.noteOn(64, 1.0f);
        processSamples(engine, 8);
        engine.setSustainPedal(true);
        engine.noteOff(64);
        processSamples(engine, 8);
        const auto sustainedResume = firstActiveSnapshot(engine);
        if (sustainedResume.state != synth::VoiceState::Held
            || sustainedResume.midiNote != 60
            || activeSnapshotCount(engine) != 1)
        {
            return false;
        }

        engine.setSustainPedal(false);
        processSamples(engine, 8);
        const auto afterPedalUp = firstActiveSnapshot(engine);
        if (afterPedalUp.state != synth::VoiceState::Held
            || afterPedalUp.midiNote != 60
            || activeSnapshotCount(engine) != 1)
        {
            return false;
        }
    }

    {
        auto parameters = makeParameters();
        synth::SynthEngine engine;
        engine.prepare(48000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.4f);
        processSamples(engine, 8);
        engine.noteOn(64, 1.0f);
        processSamples(engine, 8);
        engine.setSustainPedal(true);
        engine.noteOff(60);
        processSamples(engine, 8);
        engine.noteOff(64);
        processSamples(engine, 8);
        const auto sustainedOnlyResume = firstActiveSnapshot(engine);
        if (sustainedOnlyResume.state != synth::VoiceState::Held
            || sustainedOnlyResume.midiNote != 60
            || activeSnapshotCount(engine) != 1)
        {
            return false;
        }

        engine.setSustainPedal(false);
        processSamples(engine, 128);
        return activeSnapshotCount(engine) == 0;
    }
}

bool testVoiceModeCapChangeReleasesSurplusVoices()
{
    {
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::Poly;
        parameters.polyphony = 4;
        parameters.unisonCount = 1;
        parameters.filter.enabled = false;

        synth::SynthEngine engine;
        engine.prepare(48000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 1.0f);
        engine.noteOn(64, 1.0f);
        engine.noteOn(67, 1.0f);
        processSamples(engine, 8);
        if (activeSnapshotCount(engine) != 3)
            return false;

        parameters.voiceMode = synth::VoiceMode::Mono;
        parameters.unisonCount = 4;
        engine.setParameters(parameters);
        processSamples(engine, 1);

        auto held = heldSnapshots(engine);
        if (held.size() != 1
            || held.front().midiNote != 67
            || std::abs(held.front().voiceBi) >= 0.0001f
            || std::abs(held.front().unisonBi) >= 0.0001f
            || activeSnapshotCount(engine) <= 1)
        {
            return false;
        }

        processSamples(engine, 64);
        const auto remaining = firstActiveSnapshot(engine);
        if (activeSnapshotCount(engine) != 1
            || remaining.state != synth::VoiceState::Held
            || remaining.midiNote != 67
            || std::abs(remaining.voiceBi) >= 0.0001f
            || std::abs(remaining.unisonBi) >= 0.0001f)
        {
            return false;
        }
    }

    {
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::Poly;
        parameters.polyphony = 8;
        parameters.unisonCount = 1;
        parameters.filter.enabled = false;

        synth::SynthEngine engine;
        engine.prepare(48000.0, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 1.0f);
        engine.noteOn(64, 1.0f);
        processSamples(engine, 8);

        parameters.polyphony = 2;
        engine.setParameters(parameters);
        processSamples(engine, 1);

        auto minVoiceBi = 1.0f;
        auto maxVoiceBi = -1.0f;
        for (int i = 0; i < 32; ++i)
        {
            const auto* voice = engine.getVoice(i);
            if (voice == nullptr)
                continue;

            const auto snapshot = voice->snapshot();
            if (snapshot.state == synth::VoiceState::Idle)
                continue;

            minVoiceBi = std::min(minVoiceBi, snapshot.voiceBi);
            maxVoiceBi = std::max(maxVoiceBi, snapshot.voiceBi);
        }

        return activeSnapshotCount(engine) == 2
            && minVoiceBi < -0.9f
            && maxVoiceBi > 0.9f;
    }
}

bool testPolyCapReductionKeepsMostRecentHeldNotes()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::Poly;
    parameters.polyphony = 4;
    parameters.unisonCount = 1;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);
    engine.noteOn(64, 1.0f);
    engine.noteOn(67, 1.0f);
    processSamples(engine, 8);

    parameters.polyphony = 2;
    engine.setParameters(parameters);
    processSamples(engine, 1);

    auto held = heldSnapshots(engine);
    if (held.size() != 2
        || hasHeldNote(engine, 60)
        || !hasHeldNote(engine, 64)
        || !hasHeldNote(engine, 67)
        || activeSnapshotCount(engine) <= 2)
    {
        return false;
    }

    processSamples(engine, 64);
    auto has60 = false;
    auto has64 = false;
    auto has67 = false;
    for (const auto& snapshot : heldSnapshots(engine))
    {
        has60 = has60 || snapshot.midiNote == 60;
        has64 = has64 || snapshot.midiNote == 64;
        has67 = has67 || snapshot.midiNote == 67;
    }
    return activeSnapshotCount(engine) == 2
        && !has60
        && has64
        && has67;
}

bool testUnisonModeCollapsesHeldPolyChord()
{
    synth::SynthParameters parameters;
    parameters.voiceMode = synth::VoiceMode::Poly;
    parameters.polyphony = 4;
    parameters.unisonCount = 1;
    parameters.filter.enabled = false;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);
    engine.noteOn(64, 1.0f);
    engine.noteOn(67, 1.0f);
    processSamples(engine, 8);
    if (activeSnapshotCount(engine) != 3)
        return false;

    parameters.voiceMode = synth::VoiceMode::Unison;
    parameters.unisonCount = 4;
    engine.setParameters(parameters);
    processSamples(engine, 1);

    auto held = heldSnapshots(engine);
    if (held.size() != 1
        || held.front().midiNote != 67
        || std::abs(held.front().voiceBi) >= 0.0001f
        || std::abs(held.front().unisonBi) >= 0.0001f
        || activeSnapshotCount(engine) <= 1)
    {
        return false;
    }

    processSamples(engine, 64);
    const auto remaining = firstActiveSnapshot(engine);
    return activeSnapshotCount(engine) == 1
        && remaining.state == synth::VoiceState::Held
        && remaining.midiNote == 67
        && std::abs(remaining.voiceBi) < 0.0001f
        && std::abs(remaining.unisonBi) < 0.0001f;
}

bool testDirectAndTransModRoutes()
{
    synth::SynthParameters parameters;
    parameters.polyphony = 1;
    parameters.unisonCount = 1;
    parameters.ramp.enabled = true;
    parameters.ramp.riseMs = 1.0f;
    parameters.direct.oscKeytrackSemitones = 12.0f;
    parameters.direct.pulseKeytrack = 0.25f;
    parameters.direct.filterKeytrack = 0.5f;
    parameters.macro.motion = 0.5f;
    parameters.filter.enabled = false;
    parameters.osc.sawLevel = 1.0f;

    auto& slot = parameters.transMod.slots[0];
    slot.enabled = true;
    slot.source = synth::ModSource::Ramp;
    slot.scaler = synth::ModSource::Macro1;
    slot.oscPitchSemitones = 12.0f;
    slot.pulseWidth = 0.2f;
    slot.filterCutoffSemitones = 24.0f;
    slot.ampLevelDb = 6.0f;
    slot.pan = 0.4f;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(72, 1.0f);
    processSamples(engine, 128);

    const auto snapshot = firstActiveSnapshot(engine);
    return std::abs(snapshot.directOscPitchSemitones - 12.0f) < 0.05f
        && std::abs(snapshot.directPulseWidth - 0.25f) < 0.01f
        && std::abs(snapshot.directFilterCutoffSemitones - 6.0f) < 0.05f
        && std::abs(snapshot.transModOscPitchSemitones - 6.0f) < 0.05f
        && std::abs(snapshot.transModPulseWidth - 0.1f) < 0.01f
        && std::abs(snapshot.transModFilterCutoffSemitones - 12.0f) < 0.05f
        && std::abs(snapshot.transModAmpLevelDb - 3.0f) < 0.05f
        && std::abs(snapshot.transModPan - 0.2f) < 0.01f;
}

bool testStepLfoTracksStepTable()
{
    synth::SynthParameters parameters;
    parameters.polyphony = 1;
    parameters.unisonCount = 1;
    parameters.filter.enabled = false;
    parameters.osc.sawLevel = 1.0f;
    parameters.lfo.shape = synth::LfoShapeChoice::Step;
    parameters.lfo.rateMode = synth::LfoRateMode::Hz;
    parameters.lfo.rateHz = 1.0f;
    parameters.lfo.phaseDegrees = 0.0f;
    parameters.lfo.gateMode = synth::LfoGateMode::PolyOn;
    parameters.lfo.mono = false;
    parameters.lfo.stepCount = 4;
    parameters.lfo.stepSmooth = 0.0f;
    parameters.lfo.steps.fill(0.0f);
    parameters.lfo.steps[0] = 1.0f;
    parameters.lfo.steps[1] = -1.0f;
    parameters.lfo.steps[2] = 0.5f;
    parameters.lfo.steps[3] = -0.5f;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);

    // 1 Hz at 48 kHz with 4 steps: each step holds for 12000 samples.
    processSamples(engine, 128);
    const auto stepZero = firstActiveSnapshot(engine).lfo;

    processSamples(engine, 12000);
    const auto stepOne = firstActiveSnapshot(engine).lfo;

    // Raising smooth mid-note must blend toward the next step without
    // resetting phase: sample 18000 sits halfway through step 1.
    parameters.lfo.stepSmooth = 1.0f;
    engine.setParameters(parameters);
    processSamples(engine, 5872);
    const auto blended = firstActiveSnapshot(engine).lfo;

    const auto ok = std::abs(stepZero - 1.0f) < 0.01f
        && std::abs(stepOne + 1.0f) < 0.01f
        && std::abs(blended + 0.25f) < 0.02f;
    if (!ok)
        std::cerr << "step lfo values: step0=" << stepZero << " step1=" << stepOne
                  << " blended=" << blended << "\n";
    return ok;
}

bool testVoiceUnisonRandomAndPerformanceSources()
{
    synth::SynthParameters parameters;
    parameters.polyphony = 2;
    parameters.unisonCount = 2;
    parameters.filter.enabled = false;
    parameters.osc.sawLevel = 1.0f;

    parameters.transMod.slots[0].enabled = true;
    parameters.transMod.slots[0].source = synth::ModSource::PitchBend;
    parameters.transMod.slots[0].oscPitchSemitones = 12.0f;
    parameters.transMod.slots[1].enabled = true;
    parameters.transMod.slots[1].source = synth::ModSource::ModWheel;
    parameters.transMod.slots[1].filterCutoffSemitones = 24.0f;
    parameters.transMod.slots[2].enabled = true;
    parameters.transMod.slots[2].source = synth::ModSource::Aftertouch;
    parameters.transMod.slots[2].ampLevelDb = 6.0f;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.setPitchBend(0.5f);
    engine.setModWheel(0.25f);
    engine.setAftertouch(0.5f);
    engine.noteOn(60, 0.7f);
    processSamples(engine, 64);

    auto active = 0;
    auto minUnisonBi = 1.0f;
    auto maxUnisonBi = -1.0f;
    auto minRandom = 1.0f;
    auto maxRandom = -1.0f;
    for (int i = 0; i < 32; ++i)
    {
        const auto* voice = engine.getVoice(i);
        if (voice == nullptr)
            continue;

        const auto snapshot = voice->snapshot();
        if (snapshot.state == synth::VoiceState::Idle)
            continue;

        ++active;
        minUnisonBi = std::min(minUnisonBi, snapshot.unisonBi);
        maxUnisonBi = std::max(maxUnisonBi, snapshot.unisonBi);
        minRandom = std::min(minRandom, snapshot.randomOnNote);
        maxRandom = std::max(maxRandom, snapshot.randomOnNote);

        if (std::abs(snapshot.transModOscPitchSemitones - 6.0f) > 0.05f)
            return false;
        if (std::abs(snapshot.transModFilterCutoffSemitones - 6.0f) > 0.05f)
            return false;
        if (std::abs(snapshot.transModAmpLevelDb - 3.0f) > 0.05f)
            return false;
    }

    return active == 2
        && minUnisonBi < -0.9f
        && maxUnisonBi > 0.9f
        && std::abs(maxRandom - minRandom) > 0.001f;
}

bool testVoiceModesAndOutputSafety()
{
    {
        synth::VoiceAllocator allocator(8);
        allocator.prepare(48000.0);
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::Mono;
        parameters.polyphony = 8;
        parameters.unisonCount = 4;
        allocator.noteOn(60, 1.0f, parameters);
        allocator.noteOn(64, 1.0f, parameters);
        if (allocator.activeVoiceCount() != 1)
            return false;
    }

    {
        synth::VoiceAllocator allocator(8);
        allocator.prepare(48000.0);
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::Unison;
        parameters.polyphony = 8;
        parameters.unisonCount = 3;
        allocator.noteOn(60, 1.0f, parameters);
        allocator.noteOn(64, 1.0f, parameters);
        if (allocator.activeVoiceCount() != 3)
            return false;
    }

    synth::SynthParameters parameters;
    parameters.amp.levelDb = std::numeric_limits<float>::quiet_NaN();
    parameters.filter.cutoffSemitones = std::numeric_limits<float>::infinity();
    parameters.filter.enabled = true;
    parameters.osc.sawLevel = 1.0f;
    for (auto& slot : parameters.transMod.slots)
    {
        slot.enabled = true;
        slot.source = synth::ModSource::Velocity;
        slot.ampLevelDb = 48.0f;
    }

    synth::SynthEngine engine;
    engine.prepare(48000.0, 128);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);

    std::vector<float> left(4096);
    std::vector<float> right(4096);
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    return stats.invalidSamples == 0 && stats.peak <= 1.0f;
}

bool testFxBypassIsDryEquivalent()
{
    synth::SynthParameters dryParameters;
    dryParameters.filter.enabled = false;
    dryParameters.osc.sawLevel = 1.0f;
    dryParameters.ampEnv.releaseMs = 20.0f;

    auto bypassParameters = dryParameters;
    bypassParameters.fx.enabled = false;
    bypassParameters.fx.saturationMix = 1.0f;
    bypassParameters.fx.delayMix = 1.0f;
    bypassParameters.fx.reverbMix = 1.0f;
    bypassParameters.fx.chorusMix = 1.0f;

    synth::SynthEngine dryEngine;
    synth::SynthEngine bypassEngine;
    dryEngine.prepare(48000.0, 1);
    bypassEngine.prepare(48000.0, 1);
    dryEngine.setParameters(dryParameters);
    bypassEngine.setParameters(bypassParameters);
    dryEngine.noteOn(60, 0.8f);
    bypassEngine.noteOn(60, 0.8f);

    for (int i = 0; i < 4096; ++i)
    {
        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        float bypassLeft = 0.0f;
        float bypassRight = 0.0f;
        dryEngine.process(&dryLeft, &dryRight, 1);
        bypassEngine.process(&bypassLeft, &bypassRight, 1);

        if (std::abs(dryLeft - bypassLeft) > 1.0e-7f || std::abs(dryRight - bypassRight) > 1.0e-7f)
            return false;
    }

    return true;
}

bool testFxDelaySyncAndTail()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 1);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.delayEnabled = true;
    parameters.fx.delayMix = 1.0f;
    parameters.fx.delayFeedback = 0.0f;
    parameters.fx.delaySyncDivision = synth::DelaySyncDivision::Quarter;
    parameters.fx.reverbEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.tempoBpm = 120.0f;

    const auto delaySamples = synth::tempoSyncedDelaySamples(48000.0, parameters.tempoBpm,
                                                             parameters.fx.delaySyncDivision);
    if (delaySamples != 24000)
        return false;

    parameters.fx.delaySyncDivision = synth::DelaySyncDivision::Half;
    parameters.tempoBpm = 20.0f;
    const auto maxDelaySamples = synth::tempoSyncedDelaySamples(48000.0, parameters.tempoBpm,
                                                                parameters.fx.delaySyncDivision);
    if (maxDelaySamples != 288000)
        return false;

    auto delayedPeak = 0.0f;
    for (int sample = 0; sample <= maxDelaySamples + 8; ++sample)
    {
        const auto input = sample == 0 ? synth::FxStereoFrame { 0.5f, 0.5f } : synth::FxStereoFrame {};
        const auto output = fx.process(input, parameters);
        if (sample == maxDelaySamples)
            delayedPeak = std::max(std::abs(output.left), std::abs(output.right));
    }

    parameters.fx.reverbEnabled = true;
    parameters.fx.reverbMix = 0.25f;
    parameters.fx.reverbDecay = 0.75f;
    const auto tail = synth::fxTailLengthSeconds(parameters);
    return delayedPeak > 0.49f && tail >= 6.0f;
}

bool testFxDelayUsesMacroSpaceWetness()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 1);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.phaserEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = false;
    parameters.fx.delayEnabled = true;
    parameters.fx.delayMix = 0.0f;
    parameters.fx.delayFeedback = 0.0f;
    parameters.fx.delaySyncDivision = synth::DelaySyncDivision::Quarter;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = false;
    parameters.macro.space = 1.0f;
    parameters.tempoBpm = 120.0f;

    const auto tail = synth::fxTailLengthSeconds(parameters);
    if (tail <= 0.0f)
        return false;

    const auto delaySamples = synth::tempoSyncedDelaySamples(48000.0, parameters.tempoBpm,
                                                             parameters.fx.delaySyncDivision);
    auto delayedPeak = 0.0f;
    for (int sample = 0; sample <= delaySamples + 8; ++sample)
    {
        const auto input = sample == 0 ? synth::FxStereoFrame { 0.5f, 0.5f } : synth::FxStereoFrame {};
        const auto output = fx.process(input, parameters);
        if (sample == delaySamples)
            delayedPeak = std::max(std::abs(output.left), std::abs(output.right));
    }

    return delayedPeak > 0.16f && delayedPeak < 0.19f;
}

bool testPanicClearsFxBuffers()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = false;
    parameters.amp.levelDb = -12.0f;
    parameters.ampEnv.releaseMs = 2000.0f;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.delayEnabled = true;
    parameters.fx.delayMix = 1.0f;
    parameters.fx.delayFeedback = 0.0f;
    parameters.fx.delaySyncDivision = synth::DelaySyncDivision::Sixteenth;
    parameters.fx.reverbEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.tempoBpm = 120.0f;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 1);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);
    processSamples(engine, 64);
    engine.panic();

    const auto delaySamples = synth::tempoSyncedDelaySamples(48000.0, parameters.tempoBpm,
                                                             parameters.fx.delaySyncDivision);
    std::vector<float> left(static_cast<std::size_t>(delaySamples + 64));
    std::vector<float> right(left.size());
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    return stats.invalidSamples == 0 && stats.activeVoices == 0 && stats.peak < 1.0e-6f;
}

bool testReverbBypassClearsRightCombState()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 1);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.delayEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.fx.reverbEnabled = true;
    parameters.fx.reverbMix = 1.0f;
    parameters.fx.reverbDecay = 0.8f;
    parameters.quality.activeMode = synth::QualityMode::Normal;

    for (int sample = 0; sample < 2400; ++sample)
    {
        const auto input = sample == 0 ? synth::FxStereoFrame { 0.0f, 1.0f } : synth::FxStereoFrame {};
        fx.process(input, parameters);
    }

    parameters.fx.reverbEnabled = false;
    fx.process({}, parameters);

    parameters.fx.reverbEnabled = true;
    auto peak = 0.0f;
    for (int sample = 0; sample < 5000; ++sample)
    {
        const auto output = fx.process({}, parameters);
        peak = std::max(peak, std::max(std::abs(output.left), std::abs(output.right)));
    }

    return peak < 1.0e-7f;
}

bool testEngineFxWetOutputSafety()
{
    synth::SynthParameters parameters;
    parameters.filter.enabled = true;
    parameters.filter.cutoffSemitones = 60.0f;
    parameters.filter.resonance = 0.4f;
    parameters.amp.levelDb = -9.0f;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = true;
    parameters.fx.saturationMix = 0.4f;
    parameters.fx.saturationDrive = 0.7f;
    parameters.fx.delayEnabled = true;
    parameters.fx.delayMix = 0.25f;
    parameters.fx.delayFeedback = 0.35f;
    parameters.fx.reverbEnabled = true;
    parameters.fx.reverbMix = 0.25f;
    parameters.fx.reverbDecay = 0.55f;
    parameters.fx.chorusEnabled = true;
    parameters.fx.chorusMix = 0.18f;
    parameters.quality.activeMode = synth::QualityMode::High;

    synth::SynthEngine engine;
    engine.prepare(48000.0, 128);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);
    processSamples(engine, 12000);
    engine.noteOff(60);

    std::vector<float> left(12000);
    std::vector<float> right(12000);
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    return stats.invalidSamples == 0 && stats.peak <= 1.0f;
}

bool testExpandedFxDisabledRackIsDry()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 64);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.phaserEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = false;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = false;

    for (int sample = 0; sample < 2048; ++sample)
    {
        const auto input = synth::FxStereoFrame {
            std::sin(static_cast<float>(sample) * 0.031f) * 0.35f,
            std::cos(static_cast<float>(sample) * 0.027f) * 0.25f
        };
        const auto output = fx.process(input, parameters);
        if (std::abs(output.left - input.left) > 1.0e-7f || std::abs(output.right - input.right) > 1.0e-7f)
            return false;
    }

    return true;
}

bool testExpandedFxModulesAreFiniteAndAudible()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 64);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = true;
    parameters.fx.distortionMode = synth::DistortionMode::Fold;
    parameters.fx.saturationMix = 0.28f;
    parameters.fx.saturationDrive = 0.65f;
    parameters.fx.phaserEnabled = true;
    parameters.fx.phaserMix = 0.55f;
    parameters.fx.phaserRateHz = 0.6f;
    parameters.fx.phaserDepth = 0.8f;
    parameters.fx.phaserFeedback = 0.35f;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = true;
    parameters.fx.eqLowGainDb = 4.5f;
    parameters.fx.eqHighGainDb = -3.0f;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = true;
    parameters.fx.compressorThresholdDb = -24.0f;
    parameters.fx.compressorRatio = 4.0f;
    parameters.fx.compressorMakeupDb = 2.0f;
    parameters.fx.compressorMix = 0.75f;

    auto maxDifference = 0.0f;
    auto peak = 0.0f;
    for (int sample = 0; sample < 4096; ++sample)
    {
        const auto input = synth::FxStereoFrame {
            std::sin(static_cast<float>(sample) * 0.071f) * 0.42f,
            std::sin(static_cast<float>(sample) * 0.047f + 0.6f) * 0.38f
        };
        const auto output = fx.process(input, parameters);
        if (!std::isfinite(output.left) || !std::isfinite(output.right))
            return false;

        maxDifference = std::max(maxDifference, std::abs(output.left - input.left));
        maxDifference = std::max(maxDifference, std::abs(output.right - input.right));
        peak = std::max(peak, std::abs(output.left));
        peak = std::max(peak, std::abs(output.right));
    }

    return maxDifference > 0.01f && peak <= 1.0f;
}

bool testCompressorRatioUsesDbDomain()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 1);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.phaserEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = false;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = true;
    parameters.fx.compressorThresholdDb = -18.0f;
    parameters.fx.compressorRatio = 4.0f;
    parameters.fx.compressorMakeupDb = 0.0f;
    parameters.fx.compressorMix = 1.0f;

    synth::FxStereoFrame output {};
    for (int sample = 0; sample < 4096; ++sample)
        output = fx.process({ 1.0f, 1.0f }, parameters);

    const auto expectedGain = synth::decibelsToGain(-13.5f);
    return std::isfinite(output.left)
        && std::abs(output.left - expectedGain) < 0.03f
        && output.left < 0.28f;
}

bool testExpandedFxNonTailModulesDoNotReportTail()
{
    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = true;
    parameters.fx.saturationMix = 1.0f;
    parameters.fx.phaserEnabled = true;
    parameters.fx.phaserMix = 1.0f;
    parameters.fx.chorusEnabled = true;
    parameters.fx.chorusMix = 1.0f;
    parameters.fx.eqEnabled = true;
    parameters.fx.eqLowGainDb = 6.0f;
    parameters.fx.compressorEnabled = true;
    parameters.fx.compressorMix = 1.0f;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;

    return synth::fxTailLengthSeconds(parameters) == 0.0f;
}

bool testPhaserBypassClearsRecursiveState()
{
    synth::FxChain fx;
    fx.prepare(48000.0, 64);

    synth::SynthParameters parameters;
    parameters.fx.enabled = true;
    parameters.fx.saturationEnabled = false;
    parameters.fx.phaserEnabled = true;
    parameters.fx.phaserMix = 1.0f;
    parameters.fx.phaserDepth = 0.9f;
    parameters.fx.phaserFeedback = 0.6f;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = false;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = false;

    fx.process({ 0.85f, -0.45f }, parameters);
    for (int sample = 0; sample < 128; ++sample)
        fx.process({}, parameters);

    parameters.fx.phaserEnabled = false;
    auto output = fx.process({}, parameters);
    if (std::abs(output.left) > 1.0e-7f || std::abs(output.right) > 1.0e-7f)
        return false;

    parameters.fx.phaserEnabled = true;
    output = fx.process({}, parameters);
    if (std::abs(output.left) > 1.0e-7f || std::abs(output.right) > 1.0e-7f)
        return false;

    fx.process({ 0.85f, -0.45f }, parameters);
    parameters.fx.enabled = false;
    output = fx.process({}, parameters);
    if (std::abs(output.left) > 1.0e-7f || std::abs(output.right) > 1.0e-7f)
        return false;

    parameters.fx.enabled = true;
    output = fx.process({}, parameters);
    return std::abs(output.left) <= 1.0e-7f && std::abs(output.right) <= 1.0e-7f;
}
} // namespace

int main()
{
    if (!testOscillatorTuning())
    {
        std::cerr << "Oscillator tuning test failed.\n";
        return 1;
    }

    if (!testMidiNoteLookupCoversModulatedPitchRange())
    {
        std::cerr << "MIDI note lookup range test failed.\n";
        return 1;
    }

    if (!testDecibelGainLookupCoversAmpModulationRange())
    {
        std::cerr << "Decibel gain lookup range test failed.\n";
        return 1;
    }

    if (!testPulseWidth())
    {
        std::cerr << "Pulse width test failed.\n";
        return 1;
    }

    if (!testSubOctave())
    {
        std::cerr << "Sub octave test failed.\n";
        return 1;
    }

    if (!testOscillatorDetuneNoiseAndSync())
    {
        std::cerr << "Oscillator detune/noise/sync test failed.\n";
        return 1;
    }

    if (!testFilterMappingAndStability())
    {
        std::cerr << "Filter mapping/stability test failed.\n";
        return 1;
    }

    if (!testEngineProducesAudio())
    {
        std::cerr << "Engine audio test failed.\n";
        return 1;
    }

    if (!testInactiveLayerStateDoesNotAffectRender())
    {
        std::cerr << "Inactive layer no-op render test failed.\n";
        return 1;
    }

    if (!testAdditionalOscillatorSlotAffectsRender())
    {
        std::cerr << "Additional oscillator slot render test failed.\n";
        return 1;
    }

    if (!testLayerBMuteAndSoloAffectRender())
    {
        std::cerr << "Layer B mute/solo render test failed.\n";
        return 1;
    }

    if (!testDirectChordExpansion())
    {
        std::cerr << "Direct chord expansion test failed.\n";
        return 1;
    }

    if (!testDirectChordOverlapKeepsSharedOutputHeld())
    {
        std::cerr << "Direct chord overlap release test failed.\n";
        return 1;
    }

    if (!testDirectChordParameterChangeReleasesOriginalOutputs())
    {
        std::cerr << "Direct chord parameter-change release test failed.\n";
        return 1;
    }

    if (!testDirectChordSustainParameterChangeClearsOriginalOutputs())
    {
        std::cerr << "Direct chord sustain parameter-change release test failed.\n";
        return 1;
    }

    if (!testArpChordSustainParameterChangeClearsOriginalOutputs())
    {
        std::cerr << "Arp chord sustain parameter-change release test failed.\n";
        return 1;
    }

    if (!testArpUpModeTimingAndGate())
    {
        std::cerr << "Arp up-mode timing/gate test failed.\n";
        return 1;
    }

    if (!testArpOctaveWrapAndStepPitchVelocity())
    {
        std::cerr << "Arp octave/step pitch/velocity test failed.\n";
        return 1;
    }

    if (!testArpTieHoldAndPanic())
    {
        std::cerr << "Arp tie/hold/panic test failed.\n";
        return 1;
    }

    if (!testArpEnableDisableWhileNotesHeld())
    {
        std::cerr << "Arp enable/disable held-note test failed.\n";
        return 1;
    }

    if (!testArpHoldDisableClearsReleasedInputNotes())
    {
        std::cerr << "Arp hold-disable release test failed.\n";
        return 1;
    }

    if (!testRampGlideAndVelocityGlide())
    {
        std::cerr << "Ramp/glide/velocity glide test failed.\n";
        return 1;
    }

    if (!testRampOneShotBoundary())
    {
        std::cerr << "Ramp one-shot boundary test failed.\n";
        return 1;
    }

    if (!testGlideOneSampleBoundary())
    {
        std::cerr << "Glide one-sample boundary test failed.\n";
        return 1;
    }

    if (!testMonoModeDoesNotLegatoGlide())
    {
        std::cerr << "Mono mode no-glide test failed.\n";
        return 1;
    }

    if (!testMonoLegatoRetriggerPolicy())
    {
        std::cerr << "Mono legato retrigger test failed.\n";
        return 1;
    }

    if (!testMonoLegatoReleaseResumesHeldNote())
    {
        std::cerr << "Mono legato release resume test failed.\n";
        return 1;
    }

    if (!testVoiceModeCapChangeReleasesSurplusVoices())
    {
        std::cerr << "Voice mode cap change test failed.\n";
        return 1;
    }

    if (!testPolyCapReductionKeepsMostRecentHeldNotes())
    {
        std::cerr << "Poly cap recency test failed.\n";
        return 1;
    }

    if (!testUnisonModeCollapsesHeldPolyChord())
    {
        std::cerr << "Unison mode chord collapse test failed.\n";
        return 1;
    }

    if (!testDirectAndTransModRoutes())
    {
        std::cerr << "Direct/TransMod route test failed.\n";
        return 1;
    }

    if (!testStepLfoTracksStepTable())
    {
        std::cerr << "Step LFO table test failed.\n";
        return 1;
    }

    if (!testVoiceUnisonRandomAndPerformanceSources())
    {
        std::cerr << "Voice/unison/random/performance source test failed.\n";
        return 1;
    }

    if (!testVoiceModesAndOutputSafety())
    {
        std::cerr << "Voice mode/output safety test failed.\n";
        return 1;
    }

    if (!testFxBypassIsDryEquivalent())
    {
        std::cerr << "FX bypass equivalence test failed.\n";
        return 1;
    }

    if (!testFxDelaySyncAndTail())
    {
        std::cerr << "FX delay sync/tail test failed.\n";
        return 1;
    }

    if (!testFxDelayUsesMacroSpaceWetness())
    {
        std::cerr << "FX delay macro-space wetness test failed.\n";
        return 1;
    }

    if (!testPanicClearsFxBuffers())
    {
        std::cerr << "FX panic clear test failed.\n";
        return 1;
    }

    if (!testReverbBypassClearsRightCombState())
    {
        std::cerr << "FX reverb bypass clear test failed.\n";
        return 1;
    }

    if (!testEngineFxWetOutputSafety())
    {
        std::cerr << "FX wet output safety test failed.\n";
        return 1;
    }

    if (!testExpandedFxDisabledRackIsDry())
    {
        std::cerr << "Expanded FX disabled-rack dry equivalence test failed.\n";
        return 1;
    }

    if (!testExpandedFxModulesAreFiniteAndAudible())
    {
        std::cerr << "Expanded FX finite/audible test failed.\n";
        return 1;
    }

    if (!testCompressorRatioUsesDbDomain())
    {
        std::cerr << "Compressor dB-ratio test failed.\n";
        return 1;
    }

    if (!testExpandedFxNonTailModulesDoNotReportTail())
    {
        std::cerr << "Expanded FX non-tail report test failed.\n";
        return 1;
    }

    if (!testPhaserBypassClearsRecursiveState())
    {
        std::cerr << "Phaser bypass cleanup test failed.\n";
        return 1;
    }

    return 0;
}
