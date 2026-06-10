#pragma once

#include "Arpeggiator.h"
#include "SynthParameters.h"
#include "fx/FxChain.h"
#include "../voice/VoiceAllocator.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace synth
{
struct RenderStats
{
    int samplesRendered = 0;
    int invalidSamples = 0;
    float peak = 0.0f;
    int activeVoices = 0;
};

class SynthEngine
{
public:
    void prepare(double newSampleRate, int newMaxBlockSize);
    void reset() noexcept;

    void noteOn(int midiNote, float velocity) noexcept;
    void noteOff(int midiNote) noexcept;
    void setSustainPedal(bool down) noexcept;
    void setPitchBend(float normalizedBipolar) noexcept;
    void setModWheel(float normalized) noexcept;
    void setAftertouch(float normalized) noexcept;
    void allNotesOff() noexcept;
    void panic() noexcept;
    void setParameters(const SynthParameters& newParameters) noexcept;

    RenderStats process(float* left, float* right, int numSamples) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlockSize() const noexcept { return maxBlockSize; }
    int getActiveVoiceCount() const noexcept { return voices.activeVoiceCount(); }
    const Voice* getVoice(int index) const noexcept { return voices.getVoice(index); }
    float getMonoLfoPhase() const noexcept { return voices.monoLfoPhase(); }
    float getMonoLfoValue() const noexcept { return voices.monoLfoValue(); }

private:
    struct DirectChordOutputNote
    {
        int note = -1;
        float velocity = 0.0f;
    };

    struct DirectChordInputState
    {
        bool active = false;
        int noteCount = 0;
        std::array<int, chordVoiceCount> outputNotes {};
    };

    void triggerDirectNoteOn(int midiNote, float velocity) noexcept;
    void triggerDirectNoteOff(int midiNote) noexcept;
    void processArpEvent(const ArpGeneratedEvent& event) noexcept;
    int buildDirectChordOutputNotes(int midiNote,
                                    float velocity,
                                    std::array<DirectChordOutputNote, chordVoiceCount>& outputNotes) const noexcept;
    bool releaseTrackedDirectChordInput(int midiNote) noexcept;
    bool hasActiveVoiceForNote(int midiNote) const noexcept;
    void resetDirectChordTracking() noexcept;
    void resetInputNoteTracking() noexcept;
    void rebuildArpeggiatorFromInputNotes() noexcept;
    void triggerDirectNotesFromInputNotes() noexcept;
    void resetVoicesForHeldInputRebuild() noexcept;

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    SynthParameters parameters;
    PerformanceState performance;
    Arpeggiator arpeggiator;
    std::array<bool, 128> inputHeldNotes {};
    std::array<float, 128> inputHeldVelocities {};
    std::array<std::uint64_t, 128> inputHeldOrder {};
    std::uint64_t inputOrderCounter = 0;
    std::array<DirectChordInputState, 128> directChordInputs {};
    std::array<int, 128> directChordOutputRefCounts {};
    bool sustainPedalDown = false;
    VoiceAllocator voices { 32 };
    FxChain fx;
};
} // namespace synth
