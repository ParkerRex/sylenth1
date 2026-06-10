#pragma once

#include "../dsp/Envelope.h"
#include "../dsp/Filter.h"
#include "../dsp/Lfo.h"
#include "../dsp/OscillatorStack.h"
#include "../dsp/Ramp.h"
#include "../dsp/SynthParameters.h"

#include <array>
#include <cstddef>

namespace synth
{
enum class VoiceState
{
    Idle,
    Held,
    Releasing
};

struct VoiceSnapshot
{
    VoiceState state = VoiceState::Idle;
    int midiNote = -1;
    float velocity = 0.0f;
    float ampEnv = 0.0f;
    float modEnv = 0.0f;
    float lfo = 0.0f;
    float ramp = 0.0f;
    float randomOnNote = 0.0f;
    float effectiveMidiNote = 0.0f;
    float velocityGlide = 0.0f;
    float voiceUni = 0.0f;
    float voiceBi = 0.0f;
    float unisonUni = 0.0f;
    float unisonBi = 0.0f;
    float directOscPitchSemitones = 0.0f;
    float directPulseWidth = 0.0f;
    float directFilterCutoffSemitones = 0.0f;
    float transModOscPitchSemitones = 0.0f;
    float transModPulseWidth = 0.0f;
    float transModFilterCutoffSemitones = 0.0f;
    float transModAmpLevelDb = 0.0f;
    float transModPan = 0.0f;
    float lfoPhase = 0.0f;
};

struct StereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

class Voice
{
public:
    void prepare(double sampleRate);
    void setVoiceIndex(int index, int total) noexcept;
    void setAllocationIndices(int index, int total, int newUnisonIndex, int newUnisonCount) noexcept;
    void noteOn(int note, float normalizedVelocity, float randomValue,
                int newUnisonIndex, int newUnisonCount, const SynthParameters& parameters,
                bool allowGlide, bool retriggerModulators) noexcept;
    void noteOff() noexcept;
    void stopWithFade(int fadeSamples) noexcept;
    void reset() noexcept;
    void process(int numSamples) noexcept;
    void syncModulators(const SynthParameters& parameters) noexcept;
    StereoFrame renderSample(const SynthParameters& parameters, const float* monoLfoValue = nullptr) noexcept;
    // Renders up to renderBlockMaxSamples in staged passes, accumulating into the
    // caller's buffers and adding this voice's per-sample normalization weights.
    // Returns false when the voice went idle during the block. Per-sample math
    // and lifecycle ordering mirror renderSample exactly.
    bool renderBlock(const SynthParameters& parameters, float* accumLeft, float* accumRight,
                     float* normalizationWeights, const float* monoLfoValues, int numSamples) noexcept;

    bool isActive() const noexcept { return state != VoiceState::Idle; }
    bool isHeld() const noexcept { return state == VoiceState::Held; }
    bool isStopFading() const noexcept { return stopFadeSamples > 0 && stopFadeTotalSamples > 0; }
    int getMidiNote() const noexcept { return midiNote; }
    float normalizationPowerWeight() const noexcept;
    VoiceSnapshot snapshot() const noexcept;

private:
    struct ModulationSums
    {
        float oscPitchSemitones = 0.0f;
        float pulseWidth = 0.0f;
        float filterCutoffSemitones = 0.0f;
        float ampLevelDb = 0.0f;
        float pan = 0.0f;
    };

    struct LayerOscillatorMix
    {
        float sample = 0.0f;
        float pan = 0.0f;
    };

    // Per-block snapshot of one active TransMod route. Sources that cannot
    // change within a block (velocity, wheels, macros, per-note randoms, voice
    // indices) are resolved to constants so the per-sample loop skips their
    // switch dispatch entirely.
    struct PreparedTransModSlot
    {
        ModSource source = ModSource::None;
        ModSource scaler = ModSource::None;
        bool sourceConstant = false;
        bool scalerConstant = false;
        float sourceValue = 0.0f;
        float scalerValue = 1.0f;
        float oscPitchSemitones = 0.0f;
        float pulseWidth = 0.0f;
        float filterCutoffSemitones = 0.0f;
        float ampLevelDb = 0.0f;
        float pan = 0.0f;
    };

