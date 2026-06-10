#pragma once

#include "Voice.h"

#include <array>
#include <cstdint>
#include <vector>

namespace synth
{
class VoiceAllocator
{
public:
    explicit VoiceAllocator(int maxVoices = 32);

    void prepare(double sampleRate);
    int noteOn(int midiNote, float velocity, const SynthParameters& parameters) noexcept;
    void noteOff(int midiNote) noexcept;
    void noteOff(int midiNote, const SynthParameters& parameters) noexcept;
    void setSustainPedal(bool down) noexcept;
    void allNotesOff() noexcept;
    void panic() noexcept;
    void stopAllWithFade(int fadeSamples) noexcept;
    void syncVoiceLimit(const SynthParameters& parameters) noexcept;
    void syncActiveVoiceModulators(const SynthParameters& parameters) noexcept;
    void process(int numSamples) noexcept;
    StereoFrame renderSample(const SynthParameters& parameters) noexcept;
    // Renders up to renderBlockMaxSamples into the caller's buffers (overwritten,
    // not accumulated), preserving renderSample's per-sample normalization and
    // voice-removal semantics.
    void renderBlock(const SynthParameters& parameters, float* outLeft, float* outRight,
                     int numSamples) noexcept;

    int activeVoiceCount() const noexcept;
    const Voice* getVoice(int index) const noexcept;
    float monoLfoPhase() const noexcept { return monoLfo.getPhase(); }
    float monoLfoValue() const noexcept { return monoLfo.getValue(); }

private:
    static constexpr int maxVoiceSlots = 32;

    float nextRandom() noexcept;
    int findIdleVoice(const std::array<bool, maxVoiceSlots>& excluded) const noexcept;
    int findVoiceToSteal(int maxActiveVoices, const std::array<bool, maxVoiceSlots>& excluded) const noexcept;
    void releaseSustainedNotes() noexcept;
    void enforceVoiceLimit(const SynthParameters& parameters) noexcept;
    bool anyHeldNote() const noexcept;
    int mostRecentHeldNote() const noexcept;
    int mostRecentResumeNote(bool includeSustained, int excludedNote = -1) const noexcept;
    bool retargetActiveNote(int fromNote, int toNote, float velocity, const SynthParameters& parameters) noexcept;
    bool allocationShapeChanged(const SynthParameters& parameters) const noexcept;
    void rememberAllocationShape(const SynthParameters& parameters) noexcept;
    void syncMonoLfoConfig(const SynthParameters& parameters) noexcept;
    void markVoiceActive(int voiceIndex) noexcept;
    void removeActiveVoiceAt(int activeListIndex) noexcept;

    std::vector<Voice> voices;
    std::array<int, maxVoiceSlots> activeVoiceIndices {};
    int activeVoiceSlotCount = 0;
    Lfo monoLfo;
    bool sustainPedalDown = false;
    std::array<bool, 128> heldNotes {};
    std::array<bool, 128> sustainedNotes {};
    std::array<float, 128> heldVelocities {};
    std::array<std::uint64_t, 128> heldOrder {};
    std::uint64_t noteOrderCounter = 0;
    VoiceMode allocationVoiceMode = VoiceMode::Poly;
    int allocationPolyphony = 8;
    int allocationUnisonCount = 1;
    bool allocationShapeInitialized = false;
    LfoShapeChoice cachedMonoLfoShape = LfoShapeChoice::SawDown;
    LfoRateMode cachedMonoLfoRateMode = LfoRateMode::Sync;
    int cachedMonoLfoSyncDivision = 3;
    float cachedMonoLfoRateHz = 2.0f;
    float cachedMonoLfoPhaseDegrees = 0.0f;
    float cachedMonoTempoBpm = 120.0f;
    bool monoLfoConfigInitialized = false;
    unsigned int randomState = 0xdecafbadu;
};
} // namespace synth
