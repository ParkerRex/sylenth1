#include "VoiceAllocator.h"

#include <algorithm>
#include <cmath>

namespace synth
{
namespace
{
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

int effectiveUnisonCount(const SynthParameters& parameters) noexcept
{
    if (parameters.voiceMode == VoiceMode::Mono || parameters.voiceMode == VoiceMode::MonoLegato)
        return 1;

    return std::clamp(parameters.unisonCount, 1, 8);
}

int effectiveVoiceLimit(const SynthParameters& parameters, int availableVoices) noexcept
{
    const auto logicalPolyphony = parameters.voiceMode == VoiceMode::Poly
        ? std::max(1, parameters.polyphony)
        : 1;
    return std::clamp(logicalPolyphony * effectiveUnisonCount(parameters), 1, std::max(1, availableVoices));
}

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
} // namespace

VoiceAllocator::VoiceAllocator(int maxVoices)
    : voices(static_cast<std::size_t>(std::clamp(maxVoices, 1, maxVoiceSlots)))
{
}

void VoiceAllocator::prepare(double sampleRate)
{
    monoLfo.prepare(sampleRate);
    monoLfo2.prepare(sampleRate);
    monoLfo.setShape(LfoShape::SawDown);
    monoLfo.setRateHz(2.0f);

    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        auto& voice = voices[static_cast<std::size_t>(i)];
        voice.prepare(sampleRate);
        voice.setVoiceIndex(i, static_cast<int>(voices.size()));
    }
}

int VoiceAllocator::noteOn(int midiNote, float velocity, const SynthParameters& parameters) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(heldNotes.size()))
        return -1;

    const auto alreadyHeld = anyHeldNote();
    const auto safeVelocity = std::isfinite(velocity) ? std::clamp(velocity, 0.0f, 1.0f) : 0.0f;
    heldNotes[static_cast<std::size_t>(midiNote)] = true;
    heldVelocities[static_cast<std::size_t>(midiNote)] = safeVelocity;
    heldOrder[static_cast<std::size_t>(midiNote)] = ++noteOrderCounter;
    sustainedNotes[static_cast<std::size_t>(midiNote)] = false;

    enforceVoiceLimit(parameters);

    const auto monoMode = parameters.voiceMode == VoiceMode::Mono || parameters.voiceMode == VoiceMode::MonoLegato;
    const auto unisonCount = effectiveUnisonCount(parameters);
    const auto maxActiveVoices = effectiveVoiceLimit(parameters, static_cast<int>(voices.size()));
    const auto allowGlide = lastPlayedNote >= 0 && (alreadyHeld || parameters.portamentoMode == PortamentoMode::Slide);
    const auto retriggerModulators = !(parameters.voiceMode == VoiceMode::MonoLegato && alreadyHeld);
    const auto triggerOrder = ++voiceTriggerCounter;
    auto firstVoiceIndex = -1;
    std::array<bool, maxVoiceSlots> usedThisNote {};

    for (int unison = 0; unison < unisonCount; ++unison)
    {
        auto voiceIndex = activeVoiceCount() < maxActiveVoices ? findIdleVoice(usedThisNote) : -1;
        if (voiceIndex < 0)
            voiceIndex = findVoiceToSteal(maxActiveVoices, usedThisNote);

        if (voiceIndex < 0)
            continue;

        auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
        voice.setAllocationIndices(voiceIndex, maxActiveVoices, unison, unisonCount);
        voice.noteOn(midiNote, safeVelocity,
                     nextRandom(), unison, unisonCount, parameters,
                     allowGlide, retriggerModulators, static_cast<float>(lastPlayedNote));
        voiceTriggerOrders[static_cast<std::size_t>(voiceIndex)] = triggerOrder;
        markVoiceActive(voiceIndex);
        if (voiceIndex < static_cast<int>(usedThisNote.size()))
            usedThisNote[static_cast<std::size_t>(voiceIndex)] = true;
        if (firstVoiceIndex < 0)
            firstVoiceIndex = voiceIndex;

        if (monoMode)
            break;
    }

    lastPlayedNote = midiNote;
    return firstVoiceIndex;
}

void VoiceAllocator::noteOff(int midiNote) noexcept
{
    SynthParameters parameters;
    noteOff(midiNote, parameters);
}

