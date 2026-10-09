#pragma once

#include "NativeArchitectureTests.h"
#include "../../src/dsp/Arpeggiator.h"
#include "../../src/dsp/fx/FxChain.h"

#include <array>
#include <vector>

namespace native_arp_effects_validation
{
inline synth::SynthParameters arpParameters()
{
    auto result = native_validation::parameters();
    result.sync = true;
    result.tempoBpm = 120.0f;
    result.arp.enabled = true;
    result.arp.rate = synth::ArpRateDivision::Sixteenth;
    result.arp.gate = 1.0f;
    result.arp.stepCount = 2;
    return result;
}

inline std::vector<int> sequence(synth::SynthParameters state, int steps = 8)
{
    synth::Arpeggiator arpeggiator;
    arpeggiator.reset();
    arpeggiator.noteOn(67, 0.8f);
    arpeggiator.noteOn(60, 0.4f);
    arpeggiator.noteOn(64, 0.6f);
    std::vector<int> notes;
    for (int sample = 0; sample < steps * 1000; ++sample)
    {
        const auto event = arpeggiator.processSample(state, 8000.0);
        if (event.noteOnCount > 0)
            notes.push_back(event.noteOnNumbers[0]);
    }
    return notes;
}

inline bool modesAndWrap()
{
    const struct Case
    {
        synth::ArpMode mode;
        std::vector<int> notes;
    } cases[] {
        { synth::ArpMode::Up, { 60, 64, 67, 60, 64, 67, 60, 64 } },
        { synth::ArpMode::Down, { 67, 64, 60, 67, 64, 60, 67, 64 } },
        { synth::ArpMode::UpDown, { 60, 64, 67, 64, 60, 64, 67, 64 } },
        { synth::ArpMode::DownUp, { 67, 64, 60, 64, 67, 64, 60, 64 } },
        { synth::ArpMode::UpDownRepeat, { 60, 64, 67, 67, 64, 60, 60, 64 } },
        { synth::ArpMode::DownUpRepeat, { 67, 64, 60, 60, 64, 67, 67, 64 } },
        { synth::ArpMode::AsPlayed, { 67, 60, 64, 67, 60, 64, 67, 60 } }
    };
    auto state = arpParameters();
    state.arp.steps[1].pitchSemitones = 7;
    for (const auto& item : cases)
    {
        state.arp.mode = item.mode;
        if (sequence(state) != item.notes)
            return false;
    }
    state.arp.mode = synth::ArpMode::Up;
    state.arp.wrap = 2;
    if (sequence(state) != std::vector<int> { 60, 64, 60, 64, 60, 64, 60, 64 })
        return false;
    state.arp.wrap = 0;
    state.arp.mode = synth::ArpMode::StepSequence;
    if (sequence(state, 4) != std::vector<int> { 60, 67, 60, 67 })
        return false;
    state.arp.mode = synth::ArpMode::Random;
    const auto random = sequence(state, 32);
    if (random != sequence(state, 32) || random.size() != 32)
        return false;
    auto distinct = false;
    for (const auto note : random)
    {
        if (note != 60 && note != 64 && note != 67)
            return false;
        distinct = distinct || note != random[0];
    }
    return distinct;
}

inline bool tripletAndDottedTiming()
{
    const std::array<std::pair<synth::ArpRateDivision, int>, 3> cases {
        std::pair { synth::ArpRateDivision::SixteenthTriplet, 667 },
        std::pair { synth::ArpRateDivision::DottedSixteenth, 1500 },
        std::pair { synth::ArpRateDivision::ThirtySecondTriplet, 333 }
    };
    for (const auto& item : cases)
    {
        auto state = arpParameters();
        state.arp.rate = item.first;
        synth::Arpeggiator arpeggiator;
        arpeggiator.reset();
        arpeggiator.noteOn(60, 0.8f);
        arpeggiator.noteOn(64, 0.8f);
        if (arpeggiator.processSample(state, 8000.0).noteOnCount != 1)
            return false;
        for (int sample = 1; sample <= item.second; ++sample)
        {
            const auto event = arpeggiator.processSample(state, 8000.0);
            if ((sample == item.second && event.noteOnCount != 1)
                || (sample < item.second && event.noteOnCount != 0))
                return false;
        }
    }
    return true;
}

inline bool velocityAndChordRelease()
{
    auto state = arpParameters();
    state.arp.steps[0].velocity = 0.5f;
    const std::array<synth::ArpVelocityMode, 5> modes {
        synth::ArpVelocityMode::Step, synth::ArpVelocityMode::Key, synth::ArpVelocityMode::Hold,
        synth::ArpVelocityMode::StepKey, synth::ArpVelocityMode::StepHold
    };
    const std::array<float, 5> expected { 0.5f, 0.4f, 0.8f, 0.2f, 0.4f };
    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        state.arp.velocityMode = modes[index];
        synth::Arpeggiator arpeggiator;
        arpeggiator.reset();
        arpeggiator.noteOn(60, 0.4f);
        arpeggiator.noteOn(67, 0.8f);
        const auto event = arpeggiator.processSample(state, 8000.0);
        if (event.noteOnCount != 1 || std::abs(event.velocities[0] - expected[index]) > 1.0e-6f)
            return false;
    }
    state.arp.mode = synth::ArpMode::StepChord;
    synth::Arpeggiator chord;
    chord.reset();
    for (const auto note : { 60, 64, 67 })
        chord.noteOn(note, 0.8f);
    const auto on = chord.processSample(state, 8000.0);
    if (on.noteOnCount != 3 || on.noteOnNumbers[0] != 60 || on.noteOnNumbers[1] != 64 || on.noteOnNumbers[2] != 67)
        return false;
    for (const auto note : { 60, 64, 67 })
        chord.noteOff(note, false);
    const auto off = chord.processSample(state, 8000.0);
    return off.noteOffCount == 3 && off.noteOnCount == 0
        && off.noteOffNumbers[0] == 60 && off.noteOffNumbers[1] == 64 && off.noteOffNumbers[2] == 67
        && chord.getStepVelocity() == 0.0f;
}

