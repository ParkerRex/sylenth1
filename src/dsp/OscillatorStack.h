#pragma once

#include "SynthParameters.h"

#include <array>

namespace synth
{
class OscillatorStack
{
public:
    void prepare(double newSampleRate) noexcept;
    void reset(float randomOnNote) noexcept;
    void resetToPhase(float normalizedPhase) noexcept;

    float renderSample(float midiNote, const SynthParameters& parameters,
                       float pitchModSemitones, float pulseWidthMod) noexcept;
    float renderSample(float midiNote, const OscillatorParameters& parameters,
                       float pitchModSemitones, float pulseWidthMod) noexcept;
    float renderSawStack(float midiNote, const OscillatorParameters& parameters,
                         float pitchModSemitones, float outputGain) noexcept;
    void renderSawStackBlock(const float* midiNote, const float* pitchModSemitones,
                             float analogPitchMod, const OscillatorParameters& parameters,
                             float outputGain, float* out, int numSamples) noexcept;

    struct StereoSample
    {
        float left = 0.0f;
        float right = 0.0f;
    };
    StereoSample renderNative(float midiNote, const LayerOscillatorParameters& parameters,
                              float pitchModSemitones, float phaseModDegrees) noexcept;
    void resetNativePhase(float normalizedPhase) noexcept;

    static float detuneOffsetCents(int index, int count, float detune) noexcept;

private:
    static constexpr int maxStackCount = 8;

    float renderSaw(float& phase, float phaseIncrement) noexcept;
    float renderPulse(float& phase, float phaseIncrement, float pulseWidth) noexcept;
    float renderSub(float phase, SubWave wave, float pulseWidth) const noexcept;
    float renderNoise() noexcept;
    float advancePhase(float& phase, float increment) noexcept;
    static float polyBlep(float phase, float phaseIncrement) noexcept;
    void resetState(float basePhase) noexcept;
    void updateDetuneRatios(int stackCount, float stackDetune) noexcept;
    void updateDetunedIncrements(int stackCount, float masterIncrement,
                                 float minIncrement, float maxIncrement) noexcept;

    double sampleRate = 44100.0;
    std::array<float, maxStackCount> phases {};
    std::array<float, maxStackCount> detuneRatios {};
    std::array<float, maxStackCount> detunedIncrements {};
    int cachedDetuneStackCount = -1;
    float cachedDetuneAmount = -1.0f;
    int cachedDetunedIncrementStackCount = -1;
    float cachedDetunedMasterIncrement = -1.0f;
    float cachedBasePitch = 0.0f;
    float cachedMaxFrequency = 0.0f;
    float cachedBaseFrequency = 0.0f;
    bool cachedBaseFrequencyValid = false;
    std::array<float, maxStackCount> nativePanLeft {};
    std::array<float, maxStackCount> nativePanRight {};
    int cachedNativePanCount = -1;
    float cachedNativePan = 0.0f;
    float cachedNativeStereo = 0.0f;
    float subPhase = 0.0f;
    float syncMasterPhase = 0.0f;
    unsigned int noiseState = 0x76543210u;
};
} // namespace synth