void VoiceAllocator::noteOff(int midiNote, const SynthParameters& parameters) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(heldNotes.size()))
        return;

    heldNotes[static_cast<std::size_t>(midiNote)] = false;
    if (sustainPedalDown)
    {
        sustainedNotes[static_cast<std::size_t>(midiNote)] = true;
    }
    else
    {
        heldVelocities[static_cast<std::size_t>(midiNote)] = 0.0f;
        heldOrder[static_cast<std::size_t>(midiNote)] = 0;
    }

    if (parameters.voiceMode == VoiceMode::Mono
        || parameters.voiceMode == VoiceMode::MonoLegato
        || parameters.voiceMode == VoiceMode::Unison)
    {
        const auto resumeNote = mostRecentResumeNote(sustainPedalDown, midiNote);
        if (resumeNote >= 0
            && retargetActiveNote(midiNote, resumeNote,
                                  heldVelocities[static_cast<std::size_t>(resumeNote)], parameters))
            return;
    }

    if (sustainPedalDown)
        return;

    for (auto& voice : voices)
    {
        if (voice.isActive() && voice.getMidiNote() == midiNote)
            voice.noteOff();
    }
}

void VoiceAllocator::setSustainPedal(bool down) noexcept
{
    if (sustainPedalDown == down)
        return;

    sustainPedalDown = down;
    if (!sustainPedalDown)
        releaseSustainedNotes();
}

void VoiceAllocator::allNotesOff() noexcept
{
    heldNotes.fill(false);
    if (sustainPedalDown)
    {
        for (auto& voice : voices)
        {
            if (voice.isActive() && voice.getMidiNote() >= 0 && voice.getMidiNote() < static_cast<int>(sustainedNotes.size()))
                sustainedNotes[static_cast<std::size_t>(voice.getMidiNote())] = true;
        }
        return;
    }

    heldVelocities.fill(0.0f);
    heldOrder.fill(0);
    for (auto& voice : voices)
    {
        if (voice.isActive())
            voice.noteOff();
    }
}

void VoiceAllocator::panic() noexcept
{
    sustainPedalDown = false;
    heldNotes.fill(false);
    sustainedNotes.fill(false);
    heldVelocities.fill(0.0f);
    heldOrder.fill(0);
    for (auto& voice : voices)
        voice.reset();
    activeVoiceSlotCount = 0;
    voiceTriggerOrders.fill(0);
    voiceTriggerCounter = 0;
    lastPlayedNote = -1;
}

void VoiceAllocator::stopAllWithFade(int fadeSamples) noexcept
{
    sustainPedalDown = false;
    heldNotes.fill(false);
    sustainedNotes.fill(false);
    heldVelocities.fill(0.0f);
    heldOrder.fill(0);
    for (auto& voice : voices)
        voice.stopWithFade(fadeSamples);
}

void VoiceAllocator::syncVoiceLimit(const SynthParameters& parameters) noexcept
{
    enforceVoiceLimit(parameters);
}

void VoiceAllocator::syncActiveVoiceModulators(const SynthParameters& parameters) noexcept
{
    for (int activeIndex = 0; activeIndex < activeVoiceSlotCount;)
    {
        const auto voiceIndex = activeVoiceIndices[static_cast<std::size_t>(activeIndex)];
        if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        voice.syncModulators(parameters);
        ++activeIndex;
    }
}

void VoiceAllocator::process(int numSamples) noexcept
{
    for (int activeIndex = 0; activeIndex < activeVoiceSlotCount;)
    {
        const auto voiceIndex = activeVoiceIndices[static_cast<std::size_t>(activeIndex)];
        if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        voice.process(numSamples);
        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        ++activeIndex;
    }
}

void VoiceAllocator::advanceIdleModulators(int numSamples, const SynthParameters& parameters) noexcept
{
    if (numSamples <= 0 || activeVoiceCount() > 0) return;
    const auto advanceFirst = parameters.lfo.free || parameters.lfo.mono
        || parameters.lfo.gateMode == LfoGateMode::Mono || parameters.lfo.gateMode == LfoGateMode::Song;
    const auto advanceSecond = parameters.lfo2.free || parameters.lfo2.mono
        || parameters.lfo2.gateMode == LfoGateMode::Mono || parameters.lfo2.gateMode == LfoGateMode::Song;
    if (!advanceFirst && !advanceSecond) return;
    if (advanceFirst) syncMonoLfoConfig(parameters);
    if (advanceSecond) syncSecondMonoLfoConfig(parameters);
    for (int index = 0; index < numSamples; ++index)
    {
        if (advanceFirst) monoLfo.process();
        if (advanceSecond) monoLfo2.process();
    }
}