inline bool tiedSequenceRetainsNotes()
{
    auto state = arpParameters();
    state.arp.mode = synth::ArpMode::StepSequence;
    state.arp.stepCount = 4;
    state.arp.steps[0].tie = true;
    state.arp.steps[1].tie = true;
    state.arp.steps[2].pitchSemitones = 7;
    state.arp.steps[3].enabled = false;
    synth::Arpeggiator arpeggiator;
    arpeggiator.reset();
    arpeggiator.noteOn(60, 0.8f);
    const auto first = arpeggiator.processSample(state, 8000.0);
    if (first.noteOnCount != 1 || first.noteOnNumbers[0] != 60)
        return false;
    auto nextStep = [&] {
        synth::ArpGeneratedEvent event;
        for (int sample = 0; sample < 1000; ++sample)
            event = arpeggiator.processSample(state, 8000.0);
        return event;
    };
    const auto unchanged = nextStep();
    const auto changed = nextStep();
    const auto rest = nextStep();
    return unchanged.noteOnCount == 0 && unchanged.noteOffCount == 0
        && changed.noteOnBeforeNoteOff && changed.noteOnCount == 1 && changed.noteOnNumbers[0] == 67
        && changed.noteOffCount == 1 && changed.noteOffNumbers[0] == 60
        && rest.noteOnCount == 0 && rest.noteOffCount == 1 && rest.noteOffNumbers[0] == 67;
}

inline bool stepVelocityRoute()
{
    auto state = arpParameters();
    state.arp.steps[0].velocity = 0.8f;
    state.arp.steps[1].velocity = 0.3f;
    const auto baseline = native_validation::render(state);
    auto& slot = state.transMod.slots[0];
    slot.enabled = true;
    slot.source = synth::ModSource::StepVelocity;
    slot.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::OscA1Pitch)] = 12.0f;
    const auto routed = native_validation::render(state);
    slot.enabled = false;
    const auto cleared = native_validation::render(state);
    return baseline.finite && routed.finite && cleared.finite
        && native_validation::difference(baseline.left, routed.left) > 1.0e-3f
        && native_validation::difference(baseline.left, cleared.left) < 1.0e-7f;
}

inline synth::SynthParameters fxParameters()
{
    auto state = native_validation::parameters();
    state.fx.enabled = true;
    state.fx.saturationEnabled = false;
    state.fx.phaserEnabled = false;
    state.fx.chorusEnabled = false;
    state.fx.eqEnabled = false;
    state.fx.reverbEnabled = false;
    state.fx.delayEnabled = false;
    state.fx.compressorEnabled = false;
    return state;
}

