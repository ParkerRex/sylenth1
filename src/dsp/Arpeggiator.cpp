#include "Arpeggiator.h"

#include <algorithm>
#include <cmath>

namespace synth
{
namespace
{
float rateDivisionBeats(ArpRateDivision division) noexcept
{
    switch (division)
    {
        case ArpRateDivision::ThirtySecond: return 0.125f;
        case ArpRateDivision::Sixteenth: return 0.25f;
        case ArpRateDivision::Eighth: return 0.5f;
        case ArpRateDivision::Quarter: return 1.0f;
        case ArpRateDivision::Half: return 2.0f;
        case ArpRateDivision::DottedEighth: return 0.75f;
        case ArpRateDivision::ThirtySecondTriplet: return 1.0f / 12.0f;
        case ArpRateDivision::DottedThirtySecond: return 0.1875f;
        case ArpRateDivision::SixteenthTriplet: return 1.0f / 6.0f;
        case ArpRateDivision::DottedSixteenth: return 0.375f;
        case ArpRateDivision::EighthTriplet: return 1.0f / 3.0f;
        case ArpRateDivision::QuarterTriplet: return 2.0f / 3.0f;
        case ArpRateDivision::DottedQuarter: return 1.5f;
        case ArpRateDivision::HalfTriplet: return 4.0f / 3.0f;
        case ArpRateDivision::DottedHalf: return 3.0f;
        case ArpRateDivision::Whole: return 4.0f;
        case ArpRateDivision::WholeTriplet: return 8.0f / 3.0f;
        case ArpRateDivision::DottedWhole: return 6.0f;
    }

    return 0.25f;
}
} // namespace

void Arpeggiator::reset() noexcept
{
    heldNotes.fill(false);
    heldVelocities.fill(0.0f);
    heldOrder.fill(0);
    noteOrderCounter = 0;
    heldNoteCount = 0;
    activeOutputNote = -1;
    activeOutputCount = 0;
    lastKeyVelocity = 1.0f;
    stepVelocity = 0.0f;
    randomState = 0x9e3779b9u;
    samplesUntilStep = 0;
    samplesUntilGateOff = 0;
    sequenceStep = 0;
    currentStepTied = false;
    restartRequested = false;
}

void Arpeggiator::noteOn(int midiNote, float velocity) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(heldNotes.size()))
        return;

    const auto wasEmpty = heldNoteCount == 0;
    const auto noteIndex = static_cast<std::size_t>(midiNote);
    if (!heldNotes[noteIndex])
        ++heldNoteCount;
    heldNotes[noteIndex] = true;
    heldVelocities[noteIndex] = std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f);
    lastKeyVelocity = heldVelocities[noteIndex];
    heldOrder[noteIndex] = ++noteOrderCounter;

    if (wasEmpty)
    {
        sequenceStep = 0;
        samplesUntilStep = 0;
        samplesUntilGateOff = 0;
        currentStepTied = false;
        restartRequested = true;
    }
}

void Arpeggiator::noteOff(int midiNote, bool holdEnabled) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int>(heldNotes.size()) || holdEnabled)
        return;

    const auto noteIndex = static_cast<std::size_t>(midiNote);
    if (!heldNotes[noteIndex])
        return;

    heldNotes[noteIndex] = false;
    heldVelocities[noteIndex] = 0.0f;
    heldOrder[noteIndex] = 0;
    heldNoteCount = std::max(0, heldNoteCount - 1);

    if (heldNoteCount == 0)
        restartRequested = false;
}

void Arpeggiator::releaseOutput(ArpGeneratedEvent& event) noexcept
{
    event.noteOffCount = activeOutputCount;
    for (int i = 0; i < activeOutputCount; ++i)
        event.noteOffNumbers[static_cast<std::size_t>(i)] = activeOutputNotes[static_cast<std::size_t>(i)];
    if (activeOutputCount > 0)
    {
        event.noteOff = true;
        event.noteOffNumber = activeOutputNotes[0];
    }
    activeOutputCount = 0;
    activeOutputNote = -1;
}

