#pragma once

#include "../../src/dsp/SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace native_validation
{
struct StereoRender
{
    std::vector<float> left;
    std::vector<float> right;
    bool finite = true;
};

inline synth::SynthParameters parameters()
{
    synth::SynthParameters result;
    result.fx.enabled = false;
    result.amp.analog = 0.0f;
    result.amp.drive = 0.0f;
    result.master.levelDb = -12.0f;
    result.macro.motion = 0.0f;
    result.macro.width = 0.0f;
    result.macro.drive = 0.0f;
    result.macro.space = 0.0f;
    for (auto& layer : result.layers)
    {
        layer.filter.enabled = false;
        layer.ampEnv = { 0.0f, 0.0f, 1.0f, 5.0f };
    }
    return result;
}

inline StereoRender render(const synth::SynthParameters& state, int samples = 12000)
{
    StereoRender result { std::vector<float>(static_cast<std::size_t>(samples)),
                          std::vector<float>(static_cast<std::size_t>(samples)) };
    synth::SynthEngine engine;
    engine.prepare(48000.0, 64);
    engine.setParameters(state);
    engine.noteOn(60, 1.0f);
    for (int offset = 0; offset < samples; offset += 64)
    {
        const auto count = std::min(64, samples - offset);
        const auto stats = engine.process(result.left.data() + offset, result.right.data() + offset, count);
        result.finite = result.finite && stats.invalidSamples == 0;
    }
    for (int i = 0; i < samples; ++i)
        result.finite = result.finite && std::isfinite(result.left[static_cast<std::size_t>(i)])
            && std::isfinite(result.right[static_cast<std::size_t>(i)]);
    return result;
}

inline float difference(const std::vector<float>& first, const std::vector<float>& second)
{
    auto maximum = 0.0f;
    for (std::size_t i = 0; i < std::min(first.size(), second.size()); ++i)
        maximum = std::max(maximum, std::abs(first[i] - second[i]));
    return maximum;
}

inline double rms(const std::vector<float>& samples, std::size_t end)
{
    auto sum = 0.0;
    end = std::min(end, samples.size());
    for (std::size_t i = 0; i < end; ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return end == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(end));
}

inline double frequency(const std::vector<float>& samples)
{
    auto first = -1;
    auto last = -1;
    auto count = 0;
    for (int i = 1024; i < static_cast<int>(samples.size()); ++i)
    {
        if (samples[static_cast<std::size_t>(i - 1)] <= 0.0f && samples[static_cast<std::size_t>(i)] > 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    }
    return count > 1 ? 48000.0 * static_cast<double>(count - 1) / static_cast<double>(last - first) : 0.0;
}

inline bool oscillatorControls()
{
    auto base = parameters();
    auto& oscillator = base.layers[0].oscillators[0];
    oscillator.waveform = synth::OscillatorSlotWaveform::Sine;
    const auto reference = render(base);
    if (!reference.finite || rms(reference.left, reference.left.size()) < 0.01)
        return false;
    const auto expectedFrequency = synth::midiNoteToHz(60.0f);
    if (std::abs(1200.0 * std::log2(frequency(reference.left) / expectedFrequency)) >= 5.0)
        return false;

    auto octave = base;
    octave.layers[0].oscillators[0].octave = 1;
    const auto octaveRender = render(octave);
    if (!octaveRender.finite
        || std::abs(1200.0 * std::log2(frequency(octaveRender.left) / (2.0 * expectedFrequency))) >= 5.0)
        return false;

    for (const auto waveform : { synth::OscillatorSlotWaveform::Saw, synth::OscillatorSlotWaveform::Triangle,
                                 synth::OscillatorSlotWaveform::SawTriangle, synth::OscillatorSlotWaveform::HalfPulse,
                                 synth::OscillatorSlotWaveform::QuarterPulse, synth::OscillatorSlotWaveform::Noise })
    {
        auto state = base;
        state.layers[0].oscillators[0].waveform = waveform;
        const auto changed = render(state, 4096);
        if (!changed.finite || difference(reference.left, changed.left) <= 1.0e-4f)
            return false;
    }
    auto silent = base;
    silent.layers[0].oscillators[0].voices = 0;
    const auto silentRender = render(silent, 4096);
    if (!silentRender.finite || rms(silentRender.left, silentRender.left.size()) > 1.0e-7)
        return false;

    auto stacked = base;
    stacked.layers[0].oscillators[0].voices = 8;
    stacked.layers[0].oscillators[0].detune = 0.65f;
    const auto stackedRender = render(stacked);
    return stackedRender.finite && difference(reference.left, stackedRender.left) > 1.0e-4f;
}

inline bool stereoSeparation()
{
    auto narrow = parameters();
    auto& oscillator = narrow.layers[0].oscillators[0];
    oscillator.voices = 8;
    oscillator.detune = 0.7f;
    oscillator.stereo = 0.0f;
    const auto mono = render(narrow);
    auto wide = narrow;
    wide.layers[0].oscillators[0].stereo = 1.0f;
    const auto stereo = render(wide);
    return mono.finite && stereo.finite && difference(mono.left, mono.right) <= 1.0e-7f
        && difference(stereo.left, stereo.right) > 1.0e-3f;
}

inline synth::SynthParameters twoParts()
{
    auto result = parameters();
    result.layers[0].pan = -1.0f;
    result.layers[1].enabled = true;
    result.layers[1].pan = 1.0f;
    result.layers[1].oscillators[0] = result.layers[0].oscillators[0];
    return result;
}

inline bool independentFiltersAndEnvelopes()
{
    auto base = twoParts();
    base.layers[0].filter.enabled = true;
    base.layers[1].filter.enabled = true;
    base.layers[0].filter.cutoffSemitones = 100.0f;
    base.layers[1].filter.cutoffSemitones = 100.0f;
    const auto reference = render(base);
    auto cutoff = base;
    cutoff.layers[1].filter.cutoffSemitones = 48.0f;
    const auto filtered = render(cutoff);
    if (!reference.finite || !filtered.finite || difference(reference.left, filtered.left) > 1.0e-7f
        || difference(reference.right, filtered.right) <= 1.0e-3f)
        return false;
    auto attack = base;
    attack.layers[1].ampEnv.attackMs = 500.0f;
    const auto delayed = render(attack);
    return delayed.finite && difference(reference.left, delayed.left) <= 1.0e-7f
        && rms(delayed.right, 2400) < rms(reference.right, 2400) * 0.5;
}

inline bool crossRouting()
{
    auto base = twoParts();
    base.layers[1].input = synth::FilterInput::None;
    base.layers[0].oscillators[0].voices = 0;
    const auto silent = render(base);
    auto cross = base;
    cross.layers[0].input = synth::FilterInput::B;
    const auto routed = render(cross);
    auto disconnected = cross;
    disconnected.layers[0].input = synth::FilterInput::None;
    const auto none = render(disconnected);
    return silent.finite && routed.finite && none.finite
        && rms(silent.left, silent.left.size()) < 1.0e-7
        && rms(routed.left, routed.left.size()) > 0.01
        && rms(routed.right, routed.right.size()) < 1.0e-7
        && rms(none.left, none.left.size()) < 1.0e-7;
}

inline bool secondModulationSources()
{
    const auto base = parameters();
    const auto baseline = render(base);
    for (const auto source : { synth::ModSource::ModEnv2, synth::ModSource::Lfo2 })
    {
        auto routed = base;
        routed.modEnv2 = { 0.0f, 1000.0f, 1.0f, 100.0f };
        routed.lfo2.rateMode = synth::LfoRateMode::Hz;
        routed.lfo2.rateHz = 5.0f;
        routed.lfo2.shape = synth::LfoShapeChoice::Sine;
        auto& slot = routed.transMod.slots[0];
        slot.enabled = true;
        slot.source = source;
        slot.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::OscA1Pitch)] = 12.0f;
        const auto modulation = render(routed);
        if (!modulation.finite || difference(baseline.left, modulation.left) <= 1.0e-3f)
            return false;
        auto disabled = routed;
        disabled.transMod.slots[0].enabled = false;
        const auto cleared = render(disabled);
        if (!cleared.finite || difference(baseline.left, cleared.left) > 1.0e-7f)
            return false;
        auto otherSource = routed;
        if (source == synth::ModSource::Lfo2)
            otherSource.lfo.rateHz = 13.0f;
        else
            otherSource.modEnv.attackMs = 800.0f;
        const auto independent = render(otherSource);
        if (!independent.finite || difference(modulation.left, independent.left) > 1.0e-7f)
            return false;
    }
    return true;
}