inline bool independentDelayAndWidth()
{
    auto state = fxParameters();
    state.sync = false;
    state.fx.delayEnabled = true;
    state.fx.delaySync = false;
    state.fx.delayMix = 1.0f;
    state.fx.delayTimeLeftMs = 10.0f;
    state.fx.delayTimeRightMs = 20.0f;
    state.fx.delayFeedback = 0.0f;
    state.fx.delayPingPong = false;
    synth::FxChain chain;
    chain.prepare(8000.0, 1);
    for (int sample = 0; sample < 180; ++sample)
    {
        const auto output = chain.process(sample == 0 ? synth::FxStereoFrame { 0.3f, 0.2f } : synth::FxStereoFrame {}, state);
        const auto expectedLeft = sample == 80 ? 0.3f : 0.0f;
        const auto expectedRight = sample == 160 ? 0.2f : 0.0f;
        if (!std::isfinite(output.left) || !std::isfinite(output.right)
            || std::abs(output.left - expectedLeft) > 1.0e-6f || std::abs(output.right - expectedRight) > 1.0e-6f)
            return false;
    }
    chain.reset();
    state.fx.delayWidth = 0.0f;
    auto heard = false;
    for (int sample = 0; sample < 180; ++sample)
    {
        const auto output = chain.process(sample == 0 ? synth::FxStereoFrame { 0.3f, 0.2f } : synth::FxStereoFrame {}, state);
        if (std::abs(output.left - output.right) > 1.0e-7f)
            return false;
        heard = heard || std::abs(output.left) > 1.0e-4f;
    }
    state.fx.enabled = false;
    const auto bypass = chain.process({ 0.1f, -0.2f }, state);
    return heard && bypass.left == 0.1f && bypass.right == -0.2f;
}

inline bool reverbPreDelayAndTail()
{
    auto state = fxParameters();
    state.fx.reverbEnabled = true;
    state.fx.reverbMix = 1.0f;
    state.fx.reverbPreDelayMs = 100.0f;
    state.fx.reverbDecay = 0.4f;
    state.fx.reverbWidth = 0.0f;
    synth::FxChain chain;
    chain.prepare(8000.0, 1);
    auto heard = false;
    for (int sample = 0; sample < 8000; ++sample)
    {
        const auto output = chain.process(sample == 0 ? synth::FxStereoFrame { 0.5f, 0.0f } : synth::FxStereoFrame {}, state);
        if (!std::isfinite(output.left) || !std::isfinite(output.right) || std::abs(output.left - output.right) > 1.0e-7f)
            return false;
        if (sample < 800 && (std::abs(output.left) > 1.0e-7f || std::abs(output.right) > 1.0e-7f))
            return false;
        heard = heard || std::abs(output.left) > 1.0e-4f;
    }
    const auto tail = synth::fxTailLengthSeconds(state);
    state.fx.reverbPreDelayMs = 0.0f;
    const auto noPreDelayTail = synth::fxTailLengthSeconds(state);
    return heard && std::isfinite(tail) && tail >= noPreDelayTail + 0.099f;
}

inline bool compressorAttackAndRelease()
{
    auto fast = fxParameters();
    fast.fx.compressorEnabled = true;
    fast.fx.compressorMix = 1.0f;
    fast.fx.compressorRatio = 8.0f;
    fast.fx.compressorThresholdDb = -18.0f;
    fast.fx.compressorAttackMs = 1.0f;
    fast.fx.compressorReleaseMs = 20.0f;
    auto slow = fast;
    slow.fx.compressorAttackMs = 100.0f;
    slow.fx.compressorReleaseMs = 500.0f;
    synth::FxChain fastChain;
    synth::FxChain slowChain;
    fastChain.prepare(8000.0, 1);
    slowChain.prepare(8000.0, 1);
    synth::FxStereoFrame fastOutput, slowOutput;
    for (int sample = 0; sample < 60; ++sample)
    {
        fastOutput = fastChain.process({ 0.8f, 0.8f }, fast);
        slowOutput = slowChain.process({ 0.8f, 0.8f }, slow);
    }
    if (!std::isfinite(fastOutput.left) || !std::isfinite(slowOutput.left) || fastOutput.left >= slowOutput.left * 0.8f)
        return false;
    for (int sample = 0; sample < 8000; ++sample)
    {
        fastChain.process({ 0.8f, 0.8f }, fast);
        slowChain.process({ 0.8f, 0.8f }, slow);
    }
    for (int sample = 0; sample < 800; ++sample)
    {
        fastOutput = fastChain.process({ 0.05f, 0.05f }, fast);
        slowOutput = slowChain.process({ 0.05f, 0.05f }, slow);
    }
    return std::isfinite(fastOutput.left) && std::isfinite(slowOutput.left) && fastOutput.left > slowOutput.left * 1.5f;
}

