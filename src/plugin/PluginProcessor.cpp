#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "../presets/PresetManager.h"
#include "../presets/PresetValidator.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace
{
juce::File juceFileForPath(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return juce::File { juce::String(path.lexically_normal().string()) };

    std::error_code error;
    auto absolutePath = std::filesystem::current_path(error);
    if (!error)
        absolutePath /= path;
    else
        absolutePath = path;

    return juce::File { juce::String(absolutePath.lexically_normal().string()) };
}

class ScopedParameterStateUpdate final
{
public:
    explicit ScopedParameterStateUpdate(std::atomic<std::uint64_t>& sequenceToUpdate) noexcept
        : sequence(sequenceToUpdate)
    {
        sequence.fetch_add(1, std::memory_order_acq_rel);
    }

    ~ScopedParameterStateUpdate()
    {
        sequence.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<std::uint64_t>& sequence;
};

juce::String binaryArchitecture()
{
#if defined(__arm64__) || defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

bool validMidiControllerNumber(int controllerNumber) noexcept
{
    return controllerNumber >= 0 && controllerNumber <= 127;
}

bool reservedMidiControllerNumber(int controllerNumber) noexcept
{
    return controllerNumber == 64 || controllerNumber == 120 || controllerNumber == 123;
}

SynthAudioProcessor::PresetListItem makePresetListItem(const synth::PresetSummary& preset)
{
    SynthAudioProcessor::PresetListItem item;
    item.displayName = preset.displayName.empty() ? juce::String(preset.path.stem().string())
                                                  : juce::String(preset.displayName);
    item.author = juce::String(preset.author);
    item.description = juce::String(preset.description);
    item.bank = juce::String(preset.bank);
    item.category = juce::String(preset.category);
    item.sourceLabel = juce::String(synth::presetSourceLabel(preset.source));
    item.favoriteKey = juce::String(preset.favoriteKey);
    item.file = juceFileForPath(preset.path);
    for (const auto& tag : preset.tags)
        item.tags.add(juce::String(tag));
    item.factory = preset.factory;
    item.favorite = preset.favorite;
    return item;
}

bool hasPresetFileExtension(const juce::File& file)
{
    return file.getFileExtension().equalsIgnoreCase(".SynthiaPreset");
}

juce::File withPresetFileExtension(juce::File file)
{
    return hasPresetFileExtension(file) ? file : file.withFileExtension(".SynthiaPreset");
}

SynthAudioProcessor::PresetListItem makeInvalidPresetListItem(const synth::PresetValidationResult& validation,
                                                              synth::PresetSource source)
{
    SynthAudioProcessor::PresetListItem item;
    item.displayName = juce::String(validation.path.stem().string());
    item.bank = juce::String(synth::presetSourceLabel(source));
    item.category = "Invalid";
    item.sourceLabel = juce::String(synth::presetSourceLabel(source));
    item.validationMessage = validation.errors.empty()
        ? juce::String("Preset validation failed")
        : juce::String(validation.errors.front());
    item.file = juceFileForPath(validation.path);
    item.tags.add("invalid");
    item.valid = false;
    item.factory = source == synth::PresetSource::Factory;
    return item;
}
float longestVoiceReleaseMs(const synth::SynthParameters& parameters) noexcept
{
    auto release = parameters.modEnv.releaseMs;
    release = std::max(release, parameters.modEnv2.releaseMs);
    for (const auto& layer : parameters.layers)
        release = std::max(release, layer.ampEnv.releaseMs);
    return release;
}
} // namespace

SynthAudioProcessor::SynthAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SYNTHIA_STATE", synth::createParameterLayout())
{
    for (auto& parameterIndex : midiControllerParameterIndices)
        parameterIndex.store(-1, std::memory_order_relaxed);

    cacheParameterPointers();
    programs[0].state = parameters.copyState().createCopy();
    programs[0].baseline = programs[0].state.createCopy();
    const auto factoryPresets = synth::scanPresetDirectory(synth::factoryPresetDirectory(), true);
    int factoryIndex = 1;
    for (const auto& preset : factoryPresets)
    {
        if (factoryIndex >= programsPerSubBank || preset.displayName == "Init") continue;
        const auto loaded = synth::preparePresetState(parameters, preset.path);
        if (loaded.loaded)
        {
            auto& slot = programs[static_cast<std::size_t>(factoryIndex++)];
            slot.name = juce::String(loaded.displayName);
            slot.state = loaded.state.createCopy();
            slot.baseline = slot.state.createCopy();
        }
    }
    setPresetBaselineFingerprint(synth::fingerprintCurrentPresetState(parameters));
    for (const auto& spec : synth::getParameterSpecs())
        if (spec.presetSerialized)
            parameters.addParameterListener(juce::String(spec.id), this);
    loadMidiControllerAssignments();
    startTimerHz(60);
}

SynthAudioProcessor::~SynthAudioProcessor()
{
    stopTimer();
    for (const auto& spec : synth::getParameterSpecs())
        if (spec.presetSerialized)
            parameters.removeParameterListener(juce::String(spec.id), this);
}

void SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock);
    diagnosticSampleRate.store(sampleRate, std::memory_order_relaxed);
    diagnosticBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
}

void SynthAudioProcessor::releaseResources()
{
    engine.reset();
}

bool SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const auto totalSamples = buffer.getNumSamples();

    auto clearForStateUpdate = [this, &buffer, &midiMessages, totalSamples] {
        buffer.clear();
        midiMessages.clear();
        diagnosticPeak.store(0.0f, std::memory_order_relaxed);
        diagnosticPeakLeft.store(0.0f, std::memory_order_relaxed);
        diagnosticPeakRight.store(0.0f, std::memory_order_relaxed);
        diagnosticActiveVoices.store(0, std::memory_order_relaxed);
        diagnosticBlockSize.store(totalSamples, std::memory_order_relaxed);
    };

    const auto sequenceBeforeRead = parameterStateSequence.load(std::memory_order_acquire);
    if ((sequenceBeforeRead & 1u) != 0u)
    {
        clearForStateUpdate();
        return;
    }

    const auto tempoBpm = currentTempoBpm();
    diagnosticTempoBpm.store(tempoBpm, std::memory_order_relaxed);
    const auto hadMidi = !midiMessages.isEmpty() || uiMidiFifo.getNumReady() > 0 || uiMidiReleaseOverflow.load(std::memory_order_acquire) || uiRequestedPitchWheel.load(std::memory_order_acquire) >= 0 || uiRequestedModWheel.load(std::memory_order_acquire) >= 0;
    auto parameterSnapshot = readParameters(tempoBpm, isNonRealtime());
    const auto sequenceAfterRead = parameterStateSequence.load(std::memory_order_acquire);
    if (sequenceBeforeRead != sequenceAfterRead || (sequenceAfterRead & 1u) != 0u)
    {
        clearForStateUpdate();
        return;
    }

    if (panicRequested.exchange(false, std::memory_order_acq_rel))
        engine.panic();

    engine.setParameters(parameterSnapshot);
    drainUiMidiMessages();

    if (!hadMidi && engine.getActiveVoiceCount() <= 0 && tailDrainSamplesRemaining <= 0 && !engine.hasPendingArpeggiatorEvents())
    {
        engine.advanceIdleModulators(totalSamples);
        buffer.clear();
        diagnosticPeak.store(0.0f, std::memory_order_relaxed);
        diagnosticPeakLeft.store(0.0f, std::memory_order_relaxed);
        diagnosticPeakRight.store(0.0f, std::memory_order_relaxed);
        diagnosticActiveVoices.store(0, std::memory_order_relaxed);
        diagnosticBlockSize.store(totalSamples, std::memory_order_relaxed);
        uiLfoPhase.store(engine.getMonoLfoPhase(), std::memory_order_relaxed);
        uiLfoValue.store(engine.getMonoLfoValue(), std::memory_order_relaxed);
        uiLfoVoiceActive.store(false, std::memory_order_relaxed);
        return;
    }

    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto renderedUntil = 0;
    auto blockPeak = 0.0f;
    auto blockInvalidSamples = 0;
    auto blockActiveVoices = 0;

    for (const auto metadata : midiMessages)
    {
        const auto eventSample = std::clamp(metadata.samplePosition, 0, totalSamples);
        const auto segmentStats = renderSegment(buffer, renderedUntil, eventSample - renderedUntil);
        blockPeak = std::max(blockPeak, segmentStats.peak);
        blockInvalidSamples += segmentStats.invalidSamples;
        blockActiveVoices = segmentStats.activeVoices;
        renderedUntil = eventSample;

        if (metadata.numBytes >= 1 && metadata.numBytes <= 3)
            handleMidiMessage(metadata.getMessage());
    }

    const auto finalStats = renderSegment(buffer, renderedUntil, totalSamples - renderedUntil);
    blockPeak = std::max(blockPeak, finalStats.peak);
    blockInvalidSamples += finalStats.invalidSamples;
    blockActiveVoices = finalStats.activeVoices;

    const auto usesMonoLfo = parameterSnapshot.lfo.free || parameterSnapshot.lfo.mono
        || parameterSnapshot.lfo.gateMode == synth::LfoGateMode::Mono
        || parameterSnapshot.lfo.gateMode == synth::LfoGateMode::Song;
    publishUiVisuals(buffer, totalSamples, blockActiveVoices, usesMonoLfo);

    diagnosticPeak.store(blockPeak, std::memory_order_relaxed);
    diagnosticPeakLeft.store(buffer.getNumChannels() > 0 ? buffer.getMagnitude(0, 0, totalSamples) : 0.0f, std::memory_order_relaxed);
    diagnosticPeakRight.store(buffer.getNumChannels() > 1 ? buffer.getMagnitude(1, 0, totalSamples) : diagnosticPeakLeft.load(std::memory_order_relaxed), std::memory_order_relaxed);
    diagnosticInvalidSamples.fetch_add(blockInvalidSamples, std::memory_order_relaxed);
    diagnosticActiveVoices.store(blockActiveVoices, std::memory_order_relaxed);
    diagnosticBlockSize.store(totalSamples, std::memory_order_relaxed);
    if (hadMidi || blockActiveVoices > 0)
        tailDrainSamplesRemaining = std::max(tailDrainSamplesRemaining,
                                             tailDrainSamplesFor(parameterSnapshot));
    else if (tailDrainSamplesRemaining > 0)
        tailDrainSamplesRemaining = std::max(0, tailDrainSamplesRemaining - totalSamples);

    midiMessages.clear();
}

double SynthAudioProcessor::getTailLengthSeconds() const
{
    auto snapshot = readParameters(diagnosticTempoBpm.load(std::memory_order_relaxed), false);
    const auto voiceTail = longestVoiceReleaseMs(snapshot) * 0.001f;
    return std::max(static_cast<double>(voiceTail),
                    static_cast<double>(synth::fxTailLengthSeconds(snapshot)));
}

void SynthAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    diagnosticMidiEvents.fetch_add(1, std::memory_order_relaxed);

    if (message.isProgramChange())
        requestedProgram.store(message.getProgramChangeNumber(), std::memory_order_release);
    else if (message.isNoteOn(false))
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff(true))
        engine.noteOff(message.getNoteNumber());
    else if (message.isAllSoundOff())
        engine.panic();
    else if (message.isAllNotesOff())
        engine.allNotesOff();
    else if (message.isPitchWheel())
    {
        const auto bend = (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f;
        engine.setPitchBend(bend);
        uiPitchBend.store(bend, std::memory_order_relaxed);
    }
    else if (message.isControllerOfType(64))
        engine.setSustainPedal(message.getControllerValue() >= 64);
    else if (message.isControllerOfType(1))
    {
        const auto wheel = static_cast<float>(message.getControllerValue()) / 127.0f;
        engine.setModWheel(wheel);
        uiModWheel.store(wheel, std::memory_order_relaxed);
    }
    else if (message.isAftertouch())
        engine.setAftertouch(static_cast<float>(message.getAfterTouchValue()) / 127.0f);
    else if (message.isChannelPressure())
        engine.setAftertouch(static_cast<float>(message.getChannelPressureValue()) / 127.0f);

    if (message.isController())
        handleMappedController(message.getControllerNumber(), message.getControllerValue());
}

void SynthAudioProcessor::handleMappedController(int controllerNumber, int controllerValue) noexcept
{
    if (!validMidiControllerNumber(controllerNumber))
        return;

    const auto controllerIndex = static_cast<std::size_t>(controllerNumber);
    const auto learnedParameter = pendingMidiLearnParameterIndex.exchange(-1, std::memory_order_acq_rel);
    if (learnedParameter >= 0)
    {
        learnedMidiParameterIndex.store(learnedParameter, std::memory_order_release);
        learnedMidiControllerNumber.store(controllerNumber, std::memory_order_release);
    }

    const auto parameterIndex = midiControllerParameterIndices[controllerIndex].load(std::memory_order_relaxed);
    if (parameterIndex < 0)
        return;

    pendingMidiControllerValues[controllerIndex].value.store(std::clamp(controllerValue, 0, 127),
                                                             std::memory_order_relaxed);
    pendingMidiControllerValues[controllerIndex].sequence.fetch_add(1, std::memory_order_release);
}

