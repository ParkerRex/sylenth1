#pragma once

#include "../SynthParameters.h"

#include <array>
#include <vector>

namespace synth
{
struct FxStereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

float delayDivisionBeats(DelaySyncDivision division) noexcept;
int tempoSyncedDelaySamples(double sampleRate, float tempoBpm, DelaySyncDivision division) noexcept;
float tempoSyncedDelaySeconds(float tempoBpm, DelaySyncDivision division) noexcept;
float tempoSyncedEffectRateHz(float tempoBpm, DelaySyncDivision division) noexcept;
float fxTailLengthSeconds(const FxParameters& parameters) noexcept;
float fxTailLengthSeconds(const SynthParameters& parameters) noexcept;

class FxChain
{
public:
    void prepare(double newSampleRate, int newMaxBlockSize);
    void reset() noexcept;
    FxStereoFrame process(FxStereoFrame input, const SynthParameters& parameters) noexcept;

private:
    struct DelayBuffer
    {
        std::vector<float> samples;
        int writeIndex = 0;
        int validSamples = 0;

        void prepare(int sampleCount);
        void reset() noexcept;
        float readAtWriteHead() const noexcept;
        float read(int delaySamples) const noexcept;
        float readInterpolated(float delaySamples) const noexcept;
        void write(float value) noexcept;
    };

    FxStereoFrame processSaturation(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processPhaser(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processChorus(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processEq(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processDelay(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processReverb(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    FxStereoFrame processCompressor(FxStereoFrame input, const SynthParameters& parameters) noexcept;
    void resetPhaser() noexcept;
    void resetReverb() noexcept;
    void resetChorus() noexcept;
    void updateCompressorCoefficients(const FxParameters& parameters) noexcept;
    FxStereoFrame processPhaser(FxStereoFrame input, const FxParameters& parameters) noexcept;
    FxStereoFrame processChorus(FxStereoFrame input, const FxParameters& parameters) noexcept;
    FxStereoFrame processEq(FxStereoFrame input, const FxParameters& parameters) noexcept;
    FxStereoFrame processReverb(FxStereoFrame input, const FxParameters& parameters) noexcept;
    static FxStereoFrame applyWidth(FxStereoFrame input, float width) noexcept;
    float smooth(float target, float& state) const noexcept;
    struct Biquad
    {
        std::array<float, 5> coefficients { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float firstState = 0.0f;
        float secondState = 0.0f;
        float process(float input) noexcept;
        void reset() noexcept;
        void setShelf(double sampleRate, float frequency, float gainDb, bool high) noexcept;
        void setLowpass(double sampleRate, float frequency, float resonance) noexcept;
    };

    struct ReconstructionFilter
    {
        std::array<Biquad, 4> filters;
        void prepare(double sampleRate, int factor) noexcept;
        void reset() noexcept;
        FxStereoFrame process(FxStereoFrame input) noexcept;
    };
    ReconstructionFilter distortionReconstruction;
    ReconstructionFilter phaserReconstruction;
    ReconstructionFilter chorusReconstruction;
    FxStereoFrame previousPhaserInput;
    FxStereoFrame previousChorusInput;
    float eqNormalization = 1.0f;

    double sampleRate = 44100.0;
    int maxBlockSize = 512;
    DelayBuffer delayLeft;
    DelayBuffer delayRight;
    DelayBuffer chorusLeft;
    DelayBuffer chorusRight;
    std::array<DelayBuffer, 8> reverbCombs;
    std::array<float, 8> reverbDampingStates {};
    float phaserPhase = 0.0f;
    bool phaserWasActive = false;
    float compressorEnvelope = 0.0f;
    float compressorAttackCoefficient = 0.0f;
    float compressorReleaseCoefficient = 0.0f;
    float chorusPhase = 0.0f;
    bool reverbWasActive = false;
    std::array<std::array<float, 6>, 2> phaserStages {};
    FxStereoFrame phaserFeedback;
    FxStereoFrame previousDistortionInput;
    FxStereoFrame decimated;
    int decimationCounter = 0;
    DelayBuffer chorusSecondLeft;
    DelayBuffer chorusSecondRight;
    DelayBuffer reverbPreDelayLeft;
    DelayBuffer reverbPreDelayRight;
    std::array<DelayBuffer, 4> reverbDiffusers;
    std::array<DelayBuffer, 2> delayDiffusers;
    std::array<Biquad, 4> eqShelves;
    std::array<float, 4> eqSettings { -1.0f, -1.0f, -100.0f, -100.0f };
    FxStereoFrame delayLowState;
    FxStereoFrame delayHighState;
    float cachedDelayLowCut = -1.0f;
    float cachedDelayHighCut = -1.0f;
    float delayLowCoefficient = 0.0f;
    float delayHighCoefficient = 1.0f;
    float cachedAttackMs = -1.0f;
    float cachedReleaseMs = -1.0f;
    float smoothedDelayLeft = 0.0f;
    float smoothedDelayRight = 0.0f;
    float smoothingCoefficient = 0.0f;
    std::array<float, 8> mixStates {};
    bool controlsInitialized = false;
    bool globalWasActive = false;
    bool delayWasActive = false;
    bool chorusWasActive = false;
    std::array<std::array<float, 6>, 2> phaserFrequencyMultipliers {};
    float cachedPhaserSpread = -1.0f;
    float cachedPhaserOffset = -1.0f;
    float smoothedPhaserCenter = 1000.0f;
    float smoothedChorusDelay = 11.0f;
    float smoothedReverbSize = 0.35f;
    float smoothedReverbPreDelay = 0.0f;
};
} // namespace synth