StereoFrame VoiceAllocator::renderSample(const SynthParameters& parameters) noexcept
{
    const auto useMonoLfo = parameters.lfo.free || parameters.lfo.mono
        || parameters.lfo.gateMode == LfoGateMode::Mono
        || parameters.lfo.gateMode == LfoGateMode::Song;
    LfoFrame monoValue;
    LfoFrame monoValue2;
    const auto useMonoLfo2 = (parameters.lfo2.free || parameters.lfo2.mono
                              || parameters.lfo2.gateMode == LfoGateMode::Mono || parameters.lfo2.gateMode == LfoGateMode::Song);
    if (useMonoLfo2)
    {
        syncSecondMonoLfoConfig(parameters);
        monoValue2.value = monoLfo2.process();
        monoValue2.phase = monoLfo2.getPhase();
    }
    if (useMonoLfo)
    {
        syncMonoLfoConfig(parameters);
        monoValue.value = monoLfo.process();
        monoValue.phase = monoLfo.getPhase();
    }

    StereoFrame frame;
    auto normalizationPower = 0.0f;
    std::uint64_t selectedTriggerOrder = 0;
    auto selectedUnisonIndex = maxVoiceSlots;
    for (int activeIndex = 0; activeIndex < activeVoiceSlotCount;)
    {
        const auto voiceIndex = activeVoiceIndices[static_cast<std::size_t>(activeIndex)];
        if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        normalizationPower += voice.isStopFading() ? voice.normalizationPowerWeight() : 1.0f;
        const auto voiceFrame = voice.renderSample(parameters, useMonoLfo ? &monoValue : nullptr, useMonoLfo2 ? &monoValue2 : nullptr);
        frame.left += voiceFrame.left;
        frame.right += voiceFrame.right;

        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        const auto triggerOrder = voiceTriggerOrders[static_cast<std::size_t>(voiceIndex)];
        if (triggerOrder > selectedTriggerOrder
            || (triggerOrder == selectedTriggerOrder && voice.getUnisonIndex() < selectedUnisonIndex))
        {
            selectedTriggerOrder = triggerOrder;
            selectedUnisonIndex = voice.getUnisonIndex();
            frame.phaserCenterOffsetHz = voiceFrame.phaserCenterOffsetHz;
        }
        ++activeIndex;
    }

    if (normalizationPower > 1.0f)
    {
        const auto compensation = inverseSqrtForWeight(normalizationPower);
        frame.left *= compensation;
        frame.right *= compensation;
    }

    return frame;
}

void VoiceAllocator::renderBlock(const SynthParameters& parameters, float* outLeft, float* outRight,
                                 int numSamples, float* phaserCenterOffsetsHz) noexcept
{
    numSamples = std::min(numSamples, renderBlockMaxSamples);
    if (numSamples <= 0)
        return;

    for (int index = 0; index < numSamples; ++index)
    {
        const auto frame = renderSample(parameters);
        outLeft[index] = frame.left;
        outRight[index] = frame.right;
        if (phaserCenterOffsetsHz != nullptr) phaserCenterOffsetsHz[index] = frame.phaserCenterOffsetHz;
    }
}

void VoiceAllocator::syncSecondMonoLfoConfig(const SynthParameters& parameters) noexcept
{
    const auto& settings = parameters.lfo2;
    monoLfo2.setShape(settings.shape == LfoShapeChoice::Noise ? LfoShape::Noise : toLfoShape(settings.shape));
    monoLfo2.setRateHz(settings.free || !parameters.sync ? settings.rateHz
                                                         : parameters.tempoBpm / (60.0f * syncDivisionBeats(settings.syncDivision)));
    monoLfo2.setPhaseDegrees(settings.phaseDegrees);
    monoLfo2.setSteps(settings.steps.data(), settings.stepCount, settings.stepSmooth);
}