synth::RenderStats SynthAudioProcessor::renderSegment(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return {};

    if (buffer.getNumChannels() <= 0)
    {
        return engine.process(nullptr, nullptr, numSamples);
    }

    if (buffer.getNumChannels() > 1)
    {
        return engine.process(buffer.getWritePointer(0, startSample),
                              buffer.getWritePointer(1, startSample),
                              numSamples);
    }

    auto* mono = buffer.getWritePointer(0, startSample);
    auto remaining = numSamples;
    auto offset = 0;
    synth::RenderStats stats;
    stats.samplesRendered = numSamples;

    while (remaining > 0)
    {
        const auto chunk = std::min(remaining, scratchCapacity);
        const auto chunkStats = engine.process(scratchLeft.data(), scratchRight.data(), chunk);
        stats.invalidSamples += chunkStats.invalidSamples;
        stats.peak = std::max(stats.peak, chunkStats.peak);
        stats.activeVoices = chunkStats.activeVoices;

        for (int i = 0; i < chunk; ++i)
            mono[offset + i] = 0.5f * (scratchLeft[static_cast<std::size_t>(i)] + scratchRight[static_cast<std::size_t>(i)]);

        offset += chunk;
        remaining -= chunk;
    }

    return stats;
}

void SynthAudioProcessor::cacheParameterPointers()
{
    auto get = [this](const char* id) {
        return parameters.getRawParameterValue(id);
    };

    raw.voiceMode = get("voice.mode");
    raw.voicePolyphony = get("voice.polyphony");
    raw.voiceUnisonCount = get("voice.unison_count");
    raw.voiceGlideMs = get("voice.glide_ms");
    raw.voiceVelocityGlideMs = get("voice.velocity_glide_ms");
    for (int layer = 0; layer < synth::layerCount; ++layer)
    {
        const auto layerPrefix = "layer." + std::to_string(layer + 1) + ".";
        auto& rawLayer = raw.layers[static_cast<std::size_t>(layer)];
        rawLayer.levelDb = get((layerPrefix + "level_db").c_str());
        rawLayer.pan = get((layerPrefix + "pan").c_str());
        rawLayer.solo = get((layerPrefix + "solo").c_str());
        rawLayer.mute = get((layerPrefix + "mute").c_str());

        for (int oscillator = 0; oscillator < synth::oscillatorSlotsPerLayer; ++oscillator)
        {
            const auto oscillatorPrefix = layerPrefix + "osc." + std::to_string(oscillator + 1) + ".";
            auto& rawOscillator = rawLayer.oscillators[static_cast<std::size_t>(oscillator)];
            rawOscillator.voices = get((oscillatorPrefix + "voices").c_str());
            rawOscillator.waveform = get((oscillatorPrefix + "waveform").c_str());
            rawOscillator.octave = get((oscillatorPrefix + "octave").c_str());
            rawOscillator.note = get((oscillatorPrefix + "note").c_str());
            rawOscillator.fineCents = get((oscillatorPrefix + "fine_cents").c_str());
            rawOscillator.level = get((oscillatorPrefix + "level").c_str());
            rawOscillator.phaseDegrees = get((oscillatorPrefix + "phase_degrees").c_str());
            rawOscillator.detune = get((oscillatorPrefix + "detune").c_str());
            rawOscillator.stereo = get((oscillatorPrefix + "stereo").c_str());
            rawOscillator.pan = get((oscillatorPrefix + "pan").c_str());
            rawOscillator.retrigger = get((oscillatorPrefix + "retrigger").c_str());
            rawOscillator.invert = get((oscillatorPrefix + "invert").c_str());
        }
    }
    raw.modAttack = get("mod_env.attack_ms");
    raw.modDecay = get("mod_env.decay_ms");
    raw.modSustain = get("mod_env.sustain");
    raw.modRelease = get("mod_env.release_ms");
    raw.lfoShape = get("lfo.shape");
    raw.lfoRateHz = get("lfo.rate_hz");
    raw.lfoSyncDivision = get("lfo.sync_division");
    raw.lfoPhaseDegrees = get("lfo.phase_degrees");
    raw.lfoGateMode = get("lfo.gate_mode");
    raw.lfoMono = get("lfo.mono");
    raw.lfoStepCount = get("lfo.step_count");
    raw.lfoStepSmooth = get("lfo.step_smooth");
    for (int step = 0; step < synth::lfoStepSlotCount; ++step)
        raw.lfoSteps[static_cast<std::size_t>(step)] = get(("lfo.step." + std::to_string(step + 1)).c_str());
    raw.rampEnabled = get("ramp.enabled");
    raw.rampMode = get("ramp.mode");
    raw.rampDelayMs = get("ramp.delay_ms");
    raw.rampRiseMs = get("ramp.rise_ms");
    raw.rampCurve = get("ramp.curve");
    raw.arpEnabled = get("arp.enabled");
    raw.arpMode = get("arp.mode");
    raw.arpRate = get("arp.rate");
    raw.arpGate = get("arp.gate");
    raw.arpOctaves = get("arp.octaves");
    raw.arpHold = get("arp.hold");
    raw.arpSwing = get("arp.swing");
    raw.arpStepCount = get("arp.step_count");
    for (int step = 0; step < synth::arpStepCount; ++step)
    {
        const auto prefix = "arp.step." + std::to_string(step + 1) + ".";
        auto& rawStep = raw.arpSteps[static_cast<std::size_t>(step)];
        rawStep.enabled = get((prefix + "enabled").c_str());
        rawStep.pitchSemitones = get((prefix + "pitch_semitones").c_str());
        rawStep.velocity = get((prefix + "velocity").c_str());
        rawStep.gate = get((prefix + "gate").c_str());
        rawStep.tie = get((prefix + "tie").c_str());
    }
    raw.chordEnabled = get("chord.enabled");
    raw.chordVoiceCount = get("chord.voice_count");
    for (int voice = 0; voice < synth::chordVoiceCount; ++voice)
    {
        const auto prefix = "chord.voice." + std::to_string(voice + 1) + ".";
        auto& rawVoice = raw.chordVoices[static_cast<std::size_t>(voice)];
        rawVoice.enabled = get((prefix + "enabled").c_str());
        rawVoice.pitchSemitones = get((prefix + "pitch_semitones").c_str());
        rawVoice.velocity = get((prefix + "velocity").c_str());
    }
    raw.directFilterKeytrack = get("direct.filter_keytrack");
    raw.directFilterLfoSemitones = get("direct.filter_lfo_semitones");
    raw.directFilterModEnvSemitones = get("direct.filter_mod_env_semitones");
    raw.directOscKeytrackSemitones = get("direct.osc_keytrack_semitones");
    raw.directOscLfoSemitones = get("direct.osc_lfo_semitones");
    raw.directOscModEnvSemitones = get("direct.osc_mod_env_semitones");
    raw.fxSaturationEnabled = get("fx.saturation_enabled");
    raw.fxDistortionMode = get("fx.distortion_mode");
    raw.fxSaturationMix = get("fx.saturation_mix");
    raw.fxSaturationDrive = get("fx.saturation_drive");
    raw.fxPhaserEnabled = get("fx.phaser_enabled");
    raw.fxPhaserMix = get("fx.phaser_mix");
    raw.fxPhaserRateHz = get("fx.phaser_rate_hz");
    raw.fxPhaserDepth = get("fx.phaser_depth");
    raw.fxPhaserFeedback = get("fx.phaser_feedback");
    raw.fxDelayEnabled = get("fx.delay_enabled");
    raw.fxDelayMix = get("fx.delay_mix");
    raw.fxDelaySyncDivision = get("fx.delay_sync_division");
    raw.fxDelayFeedback = get("fx.delay_feedback");
    raw.fxReverbEnabled = get("fx.reverb_enabled");
    raw.fxReverbMix = get("fx.reverb_mix");
    raw.fxReverbDecay = get("fx.reverb_decay");
    raw.fxChorusEnabled = get("fx.chorus_enabled");
    raw.fxChorusMix = get("fx.chorus_mix");
    raw.fxChorusRateHz = get("fx.chorus_rate_hz");
    raw.fxChorusDepthMs = get("fx.chorus_depth_ms");
    raw.fxEqEnabled = get("fx.eq_enabled");
    raw.fxEqLowGainDb = get("fx.eq_low_gain_db");
    raw.fxEqHighGainDb = get("fx.eq_high_gain_db");
    raw.fxCompressorEnabled = get("fx.compressor_enabled");
    raw.fxCompressorThresholdDb = get("fx.compressor_threshold_db");
    raw.fxCompressorRatio = get("fx.compressor_ratio");
    raw.fxCompressorMakeupDb = get("fx.compressor_makeup_db");
    raw.fxCompressorMix = get("fx.compressor_mix");
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot + 1) + ".";
        auto& rawSlot = raw.transMod[static_cast<std::size_t>(slot)];
        rawSlot.enabled = get((prefix + "enabled").c_str());
        rawSlot.source = get((prefix + "source").c_str());
        rawSlot.scaler = get((prefix + "scaler").c_str());
        rawSlot.depth = get((prefix + "depth").c_str());
        rawSlot.oscPitchSemitones = get((prefix + "osc_pitch_semitones").c_str());
        rawSlot.filterCutoffSemitones = get((prefix + "filter_cutoff_semitones").c_str());
        rawSlot.ampLevelDb = get((prefix + "amp_level_db").c_str());
        rawSlot.pan = get((prefix + "pan").c_str());
    }
    raw.macroMotion = get("macro.motion");
    raw.macroWidth = get("macro.width");
    raw.macroDrive = get("macro.drive");
    raw.macroSpace = get("macro.space");
    nativeRaw.phaserSyncDivision = get("fx.phaser_sync_division");
    nativeRaw.chorusSyncDivision = get("fx.chorus_sync_division");
    nativeRaw.warmDrive = get("filter_control.warm_drive");
    nativeRaw.portamentoMode = get("voice.portamento_mode");
    nativeRaw.sync = get("global.sync");
    nativeRaw.pitchBendRange = get("voice.pitch_bend_range");
    nativeRaw.masterLevel = get("master.level_db");
    nativeRaw.filterControlCutoffSemitones = get("filter_control.cutoff_semitones");
    nativeRaw.filterControlResonance = get("filter_control.resonance");
    nativeRaw.filterControlKeytrack = get("filter_control.keytrack");
    nativeRaw.filterControlDrive = get("filter_control.drive");
    nativeRaw.layer0FilterEnabled = get("layer.1.filter.enabled");
    nativeRaw.layer0FilterMode = get("layer.1.filter.mode");
    nativeRaw.layer0FilterCutoffsemitones = get("layer.1.filter.cutoff_semitones");
    nativeRaw.layer0FilterResonance = get("layer.1.filter.resonance");
    nativeRaw.layer0FilterDrive = get("layer.1.filter.drive");
    nativeRaw.layer0FilterKeytrack = get("layer.1.filter.keytrack");
    nativeRaw.layer0FilterOversampling = get("layer.1.filter.oversampling");
    nativeRaw.layer0FilterInput = get("layer.1.filter.input");
    nativeRaw.layer0AmpAttackms = get("layer.1.amp_env.attack_ms");
    nativeRaw.layer0AmpDecayms = get("layer.1.amp_env.decay_ms");
    nativeRaw.layer0AmpSustain = get("layer.1.amp_env.sustain");
    nativeRaw.layer0AmpReleasems = get("layer.1.amp_env.release_ms");
    nativeRaw.layer1FilterEnabled = get("layer.2.filter.enabled");
    nativeRaw.layer1FilterMode = get("layer.2.filter.mode");
    nativeRaw.layer1FilterCutoffsemitones = get("layer.2.filter.cutoff_semitones");
    nativeRaw.layer1FilterResonance = get("layer.2.filter.resonance");
    nativeRaw.layer1FilterDrive = get("layer.2.filter.drive");
    nativeRaw.layer1FilterKeytrack = get("layer.2.filter.keytrack");
    nativeRaw.layer1FilterOversampling = get("layer.2.filter.oversampling");
    nativeRaw.layer1FilterInput = get("layer.2.filter.input");
    nativeRaw.layer1AmpAttackms = get("layer.2.amp_env.attack_ms");
    nativeRaw.layer1AmpDecayms = get("layer.2.amp_env.decay_ms");
    nativeRaw.layer1AmpSustain = get("layer.2.amp_env.sustain");
    nativeRaw.layer1AmpReleasems = get("layer.2.amp_env.release_ms");
    nativeRaw.modEnv2Attackms = get("mod_env.2.attack_ms");
    nativeRaw.modEnv2Decayms = get("mod_env.2.decay_ms");
    nativeRaw.modEnv2Sustain = get("mod_env.2.sustain");
    nativeRaw.modEnv2Releasems = get("mod_env.2.release_ms");
    nativeRaw.lfo0Gain = get("lfo.gain");
    nativeRaw.lfo0Offset = get("lfo.offset");
    nativeRaw.lfo0Free = get("lfo.free");
    nativeRaw.lfo1Shape = get("lfo.2.shape");
    nativeRaw.lfo1Ratehz = get("lfo.2.rate_hz");
    nativeRaw.lfo1Syncdivision = get("lfo.2.sync_division");
    nativeRaw.lfo1Phasedegrees = get("lfo.2.phase_degrees");
    nativeRaw.lfo1Gatemode = get("lfo.2.gate_mode");
    nativeRaw.lfo1Mono = get("lfo.2.mono");
    nativeRaw.lfo1Stepcount = get("lfo.2.step_count");
    nativeRaw.lfo1Stepsmooth = get("lfo.2.step_smooth");
    nativeRaw.lfo1Gain = get("lfo.2.gain");
    nativeRaw.lfo1Offset = get("lfo.2.offset");
    nativeRaw.lfo1Free = get("lfo.2.free");
    nativeRaw.fxPhasercenterhz = get("fx.phaser_center_hz");
    nativeRaw.fxPhaserspread = get("fx.phaser_spread");
    nativeRaw.fxPhaserlroffset = get("fx.phaser_lr_offset");
    nativeRaw.fxPhaserwidth = get("fx.phaser_width");
    nativeRaw.fxChorusdelayms = get("fx.chorus_delay_ms");
    nativeRaw.fxChorusfeedback = get("fx.chorus_feedback");
    nativeRaw.fxChorusdualmode = get("fx.chorus_dual_mode");
    nativeRaw.fxChoruswidth = get("fx.chorus_width");
    nativeRaw.fxEqlowfrequencyhz = get("fx.eq_low_frequency_hz");
    nativeRaw.fxEqhighfrequencyhz = get("fx.eq_high_frequency_hz");
    nativeRaw.fxDelaytimeleftms = get("fx.delay_time_left_ms");
    nativeRaw.fxDelaytimerightms = get("fx.delay_time_right_ms");
    nativeRaw.fxDelayrightsyncdivision = get("fx.delay_right_sync_division");
    nativeRaw.fxDelaypingpong = get("fx.delay_ping_pong");
    nativeRaw.fxDelayspread = get("fx.delay_spread");
    nativeRaw.fxDelaywidth = get("fx.delay_width");
    nativeRaw.fxDelaylowcuthz = get("fx.delay_low_cut_hz");
    nativeRaw.fxDelayhighcuthz = get("fx.delay_high_cut_hz");
    nativeRaw.fxDelaysmear = get("fx.delay_smear");
    nativeRaw.fxReverbpredelayms = get("fx.reverb_pre_delay_ms");
    nativeRaw.fxReverbdamp = get("fx.reverb_damp");
    nativeRaw.fxReverbwidth = get("fx.reverb_width");
    nativeRaw.fxCompressorattackms = get("fx.compressor_attack_ms");
    nativeRaw.fxCompressorreleasems = get("fx.compressor_release_ms");
    nativeRaw.arpVelocitymode = get("arp.velocity_mode");
    nativeRaw.arpWrap = get("arp.wrap");
    nativeRaw.arpTimems = get("arp.time_ms");
    for (int step = 0; step < synth::lfoStepSlotCount; ++step)
        nativeRaw.lfo2Steps[static_cast<std::size_t>(step)] = get(("lfo.2.step." + std::to_string(step + 1)).c_str());
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot + 1) + ".";
        for (const auto& destination : synth::modulationDestinationCatalog())
            if (destination.nativeIndex >= 0)
                nativeRaw.depths[static_cast<std::size_t>(slot)][static_cast<std::size_t>(destination.nativeIndex)] = get((prefix + destination.depthSuffix).c_str());
        for (int route = 0; route < 2; ++route)
        {
            const auto routePrefix = prefix + "route." + std::to_string(route + 1) + ".";
            nativeRaw.destinations[static_cast<std::size_t>(slot)][static_cast<std::size_t>(route)] = get((routePrefix + "destination").c_str());
            nativeRaw.amounts[static_cast<std::size_t>(slot)][static_cast<std::size_t>(route)] = get((routePrefix + "amount").c_str());
        }
    }
}

