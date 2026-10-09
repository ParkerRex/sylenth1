#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace synth
{
enum class SubWave
{
    Sine = 0,
    Triangle = 1,
    Saw = 2,
    Pulse = 3
};

enum class FilterMode
{
    L2 = 0,
    L4 = 1,
    B2 = 2,
    B4 = 3,
    H2 = 4,
    H4 = 5,
    Peak2 = 6,
    Notch2 = 7,
    Notch4 = 8
};

enum class LfoShapeChoice
{
    Sine = 0,
    Triangle = 1,
    SawUp = 2,
    SawDown = 3,
    Square = 4,
    SampleHold = 5,
    Noise = 6,
    Step = 7
};

enum class LfoRateMode
{
    Hz = 0,
    Sync = 1
};

enum class LfoGateMode
{
    Poly = 0,
    PolyOn = 1,
    Mono = 2,
    Song = 3
};

enum class VoiceMode
{
    Mono = 0,
    MonoLegato = 1,
    Poly = 2,
    Unison = 3
};

enum class PortamentoMode
{
    Normal = 0,
    Slide = 1
};

enum class RampMode
{
    OneShot = 0,
    Loop = 1,
    Sync = 2
};

enum class RampCurve
{
    Linear = 0,
    Exponential = 1,
    Snappy = 2
};

enum class QualityMode
{
    Eco = 0,
    Normal = 1,
    High = 2
};

enum class DelaySyncDivision
{
    Sixteenth = 0,
    Eighth = 1,
    DottedEighth = 2,
    Quarter = 3,
    Half = 4,
    ThirtySecond = 5,
    ThirtySecondTriplet = 6,
    DottedThirtySecond = 7,
    SixteenthTriplet = 8,
    DottedSixteenth = 9,
    EighthTriplet = 10,
    QuarterTriplet = 11,
    DottedQuarter = 12,
    HalfTriplet = 13,
    DottedHalf = 14,
    Whole = 15,
    WholeTriplet = 16,
    DottedWhole = 17
};

enum class DistortionMode
{
    Soft = 0,
    Clip = 1,
    Fold = 2,
    Decimate = 3,
    Bitcrush = 4
};

enum class ArpMode
{
    Up = 0,
    Down = 1,
    UpDown = 2,
    AsPlayed = 3,
    DownUp = 4,
    UpDownRepeat = 5,
    DownUpRepeat = 6,
    Random = 7,
    StepSequence = 8,
    StepChord = 9
};

enum class ArpVelocityMode
{
    Step = 0,
    Key = 1,
    Hold = 2,
    StepKey = 3,
    StepHold = 4
};

enum class ArpRateDivision
{
    ThirtySecond = 0,
    Sixteenth = 1,
    Eighth = 2,
    Quarter = 3,
    Half = 4,
    DottedEighth = 5,
    ThirtySecondTriplet = 6,
    DottedThirtySecond = 7,
    SixteenthTriplet = 8,
    DottedSixteenth = 9,
    EighthTriplet = 10,
    QuarterTriplet = 11,
    DottedQuarter = 12,
    HalfTriplet = 13,
    DottedHalf = 14,
    Whole = 15,
    WholeTriplet = 16,
    DottedWhole = 17
};

enum class OscillatorSlotWaveform
{
    Saw = 0,
    Pulse = 1,
    Noise = 2,
    Sine = 3,
    Triangle = 4,
    SawTriangle = 5,
    HalfPulse = 6,
    QuarterPulse = 7
};

enum class ModSource
{
    None = 0,
    Lfo = 1,
    Ramp = 2,
    ModEnv = 3,
    AmpEnv = 4,
    Keytrack = 5,
    Velocity = 6,
    VelocityGlide = 7,
    PitchBend = 8,
    ModWheel = 9,
    Aftertouch = 10,
    VoiceUni = 11,
    VoiceBi = 12,
    UnisonUni = 13,
    UnisonBi = 14,
    RandomOnNote = 15,
    Macro1 = 16,
    Macro2 = 17,
    Macro3 = 18,
    Macro4 = 19,
    ModEnv2 = 20,
    Lfo2 = 21,
    AmpEnv2 = 22,
    StepVelocity = 23
};

inline constexpr int transModSlotCount = 8;
inline constexpr int layerCount = 2;
inline constexpr int oscillatorSlotsPerLayer = 2;
inline constexpr int preparedOscillatorSlotCount = layerCount * oscillatorSlotsPerLayer;

// Sub-block size for block-based voice rendering; render-path scratch buffers
// are stack arrays of this size, so keep it small enough to stay in L1.
inline constexpr int renderBlockMaxSamples = 64;
inline constexpr int arpStepCount = 16;
inline constexpr int chordVoiceCount = 8;
inline constexpr int lfoStepSlotCount = 16;

// Factory step table: a descending ramp across all 16 slots so the Step LFO
// is audibly a stepper out of the box at any step count.
inline constexpr float defaultLfoStepValue(int index) noexcept
{
    return 1.0f - 2.0f * static_cast<float>(index) / static_cast<float>(lfoStepSlotCount - 1);
}

struct EnvelopeParameters
{
    float attackMs = 2.0f;
    float decayMs = 300.0f;
    float sustain = 0.8f;
    float releaseMs = 200.0f;
};

struct OscillatorParameters
{
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
    int stackCount = 1;
    float stackDetune = 0.0f;
    float sawLevel = 1.0f;
    float pulseLevel = 0.0f;
    float noiseLevel = 0.0f;
    float pulseWidth = 0.5f;
    SubWave subWave = SubWave::Saw;
    int subOctave = 1;
    float subLevel = 0.0f;
    float subPulseWidth = 0.5f;
    float syncAmount = 0.0f;
    int phaseReset = 0;
};

struct LayerOscillatorParameters
{
    bool enabled = true;
    int voices = 0;
    OscillatorSlotWaveform waveform = OscillatorSlotWaveform::Saw;
    int octave = 0;
    int note = 0;
    float fineCents = 0.0f;
    float level = 1.0f;
    float phaseDegrees = 0.0f;
    float detune = 0.0f;
    float stereo = 0.0f;
    float pan = 0.0f;
    bool retrigger = true;
    bool invert = false;
};

struct FilterParameters
{
    bool enabled = true;
    FilterMode mode = FilterMode::L4;
    float cutoffSemitones = 96.0f;
    float resonance = 0.0f;
    float drive = 0.0f;
    float keytrack = 0.0f;
    int oversampling = 0;
    bool warmDrive = false;
    bool selfOscillation = false;
    bool nativeTopology = false;
};

enum class FilterInput
{
    None = 0,
    A = 1,
    B = 2,
    AB = 3
};

struct FilterControlParameters
{
    bool warmDrive = true;
    float cutoffSemitones = 0.0f;
    float resonance = 0.0f;
    float keytrack = 0.0f;
    float drive = 0.0f;
};

struct MasterParameters
{
    float levelDb = 0.0f;
};

struct LayerParameters
{
    bool enabled = true;
    float levelDb = 0.0f;
    float pan = 0.0f;
    bool solo = false;
    bool mute = false;
    std::array<LayerOscillatorParameters, oscillatorSlotsPerLayer> oscillators {};
    FilterParameters filter;
    EnvelopeParameters ampEnv;
    FilterInput input = FilterInput::A;
};

struct AmpParameters
{
    float drive = 0.0f;
    float levelDb = -6.0f;
    float pan = 0.0f;
    float panSpread = 0.0f;
    float unisonSpread = 0.0f;
    float analog = 0.0f;
};

struct DirectModParameters
{
    float filterKeytrack = 0.0f;
    float filterLfoSemitones = 0.0f;
    float filterModEnvSemitones = 0.0f;
    float oscKeytrackSemitones = 0.0f;
    float oscLfoSemitones = 0.0f;
    float oscModEnvSemitones = 0.0f;
    float pulseKeytrack = 0.0f;
    float pulseLfo = 0.0f;
    float pulseModEnv = 0.0f;
};

struct LfoParameters
{
    LfoShapeChoice shape = LfoShapeChoice::SawDown;
    LfoRateMode rateMode = LfoRateMode::Sync;
    float rateHz = 2.0f;
    int syncDivision = 3;
    float phaseDegrees = 0.0f;
    LfoGateMode gateMode = LfoGateMode::PolyOn;
    bool mono = false;
    float swing = 0.0f;
    int stepCount = 8;
    float stepSmooth = 0.0f;
    float gain = 1.0f;
    float offset = 0.0f;
    bool free = false;
    std::array<float, lfoStepSlotCount> steps = [] {
        std::array<float, lfoStepSlotCount> values {};
        for (int i = 0; i < lfoStepSlotCount; ++i)
            values[static_cast<std::size_t>(i)] = defaultLfoStepValue(i);
        return values;
    }();
};

struct MacroParameters
{
    float motion = 0.5f;
    float width = 0.0f;
    float drive = 0.0f;
    float space = 0.0f;
};

struct FxParameters
{
    bool enabled = true;
    bool saturationEnabled = false;
    DistortionMode distortionMode = DistortionMode::Soft;
    float saturationMix = 0.0f;
    float saturationDrive = 0.35f;
    bool phaserEnabled = false;
    float phaserMix = 0.0f;
    float phaserRateHz = 0.25f;
    float phaserDepth = 0.45f;
    float phaserFeedback = 0.15f;
    bool delayEnabled = false;
    float delayMix = 0.0f;
    DelaySyncDivision delaySyncDivision = DelaySyncDivision::Eighth;
    float delayFeedback = 0.22f;
    bool reverbEnabled = false;
    float reverbMix = 0.0f;
    float reverbDecay = 0.35f;
    bool chorusEnabled = false;
    float chorusMix = 0.0f;
    float chorusRateHz = 0.35f;
    float chorusDepthMs = 5.0f;
    bool eqEnabled = false;
    float eqLowGainDb = 0.0f;
    float eqHighGainDb = 0.0f;
    bool compressorEnabled = false;
    float compressorThresholdDb = -18.0f;
    float compressorRatio = 2.0f;
    float compressorMakeupDb = 0.0f;
    float compressorMix = 0.0f;
    DelaySyncDivision phaserSyncDivision = DelaySyncDivision::Whole;
    DelaySyncDivision chorusSyncDivision = DelaySyncDivision::Whole;
    float phaserCenterHz = 1000.0f;
    float phaserCenterOffsetHz = 0.0f;
    float phaserSpread = 0.5f;
    float phaserLrOffset = 0.25f;
    float phaserWidth = 1.0f;
    float chorusDelayMs = 11.0f;
    float chorusFeedback = 0.0f;
    bool chorusDualMode = false;
    float chorusWidth = 1.0f;
    float eqLowFrequencyHz = 160.0f;
    float eqHighFrequencyHz = 6000.0f;
    bool delaySync = true;
    float delayTimeLeftMs = 250.0f;
    float delayTimeRightMs = 250.0f;
    DelaySyncDivision delayRightSyncDivision = DelaySyncDivision::Eighth;
    bool delayPingPong = true;
    float delaySpread = 1.0f;
    float delayWidth = 1.0f;
    float delayLowCutHz = 20.0f;
    float delayHighCutHz = 20000.0f;
    float delaySmear = 0.0f;
    float reverbPreDelayMs = 0.0f;
    float reverbDamp = 0.48f;
    float reverbWidth = 1.0f;
    float compressorAttackMs = 4.0f;
    float compressorReleaseMs = 80.0f;
};

struct QualityParameters
{
    QualityMode realtimeMode = QualityMode::Normal;
    QualityMode offlineMode = QualityMode::High;
    QualityMode activeMode = QualityMode::Normal;
};

struct RampParameters
{
    bool enabled = false;
    RampMode mode = RampMode::OneShot;
    float delayMs = 0.0f;
    float riseMs = 1000.0f;
    RampCurve curve = RampCurve::Linear;
};

struct ArpStepParameters
{
    bool enabled = true;
    int pitchSemitones = 0;
    float velocity = 1.0f;
    float gate = 1.0f;
    bool tie = false;
};

struct ArpParameters
{
    bool enabled = false;
    ArpMode mode = ArpMode::Up;
    ArpRateDivision rate = ArpRateDivision::Sixteenth;
    float gate = 0.75f;
    int octaves = 1;
    bool hold = false;
    float swing = 0.0f;
    int stepCount = arpStepCount;
    std::array<ArpStepParameters, arpStepCount> steps {};
    int wrap = 0;
    bool sync = true;
    float timeMs = 125.0f;
    ArpVelocityMode velocityMode = ArpVelocityMode::StepKey;
};

struct ChordVoiceParameters
{
    bool enabled = false;
    int pitchSemitones = 0;
    float velocity = 1.0f;
};

struct ChordParameters
{
    bool enabled = false;
    int voiceCount = 1;
    std::array<ChordVoiceParameters, chordVoiceCount> voices {
        ChordVoiceParameters { true, 0, 1.0f },
        ChordVoiceParameters {},
        ChordVoiceParameters {},
        ChordVoiceParameters {},
        ChordVoiceParameters {},
        ChordVoiceParameters {},
        ChordVoiceParameters {},
        ChordVoiceParameters {}
    };
};

enum class NativeModDestination
{
    OscA1Pitch,
    OscA1Level,
    OscA1Pan,
    OscA1Detune,
    OscA1Phase,
    OscA2Pitch,
    OscA2Level,
    OscA2Pan,
    OscA2Detune,
    OscA2Phase,
    OscB1Pitch,
    OscB1Level,
    OscB1Pan,
    OscB1Detune,
    OscB1Phase,
    OscB2Pitch,
    OscB2Level,
    OscB2Pan,
    OscB2Detune,
    OscB2Phase,
    FilterACutoff,
    FilterAResonance,
    FilterADrive,
    FilterBCutoff,
    FilterBResonance,
    FilterBDrive,
    LayerALevel,
    LayerAPan,
    LayerBLevel,
    LayerBPan,
    Lfo1Rate,
    Lfo1Gain,
    Lfo1Offset,
    Lfo2Rate,
    Lfo2Gain,
    Lfo2Offset,
    PhaserCenterFrequency,
    FilterResonanceAB,
    Count
};

inline constexpr int nativeModDestinationCount = static_cast<int>(NativeModDestination::Count);

struct TransModSlotParameters
{
    bool enabled = false;
    ModSource source = ModSource::None;
    ModSource scaler = ModSource::None;
    float depth = 0.0f;
    float oscPitchSemitones = 0.0f;
    float pulseWidth = 0.0f;
    float filterCutoffSemitones = 0.0f;
    float ampLevelDb = 0.0f;
    float pan = 0.0f;
    std::array<float, nativeModDestinationCount> nativeDepths {};
};

struct TransModParameters
{
    std::array<TransModSlotParameters, transModSlotCount> slots {};
    std::array<TransModSlotParameters, transModSlotCount> activeSlots {};
    int activeSlotCount = 0;
    bool activeSlotCacheValid = false;
};

struct PerformanceState
{
    float pitchBend = 0.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    float stepVelocity = 1.0f;
};

struct SynthParameters
{
    bool sync = false;
    PortamentoMode portamentoMode = PortamentoMode::Normal;
    float pitchBendRange = 2.0f;
    VoiceMode voiceMode = VoiceMode::Poly;
    int polyphony = 8;
    int unisonCount = 1;
    bool retrigger = true;
    float glideMs = 0.0f;
    float velocityGlideMs = 0.0f;
    std::array<LayerParameters, layerCount> layers {
        LayerParameters {
            true,
            0.0f,
            0.0f,
            false,
            false,
            std::array<LayerOscillatorParameters, oscillatorSlotsPerLayer> {
                LayerOscillatorParameters {
                    true,
                    1,
                    OscillatorSlotWaveform::Saw,
                    0,
                    0,
                    0.0f,
                    1.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    true,
                    false },
                LayerOscillatorParameters {} },
            FilterParameters {},
            EnvelopeParameters {},
            FilterInput::A },
        [] { LayerParameters part; part.input = FilterInput::B; return part; }()
    };
    OscillatorParameters osc;
    FilterParameters filter;
    AmpParameters amp;
    EnvelopeParameters ampEnv;
    EnvelopeParameters modEnv { 1.0f, 400.0f, 0.0f, 160.0f };
    DirectModParameters direct;
    LfoParameters lfo;
    LfoParameters lfo2;
    EnvelopeParameters modEnv2 { 1.0f, 400.0f, 0.0f, 160.0f };
    FilterControlParameters filterControl;
    MasterParameters master;
    RampParameters ramp;
    ArpParameters arp;
    ChordParameters chord;
    TransModParameters transMod;
    MacroParameters macro;
    FxParameters fx;
    QualityParameters quality;
    PerformanceState performance;
    float tempoBpm = 128.0f;
};

struct PatchCostEstimate
{
    int noteLimit = 0;
    int unisonVoices = 0;
    int maxActiveVoices = 0;
    int oscillatorSlotVoices = 0;
    int voiceUnits = 0;
    int filterOversampling = 1;
    int activeFxModules = 0;
    float filterMultiplier = 1.0f;
    float fxMultiplier = 1.0f;
    float totalUnits = 0.0f;
    int loadPercent = 0;
    bool elevated = false;
    bool high = false;
    bool overBudget = false;
};

inline float clampUnit(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float softSaturate(float value) noexcept
{
    value = std::clamp(value, -64.0f, 64.0f);
    return value / (1.0f + std::abs(value));
}

inline float inverseSqrtForCount(int count) noexcept
{
    constexpr std::array<float, 33> values {
        0.0f,
        1.0f,
        0.70710678118f,
        0.57735026919f,
        0.5f,
        0.44721359550f,
        0.40824829046f,
        0.37796447301f,
        0.35355339059f,
        0.33333333333f,
        0.31622776602f,
        0.30151134458f,
        0.28867513459f,
        0.27735009811f,
        0.26726124191f,
        0.25819888975f,
        0.25f,
        0.24253562504f,
        0.23570226040f,
        0.22941573387f,
        0.22360679775f,
        0.21821789024f,
        0.21320071636f,
        0.20851441406f,
        0.20412414523f,
        0.2f,
        0.19611613514f,
        0.19245008973f,
        0.18898223650f,
        0.18569533818f,
        0.18257418584f,
        0.17960530203f,
        0.17677669530f,
    };

    return values[static_cast<std::size_t>(std::clamp(count, 0, static_cast<int>(values.size()) - 1))];
}

inline float inverseSqrtForWeight(float weight) noexcept
{
    const auto safeWeight = std::isfinite(weight) ? weight : 1.0f;
    const auto clampedWeight = std::clamp(safeWeight, 0.0f, 32.0f);
    if (clampedWeight <= 0.0f)
        return 0.0f;

    if (clampedWeight <= 1.0f)
        return 1.0f;

    const auto lowerIndex = static_cast<int>(clampedWeight);
    const auto fraction = clampedWeight - static_cast<float>(lowerIndex);
    const auto upperIndex = std::min(lowerIndex + 1, 32);
    const auto lower = inverseSqrtForCount(lowerIndex);
    return lower + (inverseSqrtForCount(upperIndex) - lower) * fraction;
}

inline constexpr float decibelGainTableMinDb = -96.0f;
inline constexpr float decibelGainTableMaxDb = 36.0f;
inline constexpr float decibelGainTableStepDb = 0.5f;
inline constexpr auto decibelGainTableSize = 265u;
inline constexpr std::array<float, decibelGainTableSize> decibelGainTable = [] {
    std::array<float, decibelGainTableSize> values {};
    auto gain = 0.000015848931925f;
    constexpr auto stepGain = 1.05925372518f;
    for (auto& value : values)
    {
        value = gain;
        gain *= stepGain;
    }
    return values;
}();

inline float decibelsToGain(float db) noexcept
{
    const auto safeDb = std::isfinite(db) ? db : 0.0f;
    const auto clampedDb = std::clamp(safeDb, decibelGainTableMinDb, decibelGainTableMaxDb);
    const auto position = (clampedDb - decibelGainTableMinDb) / decibelGainTableStepDb;
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto nextIndex = std::min(index + 1, static_cast<int>(decibelGainTable.size()) - 1);
    return decibelGainTable[static_cast<std::size_t>(index)]
        + (decibelGainTable[static_cast<std::size_t>(nextIndex)] - decibelGainTable[static_cast<std::size_t>(index)]) * fraction;
}

inline float gainToDecibels(float gain) noexcept
{
    const auto safeGain = std::isfinite(gain) ? gain : decibelGainTable.front();
    const auto clampedGain = std::clamp(safeGain, decibelGainTable.front(), decibelGainTable.back());
    auto lowerIndex = 0;
    auto upperIndex = static_cast<int>(decibelGainTable.size()) - 1;
    while (upperIndex - lowerIndex > 1)
    {
        const auto midpoint = lowerIndex + (upperIndex - lowerIndex) / 2;
        if (decibelGainTable[static_cast<std::size_t>(midpoint)] <= clampedGain)
            lowerIndex = midpoint;
        else
            upperIndex = midpoint;
    }

    const auto lowerGain = decibelGainTable[static_cast<std::size_t>(lowerIndex)];
    const auto upperGain = decibelGainTable[static_cast<std::size_t>(upperIndex)];
    const auto span = std::max(upperGain - lowerGain, 1.0e-12f);
    const auto fraction = (clampedGain - lowerGain) / span;
    return decibelGainTableMinDb + (static_cast<float>(lowerIndex) + fraction) * decibelGainTableStepDb;
}

inline constexpr int midiNoteFrequencyTableMinNote = -192;
inline constexpr int midiNoteFrequencyTableMaxNote = 320;
inline constexpr auto midiNoteFrequencyTableSize = 513u;
inline constexpr std::array<float, midiNoteFrequencyTableSize> midiNoteFrequencyTable = [] {
    std::array<float, midiNoteFrequencyTableSize> values {};
    constexpr auto semitoneRatio = 1.05946309436f;
    auto frequency = 8.17579891564f;
    for (int note = 0; note > midiNoteFrequencyTableMinNote; --note)
        frequency /= semitoneRatio;

    for (auto& value : values)
    {
        value = frequency;
        frequency *= semitoneRatio;
    }
    return values;
}();

inline float midiNoteToHz(float midiNote) noexcept
{
    const auto safeMidiNote = std::isfinite(midiNote) ? midiNote : 0.0f;
    const auto clampedNote = std::clamp(safeMidiNote,
                                        static_cast<float>(midiNoteFrequencyTableMinNote),
                                        static_cast<float>(midiNoteFrequencyTableMaxNote));
    const auto position = clampedNote - static_cast<float>(midiNoteFrequencyTableMinNote);
    const auto index = static_cast<int>(position);
    const auto fraction = position - static_cast<float>(index);
    const auto nextIndex = std::min(index + 1, static_cast<int>(midiNoteFrequencyTable.size()) - 1);
    return midiNoteFrequencyTable[static_cast<std::size_t>(index)]
        + (midiNoteFrequencyTable[static_cast<std::size_t>(nextIndex)]
           - midiNoteFrequencyTable[static_cast<std::size_t>(index)])
        * fraction;
}

inline int oscillatorSlotVoiceCost(const LayerOscillatorParameters& oscillator, int renderedVoiceCount) noexcept
{
    return oscillator.voices > 0 && oscillator.level > 0.0f
        ? std::clamp(renderedVoiceCount, 0, 8)
        : 0;
}

inline int oscillatorSlotVoiceCost(const LayerOscillatorParameters& oscillator) noexcept
{
    return oscillatorSlotVoiceCost(oscillator, oscillator.voices);
}

inline int layerVoiceCost(const LayerParameters& layer) noexcept
{
    if (layer.mute)
        return 0;

    auto voices = 0;
    for (const auto& oscillator : layer.oscillators)
        voices += oscillatorSlotVoiceCost(oscillator);

    return voices;
}

inline int layerVoiceCost(const SynthParameters& parameters, int layerIndex) noexcept
{
    if (layerIndex < 0 || layerIndex >= layerCount)
        return 0;

    const auto& layer = parameters.layers[static_cast<std::size_t>(layerIndex)];
    if (layer.mute)
        return 0;

    auto voices = 0;
    for (int oscillatorIndex = 0; oscillatorIndex < oscillatorSlotsPerLayer; ++oscillatorIndex)
    {
        const auto& oscillator = layer.oscillators[static_cast<std::size_t>(oscillatorIndex)];
        voices += oscillatorSlotVoiceCost(oscillator);
    }

    return voices;
}

inline int layerOscillatorVoiceCost(const SynthParameters& parameters) noexcept
{
    auto hasSolo = false;
    for (const auto& layer : parameters.layers)
        hasSolo = hasSolo || layer.solo;

    auto voices = 0;
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex)
    {
        const auto& layer = parameters.layers[static_cast<std::size_t>(layerIndex)];
        if (!hasSolo || layer.solo)
            voices += layerVoiceCost(parameters, layerIndex);
    }

    return voices;
}

inline float semitonesToHz(float semitonesFromMidiZero) noexcept
{
    return midiNoteToHz(semitonesFromMidiZero);
}

inline int oversamplingFactor(int choice) noexcept
{
    switch (choice)
    {
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
        default: return 1;
    }
}

inline int patchCostNoteLimit(const SynthParameters& parameters) noexcept
{
    if (parameters.voiceMode == VoiceMode::Poly)
        return std::clamp(parameters.polyphony, 1, 32);

    return 1;
}

inline int patchCostUnisonVoices(const SynthParameters& parameters) noexcept
{
    if (parameters.voiceMode == VoiceMode::Mono || parameters.voiceMode == VoiceMode::MonoLegato)
        return 1;

    return std::clamp(parameters.unisonCount, 1, 8);
}

inline int patchCostActiveFxModules(const FxParameters& fx) noexcept
{
    if (!fx.enabled)
        return 0;

    return (fx.saturationEnabled ? 1 : 0)
        + (fx.phaserEnabled ? 1 : 0)
        + (fx.chorusEnabled ? 1 : 0)
        + (fx.delayEnabled ? 1 : 0)
        + (fx.reverbEnabled ? 1 : 0)
        + (fx.eqEnabled ? 1 : 0)
        + (fx.compressorEnabled ? 1 : 0);
}

inline PatchCostEstimate estimatePatchCost(const SynthParameters& parameters) noexcept
{
    PatchCostEstimate estimate;
    estimate.noteLimit = patchCostNoteLimit(parameters);
    estimate.unisonVoices = patchCostUnisonVoices(parameters);
    estimate.maxActiveVoices = std::clamp(estimate.noteLimit * estimate.unisonVoices, 1, 32);
    estimate.oscillatorSlotVoices = layerOscillatorVoiceCost(parameters);
    estimate.voiceUnits = estimate.maxActiveVoices * estimate.oscillatorSlotVoices;
    estimate.filterOversampling = parameters.filter.enabled ? oversamplingFactor(parameters.filter.oversampling) : 1;
    estimate.activeFxModules = patchCostActiveFxModules(parameters.fx);
    estimate.filterMultiplier = parameters.filter.enabled
        ? 0.5f + 0.5f * static_cast<float>(estimate.filterOversampling)
        : 1.0f;
    estimate.fxMultiplier = parameters.fx.enabled
        ? 1.0f + 0.06f * static_cast<float>(estimate.activeFxModules)
        : 1.0f;
    estimate.totalUnits = static_cast<float>(estimate.voiceUnits)
        * estimate.filterMultiplier
        * estimate.fxMultiplier;
    estimate.loadPercent = std::max(0, static_cast<int>(std::round(estimate.totalUnits / 240.0f * 100.0f)));
    estimate.elevated = estimate.loadPercent > 60;
    estimate.high = estimate.loadPercent > 90;
    estimate.overBudget = estimate.loadPercent > 100;
    return estimate;
}
} // namespace synth
