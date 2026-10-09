#include "ParameterRegistry.h"

#include "../dsp/SynthParameters.h"
#include "../modulation/ModulationRouteModel.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <unordered_map>

namespace synth
{
namespace
{
ParameterSpec floatParam(std::string id, std::string name, std::string group, std::string unit,
                         float minimum, float maximum, float defaultValue,
                         float interval = 0.0f, float skew = 1.0f, float smoothMs = 0.0f)
{
    ParameterSpec spec;
    spec.id = std::move(id);
    spec.name = std::move(name);
    spec.group = std::move(group);
    spec.unit = std::move(unit);
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.defaultValue = defaultValue;
    spec.interval = interval;
    spec.skew = skew;
    spec.smoothMs = smoothMs;
    return spec;
}

ParameterSpec boolParam(std::string id, std::string name, std::string group, bool defaultValue)
{
    ParameterSpec spec;
    spec.id = std::move(id);
    spec.name = std::move(name);
    spec.group = std::move(group);
    spec.kind = ParameterKind::Bool;
    spec.unit = "boolean";
    spec.defaultValue = defaultValue ? 1.0f : 0.0f;
    return spec;
}

ParameterSpec choiceParam(std::string id, std::string name, std::string group,
                          std::vector<std::string> choices, int defaultChoice)
{
    ParameterSpec spec;
    spec.id = std::move(id);
    spec.name = std::move(name);
    spec.group = std::move(group);
    spec.kind = ParameterKind::Choice;
    spec.unit = "enum";
    spec.choices = std::move(choices);
    spec.maximum = static_cast<float>(spec.choices.size() - 1);
    spec.defaultChoice = defaultChoice;
    spec.defaultValue = static_cast<float>(defaultChoice);
    return spec;
}

std::vector<std::string> sourceChoices()
{
    return {
        "None", "LFO", "Ramp", "ModEnv", "AmpEnv", "Keytrack", "Velocity",
        "VelocityGlide", "PitchBend", "ModWheel", "Aftertouch", "VoiceUni",
        "VoiceBi", "UnisonUni", "UnisonBi", "RandomOnNote", "Macro1",
        "Macro2", "Macro3", "Macro4", "ModEnv2", "LFO2", "AmpEnv2", "StepVelocity"
    };
}

bool isArpOrChordParameter(const ParameterSpec& spec) noexcept
{
    return spec.id.rfind("arp.", 0) == 0 || spec.id.rfind("chord.", 0) == 0;
}

bool isExpandedFxParameter(const ParameterSpec& spec) noexcept
{
    return spec.id == "fx.distortion_mode"
        || spec.id.rfind("fx.phaser_", 0) == 0
        || spec.id.rfind("fx.eq_", 0) == 0
        || spec.id.rfind("fx.compressor_", 0) == 0;
}

std::vector<ParameterSpec> buildSpecs()
{
    std::vector<ParameterSpec> specs = {
        choiceParam("voice.mode", "Voice Mode", "voice", { "Mono", "MonoLegato", "Poly", "Unison" }, 2),
        floatParam("voice.polyphony", "Polyphony", "voice", "voices", 1.0f, 32.0f, 8.0f, 1.0f),
        floatParam("voice.unison_count", "Unison Count", "voice", "voices", 1.0f, 8.0f, 1.0f, 1.0f),
        boolParam("voice.retrigger", "Retrigger", "voice", true),
        floatParam("voice.glide_ms", "Glide", "voice", "milliseconds", 0.0f, 2000.0f, 0.0f, 0.01f, 0.5f),
        floatParam("voice.velocity_glide_ms", "Velocity Glide", "voice", "milliseconds", 0.0f, 2000.0f, 0.0f, 0.01f, 0.5f),

        floatParam("osc.pitch_semitones", "Osc Pitch", "osc", "semitones", -48.0f, 48.0f, 0.0f, 0.01f, 1.0f, 5.0f),
        floatParam("osc.fine_cents", "Osc Fine", "osc", "cents", -100.0f, 100.0f, 0.0f, 0.01f, 1.0f, 5.0f),
        floatParam("osc.stack_count", "Stack Count", "osc", "voices", 1.0f, 5.0f, 1.0f, 1.0f),
        floatParam("osc.stack_detune", "Stack Detune", "osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 0.5f, 10.0f),
        floatParam("osc.saw_level", "Saw Level", "osc", "normalized", 0.0f, 1.0f, 1.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("osc.pulse_level", "Pulse Level", "osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("osc.noise_level", "Noise Level", "osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("osc.pulse_width", "Pulse Width", "osc", "percent", 0.05f, 0.95f, 0.5f, 0.0001f, 1.0f, 5.0f),
        choiceParam("osc.sub_wave", "Sub Wave", "osc", { "Sine", "Triangle", "Saw", "Pulse" }, 2),
        choiceParam("osc.sub_octave", "Sub Octave", "osc", { "-1", "-2", "-3" }, 0),
        floatParam("osc.sub_level", "Sub Level", "osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("osc.sub_pulse_width", "Sub Pulse Width", "osc", "percent", 0.05f, 0.95f, 0.5f, 0.0001f, 1.0f, 5.0f),
        floatParam("osc.sync_amount", "Sync Amount", "osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        choiceParam("osc.phase_reset", "Phase Reset", "osc", { "Off", "Note", "Voice", "Random" }, 0),

        boolParam("filter.enabled", "Filter Enabled", "filter", true),
        choiceParam("filter.mode", "Filter Mode", "filter",
                    { "L2", "L4", "B2", "B4", "H2", "H4", "Peak2", "Notch2", "Notch4" }, 1),
        floatParam("filter.cutoff_semitones", "Filter Cutoff", "filter", "semitones", 0.0f, 136.0f, 96.0f, 0.01f, 1.0f, 10.0f),
        floatParam("filter.resonance", "Resonance", "filter", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 10.0f),
        floatParam("filter.drive", "Filter Drive", "filter", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 10.0f),
        floatParam("filter.keytrack", "Filter Keytrack", "filter", "normalized", 0.0f, 1.0f, 0.5f, 0.0001f, 1.0f, 5.0f),
        choiceParam("filter.oversampling", "Filter Oversampling", "filter", { "Off", "2x", "4x", "8x" }, 0),

        floatParam("amp.drive", "Amp Drive", "amp", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("amp.level_db", "Level", "amp", "dB", -48.0f, 12.0f, -6.0f, 0.01f, 1.0f, 5.0f),
        floatParam("amp.pan", "Pan", "amp", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("amp.pan_spread", "Pan Spread", "amp", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("amp.unison_spread", "Unison Spread", "amp", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),
        floatParam("amp.analog", "Analog", "amp", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f),

        floatParam("amp_env.attack_ms", "Amp Attack", "amp_env", "milliseconds", 0.0f, 10000.0f, 2.0f, 0.01f, 0.35f),
        floatParam("amp_env.decay_ms", "Amp Decay", "amp_env", "milliseconds", 0.0f, 10000.0f, 300.0f, 0.01f, 0.35f),
        floatParam("amp_env.sustain", "Amp Sustain", "amp_env", "normalized", 0.0f, 1.0f, 0.8f, 0.0001f),
        floatParam("amp_env.release_ms", "Amp Release", "amp_env", "milliseconds", 0.0f, 10000.0f, 200.0f, 0.01f, 0.35f),
        floatParam("mod_env.attack_ms", "Mod Attack", "mod_env", "milliseconds", 0.0f, 10000.0f, 1.0f, 0.01f, 0.35f),
        floatParam("mod_env.decay_ms", "Mod Decay", "mod_env", "milliseconds", 0.0f, 10000.0f, 400.0f, 0.01f, 0.35f),
        floatParam("mod_env.sustain", "Mod Sustain", "mod_env", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("mod_env.release_ms", "Mod Release", "mod_env", "milliseconds", 0.0f, 10000.0f, 160.0f, 0.01f, 0.35f),

        choiceParam("lfo.shape", "LFO Shape", "lfo", { "Sine", "Triangle", "SawUp", "SawDown", "Square", "SampleHold", "Noise", "Step" }, 3),
        choiceParam("lfo.rate_mode", "LFO Rate Mode", "lfo", { "Hz", "Sync" }, 1),
        floatParam("lfo.rate_hz", "LFO Rate", "lfo", "Hz", 0.01f, 40.0f, 2.0f, 0.0001f, 0.35f),
        choiceParam("lfo.sync_division", "LFO Sync Division", "lfo", { "1/16", "1/8", "1/8D", "1/4", "1/2", "1 bar", "1/32T", "1/32D", "1/16T", "1/16D", "1/8T", "1/4T", "1/4D", "1/2T", "1/2D", "1/32", "1 barT", "1 barD" }, 3),
        floatParam("lfo.phase_degrees", "LFO Phase", "lfo", "degrees", 0.0f, 360.0f, 0.0f, 0.01f),
        choiceParam("lfo.gate_mode", "LFO Gate Mode", "lfo", { "Poly", "PolyOn", "Mono", "Song" }, 1),
        boolParam("lfo.mono", "LFO Mono", "lfo", false),
        floatParam("lfo.swing", "LFO Swing", "lfo", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("lfo.step_count", "LFO Step Count", "lfo", "steps", 2.0f, 16.0f, 8.0f, 1.0f),
        floatParam("lfo.step_smooth", "LFO Step Smooth", "lfo", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),

        boolParam("ramp.enabled", "Ramp Enabled", "ramp", false),
        choiceParam("ramp.mode", "Ramp Mode", "ramp", { "OneShot", "Loop", "Sync" }, 0),
        floatParam("ramp.delay_ms", "Ramp Delay", "ramp", "milliseconds", 0.0f, 10000.0f, 0.0f, 0.01f, 0.35f),
        floatParam("ramp.rise_ms", "Ramp Rise", "ramp", "milliseconds", 1.0f, 10000.0f, 1000.0f, 0.01f, 0.35f),
        choiceParam("ramp.curve", "Ramp Curve", "ramp", { "Linear", "Exponential", "Snappy" }, 0),

        boolParam("arp.enabled", "Arp Enabled", "arp", false),
        choiceParam("arp.mode", "Arp Mode", "arp", { "Up", "Down", "UpDown", "AsPlayed", "DownUp", "UpDownRepeat", "DownUpRepeat", "Random", "StepSequence", "StepChord" }, 0),
        choiceParam("arp.rate", "Arp Rate", "arp", { "1/32", "1/16", "1/8", "1/4", "1/2", "1/8D", "1/32T", "1/32D", "1/16T", "1/16D", "1/8T", "1/4T", "1/4D", "1/2T", "1/2D", "1 bar", "1 barT", "1 barD" }, 1),
        floatParam("arp.gate", "Arp Gate", "arp", "normalized", 0.02f, 1.0f, 0.75f, 0.0001f),
        floatParam("arp.octaves", "Arp Octaves", "arp", "octaves", 1.0f, 4.0f, 1.0f, 1.0f),
        boolParam("arp.hold", "Arp Hold", "arp", false),
        floatParam("arp.swing", "Arp Swing", "arp", "normalized", 0.0f, 0.75f, 0.0f, 0.0001f),
        floatParam("arp.step_count", "Arp Step Count", "arp", "steps", 1.0f, 16.0f, 16.0f, 1.0f),

        boolParam("chord.enabled", "Chord Enabled", "chord", false),
        floatParam("chord.voice_count", "Chord Voice Count", "chord", "voices", 1.0f, 8.0f, 1.0f, 1.0f),

        floatParam("direct.filter_keytrack", "Filter Keytrack Direct", "direct", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("direct.filter_lfo_semitones", "Filter LFO Direct", "direct", "semitones", -72.0f, 72.0f, 0.0f, 0.01f),
        floatParam("direct.filter_mod_env_semitones", "Filter Env Direct", "direct", "semitones", -72.0f, 72.0f, 0.0f, 0.01f),
        floatParam("direct.osc_keytrack_semitones", "Osc Keytrack Direct", "direct", "semitones", -48.0f, 48.0f, 0.0f, 0.01f),
        floatParam("direct.osc_lfo_semitones", "Osc LFO Direct", "direct", "semitones", -48.0f, 48.0f, 0.0f, 0.01f),
        floatParam("direct.osc_mod_env_semitones", "Osc Env Direct", "direct", "semitones", -48.0f, 48.0f, 0.0f, 0.01f),
        floatParam("direct.pulse_keytrack", "Pulse Keytrack Direct", "direct", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("direct.pulse_lfo", "Pulse LFO Direct", "direct", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("direct.pulse_mod_env", "Pulse Env Direct", "direct", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f),

        boolParam("fx.saturation_enabled", "Saturation Enabled", "fx", false),
        choiceParam("fx.distortion_mode", "Distortion Mode", "fx", { "Soft", "Clip", "Fold", "Decimate", "Bitcrush" }, 0),
        floatParam("fx.saturation_mix", "Saturation Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("fx.saturation_drive", "Saturation Drive", "fx", "normalized", 0.0f, 1.0f, 0.35f, 0.0001f),
        boolParam("fx.phaser_enabled", "Phaser Enabled", "fx", false),
        floatParam("fx.phaser_mix", "Phaser Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("fx.phaser_rate_hz", "Phaser Rate", "fx", "Hz", 0.02f, 8.0f, 0.25f, 0.0001f, 0.35f),
        floatParam("fx.phaser_depth", "Phaser Depth", "fx", "normalized", 0.0f, 1.0f, 0.45f, 0.0001f),
        floatParam("fx.phaser_feedback", "Phaser Feedback", "fx", "normalized", 0.0f, 0.95f, 0.15f, 0.0001f),
        boolParam("fx.delay_enabled", "Delay Enabled", "fx", false),
        floatParam("fx.delay_mix", "Delay Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        choiceParam("fx.delay_sync_division", "Delay Sync", "fx", { "1/16", "1/8", "1/8D", "1/4", "1/2", "1/32", "1/32T", "1/32D", "1/16T", "1/16D", "1/8T", "1/4T", "1/4D", "1/2T", "1/2D", "1 bar", "1 barT", "1 barD" }, 1),
        floatParam("fx.delay_feedback", "Delay Feedback", "fx", "normalized", 0.0f, 0.86f, 0.22f, 0.0001f),
        boolParam("fx.reverb_enabled", "Reverb Enabled", "fx", false),
        floatParam("fx.reverb_mix", "Reverb Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("fx.reverb_decay", "Reverb Decay", "fx", "normalized", 0.0f, 1.0f, 0.35f, 0.0001f),
        boolParam("fx.chorus_enabled", "Chorus Enabled", "fx", false),
        floatParam("fx.chorus_mix", "Chorus Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("fx.chorus_rate_hz", "Chorus Rate", "fx", "Hz", 0.02f, 8.0f, 0.35f, 0.0001f, 0.35f),
        floatParam("fx.chorus_depth_ms", "Chorus Depth", "fx", "milliseconds", 0.1f, 24.0f, 5.0f, 0.01f, 0.35f),
        boolParam("fx.eq_enabled", "EQ Enabled", "fx", false),
        floatParam("fx.eq_low_gain_db", "EQ Low Gain", "fx", "dB", -12.0f, 12.0f, 0.0f, 0.01f),
        floatParam("fx.eq_high_gain_db", "EQ High Gain", "fx", "dB", -12.0f, 12.0f, 0.0f, 0.01f),
        boolParam("fx.compressor_enabled", "Compressor Enabled", "fx", false),
        floatParam("fx.compressor_threshold_db", "Compressor Threshold", "fx", "dB", -36.0f, 0.0f, -18.0f, 0.01f),
        floatParam("fx.compressor_ratio", "Compressor Ratio", "fx", "ratio", 1.0f, 100.0f, 2.0f, 0.01f),
        floatParam("fx.compressor_makeup_db", "Compressor Makeup", "fx", "dB", -12.0f, 12.0f, 0.0f, 0.01f),
        floatParam("fx.compressor_mix", "Compressor Mix", "fx", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),

        choiceParam("quality.realtime_mode", "Realtime Quality", "quality", { "Eco", "Normal", "High" }, 1),
        choiceParam("quality.offline_mode", "Offline Quality", "quality", { "Eco", "Normal", "High" }, 2),

        floatParam("macro.motion", "Motion", "macro", "normalized", 0.0f, 1.0f, 0.5f, 0.0001f),
        floatParam("macro.width", "Width", "macro", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("macro.drive", "Drive", "macro", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
        floatParam("macro.space", "Space", "macro", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f),
    };

    for (int step = 1; step <= lfoStepSlotCount; ++step)
        specs.push_back(floatParam("lfo.step." + std::to_string(step),
                                   "LFO Step " + std::to_string(step), "lfo", "normalized",
                                   -1.0f, 1.0f, defaultLfoStepValue(step - 1), 0.0001f));

    const std::vector<std::string> oscillatorWaveforms { "Saw", "Pulse", "Noise", "Sine", "Triangle", "SawTriangle", "HalfPulse", "QuarterPulse" };
    for (int layer = 1; layer <= layerCount; ++layer)
    {
        const auto layerPrefix = "layer." + std::to_string(layer) + ".";
        const auto layerLetter = std::string(1, static_cast<char>('A' + layer - 1));
        const auto layerName = "Layer " + layerLetter;
        const auto layerEnabled = true;
        specs.push_back(boolParam(layerPrefix + "enabled", layerName + " Enabled", "layer", layerEnabled));
        specs.push_back(floatParam(layerPrefix + "level_db", layerName + " Level", "layer", "dB",
                                   -48.0f, 12.0f, 0.0f, 0.01f, 1.0f, 5.0f));
        specs.push_back(floatParam(layerPrefix + "pan", layerName + " Pan", "layer", "normalized",
                                   -1.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f));
        specs.push_back(boolParam(layerPrefix + "solo", layerName + " Solo", "layer", false));
        specs.push_back(boolParam(layerPrefix + "mute", layerName + " Mute", "layer", false));

        for (int oscillator = 1; oscillator <= oscillatorSlotsPerLayer; ++oscillator)
        {
            const auto oscillatorPrefix = layerPrefix + "osc." + std::to_string(oscillator) + ".";
            const auto oscillatorName = layerName + " Osc " + std::to_string(oscillator);
            const auto primaryLayerAOscillator = layer == 1 && oscillator == 1;
            specs.push_back(boolParam(oscillatorPrefix + "enabled", oscillatorName + " Enabled",
                                      "layer_osc", true));
            specs.push_back(floatParam(oscillatorPrefix + "voices", oscillatorName + " Voices",
                                       "layer_osc", "voices", 0.0f, 8.0f,
                                       primaryLayerAOscillator ? 1.0f : 0.0f, 1.0f));
            specs.push_back(choiceParam(oscillatorPrefix + "waveform", oscillatorName + " Waveform",
                                        "layer_osc", oscillatorWaveforms, 0));
            specs.push_back(floatParam(oscillatorPrefix + "octave", oscillatorName + " Octave",
                                       "layer_osc", "octaves", -4.0f, 4.0f, 0.0f, 1.0f));
            specs.push_back(floatParam(oscillatorPrefix + "note", oscillatorName + " Note",
                                       "layer_osc", "semitones", -12.0f, 12.0f, 0.0f, 1.0f));
            specs.push_back(floatParam(oscillatorPrefix + "fine_cents", oscillatorName + " Fine",
                                       "layer_osc", "cents", -100.0f, 100.0f, 0.0f, 0.01f, 1.0f, 5.0f));
            specs.push_back(floatParam(oscillatorPrefix + "level", oscillatorName + " Level",
                                       "layer_osc", "normalized", 0.0f, 1.0f,
                                       1.0f, 0.0001f, 1.0f, 5.0f));
            specs.push_back(floatParam(oscillatorPrefix + "phase_degrees", oscillatorName + " Phase",
                                       "layer_osc", "degrees", 0.0f, 360.0f, 0.0f, 0.01f));
            specs.push_back(floatParam(oscillatorPrefix + "detune", oscillatorName + " Detune",
                                       "layer_osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 0.5f, 10.0f));
            specs.push_back(floatParam(oscillatorPrefix + "stereo", oscillatorName + " Stereo",
                                       "layer_osc", "normalized", 0.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f));
            specs.push_back(floatParam(oscillatorPrefix + "pan", oscillatorName + " Pan",
                                       "layer_osc", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f, 1.0f, 5.0f));
            specs.push_back(boolParam(oscillatorPrefix + "retrigger", oscillatorName + " Retrigger",
                                      "layer_osc", true));
            specs.push_back(boolParam(oscillatorPrefix + "invert", oscillatorName + " Invert",
                                      "layer_osc", false));
        }
    }

    for (int step = 1; step <= arpStepCount; ++step)
    {
        const auto prefix = "arp.step." + std::to_string(step) + ".";
        const auto stepName = "Arp Step " + std::to_string(step);
        specs.push_back(boolParam(prefix + "enabled", stepName + " Enabled", "arp_step", true));
        specs.push_back(floatParam(prefix + "pitch_semitones", stepName + " Pitch",
                                   "arp_step", "semitones", -24.0f, 24.0f, 0.0f, 1.0f));
        specs.push_back(floatParam(prefix + "velocity", stepName + " Velocity",
                                   "arp_step", "normalized", 0.0f, 1.0f, 1.0f, 0.0001f));
        specs.push_back(floatParam(prefix + "gate", stepName + " Gate",
                                   "arp_step", "normalized", 0.02f, 1.0f, 1.0f, 0.0001f));
        specs.push_back(boolParam(prefix + "tie", stepName + " Tie", "arp_step", false));
    }

    for (int voice = 1; voice <= chordVoiceCount; ++voice)
    {
        const auto prefix = "chord.voice." + std::to_string(voice) + ".";
        const auto voiceName = "Chord Voice " + std::to_string(voice);
        specs.push_back(boolParam(prefix + "enabled", voiceName + " Enabled", "chord_voice", voice == 1));
        specs.push_back(floatParam(prefix + "pitch_semitones", voiceName + " Pitch",
                                   "chord_voice", "semitones", -24.0f, 24.0f, 0.0f, 1.0f));
        specs.push_back(floatParam(prefix + "velocity", voiceName + " Velocity",
                                   "chord_voice", "normalized", 0.0f, 1.0f, 1.0f, 0.0001f));
    }

    for (auto& spec : specs)
    {
        if (isArpOrChordParameter(spec))
            spec.auVersionHint = 2;
        else if (isExpandedFxParameter(spec))
            spec.auVersionHint = 3;
    }

    const auto sources = sourceChoices();
    for (int slot = 1; slot <= 8; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot) + ".";
        specs.push_back(boolParam(prefix + "enabled", "TransMod " + std::to_string(slot) + " Enabled", "transmod", false));
        specs.push_back(choiceParam(prefix + "source", "TransMod " + std::to_string(slot) + " Source", "transmod", sources, 0));
        specs.push_back(choiceParam(prefix + "scaler", "TransMod " + std::to_string(slot) + " Scaler", "transmod", sources, 0));
        specs.push_back(floatParam(prefix + "depth", "TransMod " + std::to_string(slot) + " Legacy Cutoff Depth", "transmod", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f));
        specs.push_back(floatParam(prefix + "osc_pitch_semitones", "TransMod " + std::to_string(slot) + " Osc Pitch", "transmod", "semitones", -48.0f, 48.0f, 0.0f, 0.01f));
        specs.push_back(floatParam(prefix + "pulse_width", "TransMod " + std::to_string(slot) + " Pulse Width", "transmod", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f));
        specs.push_back(floatParam(prefix + "filter_cutoff_semitones", "TransMod " + std::to_string(slot) + " Filter Cutoff", "transmod", "semitones", -72.0f, 72.0f, 0.0f, 0.01f));
        specs.push_back(floatParam(prefix + "amp_level_db", "TransMod " + std::to_string(slot) + " Amp Level", "transmod", "dB", -24.0f, 24.0f, 0.0f, 0.01f));
        specs.push_back(floatParam(prefix + "pan", "TransMod " + std::to_string(slot) + " Pan", "transmod", "normalized", -1.0f, 1.0f, 0.0f, 0.0001f));
    }

    const std::array<int, 4> classicSources { 3, 20, 1, 21 };
    for (int slot = 1; slot <= 4; ++slot)
        for (auto& spec : specs)
            if (spec.id == "transmod." + std::to_string(slot) + ".source")
            {
                spec.defaultChoice = classicSources[static_cast<std::size_t>(slot - 1)];
                spec.defaultValue = static_cast<float>(spec.defaultChoice);
            }
    const auto firstNative = specs.size();
    specs.push_back(choiceParam("voice.portamento_mode", "Portamento Mode", "voice", { "Normal", "Slide" }, 0));
    specs.push_back(boolParam("global.sync", "Sync", "global", false));
    specs.push_back(floatParam("voice.pitch_bend_range", "Pitch Bend Range", "voice", "semitones", 0.0f, 24.0f, 2.0f, 1.0f));
    specs.push_back(floatParam("master.level_db", "Master Volume", "master", "dB", -48.0f, 12.0f, 0.0f, 0.01f, 1.0f, 5.0f));
    specs.push_back(floatParam("filter_control.cutoff_semitones", "Shared Cutoff", "filter_control", "semitones", -72.0f, 72.0f, 0.0f, 0.01f, 1.0f, 10.0f));
    specs.push_back(floatParam("filter_control.resonance", "Shared Resonance", "filter_control", "normalized", 0.0f, 1.0f, 0.0f));
    specs.push_back(floatParam("filter_control.keytrack", "Shared Keytrack", "filter_control", "normalized", 0.0f, 1.0f, 0.0f));
    specs.push_back(boolParam("filter_control.warm_drive", "Warm Drive", "filter_control", true));
    specs.push_back(floatParam("filter_control.drive", "Shared Drive", "filter_control", "normalized", 0.0f, 1.0f, 0.0f));
    const auto legacy = specs;
    auto cloneGroup = [&specs, &legacy](const std::string& from, const std::string& to, const std::string& label) {
        for (const auto& original : legacy)
            if (original.id.rfind(from, 0) == 0)
            {
                auto spec = original;
                spec.id = to + original.id.substr(from.size());
                spec.name = label + " " + original.name;
                if (spec.id.rfind("layer.", 0) == 0 && spec.id.ends_with(".filter.mode"))
                {
                    spec.choices.resize(6);
                    spec.maximum = 5.0f;
                }
                if (spec.id.rfind("layer.", 0) == 0 && spec.id.ends_with(".filter.keytrack")) spec.defaultValue = 0.0f;
                specs.push_back(std::move(spec));
            }
    };
    for (int layer = 1; layer <= layerCount; ++layer)
    {
        const auto prefix = "layer." + std::to_string(layer) + ".";
        const auto label = "Part " + std::string(1, static_cast<char>('A' + layer - 1));
        cloneGroup("filter.", prefix + "filter.", label);
        cloneGroup("amp_env.", prefix + "amp_env.", label);
        specs.push_back(choiceParam(prefix + "filter.input", label + " Input", "filter", { "None", "A", "B", "A+B" }, layer));
    }
    cloneGroup("mod_env.", "mod_env.2.", "Mod Env 2");
    cloneGroup("lfo.", "lfo.2.", "LFO 2");
    for (const auto& prefix : { std::string("lfo."), std::string("lfo.2.") })
    {
        specs.push_back(floatParam(prefix + "gain", (prefix == "lfo." ? "LFO 1 Gain" : "LFO 2 Gain"), "lfo", "normalized", 0.0f, 1.0f, 1.0f));
        specs.push_back(floatParam(prefix + "offset", (prefix == "lfo." ? "LFO 1 Offset" : "LFO 2 Offset"), "lfo", "normalized", -1.0f, 1.0f, 0.0f));
        specs.push_back(boolParam(prefix + "free", (prefix == "lfo." ? "LFO 1 Free" : "LFO 2 Free"), "lfo", false));
    }
    std::vector<std::string> destinations { "None" };
    for (const auto& destination : modulationDestinationCatalog())
        destinations.push_back(destination.label);
    for (int slot = 1; slot <= transModSlotCount; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot) + ".";
        for (const auto& destination : modulationDestinationCatalog())
            if (destination.nativeIndex >= 0)
                specs.push_back(floatParam(prefix + destination.depthSuffix, "Mod " + std::to_string(slot) + " " + destination.label, "transmod", destination.unit, destination.minimumDepth, destination.maximumDepth, 0.0f));
        for (int route = 1; route <= 2; ++route)
        {
            const auto routePrefix = prefix + "route." + std::to_string(route) + ".";
            specs.push_back(choiceParam(routePrefix + "destination", "Mod " + std::to_string(slot) + " Destination " + std::to_string(route), "transmod", destinations, 0));
            specs.push_back(floatParam(routePrefix + "amount", "Mod " + std::to_string(slot) + " Amount " + std::to_string(route), "transmod", "normalized", -1.0f, 1.0f, 0.0f));
        }
    }
    specs.push_back(choiceParam("arp.velocity_mode", "Arp Velocity", "arp", { "Step", "Key", "Hold", "StepKey", "StepHold" }, 3));
    specs.push_back(floatParam("arp.wrap", "Arp Wrap", "arp", "steps", 0.0f, 128.0f, 0.0f, 1.0f));
    specs.push_back(floatParam("arp.time_ms", "Arp Time", "arp", "milliseconds", 1.0f, 6000.0f, 125.0f, 0.01f, 0.35f));
    specs.push_back(floatParam("fx.phaser_center_hz", "Phaser Center Hz", "fx", "Hz", 20.0f, 20000.0f, 1000.0f));
    specs.push_back(floatParam("fx.phaser_spread", "Phaser Spread", "fx", "normalized", 0.0f, 1.0f, 0.5f));
    specs.push_back(floatParam("fx.phaser_lr_offset", "Phaser Lr Offset", "fx", "normalized", 0.0f, 1.0f, 0.25f));
    specs.push_back(floatParam("fx.phaser_width", "Phaser Width", "fx", "normalized", 0.0f, 1.0f, 1.0f));
    specs.push_back(floatParam("fx.chorus_delay_ms", "Chorus Delay Ms", "fx", "milliseconds", 1.0f, 50.0f, 11.0f));
    specs.push_back(floatParam("fx.chorus_feedback", "Chorus Feedback", "fx", "normalized", -0.95f, 0.95f, 0.0f));
    specs.push_back(floatParam("fx.chorus_width", "Chorus Width", "fx", "normalized", 0.0f, 1.0f, 1.0f));
    specs.push_back(floatParam("fx.eq_low_frequency_hz", "Eq Low Frequency Hz", "fx", "Hz", 20.0f, 2000.0f, 160.0f));
    specs.push_back(floatParam("fx.eq_high_frequency_hz", "Eq High Frequency Hz", "fx", "Hz", 1000.0f, 20000.0f, 6000.0f));
    specs.push_back(floatParam("fx.delay_time_left_ms", "Delay Time Left Ms", "fx", "milliseconds", 1.0f, 6000.0f, 250.0f));
    specs.push_back(floatParam("fx.delay_time_right_ms", "Delay Time Right Ms", "fx", "milliseconds", 1.0f, 6000.0f, 250.0f));
    specs.push_back(floatParam("fx.delay_spread", "Delay Spread", "fx", "normalized", 0.0f, 1.0f, 1.0f));
    specs.push_back(floatParam("fx.delay_width", "Delay Width", "fx", "normalized", 0.0f, 1.0f, 1.0f));
    specs.push_back(floatParam("fx.delay_low_cut_hz", "Delay Low Cut Hz", "fx", "Hz", 20.0f, 2000.0f, 20.0f));
    specs.push_back(floatParam("fx.delay_high_cut_hz", "Delay High Cut Hz", "fx", "Hz", 200.0f, 20000.0f, 20000.0f));
    specs.push_back(floatParam("fx.delay_smear", "Delay Smear", "fx", "normalized", 0.0f, 1.0f, 0.0f));
    specs.push_back(floatParam("fx.reverb_pre_delay_ms", "Reverb Pre Delay Ms", "fx", "milliseconds", 0.0f, 500.0f, 0.0f));
    specs.push_back(floatParam("fx.reverb_damp", "Reverb Damp", "fx", "normalized", 0.0f, 1.0f, 0.48f));
    specs.push_back(floatParam("fx.reverb_width", "Reverb Width", "fx", "normalized", 0.0f, 1.0f, 1.0f));
    specs.push_back(floatParam("fx.compressor_attack_ms", "Compressor Attack Ms", "fx", "milliseconds", 0.1f, 500.0f, 4.0f));
    specs.push_back(floatParam("fx.compressor_release_ms", "Compressor Release Ms", "fx", "milliseconds", 1.0f, 2000.0f, 80.0f));
    specs.push_back(boolParam("fx.chorus_dual_mode", "Chorus Dual Mode", "fx", false));
    specs.push_back(boolParam("fx.delay_ping_pong", "Delay Ping Pong", "fx", true));
    specs.push_back(choiceParam("fx.delay_right_sync_division", "Delay Right Sync", "fx", { "1/16", "1/8", "1/8D", "1/4", "1/2", "1/32", "1/32T", "1/32D", "1/16T", "1/16D", "1/8T", "1/4T", "1/4D", "1/2T", "1/2D", "1 bar", "1 barT", "1 barD" }, 1));
    for (const auto& effect : { std::string("phaser"), std::string("chorus") })
        specs.push_back(choiceParam("fx." + effect + "_sync_division", effect + " Sync", "fx", { "1/16", "1/8", "1/8D", "1/4", "1/2", "1/32", "1/32T", "1/32D", "1/16T", "1/16D", "1/8T", "1/4T", "1/4D", "1/2T", "1/2D", "1 bar", "1 barT", "1 barD" }, 15));
    for (auto index = firstNative; index < specs.size(); ++index)
        specs[index].auVersionHint = 4;
    specs.erase(std::remove_if(specs.begin(), specs.end(), [](const auto& spec) {
                    return spec.id.rfind("osc.", 0) == 0 || spec.id.rfind("filter.", 0) == 0
                        || spec.id.rfind("amp.", 0) == 0 || spec.id.rfind("quality.", 0) == 0 || spec.id == "voice.retrigger"
                        || spec.id == "lfo.rate_mode" || spec.id == "lfo.2.rate_mode"
                        || spec.id == "lfo.swing" || spec.id == "lfo.2.swing"
                        || (spec.id.rfind("layer.", 0) == 0 && spec.id.ends_with(".enabled") && spec.id.find(".filter.") == std::string::npos)
                        || spec.id.rfind("amp_env.", 0) == 0 || spec.id.rfind("direct.pulse_", 0) == 0
                        || (spec.id.rfind("transmod.", 0) == 0 && spec.id.ends_with(".pulse_width"));
                }),
                specs.end());
    return specs;
}
} // namespace

const std::vector<ParameterSpec>& getParameterSpecs()
{
    static const auto specs = buildSpecs();
    return specs;
}

const ParameterSpec* findParameterSpec(const std::string& id)
{
    static const auto index = [] {
        std::unordered_map<std::string_view, const ParameterSpec*> values;
        values.reserve(getParameterSpecs().size());
        for (const auto& spec : getParameterSpecs())
            values.emplace(spec.id, &spec);
        return values;
    }();
    const auto found = index.find(id);
    return found == index.end() ? nullptr : found->second;
}

float clampPhysicalParameterValue(const ParameterSpec& spec, float value) noexcept
{
    if (!std::isfinite(value))
        return value;

    switch (spec.kind)
    {
        case ParameterKind::Bool:
            return value >= 0.5f ? 1.0f : 0.0f;

        case ParameterKind::Choice: {
            const auto maxChoice = std::max(0, static_cast<int>(spec.choices.size()) - 1);
            return static_cast<float>(std::clamp(static_cast<int>(std::round(value)), 0, maxChoice));
        }

        case ParameterKind::Float:
            return std::clamp(value, spec.minimum, spec.maximum);
    }

    return spec.defaultValue;
}

std::vector<std::string> validateParameterSpecs()
{
    std::vector<std::string> errors;
    std::set<std::string> ids;

    for (const auto& spec : getParameterSpecs())
    {
        if (spec.id.empty())
            errors.push_back("empty parameter id");

        if (!ids.insert(spec.id).second)
            errors.push_back("duplicate parameter id: " + spec.id);

        if (spec.name.empty())
            errors.push_back("empty parameter name: " + spec.id);

        if (spec.group.empty())
            errors.push_back("empty parameter group: " + spec.id);

        if (spec.kind == ParameterKind::Float)
        {
            if (spec.minimum >= spec.maximum)
                errors.push_back("invalid range: " + spec.id);

            if (spec.defaultValue < spec.minimum || spec.defaultValue > spec.maximum)
                errors.push_back("default out of range: " + spec.id);
        }

        if (spec.kind == ParameterKind::Choice)
        {
            if (spec.choices.empty())
                errors.push_back("choice parameter without choices: " + spec.id);

            if (spec.defaultChoice < 0 || spec.defaultChoice >= static_cast<int>(spec.choices.size()))
                errors.push_back("choice default out of range: " + spec.id);
        }

        if (spec.auVersionHint < 1)
            errors.push_back("invalid AU version hint: " + spec.id);
    }

    return errors;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;

    for (const auto& spec : getParameterSpecs())
    {
        const juce::ParameterID parameterId { juce::String(spec.id), spec.auVersionHint };
        const juce::String name { spec.name };

        if (spec.kind == ParameterKind::Float)
        {
            juce::NormalisableRange<float> range { spec.minimum, spec.maximum, spec.interval, spec.skew };
            parameters.push_back(std::make_unique<juce::AudioParameterFloat>(parameterId, name, range, spec.defaultValue));
        }
        else if (spec.kind == ParameterKind::Bool)
        {
            parameters.push_back(std::make_unique<juce::AudioParameterBool>(parameterId, name, spec.defaultValue >= 0.5f));
        }
        else
        {
            juce::StringArray choices;
            for (const auto& choice : spec.choices)
                choices.add(choice);

            parameters.push_back(std::make_unique<juce::AudioParameterChoice>(parameterId, name, choices, spec.defaultChoice));
        }
    }

    return { parameters.begin(), parameters.end() };
}

const char* toString(ParameterKind kind) noexcept
{
    switch (kind)
    {
        case ParameterKind::Float: return "float";
        case ParameterKind::Bool: return "bool";
        case ParameterKind::Choice: return "choice";
    }

    return "unknown";
}
} // namespace synth