synth::SynthParameters SynthAudioProcessor::readParameters(float tempoBpm, bool offlineRender) const noexcept
{
    juce::ignoreUnused(offlineRender);
    auto value = [](const std::atomic<float>* parameter, float fallback) noexcept {
        const auto loaded = parameter != nullptr ? parameter->load(std::memory_order_relaxed) : fallback;
        return std::isfinite(loaded) ? loaded : fallback;
    };

    const auto& defaults = parameterDefaults;
    auto snapshot = parameterDefaults;
    snapshot.voiceMode = static_cast<synth::VoiceMode>(static_cast<int>(std::round(value(raw.voiceMode, 2.0f))));
    snapshot.polyphony = static_cast<int>(std::round(value(raw.voicePolyphony, 8.0f)));
    snapshot.unisonCount = static_cast<int>(std::round(value(raw.voiceUnisonCount, 1.0f)));
    snapshot.glideMs = value(raw.voiceGlideMs, 0.0f);
    snapshot.velocityGlideMs = value(raw.voiceVelocityGlideMs, 0.0f);
    for (int layer = 0; layer < synth::layerCount; ++layer)
    {
        const auto& rawLayer = raw.layers[static_cast<std::size_t>(layer)];
        const auto& defaultLayer = defaults.layers[static_cast<std::size_t>(layer)];
        auto& layerSnapshot = snapshot.layers[static_cast<std::size_t>(layer)];
        layerSnapshot.levelDb = value(rawLayer.levelDb, defaultLayer.levelDb);
        layerSnapshot.pan = value(rawLayer.pan, defaultLayer.pan);
        layerSnapshot.solo = value(rawLayer.solo, defaultLayer.solo ? 1.0f : 0.0f) >= 0.5f;
        layerSnapshot.mute = value(rawLayer.mute, defaultLayer.mute ? 1.0f : 0.0f) >= 0.5f;

        for (int oscillator = 0; oscillator < synth::oscillatorSlotsPerLayer; ++oscillator)
        {
            const auto& rawOscillator = rawLayer.oscillators[static_cast<std::size_t>(oscillator)];
            const auto& defaultOscillator = defaultLayer.oscillators[static_cast<std::size_t>(oscillator)];
            auto& oscillatorSnapshot = layerSnapshot.oscillators[static_cast<std::size_t>(oscillator)];
            oscillatorSnapshot.voices = static_cast<int>(std::round(value(rawOscillator.voices,
                                                                          static_cast<float>(defaultOscillator.voices))));
            oscillatorSnapshot.waveform = static_cast<synth::OscillatorSlotWaveform>(
                static_cast<int>(std::round(value(rawOscillator.waveform,
                                                  static_cast<float>(static_cast<int>(
                                                      defaultOscillator.waveform))))));
            oscillatorSnapshot.octave = static_cast<int>(std::round(value(rawOscillator.octave,
                                                                          static_cast<float>(defaultOscillator.octave))));
            oscillatorSnapshot.note = static_cast<int>(std::round(value(rawOscillator.note,
                                                                        static_cast<float>(defaultOscillator.note))));
            oscillatorSnapshot.fineCents = value(rawOscillator.fineCents, defaultOscillator.fineCents);
            oscillatorSnapshot.level = value(rawOscillator.level, defaultOscillator.level);
            oscillatorSnapshot.phaseDegrees = value(rawOscillator.phaseDegrees, defaultOscillator.phaseDegrees);
            oscillatorSnapshot.detune = value(rawOscillator.detune, defaultOscillator.detune);
            oscillatorSnapshot.stereo = value(rawOscillator.stereo, defaultOscillator.stereo);
            oscillatorSnapshot.pan = value(rawOscillator.pan, defaultOscillator.pan);
            oscillatorSnapshot.retrigger = value(rawOscillator.retrigger,
                                                 defaultOscillator.retrigger ? 1.0f : 0.0f)
                >= 0.5f;
            oscillatorSnapshot.invert = value(rawOscillator.invert,
                                              defaultOscillator.invert ? 1.0f : 0.0f)
                >= 0.5f;
        }
    }
    snapshot.modEnv = { value(raw.modAttack, 1.0f), value(raw.modDecay, 400.0f),
                        value(raw.modSustain, 0.0f), value(raw.modRelease, 160.0f) };
    snapshot.lfo.shape = static_cast<synth::LfoShapeChoice>(static_cast<int>(std::round(value(raw.lfoShape, 3.0f))));
    snapshot.lfo.rateHz = value(raw.lfoRateHz, 2.0f);
    snapshot.lfo.syncDivision = static_cast<int>(std::round(value(raw.lfoSyncDivision, 3.0f)));
    snapshot.lfo.phaseDegrees = value(raw.lfoPhaseDegrees, 0.0f);
    snapshot.lfo.gateMode = static_cast<synth::LfoGateMode>(static_cast<int>(std::round(value(raw.lfoGateMode, 1.0f))));
    snapshot.lfo.mono = value(raw.lfoMono, 0.0f) >= 0.5f;
    snapshot.lfo.stepCount = std::clamp(static_cast<int>(std::round(value(raw.lfoStepCount, 8.0f))), 2,
                                        synth::lfoStepSlotCount);
    snapshot.lfo.stepSmooth = value(raw.lfoStepSmooth, 0.0f);
    for (int step = 0; step < synth::lfoStepSlotCount; ++step)
        snapshot.lfo.steps[static_cast<std::size_t>(step)] =
            value(raw.lfoSteps[static_cast<std::size_t>(step)], synth::defaultLfoStepValue(step));
    snapshot.ramp.enabled = value(raw.rampEnabled, 0.0f) >= 0.5f;
    snapshot.ramp.mode = static_cast<synth::RampMode>(static_cast<int>(std::round(value(raw.rampMode, 0.0f))));
    snapshot.ramp.delayMs = value(raw.rampDelayMs, 0.0f);
    snapshot.ramp.riseMs = value(raw.rampRiseMs, 1000.0f);
    snapshot.ramp.curve = static_cast<synth::RampCurve>(static_cast<int>(std::round(value(raw.rampCurve, 0.0f))));
    snapshot.arp.enabled = value(raw.arpEnabled, 0.0f) >= 0.5f;
    snapshot.arp.mode = static_cast<synth::ArpMode>(static_cast<int>(std::round(value(raw.arpMode, 0.0f))));
    snapshot.arp.rate = static_cast<synth::ArpRateDivision>(static_cast<int>(std::round(value(raw.arpRate, 1.0f))));
    snapshot.arp.gate = value(raw.arpGate, defaults.arp.gate);
    snapshot.arp.octaves = static_cast<int>(std::round(value(raw.arpOctaves, 1.0f)));
    snapshot.arp.hold = value(raw.arpHold, 0.0f) >= 0.5f;
    snapshot.arp.swing = value(raw.arpSwing, 0.0f);
    snapshot.arp.stepCount = static_cast<int>(std::round(value(raw.arpStepCount,
                                                               static_cast<float>(synth::arpStepCount))));
    for (int step = 0; step < synth::arpStepCount; ++step)
    {
        const auto& rawStep = raw.arpSteps[static_cast<std::size_t>(step)];
        const auto& defaultStep = defaults.arp.steps[static_cast<std::size_t>(step)];
        auto& stepSnapshot = snapshot.arp.steps[static_cast<std::size_t>(step)];
        stepSnapshot.enabled = value(rawStep.enabled, defaultStep.enabled ? 1.0f : 0.0f) >= 0.5f;
        stepSnapshot.pitchSemitones = static_cast<int>(std::round(value(rawStep.pitchSemitones,
                                                                        static_cast<float>(
                                                                            defaultStep.pitchSemitones))));
        stepSnapshot.velocity = value(rawStep.velocity, defaultStep.velocity);
        stepSnapshot.gate = value(rawStep.gate, defaultStep.gate);
        stepSnapshot.tie = value(rawStep.tie, defaultStep.tie ? 1.0f : 0.0f) >= 0.5f;
    }
    snapshot.chord.enabled = value(raw.chordEnabled, 0.0f) >= 0.5f;
    snapshot.chord.voiceCount = static_cast<int>(std::round(value(raw.chordVoiceCount, 1.0f)));
    for (int voice = 0; voice < synth::chordVoiceCount; ++voice)
    {
        const auto& rawVoice = raw.chordVoices[static_cast<std::size_t>(voice)];
        const auto& defaultVoice = defaults.chord.voices[static_cast<std::size_t>(voice)];
        auto& voiceSnapshot = snapshot.chord.voices[static_cast<std::size_t>(voice)];
        voiceSnapshot.enabled = value(rawVoice.enabled, defaultVoice.enabled ? 1.0f : 0.0f) >= 0.5f;
        voiceSnapshot.pitchSemitones = static_cast<int>(std::round(value(rawVoice.pitchSemitones,
                                                                         static_cast<float>(
                                                                             defaultVoice.pitchSemitones))));
        voiceSnapshot.velocity = value(rawVoice.velocity, defaultVoice.velocity);
    }
    snapshot.direct.filterKeytrack = value(raw.directFilterKeytrack, 0.0f);
    snapshot.direct.filterLfoSemitones = value(raw.directFilterLfoSemitones, 0.0f);
    snapshot.direct.filterModEnvSemitones = value(raw.directFilterModEnvSemitones, 0.0f);
    snapshot.direct.oscKeytrackSemitones = value(raw.directOscKeytrackSemitones, 0.0f);
    snapshot.direct.oscLfoSemitones = value(raw.directOscLfoSemitones, 0.0f);
    snapshot.direct.oscModEnvSemitones = value(raw.directOscModEnvSemitones, 0.0f);
    snapshot.fx.enabled = true;
    snapshot.fx.saturationEnabled = value(raw.fxSaturationEnabled, 1.0f) >= 0.5f;
    snapshot.fx.distortionMode = static_cast<synth::DistortionMode>(static_cast<int>(std::round(value(raw.fxDistortionMode, 0.0f))));
    snapshot.fx.saturationMix = value(raw.fxSaturationMix, 0.0f);
    snapshot.fx.saturationDrive = value(raw.fxSaturationDrive, 0.35f);
    snapshot.fx.phaserEnabled = value(raw.fxPhaserEnabled, 0.0f) >= 0.5f;
    snapshot.fx.phaserMix = value(raw.fxPhaserMix, 0.0f);
    snapshot.fx.phaserRateHz = value(raw.fxPhaserRateHz, 0.25f);
    snapshot.fx.phaserDepth = value(raw.fxPhaserDepth, 0.45f);
    snapshot.fx.phaserFeedback = value(raw.fxPhaserFeedback, 0.15f);
    snapshot.fx.delayEnabled = value(raw.fxDelayEnabled, 1.0f) >= 0.5f;
    snapshot.fx.delayMix = value(raw.fxDelayMix, 0.0f);
    snapshot.fx.delaySyncDivision = static_cast<synth::DelaySyncDivision>(static_cast<int>(std::round(value(raw.fxDelaySyncDivision, 1.0f))));
    snapshot.fx.delayFeedback = value(raw.fxDelayFeedback, 0.22f);
    snapshot.fx.reverbEnabled = value(raw.fxReverbEnabled, 1.0f) >= 0.5f;
    snapshot.fx.reverbMix = value(raw.fxReverbMix, 0.0f);
    snapshot.fx.reverbDecay = value(raw.fxReverbDecay, 0.35f);
    snapshot.fx.chorusEnabled = value(raw.fxChorusEnabled, 0.0f) >= 0.5f;
    snapshot.fx.chorusMix = value(raw.fxChorusMix, 0.0f);
    snapshot.fx.chorusRateHz = value(raw.fxChorusRateHz, 0.35f);
    snapshot.fx.chorusDepthMs = value(raw.fxChorusDepthMs, 5.0f);
    snapshot.fx.eqEnabled = value(raw.fxEqEnabled, 0.0f) >= 0.5f;
    snapshot.fx.eqLowGainDb = value(raw.fxEqLowGainDb, 0.0f);
    snapshot.fx.eqHighGainDb = value(raw.fxEqHighGainDb, 0.0f);
    snapshot.fx.compressorEnabled = value(raw.fxCompressorEnabled, 0.0f) >= 0.5f;
    snapshot.fx.compressorThresholdDb = value(raw.fxCompressorThresholdDb, -18.0f);
    snapshot.fx.compressorRatio = value(raw.fxCompressorRatio, 2.0f);
    snapshot.fx.compressorMakeupDb = value(raw.fxCompressorMakeupDb, 0.0f);
    snapshot.fx.compressorMix = value(raw.fxCompressorMix, 0.0f);
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        const auto& rawSlot = raw.transMod[static_cast<std::size_t>(slot)];
        auto& slotSnapshot = snapshot.transMod.slots[static_cast<std::size_t>(slot)];
        slotSnapshot.enabled = value(rawSlot.enabled, 0.0f) >= 0.5f;
        slotSnapshot.source = static_cast<synth::ModSource>(static_cast<int>(std::round(value(rawSlot.source, 0.0f))));
        slotSnapshot.scaler = static_cast<synth::ModSource>(static_cast<int>(std::round(value(rawSlot.scaler, 0.0f))));
        slotSnapshot.depth = value(rawSlot.depth, 0.0f);
        slotSnapshot.oscPitchSemitones = value(rawSlot.oscPitchSemitones, 0.0f);
        slotSnapshot.filterCutoffSemitones = value(rawSlot.filterCutoffSemitones, 0.0f);
        slotSnapshot.ampLevelDb = value(rawSlot.ampLevelDb, 0.0f);
        slotSnapshot.pan = value(rawSlot.pan, 0.0f);
    }
    snapshot.macro.motion = value(raw.macroMotion, 0.5f);
    snapshot.macro.width = value(raw.macroWidth, 0.0f);
    snapshot.macro.drive = value(raw.macroDrive, 0.0f);
    snapshot.macro.space = value(raw.macroSpace, 0.0f);
    snapshot.fx.phaserSyncDivision = static_cast<synth::DelaySyncDivision>(static_cast<int>(value(nativeRaw.phaserSyncDivision, 15.0f)));
    snapshot.fx.chorusSyncDivision = static_cast<synth::DelaySyncDivision>(static_cast<int>(value(nativeRaw.chorusSyncDivision, 15.0f)));
    snapshot.filterControl.warmDrive = value(nativeRaw.warmDrive, 1.0f) >= 0.5f;
    snapshot.portamentoMode = static_cast<synth::PortamentoMode>(static_cast<int>(value(nativeRaw.portamentoMode, 0.0f)));
    snapshot.sync = value(nativeRaw.sync, 0.0f) >= 0.5f;
    snapshot.arp.sync = snapshot.sync;
    snapshot.pitchBendRange = value(nativeRaw.pitchBendRange, static_cast<float>(defaults.pitchBendRange));
    snapshot.master.levelDb = value(nativeRaw.masterLevel, static_cast<float>(defaults.master.levelDb));
    snapshot.filterControl.cutoffSemitones = value(nativeRaw.filterControlCutoffSemitones, static_cast<float>(defaults.filterControl.cutoffSemitones));
    snapshot.filterControl.resonance = value(nativeRaw.filterControlResonance, static_cast<float>(defaults.filterControl.resonance));
    snapshot.filterControl.keytrack = value(nativeRaw.filterControlKeytrack, static_cast<float>(defaults.filterControl.keytrack));
    snapshot.filterControl.drive = value(nativeRaw.filterControlDrive, static_cast<float>(defaults.filterControl.drive));
    snapshot.layers[0].filter.enabled = value(nativeRaw.layer0FilterEnabled, static_cast<float>(defaults.layers[0].filter.enabled)) >= 0.5f;
    snapshot.layers[0].filter.mode = static_cast<synth::FilterMode>(static_cast<int>(std::round(value(nativeRaw.layer0FilterMode, static_cast<float>(defaults.layers[0].filter.mode)))));
    snapshot.layers[0].filter.cutoffSemitones = value(nativeRaw.layer0FilterCutoffsemitones, static_cast<float>(defaults.layers[0].filter.cutoffSemitones));
    snapshot.layers[0].filter.resonance = value(nativeRaw.layer0FilterResonance, static_cast<float>(defaults.layers[0].filter.resonance));
    snapshot.layers[0].filter.drive = value(nativeRaw.layer0FilterDrive, static_cast<float>(defaults.layers[0].filter.drive));
    snapshot.layers[0].filter.keytrack = value(nativeRaw.layer0FilterKeytrack, static_cast<float>(defaults.layers[0].filter.keytrack));
    snapshot.layers[0].filter.oversampling = static_cast<int>(static_cast<int>(std::round(value(nativeRaw.layer0FilterOversampling, static_cast<float>(defaults.layers[0].filter.oversampling)))));
    snapshot.layers[0].input = static_cast<synth::FilterInput>(static_cast<int>(std::round(value(nativeRaw.layer0FilterInput, static_cast<float>(defaults.layers[0].input)))));
    snapshot.layers[0].ampEnv.attackMs = value(nativeRaw.layer0AmpAttackms, static_cast<float>(defaults.layers[0].ampEnv.attackMs));
    snapshot.layers[0].ampEnv.decayMs = value(nativeRaw.layer0AmpDecayms, static_cast<float>(defaults.layers[0].ampEnv.decayMs));
    snapshot.layers[0].ampEnv.sustain = value(nativeRaw.layer0AmpSustain, static_cast<float>(defaults.layers[0].ampEnv.sustain));
    snapshot.layers[0].ampEnv.releaseMs = value(nativeRaw.layer0AmpReleasems, static_cast<float>(defaults.layers[0].ampEnv.releaseMs));
    snapshot.layers[1].filter.enabled = value(nativeRaw.layer1FilterEnabled, static_cast<float>(defaults.layers[1].filter.enabled)) >= 0.5f;
    snapshot.layers[1].filter.mode = static_cast<synth::FilterMode>(static_cast<int>(std::round(value(nativeRaw.layer1FilterMode, static_cast<float>(defaults.layers[1].filter.mode)))));
    snapshot.layers[1].filter.cutoffSemitones = value(nativeRaw.layer1FilterCutoffsemitones, static_cast<float>(defaults.layers[1].filter.cutoffSemitones));
    snapshot.layers[1].filter.resonance = value(nativeRaw.layer1FilterResonance, static_cast<float>(defaults.layers[1].filter.resonance));
    snapshot.layers[1].filter.drive = value(nativeRaw.layer1FilterDrive, static_cast<float>(defaults.layers[1].filter.drive));
    snapshot.layers[1].filter.keytrack = value(nativeRaw.layer1FilterKeytrack, static_cast<float>(defaults.layers[1].filter.keytrack));
    snapshot.layers[1].filter.oversampling = static_cast<int>(static_cast<int>(std::round(value(nativeRaw.layer1FilterOversampling, static_cast<float>(defaults.layers[1].filter.oversampling)))));
    snapshot.layers[1].input = static_cast<synth::FilterInput>(static_cast<int>(std::round(value(nativeRaw.layer1FilterInput, static_cast<float>(defaults.layers[1].input)))));
    snapshot.layers[1].ampEnv.attackMs = value(nativeRaw.layer1AmpAttackms, static_cast<float>(defaults.layers[1].ampEnv.attackMs));
    snapshot.layers[1].ampEnv.decayMs = value(nativeRaw.layer1AmpDecayms, static_cast<float>(defaults.layers[1].ampEnv.decayMs));
    snapshot.layers[1].ampEnv.sustain = value(nativeRaw.layer1AmpSustain, static_cast<float>(defaults.layers[1].ampEnv.sustain));
    snapshot.layers[1].ampEnv.releaseMs = value(nativeRaw.layer1AmpReleasems, static_cast<float>(defaults.layers[1].ampEnv.releaseMs));
    snapshot.modEnv2.attackMs = value(nativeRaw.modEnv2Attackms, static_cast<float>(defaults.modEnv2.attackMs));
    snapshot.modEnv2.decayMs = value(nativeRaw.modEnv2Decayms, static_cast<float>(defaults.modEnv2.decayMs));
    snapshot.modEnv2.sustain = value(nativeRaw.modEnv2Sustain, static_cast<float>(defaults.modEnv2.sustain));
    snapshot.modEnv2.releaseMs = value(nativeRaw.modEnv2Releasems, static_cast<float>(defaults.modEnv2.releaseMs));
    snapshot.lfo.gain = value(nativeRaw.lfo0Gain, static_cast<float>(defaults.lfo.gain));
    snapshot.lfo.offset = value(nativeRaw.lfo0Offset, static_cast<float>(defaults.lfo.offset));
    snapshot.lfo.free = value(nativeRaw.lfo0Free, static_cast<float>(defaults.lfo.free)) >= 0.5f;
    snapshot.lfo2.shape = static_cast<synth::LfoShapeChoice>(static_cast<int>(std::round(value(nativeRaw.lfo1Shape, static_cast<float>(defaults.lfo2.shape)))));
    snapshot.lfo2.rateHz = value(nativeRaw.lfo1Ratehz, static_cast<float>(defaults.lfo2.rateHz));
    snapshot.lfo2.syncDivision = static_cast<int>(static_cast<int>(std::round(value(nativeRaw.lfo1Syncdivision, static_cast<float>(defaults.lfo2.syncDivision)))));
    snapshot.lfo2.phaseDegrees = value(nativeRaw.lfo1Phasedegrees, static_cast<float>(defaults.lfo2.phaseDegrees));
    snapshot.lfo2.gateMode = static_cast<synth::LfoGateMode>(static_cast<int>(std::round(value(nativeRaw.lfo1Gatemode, static_cast<float>(defaults.lfo2.gateMode)))));
    snapshot.lfo2.mono = value(nativeRaw.lfo1Mono, static_cast<float>(defaults.lfo2.mono)) >= 0.5f;
    snapshot.lfo2.stepCount = static_cast<int>(static_cast<int>(std::round(value(nativeRaw.lfo1Stepcount, static_cast<float>(defaults.lfo2.stepCount)))));
    snapshot.lfo2.stepSmooth = value(nativeRaw.lfo1Stepsmooth, static_cast<float>(defaults.lfo2.stepSmooth));
    snapshot.lfo2.gain = value(nativeRaw.lfo1Gain, static_cast<float>(defaults.lfo2.gain));
    snapshot.lfo2.offset = value(nativeRaw.lfo1Offset, static_cast<float>(defaults.lfo2.offset));
    snapshot.lfo2.free = value(nativeRaw.lfo1Free, static_cast<float>(defaults.lfo2.free)) >= 0.5f;
    snapshot.fx.phaserCenterHz = value(nativeRaw.fxPhasercenterhz, static_cast<float>(defaults.fx.phaserCenterHz));
    snapshot.fx.phaserSpread = value(nativeRaw.fxPhaserspread, static_cast<float>(defaults.fx.phaserSpread));
    snapshot.fx.phaserLrOffset = value(nativeRaw.fxPhaserlroffset, static_cast<float>(defaults.fx.phaserLrOffset));
    snapshot.fx.phaserWidth = value(nativeRaw.fxPhaserwidth, static_cast<float>(defaults.fx.phaserWidth));
    snapshot.fx.chorusDelayMs = value(nativeRaw.fxChorusdelayms, static_cast<float>(defaults.fx.chorusDelayMs));
    snapshot.fx.chorusFeedback = value(nativeRaw.fxChorusfeedback, static_cast<float>(defaults.fx.chorusFeedback));
    snapshot.fx.chorusDualMode = value(nativeRaw.fxChorusdualmode, static_cast<float>(defaults.fx.chorusDualMode)) >= 0.5f;
    snapshot.fx.chorusWidth = value(nativeRaw.fxChoruswidth, static_cast<float>(defaults.fx.chorusWidth));
    snapshot.fx.eqLowFrequencyHz = value(nativeRaw.fxEqlowfrequencyhz, static_cast<float>(defaults.fx.eqLowFrequencyHz));
    snapshot.fx.eqHighFrequencyHz = value(nativeRaw.fxEqhighfrequencyhz, static_cast<float>(defaults.fx.eqHighFrequencyHz));
    snapshot.fx.delaySync = true;
    snapshot.fx.delayTimeLeftMs = value(nativeRaw.fxDelaytimeleftms, static_cast<float>(defaults.fx.delayTimeLeftMs));
    snapshot.fx.delayTimeRightMs = value(nativeRaw.fxDelaytimerightms, static_cast<float>(defaults.fx.delayTimeRightMs));
    snapshot.fx.delayRightSyncDivision = static_cast<synth::DelaySyncDivision>(static_cast<int>(std::round(value(nativeRaw.fxDelayrightsyncdivision, static_cast<float>(defaults.fx.delayRightSyncDivision)))));
    snapshot.fx.delayPingPong = value(nativeRaw.fxDelaypingpong, static_cast<float>(defaults.fx.delayPingPong)) >= 0.5f;
    snapshot.fx.delaySpread = value(nativeRaw.fxDelayspread, static_cast<float>(defaults.fx.delaySpread));
    snapshot.fx.delayWidth = value(nativeRaw.fxDelaywidth, static_cast<float>(defaults.fx.delayWidth));
    snapshot.fx.delayLowCutHz = value(nativeRaw.fxDelaylowcuthz, static_cast<float>(defaults.fx.delayLowCutHz));
    snapshot.fx.delayHighCutHz = value(nativeRaw.fxDelayhighcuthz, static_cast<float>(defaults.fx.delayHighCutHz));
    snapshot.fx.delaySmear = value(nativeRaw.fxDelaysmear, static_cast<float>(defaults.fx.delaySmear));
    snapshot.fx.reverbPreDelayMs = value(nativeRaw.fxReverbpredelayms, static_cast<float>(defaults.fx.reverbPreDelayMs));
    snapshot.fx.reverbDamp = value(nativeRaw.fxReverbdamp, static_cast<float>(defaults.fx.reverbDamp));
    snapshot.fx.reverbWidth = value(nativeRaw.fxReverbwidth, static_cast<float>(defaults.fx.reverbWidth));
    snapshot.fx.compressorAttackMs = value(nativeRaw.fxCompressorattackms, static_cast<float>(defaults.fx.compressorAttackMs));
    snapshot.fx.compressorReleaseMs = value(nativeRaw.fxCompressorreleasems, static_cast<float>(defaults.fx.compressorReleaseMs));
    snapshot.arp.velocityMode = static_cast<synth::ArpVelocityMode>(static_cast<int>(std::round(value(nativeRaw.arpVelocitymode, static_cast<float>(defaults.arp.velocityMode)))));
    snapshot.arp.wrap = static_cast<int>(static_cast<int>(std::round(value(nativeRaw.arpWrap, static_cast<float>(defaults.arp.wrap)))));
    snapshot.arp.timeMs = value(nativeRaw.arpTimems, static_cast<float>(defaults.arp.timeMs));
    for (int step = 0; step < synth::lfoStepSlotCount; ++step)
        snapshot.lfo2.steps[static_cast<std::size_t>(step)] = value(nativeRaw.lfo2Steps[static_cast<std::size_t>(step)], synth::defaultLfoStepValue(step));
    const auto& destinations = synth::modulationDestinationCatalog();
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        auto& slotSnapshot = snapshot.transMod.slots[static_cast<std::size_t>(slot)];
        for (int destination = 0; destination < synth::nativeModDestinationCount; ++destination)
            slotSnapshot.nativeDepths[static_cast<std::size_t>(destination)] = value(nativeRaw.depths[static_cast<std::size_t>(slot)][static_cast<std::size_t>(destination)], 0.0f);
        for (int route = 0; route < 2; ++route)
        {
            const auto selection = static_cast<int>(value(nativeRaw.destinations[static_cast<std::size_t>(slot)][static_cast<std::size_t>(route)], 0.0f)) - 1;
            if (selection < 0 || selection >= static_cast<int>(destinations.size()))
                continue;
            slotSnapshot.enabled = true;
            if (slot < 4)
            {
                constexpr std::array<synth::ModSource, 4> sources { synth::ModSource::ModEnv, synth::ModSource::ModEnv2, synth::ModSource::Lfo, synth::ModSource::Lfo2 };
                slotSnapshot.source = sources[static_cast<std::size_t>(slot)];
            }
            const auto& destination = destinations[static_cast<std::size_t>(selection)];
            const auto scale = destination.destination == synth::ModulationDestination::FilterCutoff ? 72.0f : destination.maximumDepth;
            const auto amount = value(nativeRaw.amounts[static_cast<std::size_t>(slot)][static_cast<std::size_t>(route)], 0.0f) * scale;
            switch (destination.destination)
            {
                case synth::ModulationDestination::OscPitch: slotSnapshot.oscPitchSemitones += amount; break;
                case synth::ModulationDestination::PulseWidth: slotSnapshot.pulseWidth += amount; break;
                case synth::ModulationDestination::FilterCutoff: slotSnapshot.filterCutoffSemitones += amount; break;
                case synth::ModulationDestination::AmpLevel: slotSnapshot.ampLevelDb += amount; break;
                case synth::ModulationDestination::Pan: slotSnapshot.pan += amount; break;
                case synth::ModulationDestination::Native: slotSnapshot.nativeDepths[static_cast<std::size_t>(destination.nativeIndex)] += amount; break;
            }
        }
    }
    snapshot.tempoBpm = tempoBpm;
    return snapshot;
}

