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
} // namespace

SynthAudioProcessor::SynthAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "SYNTHIA_STATE", synth::createParameterLayout())
{
    for (auto& parameterIndex : midiControllerParameterIndices)
        parameterIndex.store(-1, std::memory_order_relaxed);

    cacheParameterPointers();
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
    const auto hadMidi = !midiMessages.isEmpty();
    if (!hadMidi && engine.getActiveVoiceCount() <= 0 && tailDrainSamplesRemaining <= 0)
    {
        buffer.clear();
        diagnosticPeak.store(0.0f, std::memory_order_relaxed);
        diagnosticActiveVoices.store(0, std::memory_order_relaxed);
        diagnosticBlockSize.store(totalSamples, std::memory_order_relaxed);
        uiLfoVoiceActive.store(false, std::memory_order_relaxed);
        return;
    }

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

        handleMidiMessage(metadata.getMessage());
    }

    const auto finalStats = renderSegment(buffer, renderedUntil, totalSamples - renderedUntil);
    blockPeak = std::max(blockPeak, finalStats.peak);
    blockInvalidSamples += finalStats.invalidSamples;
    blockActiveVoices = finalStats.activeVoices;

    const auto usesMonoLfo = parameterSnapshot.lfo.mono
        || parameterSnapshot.lfo.gateMode == synth::LfoGateMode::Mono
        || parameterSnapshot.lfo.gateMode == synth::LfoGateMode::Song;
    publishUiVisuals(buffer, totalSamples, blockActiveVoices, usesMonoLfo);

    diagnosticPeak.store(blockPeak, std::memory_order_relaxed);
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
    const auto voiceTail = std::max(snapshot.ampEnv.releaseMs, snapshot.modEnv.releaseMs) * 0.001f;
    return std::max(static_cast<double>(voiceTail),
                    static_cast<double>(synth::fxTailLengthSeconds(snapshot)));
}

void SynthAudioProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    diagnosticMidiEvents.fetch_add(1, std::memory_order_relaxed);

    if (message.isNoteOn(false))
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff(true))
        engine.noteOff(message.getNoteNumber());
    else if (message.isAllSoundOff())
        engine.panic();
    else if (message.isAllNotesOff())
        engine.allNotesOff();
    else if (message.isPitchWheel())
        engine.setPitchBend((static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f);
    else if (message.isControllerOfType(64))
        engine.setSustainPedal(message.getControllerValue() >= 64);
    else if (message.isControllerOfType(1))
        engine.setModWheel(static_cast<float>(message.getControllerValue()) / 127.0f);
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
            mono[offset + i] = 0.5f * (scratchLeft[static_cast<std::size_t>(i)]
                + scratchRight[static_cast<std::size_t>(i)]);

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
    raw.voiceRetrigger = get("voice.retrigger");
    raw.voiceGlideMs = get("voice.glide_ms");
    raw.voiceVelocityGlideMs = get("voice.velocity_glide_ms");
    for (int layer = 0; layer < synth::layerCount; ++layer)
    {
        const auto layerPrefix = "layer." + std::to_string(layer + 1) + ".";
        auto& rawLayer = raw.layers[static_cast<std::size_t>(layer)];
        rawLayer.enabled = get((layerPrefix + "enabled").c_str());
        rawLayer.levelDb = get((layerPrefix + "level_db").c_str());
        rawLayer.pan = get((layerPrefix + "pan").c_str());
        rawLayer.solo = get((layerPrefix + "solo").c_str());
        rawLayer.mute = get((layerPrefix + "mute").c_str());

        for (int oscillator = 0; oscillator < synth::oscillatorSlotsPerLayer; ++oscillator)
        {
            const auto oscillatorPrefix = layerPrefix + "osc." + std::to_string(oscillator + 1) + ".";
            auto& rawOscillator = rawLayer.oscillators[static_cast<std::size_t>(oscillator)];
            rawOscillator.enabled = get((oscillatorPrefix + "enabled").c_str());
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
    raw.oscPitchSemitones = get("osc.pitch_semitones");
    raw.oscFineCents = get("osc.fine_cents");
    raw.oscStackCount = get("osc.stack_count");
    raw.oscStackDetune = get("osc.stack_detune");
    raw.oscSawLevel = get("osc.saw_level");
    raw.oscPulseLevel = get("osc.pulse_level");
    raw.oscNoiseLevel = get("osc.noise_level");
    raw.oscPulseWidth = get("osc.pulse_width");
    raw.oscSubWave = get("osc.sub_wave");
    raw.oscSubOctave = get("osc.sub_octave");
    raw.oscSubLevel = get("osc.sub_level");
    raw.oscSubPulseWidth = get("osc.sub_pulse_width");
    raw.oscSyncAmount = get("osc.sync_amount");
    raw.oscPhaseReset = get("osc.phase_reset");
    raw.filterEnabled = get("filter.enabled");
    raw.filterMode = get("filter.mode");
    raw.filterCutoffSemitones = get("filter.cutoff_semitones");
    raw.filterResonance = get("filter.resonance");
    raw.filterDrive = get("filter.drive");
    raw.filterKeytrack = get("filter.keytrack");
    raw.filterOversampling = get("filter.oversampling");
    raw.ampDrive = get("amp.drive");
    raw.ampLevelDb = get("amp.level_db");
    raw.ampPan = get("amp.pan");
    raw.ampPanSpread = get("amp.pan_spread");
    raw.ampUnisonSpread = get("amp.unison_spread");
    raw.ampAnalog = get("amp.analog");
    raw.ampAttack = get("amp_env.attack_ms");
    raw.ampDecay = get("amp_env.decay_ms");
    raw.ampSustain = get("amp_env.sustain");
    raw.ampRelease = get("amp_env.release_ms");
    raw.modAttack = get("mod_env.attack_ms");
    raw.modDecay = get("mod_env.decay_ms");
    raw.modSustain = get("mod_env.sustain");
    raw.modRelease = get("mod_env.release_ms");
    raw.lfoShape = get("lfo.shape");
    raw.lfoRateMode = get("lfo.rate_mode");
    raw.lfoRateHz = get("lfo.rate_hz");
    raw.lfoSyncDivision = get("lfo.sync_division");
    raw.lfoPhaseDegrees = get("lfo.phase_degrees");
    raw.lfoGateMode = get("lfo.gate_mode");
    raw.lfoMono = get("lfo.mono");
    raw.lfoSwing = get("lfo.swing");
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
    raw.directPulseKeytrack = get("direct.pulse_keytrack");
    raw.directPulseLfo = get("direct.pulse_lfo");
    raw.directPulseModEnv = get("direct.pulse_mod_env");
    raw.fxEnabled = get("fx.enabled");
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
    raw.qualityRealtimeMode = get("quality.realtime_mode");
    raw.qualityOfflineMode = get("quality.offline_mode");
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot + 1) + ".";
        auto& rawSlot = raw.transMod[static_cast<std::size_t>(slot)];
        rawSlot.enabled = get((prefix + "enabled").c_str());
        rawSlot.source = get((prefix + "source").c_str());
        rawSlot.scaler = get((prefix + "scaler").c_str());
        rawSlot.depth = get((prefix + "depth").c_str());
        rawSlot.oscPitchSemitones = get((prefix + "osc_pitch_semitones").c_str());
        rawSlot.pulseWidth = get((prefix + "pulse_width").c_str());
        rawSlot.filterCutoffSemitones = get((prefix + "filter_cutoff_semitones").c_str());
        rawSlot.ampLevelDb = get((prefix + "amp_level_db").c_str());
        rawSlot.pan = get((prefix + "pan").c_str());
    }
    raw.macroMotion = get("macro.motion");
    raw.macroWidth = get("macro.width");
    raw.macroDrive = get("macro.drive");
    raw.macroSpace = get("macro.space");
}

