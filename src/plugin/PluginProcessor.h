#pragma once

#include <JuceHeader.h>

#include "../dsp/SynthEngine.h"
#include "../midi/MidiControllerMap.h"
#include "../modulation/ModulationRouteModel.h"
#include "../presets/PresetManager.h"
#include "ParameterRegistry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class SynthAudioProcessor final : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    SynthAudioProcessor();
    ~SynthAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    static constexpr int programsPerSubBank = 128;
    static constexpr int subBankCount = 4;
    static constexpr int programCount = programsPerSubBank * subBankCount;
    int getNumPrograms() override { return programCount; }
    int getCurrentProgram() override { return currentProgram.load(std::memory_order_acquire); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& name) override;
    bool selectProgram(int index, juce::String& message);
    bool previousProgram(juce::String& message);
    bool nextProgram(juce::String& message);
    bool copyCurrentProgram(juce::String& message);
    bool pasteCurrentProgram(juce::String& message);
    bool insertCurrentProgram(juce::String& message);
    bool deleteCurrentProgram(juce::String& message);
    bool loadProgramBank(const juce::File& file, juce::String& message);
    bool saveProgramBank(const juce::File& file, bool overwrite, juce::String& message);
    int getSelectedPart() const noexcept { return selectedPart.load(std::memory_order_acquire); }
    void setSelectedPart(int part) noexcept { selectedPart.store(std::clamp(part, 0, 1), std::memory_order_release); }
    float getEffectiveParameterValue(const juce::String& parameterId) const noexcept;
    std::optional<int> getLearnedMidiController(const juce::String& parameterId) const;
    bool submitUiMidiMessage(const juce::MidiMessage& message) noexcept;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    struct PresetListItem
    {
        juce::String displayName;
        juce::String author;
        juce::String description;
        juce::String bank;
        juce::String category;
        juce::String sourceLabel;
        juce::String favoriteKey;
        juce::String validationMessage;
        juce::File file;
        juce::StringArray tags;
        bool valid = true;
        bool factory = false;
        bool favorite = false;
    };

    struct DiagnosticsSnapshot
    {
        double sampleRate = 0.0;
        int blockSize = 0;
        int activeVoices = 0;
        int midiEvents = 0;
        int invalidSamples = 0;
        float peak = 0.0f;
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        synth::PatchCostEstimate patchCost;
        juce::String architecture;
        juce::String currentPreset;
        juce::String lastPresetStatus;
    };

    struct PresetWorkflowSnapshot
    {
        bool dirty = false;
        bool baselineValid = false;
        bool compareSlotAReady = false;
        bool compareSlotBReady = false;
        bool resetAvailable = false;
        juce::String currentPreset;
        juce::String currentPresetPath;
        juce::String lastPresetStatus;
    };

    std::vector<PresetListItem> getPresetList() const;
    std::optional<PresetListItem> getPresetListItemForFile(const juce::File& file) const;
    juce::File getUserPresetDirectory() const;
    bool loadPresetFile(const juce::File& file, juce::String& message);
    bool savePresetFile(const juce::File& file, const juce::String& displayName, juce::String& message);
    bool savePresetFile(const juce::File& file, const synth::PresetWriteOptions& options, juce::String& message);
    bool initializeCurrentPreset(juce::String& message);
    bool resetCurrentPreset(juce::String& message);
    bool randomizeCurrentPreset(juce::String& message);
    bool randomizeCurrentPresetWithSeed(std::uint32_t seed, juce::String& message);
    PresetWorkflowSnapshot getPresetWorkflowSnapshot() const;
    bool capturePresetCompareSlot(int slotIndex, juce::String& message);
    bool recallPresetCompareSlot(int slotIndex, juce::String& message);
    juce::String getCurrentPresetName() const;
    juce::String getCurrentPresetFilePath() const;
    synth::ModulationRouteView getModulationRouteView() const;
    bool writeModulationRoute(const synth::ModulationRouteWriteRequest& request, juce::String& message);
    bool clearModulationSlot(int slotNumber, juce::String& message);
    std::vector<synth::MidiControllerAssignment> getMidiControllerAssignments() const;
    juce::String getMidiControllerStatus() const;
    bool assignMidiController(int controllerNumber, const juce::String& parameterId, juce::String& message);
    bool forgetMidiControllerForParameter(const juce::String& parameterId, juce::String& message);
    bool startMidiLearn(const juce::String& parameterId, juce::String& message);
    void cancelMidiLearn();
    DiagnosticsSnapshot getDiagnosticsSnapshot() const;
    void requestPanic() noexcept;

    // Lock-free visual feed for the editor's animated displays. The audio thread
    // publishes after rendering; the editor only ever reads the atomics.
    struct UiVisualSnapshot
    {
        bool lfoVoiceActive = false;
        float lfoPhase = 0.0f;
        float lfoValue = 0.0f;
        float pitchBend = 0.0f;
        float modWheel = 0.0f;
    };
    UiVisualSnapshot getUiVisualSnapshot() const noexcept;
    // Copies the most recent output-scope samples (oldest first). Returns the
    // count copied; tearing at the write head is acceptable for visualization.
    int readScopeSamples(float* destination, int maxSamples) const noexcept;
    static constexpr int scopeCapacity = 2048;