float SynthAudioProcessor::currentTempoBpm() const noexcept
{
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                return static_cast<float>(*bpm);
        }
    }

    return 128.0f;
}

int SynthAudioProcessor::tailDrainSamplesFor(const synth::SynthParameters& snapshot) const noexcept
{
    const auto tailSeconds = std::max(longestVoiceReleaseMs(snapshot) * 0.001f,
                                      synth::fxTailLengthSeconds(snapshot));
    const auto sampleRate = diagnosticSampleRate.load(std::memory_order_relaxed);
    const auto safeSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    return std::max(0, static_cast<int>(std::ceil(static_cast<double>(tailSeconds) * safeSampleRate)));
}

juce::AudioProcessorEditor* SynthAudioProcessor::createEditor()
{
    return new SynthAudioProcessorEditor(*this);
}

std::vector<SynthAudioProcessor::PresetListItem> SynthAudioProcessor::getPresetList() const
{
    std::vector<PresetListItem> items;
    const auto favoriteKeys = synth::readFavoritePresetKeys(synth::defaultPresetFavoritesFile());

    auto append = [&items](const std::vector<synth::PresetSummary>& presets) {
        for (const auto& preset : presets)
            items.push_back(makePresetListItem(preset));
    };

    auto appendInvalid = [&items](const std::filesystem::path& directory, synth::PresetSource source) {
        for (const auto& validation : synth::validatePresetDirectory(directory))
        {
            if (validation.passed())
                continue;

            items.push_back(makeInvalidPresetListItem(validation, source));
        }
    };

    append(synth::scanPresetDirectory(synth::factoryPresetDirectory(), synth::PresetSource::Factory, favoriteKeys));
    appendInvalid(synth::factoryPresetDirectory(), synth::PresetSource::Factory);

    const auto userPresetDirectory = synth::defaultUserPresetDirectory();
    append(synth::scanPresetDirectory(userPresetDirectory, synth::PresetSource::User, favoriteKeys));
    if (std::filesystem::exists(userPresetDirectory))
        appendInvalid(userPresetDirectory, synth::PresetSource::User);
    return items;
}