ArpGeneratedEvent Arpeggiator::processSample(const SynthParameters& parameters, double sampleRate) noexcept
{
    ArpGeneratedEvent event;
    if (heldNoteCount <= 0)
    {
        releaseOutput(event);
        samplesUntilStep = samplesUntilGateOff = sequenceStep = 0;
        currentStepTied = false;
        stepVelocity = 0.0f;
        return event;
    }

    if (restartRequested || samplesUntilStep <= 0)
    {
        std::array<Candidate, maxCandidates> candidates {};
        const auto candidateCount = collectCandidates(parameters, candidates);
        const auto previousStepTied = currentStepTied;
        releaseOutput(event);
        currentStepTied = false;
        stepVelocity = 0.0f;
        if (candidateCount <= 0)
            return event;

        const auto stepCount = std::clamp(parameters.arp.stepCount, 1, arpStepCount);
        const auto stepIndex = sequenceStep % stepCount;
        const auto& step = parameters.arp.steps[static_cast<std::size_t>(stepIndex)];
        const auto octaves = std::clamp(parameters.arp.octaves, 1, 4);
        const auto totalPositions = candidateCount * octaves;
        const auto melodicStep = parameters.arp.wrap > 0
            ? sequenceStep % std::clamp(parameters.arp.wrap, 1, 128)
            : sequenceStep;
        auto position = selectPosition(melodicStep, totalPositions, parameters.arp.mode);
        if (parameters.arp.mode == ArpMode::Random)
        {
            randomState ^= randomState << 13;
            randomState ^= randomState >> 17;
            randomState ^= randomState << 5;
            position = static_cast<int>(randomState % static_cast<std::uint32_t>(totalPositions));
        }
        const auto isSequence = parameters.arp.mode == ArpMode::StepSequence;
        const auto isChord = parameters.arp.mode == ArpMode::StepChord;
        const auto candidateIndex = isSequence || isChord ? 0 : position % candidateCount;
        const auto octaveIndex = isSequence || isChord
            ? (melodicStep / stepCount) % octaves
            : position / candidateCount;
        const auto descending = parameters.arp.mode == ArpMode::Down
            || parameters.arp.mode == ArpMode::DownUp || parameters.arp.mode == ArpMode::DownUpRepeat;
        const auto octaveOffset = (descending ? octaves - 1 - octaveIndex : octaveIndex) * 12;
        const auto pitchOffset = isSequence || isChord ? std::clamp(step.pitchSemitones, -48, 48) : 0;
        auto outputVelocity = [&](float keyVelocity) noexcept {
            const auto stepValue = std::clamp(std::isfinite(step.velocity) ? step.velocity : 0.0f, 0.0f, 1.0f);
            switch (parameters.arp.velocityMode)
            {
                case ArpVelocityMode::Step: return stepValue;
                case ArpVelocityMode::Key: return keyVelocity;
                case ArpVelocityMode::Hold: return lastKeyVelocity;
                case ArpVelocityMode::StepKey: return stepValue * keyVelocity;
                case ArpVelocityMode::StepHold: return stepValue * lastKeyVelocity;
            }
            return stepValue * keyVelocity;
        };
        auto durationParameters = parameters.arp;
        durationParameters.sync = parameters.sync;
        const auto duration = stepDurationSamples(durationParameters, parameters.tempoBpm, sampleRate, sequenceStep);
        const auto rest = !step.enabled || step.velocity <= 0.0f;
        if (!rest)
        {
            const auto outputCount = isChord ? candidateCount : 1;
            for (int i = 0; i < outputCount; ++i)
            {
                const auto& candidate = candidates[static_cast<std::size_t>(isChord ? i : candidateIndex)];
                const auto velocity = outputVelocity(candidate.velocity);
                if (velocity <= 0.0f)
                    continue;
                const auto note = std::clamp(candidate.note + octaveOffset + pitchOffset, 0, 127);
                bool duplicate = false;
                for (int j = 0; j < activeOutputCount; ++j)
                    duplicate = duplicate || activeOutputNotes[static_cast<std::size_t>(j)] == note;
                if (duplicate)
                    continue;
                const auto index = static_cast<std::size_t>(activeOutputCount++);
                activeOutputNotes[index] = note;
                event.noteOnNumbers[index] = note;
                event.velocities[index] = velocity;
            }
            event.noteOnCount = activeOutputCount;
            if (activeOutputCount > 0)
            {
                event.noteOn = true;
                event.noteOnNumber = activeOutputNote = activeOutputNotes[0];
                event.velocity = event.velocities[0];
            }
            // The modulation signal describes the sequencer gate, independently of key velocity.
            stepVelocity = std::clamp(std::isfinite(step.velocity) ? step.velocity : 0.0f, 0.0f, 1.0f);
            currentStepTied = step.tie;
            const auto gateProduct = parameters.arp.gate * step.gate;
            const auto gate = std::clamp(std::isfinite(gateProduct) ? gateProduct : 0.75f, 0.02f, 1.0f);
            samplesUntilGateOff = std::clamp(static_cast<int>(static_cast<float>(duration) * gate + 0.5f), 1, duration);
        }
        else
            samplesUntilGateOff = 0;
        if (previousStepTied && event.noteOnCount > 0)
        {
            // Held steps preserve common notes and overlap changed pitches for mono legato.
            std::array<bool, maxCandidates> preserved {};
            auto newCount = 0;
            for (int i = 0; i < event.noteOnCount; ++i)
            {
                bool common = false;
                for (int j = 0; j < event.noteOffCount; ++j)
                    if (event.noteOnNumbers[static_cast<std::size_t>(i)] == event.noteOffNumbers[static_cast<std::size_t>(j)])
                    {
                        preserved[static_cast<std::size_t>(j)] = true;
                        common = true;
                    }
                if (!common)
                {
                    event.noteOnNumbers[static_cast<std::size_t>(newCount)] = event.noteOnNumbers[static_cast<std::size_t>(i)];
                    event.velocities[static_cast<std::size_t>(newCount++)] = event.velocities[static_cast<std::size_t>(i)];
                }
            }
            auto oldCount = 0;
            for (int i = 0; i < event.noteOffCount; ++i)
                if (!preserved[static_cast<std::size_t>(i)])
                    event.noteOffNumbers[static_cast<std::size_t>(oldCount++)] = event.noteOffNumbers[static_cast<std::size_t>(i)];
            event.noteOnCount = newCount;
            event.noteOffCount = oldCount;
            event.noteOn = newCount > 0;
            event.noteOff = oldCount > 0;
            event.noteOnNumber = newCount > 0 ? event.noteOnNumbers[0] : -1;
            event.noteOffNumber = oldCount > 0 ? event.noteOffNumbers[0] : -1;
            event.velocity = newCount > 0 ? event.velocities[0] : 0.0f;
            event.noteOnBeforeNoteOff = true;
        }
        samplesUntilStep = duration;
        // Bound the counter without changing the musical cycle or overflowing signed arithmetic.
        sequenceStep = sequenceStep < 1000000000 ? sequenceStep + 1 : 0;
        restartRequested = false;
    }
    else if (!currentStepTied && samplesUntilGateOff <= 0)
    {
        releaseOutput(event);
        stepVelocity = 0.0f;
    }
    if (samplesUntilStep > 0)
        --samplesUntilStep;
    if (!currentStepTied && samplesUntilGateOff > 0)
        --samplesUntilGateOff;
    return event;
}