private:
    struct ProgramSlot
    {
        juce::String name { "Init" };
        juce::ValueTree state;
        juce::ValueTree baseline;
    };
    void storeCurrentProgram();
    juce::ValueTree createProgramBankState();
    bool restoreProgramBankState(const juce::ValueTree& bank, juce::String& message);
    void applyPendingProgramRequest();
    void drainUiMidiMessages() noexcept;
    std::array<ProgramSlot, programCount> programs;
    ProgramSlot programClipboard;
    bool programClipboardValid = false;
    mutable juce::CriticalSection programLock;
    std::atomic<int> selectedPart { 0 };
    std::atomic<int> currentProgram { 0 };
    std::atomic<int> requestedProgram { -1 };
    std::atomic<int> midiSubBank { 0 };
    static constexpr int uiMidiCapacity = 256;
    juce::AbstractFifo uiMidiFifo { uiMidiCapacity };
    std::atomic<bool> uiMidiReleaseOverflow { false };
    std::atomic<int> uiRequestedPitchWheel { -1 };
    std::atomic<int> uiRequestedModWheel { -1 };
    struct UiMidiPacket
    {
        std::array<juce::uint8, 3> bytes {};
        int size = 0;
    };
    std::array<UiMidiPacket, uiMidiCapacity> uiMidiPackets;

    struct PendingMidiControllerValue
    {
        std::atomic<int> value { -1 };
        std::atomic<std::uint32_t> sequence { 0 };
    };

    struct RawParameterPointers
    {
        struct RawLayerOscillator
        {
            std::atomic<float>* voices = nullptr;
            std::atomic<float>* waveform = nullptr;
            std::atomic<float>* octave = nullptr;
            std::atomic<float>* note = nullptr;
            std::atomic<float>* fineCents = nullptr;
            std::atomic<float>* level = nullptr;
            std::atomic<float>* phaseDegrees = nullptr;
            std::atomic<float>* detune = nullptr;
            std::atomic<float>* stereo = nullptr;
            std::atomic<float>* pan = nullptr;
            std::atomic<float>* retrigger = nullptr;
            std::atomic<float>* invert = nullptr;
        };

        struct RawLayer
        {
            std::atomic<float>* levelDb = nullptr;
            std::atomic<float>* pan = nullptr;
            std::atomic<float>* solo = nullptr;
            std::atomic<float>* mute = nullptr;
            std::array<RawLayerOscillator, synth::oscillatorSlotsPerLayer> oscillators {};
        };

        struct RawTransModSlot
        {
            std::atomic<float>* enabled = nullptr;
            std::atomic<float>* source = nullptr;
            std::atomic<float>* scaler = nullptr;
            std::atomic<float>* depth = nullptr;
            std::atomic<float>* oscPitchSemitones = nullptr;
            std::atomic<float>* filterCutoffSemitones = nullptr;
            std::atomic<float>* ampLevelDb = nullptr;
            std::atomic<float>* pan = nullptr;
        };

        struct RawArpStep
        {
            std::atomic<float>* enabled = nullptr;
            std::atomic<float>* pitchSemitones = nullptr;
            std::atomic<float>* velocity = nullptr;
            std::atomic<float>* gate = nullptr;
            std::atomic<float>* tie = nullptr;
        };

        struct RawChordVoice
        {
            std::atomic<float>* enabled = nullptr;
            std::atomic<float>* pitchSemitones = nullptr;
            std::atomic<float>* velocity = nullptr;
        };

        std::atomic<float>* voiceMode = nullptr;
        std::atomic<float>* voicePolyphony = nullptr;
        std::atomic<float>* voiceUnisonCount = nullptr;
        std::atomic<float>* voiceGlideMs = nullptr;
        std::atomic<float>* voiceVelocityGlideMs = nullptr;
        std::array<RawLayer, synth::layerCount> layers {};
        std::atomic<float>* modAttack = nullptr;
        std::atomic<float>* modDecay = nullptr;
        std::atomic<float>* modSustain = nullptr;
        std::atomic<float>* modRelease = nullptr;
        std::atomic<float>* lfoShape = nullptr;
        std::atomic<float>* lfoRateHz = nullptr;
        std::atomic<float>* lfoSyncDivision = nullptr;
        std::atomic<float>* lfoPhaseDegrees = nullptr;
        std::atomic<float>* lfoGateMode = nullptr;
        std::atomic<float>* lfoMono = nullptr;
        std::atomic<float>* lfoStepCount = nullptr;
        std::atomic<float>* lfoStepSmooth = nullptr;
        std::array<std::atomic<float>*, synth::lfoStepSlotCount> lfoSteps {};
        std::atomic<float>* rampEnabled = nullptr;
        std::atomic<float>* rampMode = nullptr;
        std::atomic<float>* rampDelayMs = nullptr;
        std::atomic<float>* rampRiseMs = nullptr;
        std::atomic<float>* rampCurve = nullptr;
        std::atomic<float>* arpEnabled = nullptr;
        std::atomic<float>* arpMode = nullptr;
        std::atomic<float>* arpRate = nullptr;
        std::atomic<float>* arpGate = nullptr;
        std::atomic<float>* arpOctaves = nullptr;
        std::atomic<float>* arpHold = nullptr;
        std::atomic<float>* arpSwing = nullptr;
        std::atomic<float>* arpStepCount = nullptr;
        std::array<RawArpStep, synth::arpStepCount> arpSteps {};
        std::atomic<float>* chordEnabled = nullptr;
        std::atomic<float>* chordVoiceCount = nullptr;
        std::array<RawChordVoice, synth::chordVoiceCount> chordVoices {};
        std::atomic<float>* directFilterKeytrack = nullptr;
        std::atomic<float>* directFilterLfoSemitones = nullptr;
        std::atomic<float>* directFilterModEnvSemitones = nullptr;
        std::atomic<float>* directOscKeytrackSemitones = nullptr;
        std::atomic<float>* directOscLfoSemitones = nullptr;
        std::atomic<float>* directOscModEnvSemitones = nullptr;
        std::atomic<float>* fxSaturationEnabled = nullptr;
        std::atomic<float>* fxDistortionMode = nullptr;
        std::atomic<float>* fxSaturationMix = nullptr;
        std::atomic<float>* fxSaturationDrive = nullptr;
        std::atomic<float>* fxPhaserEnabled = nullptr;
        std::atomic<float>* fxPhaserMix = nullptr;
        std::atomic<float>* fxPhaserRateHz = nullptr;
        std::atomic<float>* fxPhaserDepth = nullptr;
        std::atomic<float>* fxPhaserFeedback = nullptr;
        std::atomic<float>* fxDelayEnabled = nullptr;
        std::atomic<float>* fxDelayMix = nullptr;
        std::atomic<float>* fxDelaySyncDivision = nullptr;
        std::atomic<float>* fxDelayFeedback = nullptr;
        std::atomic<float>* fxReverbEnabled = nullptr;
        std::atomic<float>* fxReverbMix = nullptr;
        std::atomic<float>* fxReverbDecay = nullptr;
        std::atomic<float>* fxChorusEnabled = nullptr;
        std::atomic<float>* fxChorusMix = nullptr;
        std::atomic<float>* fxChorusRateHz = nullptr;
        std::atomic<float>* fxChorusDepthMs = nullptr;
        std::atomic<float>* fxEqEnabled = nullptr;
        std::atomic<float>* fxEqLowGainDb = nullptr;
        std::atomic<float>* fxEqHighGainDb = nullptr;
        std::atomic<float>* fxCompressorEnabled = nullptr;
        std::atomic<float>* fxCompressorThresholdDb = nullptr;
        std::atomic<float>* fxCompressorRatio = nullptr;
        std::atomic<float>* fxCompressorMakeupDb = nullptr;
        std::atomic<float>* fxCompressorMix = nullptr;
        std::array<RawTransModSlot, synth::transModSlotCount> transMod {};
        std::atomic<float>* macroMotion = nullptr;
        std::atomic<float>* macroWidth = nullptr;
        std::atomic<float>* macroDrive = nullptr;
        std::atomic<float>* macroSpace = nullptr;
    };

    struct RawNativeParameters
    {
        std::atomic<float>* phaserSyncDivision = nullptr;
        std::atomic<float>* chorusSyncDivision = nullptr;
        std::atomic<float>* warmDrive = nullptr;
        std::atomic<float>* portamentoMode = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* pitchBendRange = nullptr;
        std::atomic<float>* masterLevel = nullptr;
        std::atomic<float>* filterControlCutoffSemitones = nullptr;
        std::atomic<float>* filterControlResonance = nullptr;
        std::atomic<float>* filterControlKeytrack = nullptr;
        std::atomic<float>* filterControlDrive = nullptr;
        std::atomic<float>* layer0FilterEnabled = nullptr;
        std::atomic<float>* layer0FilterMode = nullptr;
        std::atomic<float>* layer0FilterCutoffsemitones = nullptr;
        std::atomic<float>* layer0FilterResonance = nullptr;
        std::atomic<float>* layer0FilterDrive = nullptr;
        std::atomic<float>* layer0FilterKeytrack = nullptr;
        std::atomic<float>* layer0FilterOversampling = nullptr;
        std::atomic<float>* layer0FilterInput = nullptr;
        std::atomic<float>* layer0AmpAttackms = nullptr;
        std::atomic<float>* layer0AmpDecayms = nullptr;
        std::atomic<float>* layer0AmpSustain = nullptr;
        std::atomic<float>* layer0AmpReleasems = nullptr;
        std::atomic<float>* layer1FilterEnabled = nullptr;
        std::atomic<float>* layer1FilterMode = nullptr;
        std::atomic<float>* layer1FilterCutoffsemitones = nullptr;
        std::atomic<float>* layer1FilterResonance = nullptr;
        std::atomic<float>* layer1FilterDrive = nullptr;
        std::atomic<float>* layer1FilterKeytrack = nullptr;
        std::atomic<float>* layer1FilterOversampling = nullptr;
        std::atomic<float>* layer1FilterInput = nullptr;
        std::atomic<float>* layer1AmpAttackms = nullptr;
        std::atomic<float>* layer1AmpDecayms = nullptr;
        std::atomic<float>* layer1AmpSustain = nullptr;
        std::atomic<float>* layer1AmpReleasems = nullptr;
        std::atomic<float>* modEnv2Attackms = nullptr;
        std::atomic<float>* modEnv2Decayms = nullptr;
        std::atomic<float>* modEnv2Sustain = nullptr;
        std::atomic<float>* modEnv2Releasems = nullptr;
        std::atomic<float>* lfo0Gain = nullptr;
        std::atomic<float>* lfo0Offset = nullptr;
        std::atomic<float>* lfo0Free = nullptr;
        std::atomic<float>* lfo1Shape = nullptr;
        std::atomic<float>* lfo1Ratehz = nullptr;
        std::atomic<float>* lfo1Syncdivision = nullptr;
        std::atomic<float>* lfo1Phasedegrees = nullptr;
        std::atomic<float>* lfo1Gatemode = nullptr;
        std::atomic<float>* lfo1Mono = nullptr;
        std::atomic<float>* lfo1Stepcount = nullptr;
        std::atomic<float>* lfo1Stepsmooth = nullptr;
        std::atomic<float>* lfo1Gain = nullptr;
        std::atomic<float>* lfo1Offset = nullptr;
        std::atomic<float>* lfo1Free = nullptr;
        std::atomic<float>* fxPhasercenterhz = nullptr;
        std::atomic<float>* fxPhaserspread = nullptr;
        std::atomic<float>* fxPhaserlroffset = nullptr;
        std::atomic<float>* fxPhaserwidth = nullptr;
        std::atomic<float>* fxChorusdelayms = nullptr;
        std::atomic<float>* fxChorusfeedback = nullptr;
        std::atomic<float>* fxChorusdualmode = nullptr;
        std::atomic<float>* fxChoruswidth = nullptr;
        std::atomic<float>* fxEqlowfrequencyhz = nullptr;
        std::atomic<float>* fxEqhighfrequencyhz = nullptr;
        std::atomic<float>* fxDelaytimeleftms = nullptr;
        std::atomic<float>* fxDelaytimerightms = nullptr;
        std::atomic<float>* fxDelayrightsyncdivision = nullptr;
        std::atomic<float>* fxDelaypingpong = nullptr;
        std::atomic<float>* fxDelayspread = nullptr;
        std::atomic<float>* fxDelaywidth = nullptr;
        std::atomic<float>* fxDelaylowcuthz = nullptr;
        std::atomic<float>* fxDelayhighcuthz = nullptr;
        std::atomic<float>* fxDelaysmear = nullptr;
        std::atomic<float>* fxReverbpredelayms = nullptr;
        std::atomic<float>* fxReverbdamp = nullptr;
        std::atomic<float>* fxReverbwidth = nullptr;
        std::atomic<float>* fxCompressorattackms = nullptr;
        std::atomic<float>* fxCompressorreleasems = nullptr;
        std::atomic<float>* arpVelocitymode = nullptr;
        std::atomic<float>* arpWrap = nullptr;
        std::atomic<float>* arpTimems = nullptr;
        std::array<std::atomic<float>*, synth::lfoStepSlotCount> lfo2Steps {};
        std::array<std::array<std::atomic<float>*, synth::nativeModDestinationCount>, synth::transModSlotCount> depths {};
        std::array<std::array<std::atomic<float>*, 2>, synth::transModSlotCount> destinations {};
        std::array<std::array<std::atomic<float>*, 2>, synth::transModSlotCount> amounts {};
    };

    void cacheParameterPointers();
    synth::SynthParameters readParameters(float tempoBpm, bool offlineRender) const noexcept;
    float currentTempoBpm() const noexcept;
    int tailDrainSamplesFor(const synth::SynthParameters& snapshot) const noexcept;
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void handleMappedController(int controllerNumber, int controllerValue) noexcept;
    synth::RenderStats renderSegment(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void setPresetMetadata(const juce::String& presetName,
                           const juce::String& status,
                           const juce::String& presetFilePath);
    void setPresetBaselineFingerprint(const synth::PresetStateFingerprint& fingerprint);
    void timerCallback() override;
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    void loadMidiControllerAssignments();
    void publishMidiControllerAssignments();
    bool assignMidiControllerInternal(int controllerNumber,
                                      const juce::String& parameterId,
                                      juce::String& message,
                                      bool persist);
    bool applyModulationRouteParameterEdits(const std::vector<synth::ModulationRouteParameterEdit>& edits, juce::String& message);
    bool applyPreparedPresetState(const juce::ValueTree& state,
                                  const synth::PresetStateFingerprint& baselineFingerprint,
                                  const juce::String& status,
                                  const juce::String& presetName,
                                  const juce::String& presetFilePath,
                                  juce::String& message,
                                  bool updateProgramBaseline = true);
    int parameterIndexForId(const juce::String& parameterId) const;
    void applyPendingMidiLearns();
    void applyPendingMappedControllers();
    void applyMappedControllerValue(int parameterIndex, int controllerValue);
    void setMidiControllerStatus(const juce::String& status);
    void publishUiVisuals(const juce::AudioBuffer<float>& buffer, int totalSamples,
                          int activeVoices, bool usesMonoLfo) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    synth::SynthEngine engine;
    const synth::SynthParameters parameterDefaults {};
    RawParameterPointers raw;
    RawNativeParameters nativeRaw;
    std::array<std::atomic<int>, 128> midiControllerParameterIndices {};
    std::array<PendingMidiControllerValue, 128> pendingMidiControllerValues {};
    std::array<std::uint32_t, 128> appliedMidiControllerSequences {};
    std::atomic<int> pendingMidiLearnParameterIndex { -1 };
    std::atomic<int> learnedMidiControllerNumber { -1 };
    std::atomic<int> learnedMidiParameterIndex { -1 };
    mutable juce::CriticalSection midiControllerLock;
    std::vector<synth::MidiControllerAssignment> midiControllerAssignments;
    juce::String midiControllerStatus { "MIDI learn ready" };
    static constexpr int scratchCapacity = 32768;
    std::array<float, scratchCapacity> scratchLeft {};
    std::array<float, scratchCapacity> scratchRight {};
    std::atomic<double> diagnosticSampleRate { 0.0 };
    std::atomic<int> diagnosticBlockSize { 0 };
    std::atomic<int> diagnosticActiveVoices { 0 };
    std::atomic<int> diagnosticMidiEvents { 0 };
    std::atomic<int> diagnosticInvalidSamples { 0 };
    std::atomic<float> diagnosticPeak { 0.0f };
    std::atomic<float> diagnosticPeakLeft { 0.0f };
    std::atomic<float> diagnosticPeakRight { 0.0f };
    std::atomic<float> diagnosticTempoBpm { 128.0f };
    std::atomic<bool> panicRequested { false };
    std::array<std::atomic<float>, scopeCapacity> scopeSamples {};
    std::atomic<int> scopeWritePosition { 0 };
    std::atomic<bool> uiLfoVoiceActive { false };
    std::atomic<float> uiLfoPhase { 0.0f };
    std::atomic<float> uiLfoValue { 0.0f };
    std::atomic<float> uiPitchBend { 0.0f };
    std::atomic<float> uiModWheel { 0.0f };
    int tailDrainSamplesRemaining = 0;
    std::atomic<std::uint64_t> parameterStateSequence { 0 };
    std::atomic<std::uint64_t> presetParameterRevision { 1 };
    std::atomic<std::uint64_t> presetBaselineRevision { 1 };
    mutable juce::CriticalSection presetMetadataLock;
    juce::String currentPresetName { "Init" };
    juce::String currentPresetFilePath;
    juce::String lastPresetStatus { "Init state" };
    mutable juce::CriticalSection presetWorkflowLock;
    synth::PresetStateFingerprint presetBaselineFingerprint;
    std::array<synth::PresetCompareSlot, 2> presetCompareSlots {};
    mutable juce::CriticalSection presetDirtyStateLock;
    mutable std::uint64_t cachedPresetParameterRevision = 0;
    mutable std::uint64_t cachedPresetBaselineRevision = 0;
    mutable synth::PresetDirtyState cachedPresetDirtyState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessor)
};