std::optional<SynthAudioProcessor::PresetListItem> SynthAudioProcessor::getPresetListItemForFile(const juce::File& file) const
{
    if (file == juce::File())
        return std::nullopt;

    const auto target = withPresetFileExtension(file);
    for (const auto& item : getPresetList())
    {
        if (item.file.getFullPathName() == target.getFullPathName())
            return item;
    }

    const auto favoriteKeys = synth::readFavoritePresetKeys(synth::defaultPresetFavoritesFile());
    const auto parentPath = std::filesystem::path(target.getParentDirectory().getFullPathName().toStdString());
    const auto summaries = synth::scanPresetDirectory(parentPath, synth::PresetSource::User, favoriteKeys);
    for (const auto& preset : summaries)
    {
        const auto presetFile = juceFileForPath(preset.path);
        if (presetFile.getFullPathName() == target.getFullPathName())
            return makePresetListItem(preset);
    }

    return std::nullopt;
}

juce::File SynthAudioProcessor::getUserPresetDirectory() const
{
    return juceFileForPath(synth::defaultUserPresetDirectory());
}

bool SynthAudioProcessor::loadPresetFile(const juce::File& file, juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    const auto result = synth::preparePresetState(parameters, file.getFullPathName().toStdString());
    if (!result.loaded)
    {
        message = juce::String(result.message);
        setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
        return false;
    }
    const auto name = result.displayName.empty() ? file.getFileNameWithoutExtension() : juce::String(result.displayName);
    return applyPreparedPresetState(result.state, result.fingerprint, juce::String(result.message), name, file.getFullPathName(), message);
}

bool SynthAudioProcessor::savePresetFile(const juce::File& file,
                                         const juce::String& displayName,
                                         juce::String& message)
{
    synth::PresetWriteOptions options;
    options.metadata.displayName = displayName.toStdString();
    return savePresetFile(file, options, message);
}

bool SynthAudioProcessor::savePresetFile(const juce::File& file,
                                         const synth::PresetWriteOptions& options,
                                         juce::String& message)
{
    std::string error;
    const auto saved = synth::writeCurrentPreset(parameters, file.getFullPathName().toStdString(),
                                                 options, error);
    if (saved)
    {
        const auto metadataName = juce::String(options.metadata.displayName);
        const auto presetName = metadataName.isNotEmpty() ? metadataName : file.getFileNameWithoutExtension();
        const auto presetFile = withPresetFileExtension(file);
        message = "Saved preset: " + presetName;
        setPresetMetadata(presetName, message, presetFile.getFullPathName());
        setPresetBaselineFingerprint(synth::fingerprintCurrentPresetState(parameters));
        return true;
    }

    message = "Preset save failed: " + juce::String(error);
    setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
    return false;
}

bool SynthAudioProcessor::initializeCurrentPreset(juce::String& message)
{
    const auto result = synth::prepareInitPresetState(parameters);
    if (!result.loaded)
    {
        message = juce::String(result.message);
        setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
        return false;
    }

    return applyPreparedPresetState(result.state, result.fingerprint, juce::String(result.message),
                                    "Init", "", message);
}

bool SynthAudioProcessor::resetCurrentPreset(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    const auto& slot = programs[static_cast<std::size_t>(getCurrentProgram())];
    if (slot.baseline.isValid())
        return applyPreparedPresetState(slot.baseline.createCopy(), synth::fingerprintPresetState(slot.baseline),
                                        "Program reset", slot.name, getCurrentPresetFilePath(), message, false);
    return initializeCurrentPreset(message);
}

bool SynthAudioProcessor::randomizeCurrentPreset(juce::String& message)
{
    const auto seed = static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt64());
    return randomizeCurrentPresetWithSeed(seed, message);
}

