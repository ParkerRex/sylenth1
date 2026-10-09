#pragma once

#include "SynthParameters.h"

#include <array>
#include <cstdint>

namespace synth
{
struct ArpGeneratedEvent
{
    bool noteOff = false;
    int noteOffNumber = -1;
    bool noteOn = false;
    int noteOnNumber = -1;
    float velocity = 0.0f;
    std::array<int, 64> noteOnNumbers {};
    std::array<int, 64> noteOffNumbers {};
    std::array<float, 64> velocities {};
    bool noteOnBeforeNoteOff = false;
    int noteOnCount = 0;
    int noteOffCount = 0;
};

class Arpeggiator
{
public:
    void reset() noexcept;
    void noteOn(int midiNote, float velocity) noexcept;
    void noteOff(int midiNote, bool holdEnabled) noexcept;
    ArpGeneratedEvent processSample(const SynthParameters& parameters, double sampleRate) noexcept;
    bool hasPendingEvents() const noexcept { return heldNoteCount > 0 || activeOutputCount > 0; }
    float getStepVelocity() const noexcept { return stepVelocity; }

private:
    struct Candidate
    {
        int note = 0;
        float velocity = 0.0f;
        std::uint64_t order = 0;
    };

    static constexpr int maxCandidates = 64;

    int collectCandidates(const SynthParameters& parameters,
                          std::array<Candidate, maxCandidates>& candidates) const noexcept;
    static void sortCandidates(std::array<Candidate, maxCandidates>& candidates,
                               int candidateCount,
                               ArpMode mode) noexcept;
    static bool candidateComesBefore(const Candidate& left,
                                     const Candidate& right,
                                     ArpMode mode) noexcept;
    static int stepDurationSamples(const ArpParameters& parameters,
                                   float tempoBpm,
                                   double sampleRate,
                                   int sequenceStep) noexcept;
    static int selectPosition(int sequenceStep, int totalPositions, ArpMode mode) noexcept;
    void releaseOutput(ArpGeneratedEvent& event) noexcept;

    std::array<bool, 128> heldNotes {};
    std::array<float, 128> heldVelocities {};
    std::array<std::uint64_t, 128> heldOrder {};
    std::uint64_t noteOrderCounter = 0;
    int heldNoteCount = 0;
    int activeOutputNote = -1;
    std::array<int, maxCandidates> activeOutputNotes {};
    int activeOutputCount = 0;
    float lastKeyVelocity = 1.0f;
    float stepVelocity = 0.0f;
    std::uint32_t randomState = 0x9e3779b9u;
    int samplesUntilStep = 0;
    int samplesUntilGateOff = 0;
    int sequenceStep = 0;
    bool currentStepTied = false;
    bool restartRequested = false;
};
} // namespace synth