inline bool effectSyncPreservesPhase()
{
    for (const auto phaser : { false, true })
    {
        auto free = fxParameters();
        free.fx.phaserEnabled = phaser;
        free.fx.phaserMix = 0.7f;
        free.fx.phaserRateHz = 0.5f;
        free.fx.chorusEnabled = !phaser;
        free.fx.chorusMix = 0.7f;
        free.fx.chorusRateHz = 0.5f;
        free.tempoBpm = 120.0f;
        auto synced = free;
        synced.sync = true;
        synth::FxChain freeChain, syncedChain;
        freeChain.prepare(8000.0, 1);
        syncedChain.prepare(8000.0, 1);
        for (int sample = 0; sample < 8000; ++sample)
        {
            if (sample == 4000)
            {
                synced.tempoBpm = 240.0f;
                free.fx.phaserRateHz = free.fx.chorusRateHz = 1.0f;
            }
            const auto input = synth::FxStereoFrame { 0.1f * std::sin(static_cast<float>(sample) * 0.11f),
                                                      0.08f * std::sin(static_cast<float>(sample) * 0.17f) };
            const auto actual = syncedChain.process(input, synced);
            const auto expected = freeChain.process(input, free);
            if (!std::isfinite(actual.left) || !std::isfinite(actual.right)
                || std::abs(actual.left - expected.left) > 1.0e-7f || std::abs(actual.right - expected.right) > 1.0e-7f)
                return false;
        }
    }
    return true;
}

inline bool fixedEffectsOrder()
{
    auto state = fxParameters();
    state.fx.saturationEnabled = state.fx.phaserEnabled = state.fx.chorusEnabled = state.fx.eqEnabled = true;
    state.fx.reverbEnabled = state.fx.delayEnabled = state.fx.compressorEnabled = true;
    state.fx.saturationMix = 0.2f;
    state.fx.phaserMix = 0.25f;
    state.fx.chorusMix = 0.25f;
    state.fx.eqLowGainDb = 3.0f;
    state.fx.eqHighGainDb = -3.0f;
    state.fx.reverbMix = 0.3f;
    state.fx.delayMix = 0.3f;
    state.fx.delaySync = false;
    state.fx.delayTimeLeftMs = 10.0f;
    state.fx.delayTimeRightMs = 23.0f;
    state.fx.compressorMix = 0.5f;
    synth::FxChain rack;
    rack.prepare(8000.0, 1);
    std::array<synth::FxChain, 7> modules;
    std::array<synth::SynthParameters, 7> states;
    for (std::size_t index = 0; index < modules.size(); ++index)
    {
        modules[index].prepare(8000.0, 1);
        states[index] = state;
        auto& effects = states[index].fx;
        effects.saturationEnabled = index == 0;
        effects.phaserEnabled = index == 1;
        effects.chorusEnabled = index == 2;
        effects.eqEnabled = index == 3;
        effects.reverbEnabled = index == 4;
        effects.delayEnabled = index == 5;
        effects.compressorEnabled = index == 6;
    }
    for (int sample = 0; sample < 8000; ++sample)
    {
        const auto input = synth::FxStereoFrame { 0.01f * std::sin(static_cast<float>(sample) * 0.17f),
                                                  0.008f * std::sin(static_cast<float>(sample) * 0.31f) };
        const auto actual = rack.process(input, state);
        auto expected = input;
        for (std::size_t index = 0; index < modules.size(); ++index)
            expected = modules[index].process(expected, states[index]);
        if (!std::isfinite(actual.left) || !std::isfinite(actual.right)
            || std::abs(actual.left - expected.left) > 1.0e-6f || std::abs(actual.right - expected.right) > 1.0e-6f)
            return false;
    }
    return true;
}

inline bool run()
{
    const struct Case
    {
        const char* name;
        bool (*check)();
    } cases[] {
        { "native arpeggiator mode sequences and wrap", modesAndWrap },
        { "native arpeggiator triplet and dotted sample timing", tripletAndDottedTiming },
        { "native arpeggiator velocity modes and chord releases", velocityAndChordRelease },
        { "native tied sequence preserves common notes and releases rests", tiedSequenceRetainsNotes },
        { "native arpeggiator step velocity modulation route", stepVelocityRoute },
        { "native independent delay times, wet signal and width", independentDelayAndWidth },
        { "native reverb predelay, width and tail", reverbPreDelayAndTail },
        { "native compressor attack and release response", compressorAttackAndRelease },
        { "phaser/chorus sync tempo changes preserve phase", effectSyncPreservesPhase },
        { "native documented effects order", fixedEffectsOrder }
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
} // namespace native_arp_effects_validation