bool SynthAudioProcessor::randomizeCurrentPresetWithSeed(std::uint32_t seed, juce::String& message)
{
    const auto result = synth::prepareRandomizedPresetState(parameters, seed);
    if (!result.loaded)
    {
        message = juce::String(result.message);
        setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
        return false;
    }

    return applyPreparedPresetState(result.state, result.fingerprint, juce::String(result.message),
                                    juce::String(result.displayName), "", message);
}

SynthAudioProcessor::PresetWorkflowSnapshot SynthAudioProcessor::getPresetWorkflowSnapshot() const
{
    PresetWorkflowSnapshot snapshot;
    {
        const juce::CriticalSection::ScopedLockType lock(presetMetadataLock);
        snapshot.currentPreset = currentPresetName;
        snapshot.currentPresetPath = currentPresetFilePath;
        snapshot.lastPresetStatus = lastPresetStatus;
        snapshot.resetAvailable = true;
    }

    synth::PresetStateFingerprint baseline;
    {
        const juce::CriticalSection::ScopedLockType lock(presetWorkflowLock);
        baseline = presetBaselineFingerprint;
        snapshot.compareSlotAReady = presetCompareSlots[0].captured;
        snapshot.compareSlotBReady = presetCompareSlots[1].captured;
    }

    const auto parameterRevision = presetParameterRevision.load(std::memory_order_acquire);
    const auto baselineRevision = presetBaselineRevision.load(std::memory_order_acquire);
    synth::PresetDirtyState dirtyState;
    auto cacheHit = false;
    {
        const juce::CriticalSection::ScopedLockType lock(presetDirtyStateLock);
        cacheHit = cachedPresetParameterRevision == parameterRevision
            && cachedPresetBaselineRevision == baselineRevision
            && cachedPresetDirtyState.current.valid;
        if (cacheHit)
            dirtyState = cachedPresetDirtyState;
    }

    if (!cacheHit)
    {
        dirtyState = synth::comparePresetDirtyState(parameters, baseline);
        const juce::CriticalSection::ScopedLockType lock(presetDirtyStateLock);
        cachedPresetParameterRevision = parameterRevision;
        cachedPresetBaselineRevision = baselineRevision;
        cachedPresetDirtyState = dirtyState;
    }

    snapshot.baselineValid = dirtyState.baseline.valid;
    snapshot.dirty = dirtyState.dirty;
    return snapshot;
}

bool SynthAudioProcessor::capturePresetCompareSlot(int slotIndex, juce::String& message)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(presetCompareSlots.size()))
    {
        message = "Compare slot unavailable";
        return false;
    }

    const auto label = slotIndex == 0 ? std::string("Compare A") : std::string("Compare B");
    auto slot = synth::capturePresetCompareSlot(parameters, label);
    if (!slot.captured)
    {
        message = "Compare capture failed";
        return false;
    }

    {
        const juce::CriticalSection::ScopedLockType lock(presetWorkflowLock);
        presetCompareSlots[static_cast<std::size_t>(slotIndex)] = std::move(slot);
    }

    message = slotIndex == 0 ? "Captured compare A" : "Captured compare B";
    setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
    return true;
}

bool SynthAudioProcessor::recallPresetCompareSlot(int slotIndex, juce::String& message)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(presetCompareSlots.size()))
    {
        message = "Compare slot unavailable";
        return false;
    }

    synth::PresetCompareSlot slot;
    {
        const juce::CriticalSection::ScopedLockType lock(presetWorkflowLock);
        slot = presetCompareSlots[static_cast<std::size_t>(slotIndex)];
    }

    if (!slot.captured)
    {
        message = slotIndex == 0 ? "Compare A is empty" : "Compare B is empty";
        return false;
    }

    const auto result = synth::preparePresetCompareSlotState(parameters, slot);
    if (!result.loaded)
    {
        message = juce::String(result.message);
        return false;
    }

    return applyPreparedPresetState(result.state, result.fingerprint, juce::String(result.message),
                                    juce::String(result.displayName), "", message);
}

juce::String SynthAudioProcessor::getCurrentPresetName() const
{
    const juce::CriticalSection::ScopedLockType lock(presetMetadataLock);
    return currentPresetName;
}

juce::String SynthAudioProcessor::getCurrentPresetFilePath() const
{
    const juce::CriticalSection::ScopedLockType lock(presetMetadataLock);
    return currentPresetFilePath;
}

synth::ModulationRouteView SynthAudioProcessor::getModulationRouteView() const
{
    return synth::buildModulationRouteView(readParameters(128.0f, false).transMod);
}

bool SynthAudioProcessor::writeModulationRoute(const synth::ModulationRouteWriteRequest& request,
                                               juce::String& message)
{
    const auto write = synth::buildModulationRouteWrite(request);
    if (!write.ok)
    {
        message = juce::String(write.message);
        return false;
    }

    if (!applyModulationRouteParameterEdits(write.edits, message))
        return false;

    message = "Assigned modulation route in slot " + juce::String(request.slotNumber);
    return true;
}

bool SynthAudioProcessor::clearModulationSlot(int slotNumber, juce::String& message)
{
    const auto write = synth::buildModulationSlotClear(slotNumber);
    if (!write.ok)
    {
        message = juce::String(write.message);
        return false;
    }

    if (!applyModulationRouteParameterEdits(write.edits, message))
        return false;

    message = "Cleared modulation slot " + juce::String(slotNumber);
    return true;
}

std::vector<synth::MidiControllerAssignment> SynthAudioProcessor::getMidiControllerAssignments() const
{
    const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
    return midiControllerAssignments;
}

juce::String SynthAudioProcessor::getMidiControllerStatus() const
{
    const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
    return midiControllerStatus;
}

bool SynthAudioProcessor::assignMidiController(int controllerNumber,
                                               const juce::String& parameterId,
                                               juce::String& message)
{
    return assignMidiControllerInternal(controllerNumber, parameterId, message, true);
}

bool SynthAudioProcessor::forgetMidiControllerForParameter(const juce::String& parameterId, juce::String& message)
{
    const auto trimmedParameterId = parameterId.trim();
    if (trimmedParameterId.isEmpty())
    {
        message = "Choose a parameter first";
        return false;
    }

    std::vector<synth::MidiControllerAssignment> nextAssignments;
    bool removed = false;
    {
        const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
        nextAssignments = midiControllerAssignments;
    }

    const auto before = nextAssignments.size();
    nextAssignments.erase(std::remove_if(nextAssignments.begin(),
                                         nextAssignments.end(),
                                         [&trimmedParameterId](const auto& assignment) {
                                             return assignment.parameterId == trimmedParameterId.toStdString();
                                         }),
                          nextAssignments.end());
    removed = nextAssignments.size() != before;

    if (removed)
    {
        nextAssignments = synth::normalizeMidiControllerAssignments(std::move(nextAssignments));
    }

    if (!removed)
    {
        message = "No MIDI CC mapping for " + trimmedParameterId;
        setMidiControllerStatus(message);
        return false;
    }

    std::string error;
    if (!synth::writeMidiControllerAssignments(synth::defaultMidiControllerMapFile(),
                                               nextAssignments,
                                               error))
    {
        message = "MIDI map save failed: " + juce::String(error);
        setMidiControllerStatus(message);
        return false;
    }

    {
        const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
        midiControllerAssignments = std::move(nextAssignments);
    }
    publishMidiControllerAssignments();
    message = "Forgot MIDI CC for " + trimmedParameterId;
    setMidiControllerStatus(message);
    return true;
}

bool SynthAudioProcessor::startMidiLearn(const juce::String& parameterId, juce::String& message)
{
    const auto parameterIndex = parameterIndexForId(parameterId);
    if (parameterIndex < 0)
    {
        message = "Choose a learnable parameter first";
        setMidiControllerStatus(message);
        return false;
    }

    pendingMidiLearnParameterIndex.store(parameterIndex, std::memory_order_release);
    learnedMidiControllerNumber.store(-1, std::memory_order_release);
    learnedMidiParameterIndex.store(-1, std::memory_order_release);
    message = "MIDI learn armed for " + parameterId;
    setMidiControllerStatus(message);
    return true;
}

void SynthAudioProcessor::cancelMidiLearn()
{
    pendingMidiLearnParameterIndex.store(-1, std::memory_order_release);
    learnedMidiControllerNumber.store(-1, std::memory_order_release);
    learnedMidiParameterIndex.store(-1, std::memory_order_release);
    setMidiControllerStatus("MIDI learn canceled");
}

SynthAudioProcessor::DiagnosticsSnapshot SynthAudioProcessor::getDiagnosticsSnapshot() const
{
    DiagnosticsSnapshot snapshot;
    snapshot.sampleRate = diagnosticSampleRate.load(std::memory_order_relaxed);
    snapshot.blockSize = diagnosticBlockSize.load(std::memory_order_relaxed);
    snapshot.activeVoices = diagnosticActiveVoices.load(std::memory_order_relaxed);
    snapshot.midiEvents = diagnosticMidiEvents.load(std::memory_order_relaxed);
    snapshot.invalidSamples = diagnosticInvalidSamples.load(std::memory_order_relaxed);
    snapshot.peak = diagnosticPeak.load(std::memory_order_relaxed);
    snapshot.peakLeft = diagnosticPeakLeft.load(std::memory_order_relaxed);
    snapshot.peakRight = diagnosticPeakRight.load(std::memory_order_relaxed);
    snapshot.patchCost = synth::estimatePatchCost(readParameters(128.0f, false));
    snapshot.architecture = binaryArchitecture();
    const juce::CriticalSection::ScopedLockType lock(presetMetadataLock);
    snapshot.currentPreset = currentPresetName;
    snapshot.lastPresetStatus = lastPresetStatus;
    return snapshot;
}

void SynthAudioProcessor::requestPanic() noexcept
{
    panicRequested.store(true, std::memory_order_release);
}

void SynthAudioProcessor::publishUiVisuals(const juce::AudioBuffer<float>& buffer, int totalSamples,
                                           int activeVoices, bool usesMonoLfo) noexcept
{
    if (buffer.getNumChannels() > 0 && totalSamples > 0)
    {
        const auto* left = buffer.getReadPointer(0);
        const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;
        auto writePosition = scopeWritePosition.load(std::memory_order_relaxed);
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto sample = 0.5f * (left[i] + right[i]);
            scopeSamples[static_cast<std::size_t>(writePosition)]
                .store(std::isfinite(sample) ? sample : 0.0f, std::memory_order_relaxed);
            writePosition = (writePosition + 1) & (scopeCapacity - 1);
        }
        scopeWritePosition.store(writePosition, std::memory_order_release);
    }

    auto published = false;
    if (activeVoices > 0)
    {
        if (usesMonoLfo)
        {
            uiLfoPhase.store(engine.getMonoLfoPhase(), std::memory_order_relaxed);
            uiLfoValue.store(engine.getMonoLfoValue(), std::memory_order_relaxed);
            published = true;
        }

        for (int index = 0; index < 32 && !published; ++index)
        {
            const auto* voice = engine.getVoice(index);
            if (voice == nullptr)
                break;
            if (!voice->isActive())
                continue;

            const auto voiceState = voice->snapshot();
            uiLfoPhase.store(voiceState.lfoPhase, std::memory_order_relaxed);
            uiLfoValue.store(voiceState.lfo, std::memory_order_relaxed);
            published = true;
        }
    }
    uiLfoVoiceActive.store(published, std::memory_order_relaxed);
}

SynthAudioProcessor::UiVisualSnapshot SynthAudioProcessor::getUiVisualSnapshot() const noexcept
{
    UiVisualSnapshot snapshot;
    snapshot.lfoVoiceActive = uiLfoVoiceActive.load(std::memory_order_relaxed);
    snapshot.lfoPhase = uiLfoPhase.load(std::memory_order_relaxed);
    snapshot.lfoValue = uiLfoValue.load(std::memory_order_relaxed);
    snapshot.pitchBend = uiPitchBend.load(std::memory_order_relaxed);
    snapshot.modWheel = uiModWheel.load(std::memory_order_relaxed);
    return snapshot;
}

