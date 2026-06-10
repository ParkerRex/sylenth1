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
        default: return 1.0f;
    }
}

float effectiveLfoRateHz(const SynthParameters& parameters) noexcept
{
    if (parameters.lfo.rateMode == LfoRateMode::Hz)
        return parameters.lfo.rateHz;

    return std::clamp(parameters.tempoBpm, 20.0f, 300.0f)
        / (60.0f * std::max(0.01f, syncDivisionBeats(parameters.lfo.syncDivision)));
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
    : voices(static_cast<std::size_t>(std::max(1, maxVoices)))
{
}

void VoiceAllocator::prepare(double sampleRate)
{
    monoLfo.prepare(sampleRate);
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
    const auto safeVelocity = std::clamp(velocity, 0.0f, 1.0f);
    heldNotes[static_cast<std::size_t>(midiNote)] = true;
    heldVelocities[static_cast<std::size_t>(midiNote)] = safeVelocity;
    heldOrder[static_cast<std::size_t>(midiNote)] = ++noteOrderCounter;
    sustainedNotes[static_cast<std::size_t>(midiNote)] = false;

    enforceVoiceLimit(parameters);

    const auto monoMode = parameters.voiceMode == VoiceMode::Mono || parameters.voiceMode == VoiceMode::MonoLegato;
    const auto unisonCount = effectiveUnisonCount(parameters);
    const auto maxActiveVoices = effectiveVoiceLimit(parameters, static_cast<int>(voices.size()));
    const auto allowGlide = parameters.voiceMode == VoiceMode::MonoLegato && alreadyHeld;
    const auto retriggerModulators = !allowGlide
        || parameters.voiceMode == VoiceMode::Mono
        || parameters.retrigger;
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
                     allowGlide, retriggerModulators);
        markVoiceActive(voiceIndex);
        if (voiceIndex < static_cast<int>(usedThisNote.size()))
            usedThisNote[static_cast<std::size_t>(voiceIndex)] = true;
        if (firstVoiceIndex < 0)
            firstVoiceIndex = voiceIndex;

        if (monoMode)
            break;
    }

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
    monoLfoConfigInitialized = false;
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

StereoFrame VoiceAllocator::renderSample(const SynthParameters& parameters) noexcept
{
    const auto useMonoLfo = parameters.lfo.mono
        || parameters.lfo.gateMode == LfoGateMode::Mono
        || parameters.lfo.gateMode == LfoGateMode::Song;
    auto monoValue = 0.0f;
    if (useMonoLfo)
    {
        syncMonoLfoConfig(parameters);
        monoValue = monoLfo.process();
    }

    StereoFrame frame;
    auto normalizationPower = 0.0f;
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
        const auto voiceFrame = voice.renderSample(parameters, useMonoLfo ? &monoValue : nullptr);
        frame.left += voiceFrame.left;
        frame.right += voiceFrame.right;

        if (!voice.isActive())
        {
            removeActiveVoiceAt(activeIndex);
            continue;
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
                                 int numSamples) noexcept
{
    numSamples = std::min(numSamples, renderBlockMaxSamples);
    if (numSamples <= 0)
        return;

    const auto useMonoLfo = parameters.lfo.mono
        || parameters.lfo.gateMode == LfoGateMode::Mono
        || parameters.lfo.gateMode == LfoGateMode::Song;
    float monoValues[renderBlockMaxSamples];
    if (useMonoLfo)
    {
        syncMonoLfoConfig(parameters);
        for (int i = 0; i < numSamples; ++i)
            monoValues[i] = monoLfo.process();
    }

    float weights[renderBlockMaxSamples];
    for (int i = 0; i < numSamples; ++i)
    {
        outLeft[i] = 0.0f;
        outRight[i] = 0.0f;
        weights[i] = 0.0f;
    }

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

        const auto stillActive = voice.renderBlock(parameters, outLeft, outRight, weights,
                                                   useMonoLfo ? monoValues : nullptr, numSamples);
        if (!stillActive)
        {
            removeActiveVoiceAt(activeIndex);
            continue;
        }

        ++activeIndex;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (weights[i] > 1.0f)
        {
            const auto compensation = inverseSqrtForWeight(weights[i]);
            outLeft[i] *= compensation;
            outRight[i] *= compensation;
        }
    }
}

void VoiceAllocator::syncMonoLfoConfig(const SynthParameters& parameters) noexcept
{
    // Step-table edits skip the config gate so live tweaks never reset phase,
    // mirroring Voice::syncModulatorConfig.
    monoLfo.setSteps(parameters.lfo.steps.data(), parameters.lfo.stepCount, parameters.lfo.stepSmooth);

    const auto lfoConfigChanged = !monoLfoConfigInitialized
        || cachedMonoLfoShape != parameters.lfo.shape
        || cachedMonoLfoRateMode != parameters.lfo.rateMode
        || cachedMonoLfoSyncDivision != parameters.lfo.syncDivision
        || std::abs(cachedMonoLfoRateHz - parameters.lfo.rateHz) > 0.000001f
        || std::abs(cachedMonoLfoPhaseDegrees - parameters.lfo.phaseDegrees) > 0.000001f
        || std::abs(cachedMonoTempoBpm - parameters.tempoBpm) > 0.000001f;

    if (!lfoConfigChanged)
        return;

    monoLfo.setShape(toLfoShape(parameters.lfo.shape));
    monoLfo.setRateHz(effectiveLfoRateHz(parameters));
    monoLfo.setPhaseDegrees(parameters.lfo.phaseDegrees);
    cachedMonoLfoShape = parameters.lfo.shape;
    cachedMonoLfoRateMode = parameters.lfo.rateMode;
    cachedMonoLfoSyncDivision = parameters.lfo.syncDivision;
    cachedMonoLfoRateHz = parameters.lfo.rateHz;
    cachedMonoLfoPhaseDegrees = parameters.lfo.phaseDegrees;
    cachedMonoTempoBpm = parameters.tempoBpm;
    monoLfoConfigInitialized = true;
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
    const auto allowGlide = parameters.voiceMode == VoiceMode::MonoLegato;
    const auto retriggerModulators = parameters.voiceMode == VoiceMode::Mono || parameters.retrigger;
    auto unison = 0;
    for (auto& voice : voices)
    {
        if (!voice.isActive() || voice.getMidiNote() != fromNote)
            continue;

        voice.setAllocationIndices(unison, allocationTotal, unison, unisonCount);
        voice.noteOn(toNote, std::clamp(velocity, 0.0f, 1.0f),
                     nextRandom(), unison, unisonCount, parameters,
                     allowGlide, retriggerModulators);
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