int Arpeggiator::collectCandidates(const SynthParameters& parameters,
                                   std::array<Candidate, maxCandidates>& candidates) const noexcept
{
    auto count = 0;

    for (int midiNote = 0; midiNote < static_cast<int>(heldNotes.size()); ++midiNote)
    {
        if (!heldNotes[static_cast<std::size_t>(midiNote)])
            continue;

        auto appendCandidate = [&](int note, float velocity, int orderOffset) {
            if (count >= maxCandidates)
                return;

            auto& candidate = candidates[static_cast<std::size_t>(count++)];
            candidate.note = std::clamp(note, 0, 127);
            candidate.velocity = std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f);
            candidate.order = heldOrder[static_cast<std::size_t>(midiNote)] * 16u
                + static_cast<std::uint64_t>(orderOffset);
        };

        const auto baseVelocity = heldVelocities[static_cast<std::size_t>(midiNote)];
        if (!parameters.chord.enabled)
        {
            appendCandidate(midiNote, baseVelocity, 0);
            continue;
        }

        const auto voiceLimit = std::clamp(parameters.chord.voiceCount, 1, chordVoiceCount);
        auto addedChordVoice = false;
        for (int voiceIndex = 0; voiceIndex < voiceLimit; ++voiceIndex)
        {
            const auto& voice = parameters.chord.voices[static_cast<std::size_t>(voiceIndex)];
            if (!voice.enabled)
                continue;

            appendCandidate(midiNote + voice.pitchSemitones,
                            baseVelocity * voice.velocity,
                            voiceIndex);
            addedChordVoice = true;
        }

        if (!addedChordVoice)
            appendCandidate(midiNote, baseVelocity, 0);
    }

    sortCandidates(candidates, count, parameters.arp.mode);
    return count;
}