int SynthAudioProcessor::readScopeSamples(float* destination, int maxSamples) const noexcept
{
    if (destination == nullptr || maxSamples <= 0)
        return 0;

    const auto count = std::min(maxSamples, scopeCapacity);
    const auto end = scopeWritePosition.load(std::memory_order_acquire);
    auto index = (end - count + scopeCapacity) & (scopeCapacity - 1);
    for (int i = 0; i < count; ++i)
    {
        destination[i] = scopeSamples[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
        index = (index + 1) & (scopeCapacity - 1);
    }
    return count;
}

void SynthAudioProcessor::timerCallback()
{
    applyPendingProgramRequest();
    applyPendingMidiLearns();
    applyPendingMappedControllers();
}

void SynthAudioProcessor::loadMidiControllerAssignments()
{
    {
        const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
        midiControllerAssignments = synth::readMidiControllerAssignments(synth::defaultMidiControllerMapFile());
        midiControllerStatus = midiControllerAssignments.empty()
            ? "MIDI learn ready"
            : "Loaded " + juce::String(midiControllerAssignments.size()) + " MIDI CC mappings";
    }
    publishMidiControllerAssignments();
}

void SynthAudioProcessor::publishMidiControllerAssignments()
{
    for (auto& parameterIndex : midiControllerParameterIndices)
        parameterIndex.store(-1, std::memory_order_release);

    const auto assignments = getMidiControllerAssignments();
    for (const auto& assignment : assignments)
    {
        if (!validMidiControllerNumber(assignment.controllerNumber)
            || reservedMidiControllerNumber(assignment.controllerNumber))
            continue;

        const auto parameterIndex = parameterIndexForId(juce::String(assignment.parameterId));
        if (parameterIndex < 0)
            continue;

        midiControllerParameterIndices[static_cast<std::size_t>(assignment.controllerNumber)]
            .store(parameterIndex, std::memory_order_release);
    }
}

bool SynthAudioProcessor::assignMidiControllerInternal(int controllerNumber,
                                                       const juce::String& parameterId,
                                                       juce::String& message,
                                                       bool persist)
{
    const auto trimmedParameterId = parameterId.trim();
    const auto parameterIndex = parameterIndexForId(trimmedParameterId);
    if (!validMidiControllerNumber(controllerNumber))
    {
        message = "MIDI CC must be 0-127";
        setMidiControllerStatus(message);
        return false;
    }
    if (reservedMidiControllerNumber(controllerNumber))
    {
        message = "MIDI CC" + juce::String(controllerNumber) + " is reserved";
        setMidiControllerStatus(message);
        return false;
    }
    if (parameterIndex < 0)
    {
        message = "Choose a learnable parameter first";
        setMidiControllerStatus(message);
        return false;
    }

    std::vector<synth::MidiControllerAssignment> nextAssignments;
    {
        const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
        nextAssignments = midiControllerAssignments;
    }

    nextAssignments.erase(std::remove_if(nextAssignments.begin(),
                                         nextAssignments.end(),
                                         [controllerNumber, &trimmedParameterId](const auto& assignment) {
                                             return assignment.controllerNumber == controllerNumber
                                                 || assignment.parameterId == trimmedParameterId.toStdString();
                                         }),
                          nextAssignments.end());
    nextAssignments.push_back({ controllerNumber, trimmedParameterId.toStdString() });
    nextAssignments = synth::normalizeMidiControllerAssignments(std::move(nextAssignments));

    if (persist)
    {
        std::string error;
        if (!synth::writeMidiControllerAssignments(synth::defaultMidiControllerMapFile(),
                                                   nextAssignments,
                                                   error))
        {
            message = "MIDI map save failed: " + juce::String(error);
            setMidiControllerStatus(message);
            return false;
        }
    }

    {
        const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
        midiControllerAssignments = std::move(nextAssignments);
    }
    publishMidiControllerAssignments();
    message = "Mapped CC" + juce::String(controllerNumber) + " to " + trimmedParameterId;
    setMidiControllerStatus(message);
    return true;
}

bool SynthAudioProcessor::applyModulationRouteParameterEdits(
    const std::vector<synth::ModulationRouteParameterEdit>& edits,
    juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    struct ParameterTarget
    {
        juce::RangedAudioParameter* parameter = nullptr;
        float normalizedValue = 0.0f;
    };

    std::vector<ParameterTarget> targets;
    targets.reserve(edits.size());

    for (const auto& edit : edits)
    {
        const auto* spec = synth::findParameterSpec(edit.parameterId);
        if (spec == nullptr)
        {
            message = "Unknown modulation parameter: " + juce::String(edit.parameterId);
            return false;
        }

        auto* parameter = parameters.getParameter(juce::String(edit.parameterId));
        if (parameter == nullptr)
        {
            message = "Missing modulation parameter: " + juce::String(edit.parameterId);
            return false;
        }

        if (!std::isfinite(edit.value))
        {
            message = "Invalid modulation parameter value: " + juce::String(edit.parameterId);
            return false;
        }

        const auto physicalValue = synth::clampPhysicalParameterValue(*spec, edit.value);
        targets.push_back({ parameter, parameter->convertTo0to1(physicalValue) });
    }

    {
        ScopedParameterStateUpdate update(parameterStateSequence);
        for (auto& target : targets)
        {
            target.parameter->beginChangeGesture();
            target.parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, target.normalizedValue));
            target.parameter->endChangeGesture();
        }
    }

    return true;
}

bool SynthAudioProcessor::applyPreparedPresetState(const juce::ValueTree& state,
                                                   const synth::PresetStateFingerprint& baselineFingerprint,
                                                   const juce::String& status,
                                                   const juce::String& presetName,
                                                   const juce::String& presetFilePath,
                                                   juce::String& message,
                                                   bool updateProgramBaseline)
{
    const juce::ScopedLock operationLock(programLock);
    if (!state.isValid())
    {
        message = "Preset command failed: invalid state";
        setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
        return false;
    }

    {
        ScopedParameterStateUpdate update(parameterStateSequence);
        parameters.replaceState(state);
    }

    {
        auto& program = programs[static_cast<std::size_t>(getCurrentProgram())];
        program.state = state.createCopy();
        if (updateProgramBaseline || !program.baseline.isValid())
            program.baseline = state.createCopy();
        program.name = presetName;
    }
    message = status;
    setPresetMetadata(presetName, message, presetFilePath);
    setPresetBaselineFingerprint(baselineFingerprint);
    panicRequested.store(true, std::memory_order_release);
    return true;
}

int SynthAudioProcessor::parameterIndexForId(const juce::String& parameterId) const
{
    const auto id = parameterId.trim().toStdString();
    const auto& specs = synth::getParameterSpecs();
    for (int index = 0; index < static_cast<int>(specs.size()); ++index)
    {
        const auto& spec = specs[static_cast<std::size_t>(index)];
        if (spec.id == id && spec.automatable)
            return index;
    }
    return -1;
}

void SynthAudioProcessor::applyPendingMidiLearns()
{
    const auto controllerNumber = learnedMidiControllerNumber.exchange(-1, std::memory_order_acq_rel);
    const auto parameterIndex = learnedMidiParameterIndex.exchange(-1, std::memory_order_acq_rel);
    if (!validMidiControllerNumber(controllerNumber) || parameterIndex < 0)
        return;

    const auto& specs = synth::getParameterSpecs();
    if (parameterIndex >= static_cast<int>(specs.size()))
        return;

    juce::String message;
    assignMidiControllerInternal(controllerNumber,
                                 juce::String(specs[static_cast<std::size_t>(parameterIndex)].id),
                                 message,
                                 true);
}

void SynthAudioProcessor::applyPendingMappedControllers()
{
    for (int controllerNumber = 0; controllerNumber < static_cast<int>(pendingMidiControllerValues.size()); ++controllerNumber)
    {
        const auto controllerIndex = static_cast<std::size_t>(controllerNumber);
        const auto sequence = pendingMidiControllerValues[controllerIndex].sequence.load(std::memory_order_acquire);
        if (sequence == appliedMidiControllerSequences[controllerIndex])
            continue;

        appliedMidiControllerSequences[controllerIndex] = sequence;
        const auto parameterIndex = midiControllerParameterIndices[controllerIndex].load(std::memory_order_acquire);
        const auto controllerValue = pendingMidiControllerValues[controllerIndex].value.load(std::memory_order_relaxed);
        if (parameterIndex >= 0 && controllerValue >= 0)
            applyMappedControllerValue(parameterIndex, controllerValue);
    }
}

void SynthAudioProcessor::applyMappedControllerValue(int parameterIndex, int controllerValue)
{
    const juce::ScopedLock operationLock(programLock);
    const auto& specs = synth::getParameterSpecs();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(specs.size()))
        return;

    const auto& spec = specs[static_cast<std::size_t>(parameterIndex)];
    auto* parameter = parameters.getParameter(juce::String(spec.id));
    if (parameter == nullptr)
        return;

    const auto normalizedController = static_cast<float>(std::clamp(controllerValue, 0, 127)) / 127.0f;
    auto normalizedParameter = normalizedController;
    if (spec.kind == synth::ParameterKind::Bool)
        normalizedParameter = controllerValue >= 64 ? 1.0f : 0.0f;
    else if (spec.kind == synth::ParameterKind::Choice)
    {
        const auto maxChoice = std::max(0, static_cast<int>(spec.choices.size()) - 1);
        const auto choice = static_cast<float>(juce::jlimit(0, maxChoice,
                                                            static_cast<int>(std::round(normalizedController * static_cast<float>(maxChoice)))));
        normalizedParameter = parameter->convertTo0to1(choice);
    }

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedParameter));
    parameter->endChangeGesture();
}

void SynthAudioProcessor::setMidiControllerStatus(const juce::String& status)
{
    const juce::CriticalSection::ScopedLockType lock(midiControllerLock);
    midiControllerStatus = status;
}

void SynthAudioProcessor::setPresetMetadata(const juce::String& presetName,
                                            const juce::String& status,
                                            const juce::String& presetFilePath)
{
    const juce::CriticalSection::ScopedLockType lock(presetMetadataLock);
    currentPresetName = presetName;
    currentPresetFilePath = presetFilePath;
    lastPresetStatus = status;
}

void SynthAudioProcessor::setPresetBaselineFingerprint(const synth::PresetStateFingerprint& fingerprint)
{
    const juce::CriticalSection::ScopedLockType lock(presetWorkflowLock);
    presetBaselineFingerprint = fingerprint;
    presetBaselineRevision.fetch_add(1, std::memory_order_release);
}

void SynthAudioProcessor::parameterChanged(const juce::String& parameterId, float newValue)
{
    juce::ignoreUnused(parameterId, newValue);
    presetParameterRevision.fetch_add(1, std::memory_order_release);
}

void SynthAudioProcessor::setCurrentProgram(int index)
{
    if (index >= 0 && index < programCount)
        requestedProgram.store(index, std::memory_order_release);
}

const juce::String SynthAudioProcessor::getProgramName(int index)
{
    if (index < 0 || index >= programCount)
        return {};
    const juce::ScopedLock lock(programLock);
    return programs[static_cast<std::size_t>(index)].name;
}

void SynthAudioProcessor::changeProgramName(int index, const juce::String& name)
{
    const juce::ScopedLock operationLock(programLock);
    const auto trimmed = name.trim().substring(0, 128);
    if (index < 0 || index >= programCount || trimmed.isEmpty())
        return;
    {
        const juce::ScopedLock lock(programLock);
        programs[static_cast<std::size_t>(index)].name = trimmed;
    }
    if (index == getCurrentProgram())
        setPresetMetadata(trimmed, "Program renamed", getCurrentPresetFilePath());
}

void SynthAudioProcessor::storeCurrentProgram()
{
    const juce::ScopedLock operationLock(programLock);
    auto state = parameters.copyState().createCopy();
    state.removeChild(state.getChildWithName("PROGRAM_BANK"), nullptr);
    const auto name = getCurrentPresetName();
    const juce::ScopedLock lock(programLock);
    auto& slot = programs[static_cast<std::size_t>(getCurrentProgram())];
    slot.state = state;
    slot.name = name;
    if (!slot.baseline.isValid())
        slot.baseline = state.createCopy();
}

bool SynthAudioProcessor::selectProgram(int index, juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    if (index < 0 || index >= programCount)
    {
        message = "Program must be 0 through 511";
        return false;
    }
    storeCurrentProgram();
    ProgramSlot slot;
    {
        const juce::ScopedLock lock(programLock);
        slot = programs[static_cast<std::size_t>(index)];
    }
    auto state = slot.state.isValid() ? synth::mergeParameterStateWithDefaults(parameters, slot.state)
                                      : synth::prepareInitPresetState(parameters).state;
    state.setProperty("current_preset", slot.name, nullptr);
    state.setProperty("current_preset_path", "", nullptr);
    currentProgram.store(index, std::memory_order_release);
    if (!applyPreparedPresetState(state, synth::fingerprintPresetState(slot.baseline.isValid() ? slot.baseline : state), "Selected program", slot.name, {}, message, false))
        return false;
    currentProgram.store(index, std::memory_order_release);
    midiSubBank.store(index / programsPerSubBank, std::memory_order_release);
    {
        const juce::ScopedLock lock(programLock);
        auto& selected = programs[static_cast<std::size_t>(index)];
        selected.state = state.createCopy();
        if (!selected.baseline.isValid())
            selected.baseline = state.createCopy();
    }
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
    return true;
}

bool SynthAudioProcessor::previousProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    return selectProgram((getCurrentProgram() + programCount - 1) % programCount, message);
}

bool SynthAudioProcessor::nextProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    return selectProgram((getCurrentProgram() + 1) % programCount, message);
}

bool SynthAudioProcessor::copyCurrentProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    storeCurrentProgram();
    const juce::ScopedLock lock(programLock);
    programClipboard = programs[static_cast<std::size_t>(getCurrentProgram())];
    programClipboard.state = programClipboard.state.createCopy();
    programClipboardValid = true;
    message = "Program copied";
    return true;
}