    float processGlide(const SynthParameters& parameters) noexcept;
    float processVelocityGlide(const SynthParameters& parameters) noexcept;
    void syncModulatorConfig(const SynthParameters& parameters) noexcept;
    void resetLayerOscillators(const SynthParameters& parameters, float fallbackPhase) noexcept;
    LayerOscillatorMix renderLayerOscillators(const SynthParameters& parameters, float effectiveNote,
                                              float pitchModSemitones, float pulseWidthMod,
                                              float analogPitchMod, float unisonBi) noexcept;
    float evalModSource(const SynthParameters& parameters, ModSource source, float lfoValue, float rampValue,
                        float modEnvValue, float ampEnvValue, float effectiveNote,
                        float velocityGlideValue) const noexcept;
    ModulationSums evaluateTransMod(const SynthParameters& parameters, float lfoValue,
                                    float rampValue, float modEnvValue, float ampEnvValue,
                                    float effectiveNote, float velocityGlideValue) const noexcept;
    void prepareTransModSlots(const SynthParameters& parameters) noexcept;
    ModulationSums evaluatePreparedTransMod(const SynthParameters& parameters, float lfoValue,
                                            float rampValue, float modEnvValue, float ampEnvValue,
                                            float effectiveNote, float velocityGlideValue) const noexcept;

    VoiceState state = VoiceState::Idle;
    int midiNote = -1;
    float velocity = 0.0f;
    float currentVelocity = 0.0f;
    float startVelocity = 0.0f;
    int velocityGlideSamples = 0;
    int velocityGlideTotalSamples = 0;
    float currentMidiNote = 0.0f;
    float startMidiNote = 0.0f;
    float targetMidiNote = 0.0f;
    int glideSamples = 0;
    int glideTotalSamples = 0;
    float randomOnNote = 0.0f;
    int voiceIndex = 0;
    int voiceCount = 1;
    int unisonIndex = 0;
    int unisonCount = 1;
    int stopFadeSamples = 0;
    int stopFadeTotalSamples = 0;
    double sampleRate = 44100.0;
    Envelope ampEnvelope;
    Envelope modEnvelope;
    Lfo lfo;
    Ramp ramp;
    OscillatorStack oscillator;
    static constexpr auto layerOscillatorCount =
        static_cast<std::size_t>(layerCount) * static_cast<std::size_t>(oscillatorSlotsPerLayer);
    std::array<OscillatorStack, layerOscillatorCount> layerOscillators;
    Filter filter;
    EnvelopeParameters cachedAmpEnv;
    EnvelopeParameters cachedModEnv;
    LfoShapeChoice cachedLfoShape = LfoShapeChoice::SawDown;
    LfoRateMode cachedLfoRateMode = LfoRateMode::Sync;
    int cachedLfoSyncDivision = 3;
    float cachedLfoRateHz = 2.0f;
    float cachedLfoPhaseDegrees = 0.0f;
    float cachedTempoBpm = 120.0f;
    bool modulatorConfigInitialized = false;
    ModulationSums lastDirectSums;
    ModulationSums lastTransModSums;
    float lastRampValue = 0.0f;
    float cachedAmpDrive = 0.0f;
    float cachedAmpDriveGain = 1.0f;
    float cachedAmpDriveNormalizer = 1.0f;
    bool cachedAmpDriveValid = false;
    float cachedAmpGainDb = 0.0f;
    float cachedAmpGain = 1.0f;
    bool cachedAmpGainValid = false;
    float cachedPan = 0.0f;
    float cachedPanLeftGain = 0.70710677f;
    float cachedPanRightGain = 0.70710677f;
    bool cachedPanValid = false;
    float cachedVoiceUni = 0.0f;
    float cachedVoiceBi = 0.0f;
    float cachedUnisonUni = 0.0f;
    float cachedUnisonBi = 0.0f;
    std::array<PreparedTransModSlot, transModSlotCount> preparedTransModSlots {};
    int preparedTransModCount = -1;
};
} // namespace synth