void Arpeggiator::sortCandidates(std::array<Candidate, maxCandidates>& candidates,
                                 int candidateCount,
                                 ArpMode mode) noexcept
{
    for (int i = 1; i < candidateCount; ++i)
    {
        const auto current = candidates[static_cast<std::size_t>(i)];
        auto j = i - 1;
        while (j >= 0 && candidateComesBefore(current, candidates[static_cast<std::size_t>(j)], mode))
        {
            const auto destinationIndex = static_cast<std::size_t>(j) + 1u;
            candidates[destinationIndex] = candidates[static_cast<std::size_t>(j)];
            --j;
        }
        const auto insertionIndex = j < 0 ? std::size_t { 0 } : static_cast<std::size_t>(j) + 1u;
        candidates[insertionIndex] = current;
    }
}

bool Arpeggiator::candidateComesBefore(const Candidate& left,
                                       const Candidate& right,
                                       ArpMode mode) noexcept
{
    if (mode == ArpMode::AsPlayed)
    {
        if (left.order != right.order)
            return left.order < right.order;
        return left.note < right.note;
    }

    if (mode == ArpMode::Down || mode == ArpMode::DownUp || mode == ArpMode::DownUpRepeat)
    {
        if (left.note != right.note)
            return left.note > right.note;
        return left.order < right.order;
    }

    if (left.note != right.note)
        return left.note < right.note;
    return left.order < right.order;
}

int Arpeggiator::stepDurationSamples(const ArpParameters& parameters,
                                     float tempoBpm,
                                     double sampleRate,
                                     int sequenceStep) noexcept
{
    const auto safeTempo = std::clamp(std::isfinite(tempoBpm) ? tempoBpm : 128.0f, 20.0f, 300.0f);
    const auto safeSampleRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? std::clamp(sampleRate, 8000.0, 384000.0) : 44100.0;
    const auto seconds = parameters.sync ? (60.0f / safeTempo) * rateDivisionBeats(parameters.rate)
                                         : std::clamp(std::isfinite(parameters.timeMs) ? parameters.timeMs : 125.0f, 1.0f, 6000.0f) * 0.001f;
    const auto swing = std::clamp(std::isfinite(parameters.swing) ? parameters.swing : 0.0f, 0.0f, 0.75f);
    const auto swingScale = (sequenceStep % 2) == 0 ? (1.0f - swing * 0.5f)
                                                    : (1.0f + swing * 0.5f);
    return std::max(1, static_cast<int>(seconds * swingScale * static_cast<float>(safeSampleRate) + 0.5f));
}

int Arpeggiator::selectPosition(int sequenceStep, int totalPositions, ArpMode mode) noexcept
{
    if (totalPositions <= 1)
        return 0;

    const auto repeat = mode == ArpMode::UpDownRepeat || mode == ArpMode::DownUpRepeat;
    if (mode != ArpMode::UpDown && mode != ArpMode::DownUp && !repeat)
        return sequenceStep % totalPositions;
    if (repeat)
    {
        const auto cyclePosition = sequenceStep % (totalPositions * 2);
        return cyclePosition < totalPositions ? cyclePosition : totalPositions * 2 - 1 - cyclePosition;
    }

    const auto cycleLength = totalPositions * 2 - 2;
    const auto cyclePosition = sequenceStep % cycleLength;
    return cyclePosition < totalPositions ? cyclePosition : cycleLength - cyclePosition;
}
} // namespace synth