void VoiceAllocator::syncMonoLfoConfig(const SynthParameters& parameters) noexcept
{
    monoLfo.setShape(parameters.lfo.shape == LfoShapeChoice::Noise ? LfoShape::Noise : toLfoShape(parameters.lfo.shape));
    monoLfo.setRateHz(parameters.lfo.free || !parameters.sync
                          ? parameters.lfo.rateHz
                          : parameters.tempoBpm / (60.0f * syncDivisionBeats(parameters.lfo.syncDivision)));
    monoLfo.setPhaseDegrees(parameters.lfo.phaseDegrees);
    monoLfo.setSteps(parameters.lfo.steps.data(), parameters.lfo.stepCount, parameters.lfo.stepSmooth);
}

int VoiceAllocator::activeVoiceCount() const noexcept
{
    return activeVoiceSlotCount;
}

const Voice* VoiceAllocator::getVoice(int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(voices.size()))
        return nullptr;

    return &voices[static_cast<std::size_t>(index)];
}

void VoiceAllocator::markVoiceActive(int voiceIndex) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        return;

    for (int activeIndex = 0; activeIndex < activeVoiceSlotCount; ++activeIndex)
        if (activeVoiceIndices[static_cast<std::size_t>(activeIndex)] == voiceIndex)
            return;

    if (activeVoiceSlotCount >= static_cast<int>(activeVoiceIndices.size()))
        return;

    activeVoiceIndices[static_cast<std::size_t>(activeVoiceSlotCount++)] = voiceIndex;
}

void VoiceAllocator::removeActiveVoiceAt(int activeListIndex) noexcept
{
    if (activeListIndex < 0 || activeListIndex >= activeVoiceSlotCount)
        return;

    --activeVoiceSlotCount;
    activeVoiceIndices[static_cast<std::size_t>(activeListIndex)] =
        activeVoiceIndices[static_cast<std::size_t>(activeVoiceSlotCount)];
    activeVoiceIndices[static_cast<std::size_t>(activeVoiceSlotCount)] = 0;
}