synth::SynthParameters SynthAudioProcessor::readParameters(float tempoBpm, bool offlineRender) const noexcept
{
    auto value = [](const std::atomic<float>* parameter, float fallback) noexcept {
        const auto loaded = parameter != nullptr ? parameter->load(std::memory_order_relaxed) : fallback;
        return std::isfinite(loaded) ? loaded : fallback;
    };

    const auto& defaults = parameterDefaults;
    auto snapshot = parameterDefaults;
    snapshot.voiceMode = static_cast<synth::VoiceMode>(static_cast<int>(std::round(value(raw.voiceMode, 2.0f))));
    snapshot.polyphony = static_cast<int>(std::round(value(raw.voicePolyphony, 8.0f)));
    snapshot.unisonCount = static_cast<int>(std::round(value(raw.voiceUnisonCount, 1.0f)));
    snapshot.retrigger = value(raw.voiceRetrigger, 1.0f) >= 0.5f;
    snapshot.glideMs = value(raw.voiceGlideMs, 0.0f);
    snapshot.velocityGlideMs = value(raw.voiceVelocityGlideMs, 0.0f);
    for (int layer = 0; layer < synth::layerCount; ++layer)
    {
        const auto& rawLayer = raw.layers[static_cast<std::size_t>(layer)];
        const auto& defaultLayer = defaults.layers[static_cast<std::size_t>(layer)];
        auto& layerSnapshot = snapshot.layers[static_cast<std::size_t>(layer)];
        layerSnapshot.enabled = value(rawLayer.enabled, defaultLayer.enabled ? 1.0f : 0.0f) >= 0.5f;
        layerSnapshot.levelDb = value(rawLayer.levelDb, defaultLayer.levelDb);
        layerSnapshot.pan = value(rawLayer.pan, defaultLayer.pan);
        layerSnapshot.solo = value(rawLayer.solo, defaultLayer.solo ? 1.0f : 0.0f) >= 0.5f;
        layerSnapshot.mute = value(rawLayer.mute, defaultLayer.mute ? 1.0f : 0.0f) >= 0.5f;

        for (int oscillator = 0; oscillator < synth::oscillatorSlotsPerLayer; ++oscillator)
        {
            const auto& rawOscillator = rawLayer.oscillators[static_cast<std::size_t>(oscillator)];
            const auto& defaultOscillator = defaultLayer.oscillators[static_cast<std::size_t>(oscillator)];
            auto& oscillatorSnapshot = layerSnapshot.oscillators[static_cast<std::size_t>(oscillator)];
            oscillatorSnapshot.enabled = value(rawOscillator.enabled,
                                               defaultOscillator.enabled ? 1.0f : 0.0f) >= 0.5f;
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
                                                 defaultOscillator.retrigger ? 1.0f : 0.0f) >= 0.5f;
            oscillatorSnapshot.invert = value(rawOscillator.invert,
                                              defaultOscillator.invert ? 1.0f : 0.0f) >= 0.5f;
        }
    }
    snapshot.osc.pitchSemitones = value(raw.oscPitchSemitones, 0.0f);
    snapshot.osc.fineCents = value(raw.oscFineCents, 0.0f);
    snapshot.osc.stackCount = static_cast<int>(std::round(value(raw.oscStackCount, 1.0f)));
    snapshot.osc.stackDetune = value(raw.oscStackDetune, 0.0f);
    snapshot.osc.sawLevel = value(raw.oscSawLevel, 1.0f);
    snapshot.osc.pulseLevel = value(raw.oscPulseLevel, 0.0f);
    snapshot.osc.noiseLevel = value(raw.oscNoiseLevel, 0.0f);
    snapshot.osc.pulseWidth = value(raw.oscPulseWidth, 0.5f);
    snapshot.osc.subWave = static_cast<synth::SubWave>(static_cast<int>(std::round(value(raw.oscSubWave, 2.0f))));
    snapshot.osc.subOctave = static_cast<int>(std::round(value(raw.oscSubOctave, 0.0f))) + 1;
    snapshot.osc.subLevel = value(raw.oscSubLevel, 0.0f);
    snapshot.osc.subPulseWidth = value(raw.oscSubPulseWidth, 0.5f);
    snapshot.osc.syncAmount = value(raw.oscSyncAmount, 0.0f);
    snapshot.osc.phaseReset = static_cast<int>(std::round(value(raw.oscPhaseReset, 0.0f)));
    snapshot.filter.enabled = value(raw.filterEnabled, 1.0f) >= 0.5f;
    snapshot.filter.mode = static_cast<synth::FilterMode>(static_cast<int>(std::round(value(raw.filterMode, 1.0f))));
    snapshot.filter.cutoffSemitones = value(raw.filterCutoffSemitones, 96.0f);
    snapshot.filter.resonance = value(raw.filterResonance, 0.0f);
    snapshot.filter.drive = value(raw.filterDrive, 0.0f);
    snapshot.filter.keytrack = value(raw.filterKeytrack, 0.5f);
    snapshot.filter.oversampling = static_cast<int>(std::round(value(raw.filterOversampling, 0.0f)));
    snapshot.amp.drive = value(raw.ampDrive, 0.0f);
    snapshot.amp.levelDb = value(raw.ampLevelDb, -6.0f);
    snapshot.amp.pan = value(raw.ampPan, 0.0f);
    snapshot.amp.panSpread = value(raw.ampPanSpread, 0.0f);
    snapshot.amp.unisonSpread = value(raw.ampUnisonSpread, 0.0f);
    snapshot.amp.analog = value(raw.ampAnalog, 0.0f);
    snapshot.ampEnv = { value(raw.ampAttack, 2.0f), value(raw.ampDecay, 300.0f),
                        value(raw.ampSustain, 0.8f), value(raw.ampRelease, 200.0f) };
    snapshot.modEnv = { value(raw.modAttack, 1.0f), value(raw.modDecay, 400.0f),
                        value(raw.modSustain, 0.0f), value(raw.modRelease, 160.0f) };
    snapshot.lfo.shape = static_cast<synth::LfoShapeChoice>(static_cast<int>(std::round(value(raw.lfoShape, 3.0f))));
    snapshot.lfo.rateMode = static_cast<synth::LfoRateMode>(static_cast<int>(std::round(value(raw.lfoRateMode, 1.0f))));
    snapshot.lfo.rateHz = value(raw.lfoRateHz, 2.0f);
    snapshot.lfo.syncDivision = static_cast<int>(std::round(value(raw.lfoSyncDivision, 3.0f)));
    snapshot.lfo.phaseDegrees = value(raw.lfoPhaseDegrees, 0.0f);
    snapshot.lfo.gateMode = static_cast<synth::LfoGateMode>(static_cast<int>(std::round(value(raw.lfoGateMode, 1.0f))));
    snapshot.lfo.mono = value(raw.lfoMono, 0.0f) >= 0.5f;
    snapshot.lfo.swing = value(raw.lfoSwing, 0.0f);
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
    snapshot.direct.pulseKeytrack = value(raw.directPulseKeytrack, 0.0f);
    snapshot.direct.pulseLfo = value(raw.directPulseLfo, 0.0f);
    snapshot.direct.pulseModEnv = value(raw.directPulseModEnv, 0.0f);
    snapshot.fx.enabled = value(raw.fxEnabled, 0.0f) >= 0.5f;
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
    snapshot.quality.realtimeMode = static_cast<synth::QualityMode>(static_cast<int>(std::round(value(raw.qualityRealtimeMode, 1.0f))));
    snapshot.quality.offlineMode = static_cast<synth::QualityMode>(static_cast<int>(std::round(value(raw.qualityOfflineMode, 2.0f))));
    snapshot.quality.activeMode = offlineRender ? snapshot.quality.offlineMode : snapshot.quality.realtimeMode;
    for (int slot = 0; slot < synth::transModSlotCount; ++slot)
    {
        const auto& rawSlot = raw.transMod[static_cast<std::size_t>(slot)];
        auto& slotSnapshot = snapshot.transMod.slots[static_cast<std::size_t>(slot)];
        slotSnapshot.enabled = value(rawSlot.enabled, 0.0f) >= 0.5f;
        slotSnapshot.source = static_cast<synth::ModSource>(static_cast<int>(std::round(value(rawSlot.source, 0.0f))));
        slotSnapshot.scaler = static_cast<synth::ModSource>(static_cast<int>(std::round(value(rawSlot.scaler, 0.0f))));
        slotSnapshot.depth = value(rawSlot.depth, 0.0f);
        slotSnapshot.oscPitchSemitones = value(rawSlot.oscPitchSemitones, 0.0f);
        slotSnapshot.pulseWidth = value(rawSlot.pulseWidth, 0.0f);
        slotSnapshot.filterCutoffSemitones = value(rawSlot.filterCutoffSemitones, 0.0f);
        slotSnapshot.ampLevelDb = value(rawSlot.ampLevelDb, 0.0f);
        slotSnapshot.pan = value(rawSlot.pan, 0.0f);
    }
    snapshot.macro.motion = value(raw.macroMotion, 0.5f);
    snapshot.macro.width = value(raw.macroWidth, 0.0f);
    snapshot.macro.drive = value(raw.macroDrive, 0.0f);
    snapshot.macro.space = value(raw.macroSpace, 0.0f);
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
    const auto tailSeconds = std::max(std::max(snapshot.ampEnv.releaseMs, snapshot.modEnv.releaseMs) * 0.001f,
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
    const auto result = synth::preparePresetState(parameters, file.getFullPathName().toStdString());
    message = result.message;
    if (result.loaded)
    {
        {
            ScopedParameterStateUpdate update(parameterStateSequence);
            parameters.replaceState(result.state);
        }

        const auto presetName = result.displayName.empty() ? file.getFileNameWithoutExtension()
                                                           : juce::String(result.displayName);
        setPresetMetadata(presetName, message, file.getFullPathName());
        setPresetBaselineFingerprint(result.fingerprint);
        panicRequested.store(true, std::memory_order_release);
    }
    else
    {
        setPresetMetadata(getCurrentPresetName(), message, getCurrentPresetFilePath());
    }

    return result.loaded;
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
    const auto presetPath = getCurrentPresetFilePath();
    if (presetPath.isEmpty())
        return initializeCurrentPreset(message);

    return loadPresetFile(juceFileForPath(std::filesystem::path { presetPath.toStdString() }), message);
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
        snapshot.resetAvailable = currentPresetFilePath.isNotEmpty();
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
                                                   juce::String& message)
{
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

void SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty("schema_version", 1, nullptr);
    state.setProperty("plugin_version", ProjectInfo::versionString, nullptr);
    state.setProperty("current_preset", getCurrentPresetName(), nullptr);
    state.setProperty("current_preset_path", getCurrentPresetFilePath(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName("SYNTHIA_STATE"))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            if (state.isValid())
            {
                const auto presetName = state.getProperty("current_preset", "Restored State").toString();
                const auto presetPath = state.getProperty("current_preset_path", "").toString();
                const auto migratedState = synth::mergeParameterStateWithDefaults(parameters, state);
                {
                    ScopedParameterStateUpdate update(parameterStateSequence);
                    parameters.replaceState(migratedState);
                }
                setPresetMetadata(presetName, "Host state restored", presetPath);
                setPresetBaselineFingerprint(synth::fingerprintPresetState(migratedState));
                panicRequested.store(true, std::memory_order_release);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthAudioProcessor();
}