inline bool volumeEndpoints()
{
    auto state = parameters();
    state.layers[0].levelDb = -48.0f;
    const auto partMuted = render(state);
    if (!partMuted.finite || rms(partMuted.left, partMuted.left.size()) != 0.0)
        return false;
    state.layers[0].levelDb = 0.0f;
    synth::SynthEngine engine;
    engine.prepare(48000.0, 512);
    engine.setParameters(state);
    engine.noteOn(60, 0.8f);
    std::vector<float> left(512), right(512);
    engine.process(left.data(), right.data(), 512);
    if (rms(left, left.size()) <= 0.001)
        return false;
    state.master.levelDb = -48.0f;
    engine.setParameters(state);
    engine.process(left.data(), right.data(), 512);
    for (std::size_t index = 200; index < left.size(); ++index)
        if (left[index] != 0.0f || right[index] != 0.0f)
            return false;
    state.master.levelDb = -12.0f;
    engine.setParameters(state);
    engine.process(left.data(), right.data(), 512);
    return rms(left, left.size()) > 0.001;
}

inline bool sharedResonanceRoute()
{
    auto state = twoParts();
    for (auto& part : state.layers)
    {
        part.filter.enabled = true;
        part.filter.cutoffSemitones = 72.0f;
    }
    const auto baseline = render(state);
    auto& route = state.transMod.slots[7];
    route.enabled = true;
    route.source = synth::ModSource::Macro1;
    state.macro.motion = 1.0f;
    route.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::FilterResonanceAB)] = 0.7f;
    const auto modulated = render(state);
    route.enabled = false;
    const auto cleared = render(state);
    return baseline.finite && modulated.finite && cleared.finite
        && difference(baseline.left, modulated.left) > 1.0e-3f && difference(baseline.right, modulated.right) > 1.0e-3f
        && difference(baseline.left, cleared.left) <= 1.0e-7f && difference(baseline.right, cleared.right) <= 1.0e-7f;
}