float VoiceAllocator::nextRandom() noexcept
{
    randomState = randomState * 1664525u + 1013904223u;
    return (static_cast<float>((randomState >> 8) & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

int VoiceAllocator::findIdleVoice(const std::array<bool, maxVoiceSlots>& excluded) const noexcept
{
    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (i < static_cast<int>(excluded.size()) && excluded[static_cast<std::size_t>(i)])
            continue;

        if (!voices[static_cast<std::size_t>(i)].isActive())
            return i;
    }

    return -1;
}

int VoiceAllocator::findVoiceToSteal(int maxActiveVoices, const std::array<bool, maxVoiceSlots>& excluded) const noexcept
{
    for (int i = 0; i < static_cast<int>(voices.size()) && i < maxActiveVoices; ++i)
    {
        if (i < static_cast<int>(excluded.size()) && excluded[static_cast<std::size_t>(i)])
            continue;

        if (voices[static_cast<std::size_t>(i)].isActive() && !voices[static_cast<std::size_t>(i)].isHeld())
            return i;
    }

    for (int i = 0; i < static_cast<int>(voices.size()) && i < maxActiveVoices; ++i)
    {
        if (i < static_cast<int>(excluded.size()) && excluded[static_cast<std::size_t>(i)])
            continue;

        return i;
    }

    return -1;
}

void VoiceAllocator::releaseSustainedNotes() noexcept
{
    const auto wasSustained = sustainedNotes;
    for (auto& voice : voices)
    {
        const auto note = voice.getMidiNote();
        if (voice.isActive()
            && note >= 0
            && note < static_cast<int>(sustainedNotes.size())
            && sustainedNotes[static_cast<std::size_t>(note)]
            && !heldNotes[static_cast<std::size_t>(note)])
        {
            voice.noteOff();
        }
    }

    for (int note = 0; note < static_cast<int>(wasSustained.size()); ++note)
    {
        if (wasSustained[static_cast<std::size_t>(note)] && !heldNotes[static_cast<std::size_t>(note)])
        {
            heldVelocities[static_cast<std::size_t>(note)] = 0.0f;
            heldOrder[static_cast<std::size_t>(note)] = 0;
        }
    }

    sustainedNotes.fill(false);
}

void VoiceAllocator::enforceVoiceLimit(const SynthParameters& parameters) noexcept
{
    const auto maxActiveVoices = effectiveVoiceLimit(parameters, static_cast<int>(voices.size()));
    const auto activeVoices = activeVoiceCount();
    const auto shapeChanged = allocationShapeChanged(parameters);
    if (activeVoices <= 0)
    {
        rememberAllocationShape(parameters);
        return;
    }

    if (!shapeChanged && activeVoices <= maxActiveVoices)
        return;

    std::array<bool, maxVoiceSlots> keep {};
    auto kept = 0;
    const auto preferredNote = mostRecentResumeNote(sustainPedalDown);
    const auto singlePitchMode = parameters.voiceMode != VoiceMode::Poly && preferredNote >= 0;
    if (singlePitchMode || activeVoices > maxActiveVoices)
    {
        if (singlePitchMode && preferredNote >= 0)
        {
            for (int i = 0; i < static_cast<int>(voices.size()) && kept < maxActiveVoices; ++i)
            {
                if (i >= static_cast<int>(keep.size()))
                    break;

                const auto& voice = voices[static_cast<std::size_t>(i)];
                if (voice.isActive() && voice.getMidiNote() == preferredNote)
                {
                    keep[static_cast<std::size_t>(i)] = true;
                    ++kept;
                }
            }
        }
        else if (!singlePitchMode)
        {
            std::array<bool, 128> selectedNotes {};
            while (kept < maxActiveVoices)
            {
                auto bestNote = -1;
                std::uint64_t bestOrder = 0;
                for (int note = 0; note < static_cast<int>(heldNotes.size()); ++note)
                {
                    const auto index = static_cast<std::size_t>(note);
                    if (heldNotes[index] && !selectedNotes[index] && heldOrder[index] > bestOrder)
                    {
                        bestOrder = heldOrder[index];
                        bestNote = note;
                    }
                }

                if (bestNote < 0)
                    break;

                selectedNotes[static_cast<std::size_t>(bestNote)] = true;
                for (int i = 0; i < static_cast<int>(voices.size()) && kept < maxActiveVoices; ++i)
                {
                    if (i >= static_cast<int>(keep.size()))
                        break;

                    const auto& voice = voices[static_cast<std::size_t>(i)];
                    if (voice.isActive()
                        && voice.getMidiNote() == bestNote
                        && !keep[static_cast<std::size_t>(i)])
                    {
                        keep[static_cast<std::size_t>(i)] = true;
                        ++kept;
                    }
                }
            }
        }

        if (kept == 0 || (!singlePitchMode && kept < maxActiveVoices))
        {
            for (int i = 0; i < static_cast<int>(voices.size()) && kept < maxActiveVoices; ++i)
            {
                if (i >= static_cast<int>(keep.size()))
                    break;

                const auto& voice = voices[static_cast<std::size_t>(i)];
                if (voice.isActive() && !keep[static_cast<std::size_t>(i)])
                {
                    keep[static_cast<std::size_t>(i)] = true;
                    ++kept;
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int>(voices.size()) && i < static_cast<int>(keep.size()); ++i)
        {
            const auto& voice = voices[static_cast<std::size_t>(i)];
            if (voice.isActive())
            {
                keep[static_cast<std::size_t>(i)] = true;
                ++kept;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (i >= static_cast<int>(keep.size()))
        {
            voices[static_cast<std::size_t>(i)].stopWithFade(64);
            continue;
        }

        auto& voice = voices[static_cast<std::size_t>(i)];
        if (voice.isActive() && !keep[static_cast<std::size_t>(i)])
            voice.stopWithFade(64);
    }

    auto allocationIndex = 0;
    for (int i = 0; i < static_cast<int>(voices.size()) && i < static_cast<int>(keep.size()); ++i)
    {
        if (!keep[static_cast<std::size_t>(i)])
            continue;

        auto& voice = voices[static_cast<std::size_t>(i)];
        if (!voice.isActive())
            continue;

        auto noteUnisonIndex = 0;
        auto noteUnisonCount = 0;
        const auto note = voice.getMidiNote();
        for (int j = 0; j < static_cast<int>(voices.size()) && j < static_cast<int>(keep.size()); ++j)
        {
            if (!keep[static_cast<std::size_t>(j)])
                continue;

            const auto& peer = voices[static_cast<std::size_t>(j)];
            if (peer.isActive() && peer.getMidiNote() == note)
            {
                if (j < i)
                    ++noteUnisonIndex;
                ++noteUnisonCount;
            }
        }

        const auto allocationTotal = singlePitchMode ? std::max(1, kept) : maxActiveVoices;
        const auto unisonCount = singlePitchMode
            ? std::max(1, kept)
            : std::max(1, noteUnisonCount);
        const auto unisonIndex = std::min(noteUnisonIndex, unisonCount - 1);
        voice.setAllocationIndices(allocationIndex, allocationTotal, unisonIndex, unisonCount);
        ++allocationIndex;
    }

    rememberAllocationShape(parameters);
}

bool VoiceAllocator::anyHeldNote() const noexcept
{
    return std::any_of(heldNotes.begin(), heldNotes.end(), [](bool held) {
        return held;
    });
}

int VoiceAllocator::mostRecentHeldNote() const noexcept
{
    auto bestNote = -1;
    std::uint64_t bestOrder = 0;
    for (int note = 0; note < static_cast<int>(heldNotes.size()); ++note)
    {
        if (heldNotes[static_cast<std::size_t>(note)]
            && heldOrder[static_cast<std::size_t>(note)] > bestOrder)
        {
            bestOrder = heldOrder[static_cast<std::size_t>(note)];
            bestNote = note;
        }
    }

    return bestNote;
}

int VoiceAllocator::mostRecentResumeNote(bool includeSustained, int excludedNote) const noexcept
{
    auto bestNote = -1;
    std::uint64_t bestOrder = 0;
    for (int note = 0; note < static_cast<int>(heldNotes.size()); ++note)
    {
        if (note == excludedNote)
            continue;

        const auto index = static_cast<std::size_t>(note);
        if ((heldNotes[index] || (includeSustained && sustainedNotes[index]))
            && heldOrder[index] > bestOrder)
        {
            bestOrder = heldOrder[index];
            bestNote = note;
        }
    }

    return bestNote;
}

bool VoiceAllocator::retargetActiveNote(int fromNote, int toNote, float velocity, const SynthParameters& parameters) noexcept
{
    const auto matchingCount = static_cast<int>(std::count_if(voices.begin(), voices.end(), [fromNote](const auto& voice) {
        return voice.isActive() && voice.getMidiNote() == fromNote;
    }));
    if (matchingCount <= 0)
        return false;

    const auto unisonCount = parameters.voiceMode == VoiceMode::Unison
        ? std::min(matchingCount, effectiveUnisonCount(parameters))
        : 1;
    const auto allocationTotal = parameters.voiceMode == VoiceMode::Unison ? unisonCount : 1;
    const auto allowGlide = true;
    const auto retriggerModulators = parameters.voiceMode != VoiceMode::MonoLegato;
    const auto triggerOrder = ++voiceTriggerCounter;
    auto unison = 0;
    for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        auto& voice = voices[voiceIndex];
        if (!voice.isActive() || voice.getMidiNote() != fromNote)
            continue;

        voice.setAllocationIndices(unison, allocationTotal, unison, unisonCount);
        voice.noteOn(toNote, std::clamp(velocity, 0.0f, 1.0f),
                     nextRandom(), unison, unisonCount, parameters,
                     allowGlide, retriggerModulators);
        voiceTriggerOrders[voiceIndex] = triggerOrder;
        ++unison;
        if (unison >= unisonCount)
            break;
    }

    return unison > 0;
}

bool VoiceAllocator::allocationShapeChanged(const SynthParameters& parameters) const noexcept
{
    return !allocationShapeInitialized
        || allocationVoiceMode != parameters.voiceMode
        || allocationPolyphony != std::clamp(parameters.polyphony, 1, 32)
        || allocationUnisonCount != std::clamp(parameters.unisonCount, 1, 8);
}

void VoiceAllocator::rememberAllocationShape(const SynthParameters& parameters) noexcept
{
    allocationVoiceMode = parameters.voiceMode;
    allocationPolyphony = std::clamp(parameters.polyphony, 1, 32);
    allocationUnisonCount = std::clamp(parameters.unisonCount, 1, 8);
    allocationShapeInitialized = true;
}
} // namespace synth