bool SynthAudioProcessor::pasteCurrentProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    ProgramSlot copy;
    {
        const juce::ScopedLock lock(programLock);
        if (!programClipboardValid)
        {
            message = "No copied program";
            return false;
        }
        copy = programClipboard;
    }
    auto state = copy.state.createCopy();
    return applyPreparedPresetState(state, synth::fingerprintPresetState(state), "Program pasted", copy.name, {}, message);
}

bool SynthAudioProcessor::insertCurrentProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    if (!programClipboardValid)
    {
        message = "No copied program";
        return false;
    }
    storeCurrentProgram();
    const auto index = getCurrentProgram();
    const auto end = (index / programsPerSubBank + 1) * programsPerSubBank - 1;
    for (int destination = end; destination > index; --destination)
        programs[static_cast<std::size_t>(destination)] = programs[static_cast<std::size_t>(destination - 1)];
    programs[static_cast<std::size_t>(index)] = programClipboard;
    return pasteCurrentProgram(message);
}

bool SynthAudioProcessor::deleteCurrentProgram(juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    const auto index = getCurrentProgram();
    const auto end = (index / programsPerSubBank + 1) * programsPerSubBank - 1;
    ProgramSlot replacement;
    {
        const juce::ScopedLock lock(programLock);
        for (int destination = index; destination < end; ++destination)
            programs[static_cast<std::size_t>(destination)] = programs[static_cast<std::size_t>(destination + 1)];
        programs[static_cast<std::size_t>(end)] = ProgramSlot {};
        replacement = programs[static_cast<std::size_t>(index)];
    }
    const auto state = replacement.state.isValid() ? replacement.state.createCopy() : synth::prepareInitPresetState(parameters).state;
    return applyPreparedPresetState(state, synth::fingerprintPresetState(replacement.baseline.isValid() ? replacement.baseline : state), "Program deleted", replacement.name, {}, message, false);
}

juce::ValueTree SynthAudioProcessor::createProgramBankState()
{
    const juce::ScopedLock operationLock(programLock);
    storeCurrentProgram();
    juce::ValueTree bank("PROGRAM_BANK");
    bank.setProperty("schema_version", 1, nullptr);
    bank.setProperty("current_program", getCurrentProgram(), nullptr);
    const juce::ScopedLock lock(programLock);
    for (int index = 0; index < programCount; ++index)
    {
        const auto& slot = programs[static_cast<std::size_t>(index)];
        juce::ValueTree program("PROGRAM");
        program.setProperty("index", index, nullptr);
        program.setProperty("name", slot.name, nullptr);
        if (slot.state.isValid())
            program.appendChild(slot.state.createCopy(), nullptr);
        if (slot.baseline.isValid())
        {
            juce::ValueTree baseline("BASELINE");
            baseline.appendChild(slot.baseline.createCopy(), nullptr);
            program.appendChild(baseline, nullptr);
        }
        bank.appendChild(program, nullptr);
    }
    return bank;
}

bool SynthAudioProcessor::restoreProgramBankState(const juce::ValueTree& bank, juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    if (!bank.hasType("PROGRAM_BANK") || static_cast<int>(bank.getProperty("schema_version")) != 1
        || bank.getNumChildren() != programCount)
    {
        message = "Invalid Synthia bank version or program count";
        return false;
    }
    std::array<ProgramSlot, programCount> prepared;
    std::array<bool, programCount> seen {};
    for (const auto& entry : bank)
    {
        const auto index = static_cast<int>(entry.getProperty("index", -1));
        if (!entry.hasType("PROGRAM") || index < 0 || index >= programCount || seen[static_cast<std::size_t>(index)])
        {
            message = "Invalid or duplicate program index";
            return false;
        }
        seen[static_cast<std::size_t>(index)] = true;
        const auto name = entry.getProperty("name").toString().trim();
        if (name.isEmpty() || name.length() > 128 || entry.getNumChildren() > 2)
        {
            message = "Invalid program name or state";
            return false;
        }
        auto& slot = prepared[static_cast<std::size_t>(index)];
        slot.name = name;
        if (entry.getNumChildren() >= 1)
        {
            const auto state = entry.getChild(0);
            if (!state.hasType("SYNTHIA_STATE"))
            {
                message = "Invalid program state type";
                return false;
            }
            for (const auto& child : state)
                if (child.hasType("PARAM"))
                    if (const auto* spec = synth::findParameterSpec(child.getProperty("id").toString().toStdString()))
                    {
                        const auto value = synth::parseStateParameterNumber(child.getProperty("value"));
                        if (!value.has_value() || std::abs(synth::clampPhysicalParameterValue(*spec, *value) - *value) > 0.00001f)
                        {
                            message = "Invalid program parameter: " + child.getProperty("id").toString();
                            return false;
                        }
                    }
            slot.state = synth::mergeParameterStateWithDefaults(parameters, state);
            slot.state.removeChild(slot.state.getChildWithName("PROGRAM_BANK"), nullptr);
            const auto baseline = entry.getChildWithName("BASELINE");
            if (baseline.isValid() && baseline.getNumChildren() == 1 && baseline.getChild(0).hasType("SYNTHIA_STATE"))
                slot.baseline = synth::mergeParameterStateWithDefaults(parameters, baseline.getChild(0));
            else
                slot.baseline = slot.state.createCopy();
        }
    }
    const auto selected = static_cast<int>(bank.getProperty("current_program", 0));
    if (selected < 0 || selected >= programCount)
    {
        message = "Invalid current program";
        return false;
    }
    {
        const juce::ScopedLock lock(programLock);
        programs = std::move(prepared);
    }
    requestedProgram.store(-1, std::memory_order_release);
    currentProgram.store(selected, std::memory_order_release);
    midiSubBank.store(selected / programsPerSubBank, std::memory_order_release);
    message = "Program bank restored";
    return true;
}

bool SynthAudioProcessor::loadProgramBank(const juce::File& file, juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    if (!file.hasFileExtension("SynthiaBank"))
    {
        message = "Supported bank format is .SynthiaBank; proprietary .fxb/.fxp import is unavailable";
        return false;
    }
    if (file.getSize() > 64 * 1024 * 1024)
    {
        message = "Bank exceeds the 64 MB limit";
        return false;
    }
    const auto json = juce::JSON::parse(file.loadFileAsString());
    const auto* object = json.getDynamicObject();
    if (object == nullptr || object->getProperty("fileType").toString() != "SynthiaBank"
        || static_cast<int>(object->getProperty("schema_version")) != 1)
    {
        message = "Invalid Synthia bank envelope";
        return false;
    }
    const auto xml = juce::parseXML(object->getProperty("program_state_xml").toString());
    if (xml == nullptr || !restoreProgramBankState(juce::ValueTree::fromXml(*xml), message))
        return false;
    ProgramSlot selected;
    {
        const juce::ScopedLock lock(programLock);
        selected = programs[static_cast<std::size_t>(getCurrentProgram())];
    }
    const auto state = selected.state.isValid() ? selected.state.createCopy() : synth::prepareInitPresetState(parameters).state;
    return applyPreparedPresetState(state, synth::fingerprintPresetState(selected.baseline.isValid() ? selected.baseline : state), "Bank loaded", selected.name, {}, message, false);
}

bool SynthAudioProcessor::saveProgramBank(const juce::File& file, bool overwrite, juce::String& message)
{
    const juce::ScopedLock operationLock(programLock);
    if (file.hasFileExtension("fxb;fxp"))
    {
        message = "Proprietary .fxb/.fxp export is unavailable; choose .SynthiaBank";
        return false;
    }
    const auto destination = file.hasFileExtension("SynthiaBank") ? file : file.withFileExtension("SynthiaBank");
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("fileType", "SynthiaBank");
    root->setProperty("schema_version", 1);
    root->setProperty("subbanks", subBankCount);
    root->setProperty("programs_per_subbank", programsPerSubBank);
    root->setProperty("program_state_xml", createProgramBankState().toXmlString());
    std::string error;
    if (!synth::writeOwnedStateFile(destination.getFullPathName().toStdString(), juce::JSON::toString(juce::var(root.release()), true), overwrite, error))
    {
        message = juce::String(error);
        return false;
    }
    message = "Saved Synthia bank: " + destination.getFileName();
    return true;
}

void SynthAudioProcessor::applyPendingProgramRequest()
{
    const auto requested = requestedProgram.exchange(-1, std::memory_order_acq_rel);
    if (requested >= 0)
    {
        juce::String message;
        selectProgram(requested, message);
    }
}

float SynthAudioProcessor::getEffectiveParameterValue(const juce::String& parameterId) const noexcept
{
    if (const auto* value = parameters.getRawParameterValue(parameterId))
    {
        const auto physical = value->load(std::memory_order_relaxed);
        return std::isfinite(physical) ? physical : 0.0f;
    }
    return 0.0f;
}

std::optional<int> SynthAudioProcessor::getLearnedMidiController(const juce::String& parameterId) const
{
    for (const auto& assignment : getMidiControllerAssignments())
        if (assignment.parameterId == parameterId.toStdString())
            return assignment.controllerNumber;
    return std::nullopt;
}

bool SynthAudioProcessor::submitUiMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.getRawDataSize() <= 0 || message.getRawDataSize() > 3)
        return false;
    if (message.isPitchWheel())
    {
        uiRequestedPitchWheel.store(message.getPitchWheelValue(), std::memory_order_release);
        return true;
    }
    if (message.isControllerOfType(1))
    {
        uiRequestedModWheel.store(message.getControllerValue(), std::memory_order_release);
        return true;
    }
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    uiMidiFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 == 0)
    {
        if (message.isNoteOff(true) || message.isAllNotesOff() || message.isAllSoundOff())
            uiMidiReleaseOverflow.store(true, std::memory_order_release);
        return false;
    }
    auto& packet = uiMidiPackets[static_cast<std::size_t>(start1)];
    packet.size = message.getRawDataSize();
    std::copy_n(message.getRawData(), packet.size, packet.bytes.begin());
    uiMidiFifo.finishedWrite(1);
    return true;
}

void SynthAudioProcessor::drainUiMidiMessages() noexcept
{
    if (uiMidiReleaseOverflow.exchange(false, std::memory_order_acq_rel))
    {
        const auto pending = uiMidiFifo.getNumReady();
        uiMidiFifo.finishedRead(pending);
        engine.panic();
    }
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    uiMidiFifo.prepareToRead(uiMidiCapacity, start1, size1, start2, size2);
    auto drain = [this](int start, int count) noexcept {
        for (int index = start; index < start + count; ++index)
        {
            const auto& packet = uiMidiPackets[static_cast<std::size_t>(index)];
            handleMidiMessage(juce::MidiMessage(packet.bytes.data(), packet.size, 0.0));
        }
    };
    drain(start1, size1);
    drain(start2, size2);
    uiMidiFifo.finishedRead(size1 + size2);
    const auto pitch = uiRequestedPitchWheel.exchange(-1, std::memory_order_acq_rel);
    if (pitch >= 0) handleMidiMessage(juce::MidiMessage::pitchWheel(1, pitch));
    const auto wheel = uiRequestedModWheel.exchange(-1, std::memory_order_acq_rel);
    if (wheel >= 0) handleMidiMessage(juce::MidiMessage::controllerEvent(1, 1, wheel));
}

void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock operationLock(programLock);
    auto state = parameters.copyState();
    state.setProperty("schema_version", 2, nullptr);
    state.setProperty("plugin_version", ProjectInfo::versionString, nullptr);
    state.setProperty("selected_part", getSelectedPart(), nullptr);
    state.setProperty("current_preset", getCurrentPresetName(), nullptr);
    state.setProperty("current_preset_path", getCurrentPresetFilePath(), nullptr);

    state.removeChild(state.getChildWithName("PROGRAM_BANK"), nullptr);
    state.appendChild(createProgramBankState(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::ScopedLock operationLock(programLock);
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName("SYNTHIA_STATE"))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            if (state.isValid())
            {
                requestedProgram.store(-1, std::memory_order_release);
                const auto bank = state.getChildWithName("PROGRAM_BANK");
                if (bank.isValid())
                {
                    juce::String bankMessage;
                    if (!restoreProgramBankState(bank, bankMessage))
                        return;
                    state.removeChild(bank, nullptr);
                }
                setSelectedPart(static_cast<int>(state.getProperty("selected_part", 0)));
                const auto presetName = state.getProperty("current_preset", "Restored State").toString();
                const auto presetPath = state.getProperty("current_preset_path", "").toString();
                const auto migratedState = synth::mergeParameterStateWithDefaults(parameters, state);
                {
                    ScopedParameterStateUpdate update(parameterStateSequence);
                    parameters.replaceState(migratedState);
                }
                setPresetMetadata(presetName, "Host state restored", presetPath);
                const auto& restoredProgram = programs[static_cast<std::size_t>(getCurrentProgram())];
                setPresetBaselineFingerprint(synth::fingerprintPresetState(restoredProgram.baseline.isValid() ? restoredProgram.baseline : migratedState));
                panicRequested.store(true, std::memory_order_release);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}