inline bool globalModulationVoiceSelection()
{
    auto state = parameters();
    auto& route = state.transMod.slots[7];
    route.enabled = true;
    route.source = synth::ModSource::Velocity;
    route.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::PhaserCenterFrequency)] = 1000.0f;
    synth::VoiceAllocator voices;
    voices.prepare(48000.0);
    voices.noteOn(60, 0.25f, state);
    if (std::abs(voices.renderSample(state).phaserCenterOffsetHz - 250.0f) > 0.01f)
        return false;
    voices.noteOn(64, 0.75f, state);
    if (std::abs(voices.renderSample(state).phaserCenterOffsetHz - 750.0f) > 0.01f)
        return false;
    voices.noteOff(60, state);
    for (int sample = 0; sample < 1000; ++sample)
        voices.renderSample(state);
    voices.noteOn(67, 0.4f, state);
    if (std::abs(voices.renderSample(state).phaserCenterOffsetHz - 400.0f) > 0.01f)
        return false;
    voices.noteOff(67, state);
    if (std::abs(voices.renderSample(state).phaserCenterOffsetHz - 400.0f) > 0.01f)
        return false;
    for (int sample = 0; sample < 1000; ++sample)
        voices.renderSample(state);
    if (std::abs(voices.renderSample(state).phaserCenterOffsetHz - 750.0f) > 0.01f)
        return false;
    synth::VoiceAllocator scalar, block;
    scalar.prepare(48000.0);
    block.prepare(48000.0);
    state.unisonCount = 2;
    scalar.noteOn(60, 0.75f, state);
    block.noteOn(60, 0.75f, state);
    std::array<float, 64> left {}, right {}, offsets {};
    block.renderBlock(state, left.data(), right.data(), 64, offsets.data());
    for (int sample = 0; sample < 64; ++sample)
    {
        const auto frame = scalar.renderSample(state);
        if (std::abs(frame.phaserCenterOffsetHz - 750.0f) > 0.01f
            || frame.phaserCenterOffsetHz != offsets[static_cast<std::size_t>(sample)]
            || std::abs(frame.left - left[static_cast<std::size_t>(sample)]) > 1.0e-7f
            || std::abs(frame.right - right[static_cast<std::size_t>(sample)]) > 1.0e-7f)
            return false;
    }
    return true;
}

inline bool independentPartReleaseLifetime()
{
    auto state = twoParts();
    state.layers[0].ampEnv.releaseMs = 1.0f;
    state.layers[1].ampEnv.releaseMs = 200.0f;
    synth::SynthEngine engine;
    engine.prepare(48000.0, 64);
    engine.setParameters(state);
    engine.noteOn(60, 0.8f);
    std::vector<float> left(1024), right(1024);
    engine.process(left.data(), right.data(), 1024);
    engine.noteOff(60);
    auto stats = engine.process(left.data(), right.data(), 1024);
    auto leftTail = 0.0f;
    for (std::size_t index = 200; index < left.size(); ++index)
        leftTail = std::max(leftTail, std::abs(left[index]));
    if (stats.activeVoices != 1 || leftTail > 1.0e-7f || rms(right, right.size()) < 0.001)
        return false;
    for (int block = 0; block < 24; ++block)
        stats = engine.process(left.data(), right.data(), 1024);
    return stats.activeVoices == 0 && rms(right, right.size()) == 0.0;
}

inline bool run()
{
    const struct Case
    {
        const char* name;
        bool (*check)();
    } cases[] {
        { "native A1 octave, waveform and voices", oscillatorControls },
        { "native oscillator stereo separation", stereoSeparation },
        { "native independent A/B filters and envelopes", independentFiltersAndEnvelopes },
        { "native filter input crossrouting", crossRouting },
        { "native independent second envelope and LFO routes", secondModulationSources },
        { "part/master volume endpoints silence held notes and restore", volumeEndpoints },
        { "shared resonance route affects independent A/B filters", sharedResonanceRoute },
        { "global modulation selects most recent active voice without attenuation", globalModulationVoiceSelection },
        { "Part B release continues after Part A and ends the voice", independentPartReleaseLifetime }
    };
    auto passed = true;
    for (const auto& item : cases)
    {
        if (!item.check())
        {
            std::cerr << item.name << " failed.\n";
            passed = false;
        }
    }
    return passed;
}
} // namespace native_validation
