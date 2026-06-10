#include "../../src/plugin/ParameterRegistry.h"
#include "../../src/dsp/SynthParameters.h"
#include "../../src/midi/MidiControllerMap.h"
#include "../../src/modulation/ModulationRouteModel.h"
#include "../../src/presets/PresetManager.h"
#include "../../src/presets/PresetValidator.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace
{
class StateRoundTripProcessor final : public juce::AudioProcessor
{
public:
    StateRoundTripProcessor()
        : parameters(*this, nullptr, "SYNTHIA_STATE", synth::createParameterLayout())
    {
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "StateRoundTripProcessor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState parameters;
};

bool parameterValueMatches(const juce::AudioProcessorValueTreeState& parameters,
                           const char* id,
                           float expected,
                           float tolerance = 0.001f);

bool setPhysicalParameter(juce::AudioProcessorValueTreeState& parameters, const char* id, float value);

bool checkAutomationParameterExposure()
{
    StateRoundTripProcessor processor;
    auto exposedCount = 0;
    auto automatableCount = 0;

    for (const auto& spec : synth::getParameterSpecs())
    {
        auto* parameter = processor.parameters.getParameter(spec.id);
        if (parameter == nullptr)
        {
            std::cerr << "APVTS omitted registry parameter: " << spec.id << "\n";
            return false;
        }

        ++exposedCount;
        if (spec.automatable)
        {
            ++automatableCount;
            if (!parameter->isAutomatable())
            {
                std::cerr << "Parameter is not host-automatable: " << spec.id << "\n";
                return false;
            }
        }

        if ((spec.kind == synth::ParameterKind::Float && dynamic_cast<juce::AudioParameterFloat*>(parameter) == nullptr)
            || (spec.kind == synth::ParameterKind::Bool && dynamic_cast<juce::AudioParameterBool*>(parameter) == nullptr)
            || (spec.kind == synth::ParameterKind::Choice && dynamic_cast<juce::AudioParameterChoice*>(parameter) == nullptr))
        {
            std::cerr << "APVTS parameter type mismatch: " << spec.id << "\n";
            return false;
        }

        if (!setPhysicalParameter(processor.parameters, spec.id.c_str(), spec.defaultValue)
            || !parameterValueMatches(processor.parameters, spec.id.c_str(),
                                      synth::clampPhysicalParameterValue(spec, spec.defaultValue), 0.001f))
        {
            std::cerr << "Host-notifying default write failed for parameter: " << spec.id << "\n";
            return false;
        }
    }

    if (exposedCount != static_cast<int>(synth::getParameterSpecs().size()) || automatableCount < 200)
    {
        std::cerr << "Unexpected automation exposure count: exposed " << exposedCount
                  << ", automatable " << automatableCount << "\n";
        return false;
    }

    return true;
}

bool checkStateRoundTrip()
{
    StateRoundTripProcessor source;
    if (!setPhysicalParameter(source.parameters, "layer.2.enabled", 1.0f)
        || !setPhysicalParameter(source.parameters, "layer.2.osc.2.voices", 6.0f)
        || !setPhysicalParameter(source.parameters, "layer.2.osc.2.waveform", 3.0f)
        || !setPhysicalParameter(source.parameters, "layer.1.osc.2.invert", 1.0f)
        || !setPhysicalParameter(source.parameters, "arp.enabled", 1.0f)
        || !setPhysicalParameter(source.parameters, "arp.step.2.pitch_semitones", 7.0f)
        || !setPhysicalParameter(source.parameters, "chord.enabled", 1.0f)
        || !setPhysicalParameter(source.parameters, "chord.voice.2.pitch_semitones", 4.0f))
    {
        return false;
    }

    auto state = source.parameters.copyState();
    state.setProperty("schema_version", 1, nullptr);
    state.setProperty("plugin_version", "0.1.0-test", nullptr);

    const auto xml = state.createXml();
    if (xml == nullptr)
        return false;

    const auto restoredState = juce::ValueTree::fromXml(*xml);
    if (!restoredState.isValid() || !restoredState.hasType("SYNTHIA_STATE"))
        return false;

    StateRoundTripProcessor destination;
    destination.parameters.replaceState(restoredState);

    return destination.parameters.state.hasProperty("schema_version")
        && destination.parameters.getRawParameterValue("filter.cutoff_semitones") != nullptr
        && parameterValueMatches(destination.parameters, "layer.2.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "layer.2.osc.2.voices", 6.0f)
        && parameterValueMatches(destination.parameters, "layer.2.osc.2.waveform", 3.0f)
        && parameterValueMatches(destination.parameters, "layer.1.osc.2.invert", 1.0f)
        && parameterValueMatches(destination.parameters, "arp.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "arp.step.2.pitch_semitones", 7.0f)
        && parameterValueMatches(destination.parameters, "chord.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "chord.voice.2.pitch_semitones", 4.0f);
}

bool parameterValueMatches(const juce::AudioProcessorValueTreeState& parameters,
                           const char* id,
                           float expected,
                           float tolerance)
{
    const auto* value = parameters.getRawParameterValue(id);
    return value != nullptr && std::abs(value->load() - expected) <= tolerance;
}

bool setPhysicalParameter(juce::AudioProcessorValueTreeState& parameters, const char* id, float value)
{
    auto* parameter = parameters.getParameter(id);
    if (parameter == nullptr)
        return false;

    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    return true;
}

float stateParameterValue(const juce::ValueTree& state, const char* id, float fallback = -9999.0f)
{
    const auto parameterId = juce::String(id);
    for (const auto& child : state)
    {
        if (child.hasType("PARAM") && child.getProperty("id").toString() == parameterId)
            return static_cast<float>(static_cast<double>(child.getProperty("value", fallback)));
    }

    return fallback;
}

bool setStateParameterValue(juce::ValueTree& state, const char* id, float value)
{
    const auto parameterId = juce::String(id);
    for (auto child : state)
    {
        if (child.hasType("PARAM") && child.getProperty("id").toString() == parameterId)
        {
            child.setProperty("value", value, nullptr);
            return true;
        }
    }

    return false;
}

bool containsString(const std::vector<std::string>& values, const std::string& expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

bool editValueMatches(const synth::ModulationRouteWriteResult& write,
                      const std::string& parameterId,
                      float expected,
                      float tolerance = 0.001f)
{
    return std::any_of(write.edits.begin(), write.edits.end(), [&](const auto& edit) {
        return edit.parameterId == parameterId && std::abs(edit.value - expected) <= tolerance;
    });
}

bool applyParameterEdits(juce::AudioProcessorValueTreeState& parameters,
                         const std::vector<synth::ModulationRouteParameterEdit>& edits)
{
    for (const auto& edit : edits)
    {
        const auto* spec = synth::findParameterSpec(edit.parameterId);
        if (spec == nullptr || !std::isfinite(edit.value))
            return false;

        const auto value = synth::clampPhysicalParameterValue(*spec, edit.value);
        if (!setPhysicalParameter(parameters, edit.parameterId.c_str(), value))
            return false;
    }

    return true;
}

bool checkModulationRouteModel()
{
    const auto& sources = synth::modulationSourceCatalog();
    const auto& destinations = synth::modulationDestinationCatalog();

    const auto* lfoSource = synth::findModulationSourceInfo(synth::ModSource::Lfo);
    const auto* macroSource = synth::findModulationSourceInfo("macro.motion");
    const auto* filterDestination = synth::findModulationDestinationInfo("filter.cutoff");
    if (sources.size() != 20
        || destinations.size() != 5
        || lfoSource == nullptr
        || lfoSource->id != "lfo.1"
        || lfoSource->polarity != synth::ModulationPolarity::Bipolar
        || lfoSource->scope != synth::ModulationScope::Voice
        || lfoSource->updateRate != synth::ModulationUpdateRate::Audio
        || macroSource == nullptr
        || macroSource->source != synth::ModSource::Macro1
        || filterDestination == nullptr
        || filterDestination->targetParameterId != "filter.cutoff_semitones"
        || synth::transModDepthParameterId(1, *filterDestination) != "transmod.1.filter_cutoff_semitones"
        || !synth::transModDepthParameterId(0, *filterDestination).empty())
    {
        std::cerr << "Modulation source/destination catalog mismatch.\n";
        return false;
    }

    synth::SynthParameters parameters;
    auto& slotOne = parameters.transMod.slots[0];
    slotOne.enabled = true;
    slotOne.source = synth::ModSource::Lfo;
    slotOne.scaler = synth::ModSource::Macro1;
    slotOne.oscPitchSemitones = 7.0f;
    slotOne.filterCutoffSemitones = 12.0f;
    slotOne.pan = -0.25f;

    auto& slotTwo = parameters.transMod.slots[1];
    slotTwo.enabled = true;
    slotTwo.source = synth::ModSource::Ramp;
    slotTwo.depth = 0.5f; // legacy normalized cutoff depth

    auto& cancellingLegacySlot = parameters.transMod.slots[2];
    cancellingLegacySlot.enabled = true;
    cancellingLegacySlot.source = synth::ModSource::Macro2;
    cancellingLegacySlot.filterCutoffSemitones = 72.0f;
    cancellingLegacySlot.depth = -1.0f;

    auto& disabledSlot = parameters.transMod.slots[3];
    disabledSlot.enabled = false;
    disabledSlot.source = synth::ModSource::ModWheel;
    disabledSlot.ampLevelDb = 6.0f;

    const auto view = synth::buildModulationRouteView(parameters.transMod);
    auto findRoute = [&view](int slotNumber, const std::string& destinationId) {
        return std::find_if(view.activeRoutes.begin(), view.activeRoutes.end(),
                            [slotNumber, &destinationId](const auto& route) {
                                return route.slotNumber == slotNumber && route.destinationId == destinationId;
                            });
    };

    const auto slotOneOscRoute = findRoute(1, "osc.pitch");
    const auto slotOneFilterRoute = findRoute(1, "filter.cutoff");
    const auto slotTwoFilterRoute = findRoute(2, "filter.cutoff");
    const auto cancellingLegacyRoute = findRoute(3, "filter.cutoff");
    if (view.slots.size() != synth::transModSlotCount
        || view.activeRoutes.size() != 5
        || view.slots[0].routes.size() != 3
        || view.slots[2].routes.size() != 1
        || !view.slots[3].routes.empty()
        || slotOneOscRoute == view.activeRoutes.end()
        || slotOneOscRoute->sourceId != "lfo.1"
        || slotOneOscRoute->scalerId != "macro.motion"
        || slotOneOscRoute->depthParameterId != "transmod.1.osc_pitch_semitones"
        || std::abs(slotOneOscRoute->depth - 7.0f) > 0.0001f
        || slotOneFilterRoute == view.activeRoutes.end()
        || slotOneFilterRoute->depthParameterId != "transmod.1.filter_cutoff_semitones"
        || containsString(slotOneFilterRoute->depthParameterIds, "transmod.1.depth")
        || slotTwoFilterRoute == view.activeRoutes.end()
        || slotTwoFilterRoute->depthParameterId != "transmod.2.depth"
        || !containsString(slotTwoFilterRoute->depthParameterIds, "transmod.2.depth")
        || std::abs(slotTwoFilterRoute->depth - 36.0f) > 0.0001f
        || cancellingLegacyRoute == view.activeRoutes.end()
        || cancellingLegacyRoute->depthParameterId != "transmod.3.filter_cutoff_semitones"
        || !containsString(cancellingLegacyRoute->depthParameterIds, "transmod.3.depth")
        || std::abs(cancellingLegacyRoute->depth) > 0.0001f)
    {
        std::cerr << "Modulation route view did not reflect TransMod slots correctly.\n";
        return false;
    }

    return true;
}

bool checkModulationRouteWriteAdapter()
{
    const auto write = synth::buildModulationRouteWrite({
        3,
        "lfo.1",
        "macro.motion",
        "filter.cutoff",
        96.0f
    });

    if (!write.ok
        || write.edits.size() != 10
        || !editValueMatches(write, "transmod.3.enabled", 1.0f)
        || !editValueMatches(write, "transmod.3.source", static_cast<float>(static_cast<int>(synth::ModSource::Lfo)))
        || !editValueMatches(write, "transmod.3.scaler", static_cast<float>(static_cast<int>(synth::ModSource::Macro1)))
        || !editValueMatches(write, "transmod.3.depth", 0.0f)
        || !editValueMatches(write, "transmod.3.osc_pitch_semitones", 0.0f)
        || !editValueMatches(write, "transmod.3.filter_cutoff_semitones", 72.0f))
    {
        std::cerr << "Modulation route write did not compile expected parameter edits.\n";
        return false;
    }

    StateRoundTripProcessor processor;
    if (!setPhysicalParameter(processor.parameters, "transmod.3.osc_pitch_semitones", 12.0f)
        || !setPhysicalParameter(processor.parameters, "transmod.3.pan", 0.5f)
        || !applyParameterEdits(processor.parameters, write.edits)
        || !parameterValueMatches(processor.parameters, "transmod.3.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.source", 1.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.scaler", 16.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.osc_pitch_semitones", 0.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.filter_cutoff_semitones", 72.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.pan", 0.0f))
    {
        std::cerr << "Modulation route write edits did not apply to APVTS state.\n";
        return false;
    }

    const auto panWrite = synth::buildModulationRouteWrite({
        4,
        "random_on_note",
        "none",
        "amp.pan",
        -0.25f
    });
    if (!panWrite.ok
        || !editValueMatches(panWrite, "transmod.4.source", static_cast<float>(static_cast<int>(synth::ModSource::RandomOnNote)))
        || !editValueMatches(panWrite, "transmod.4.scaler", 0.0f)
        || !editValueMatches(panWrite, "transmod.4.pan", -0.25f))
    {
        std::cerr << "Modulation route write did not handle none scaler or pan destination.\n";
        return false;
    }

    const auto* sourceSpec = synth::findParameterSpec("transmod.4.source");
    const auto* scalerSpec = synth::findParameterSpec("transmod.4.scaler");
    const auto randomOnNoteValue = static_cast<float>(static_cast<int>(synth::ModSource::RandomOnNote));
    const auto macroMotionValue = static_cast<float>(static_cast<int>(synth::ModSource::Macro1));
    if (sourceSpec == nullptr
        || scalerSpec == nullptr
        || std::abs(synth::clampPhysicalParameterValue(*sourceSpec, randomOnNoteValue) - randomOnNoteValue) > 0.001f
        || std::abs(synth::clampPhysicalParameterValue(*scalerSpec, macroMotionValue) - macroMotionValue) > 0.001f
        || std::abs(synth::clampPhysicalParameterValue(*sourceSpec, 999.0f) - 19.0f) > 0.001f
        || std::abs(synth::clampPhysicalParameterValue(*sourceSpec, -2.0f)) > 0.001f)
    {
        std::cerr << "Choice-valued modulation parameters did not preserve valid source/scaler indices.\n";
        return false;
    }

    StateRoundTripProcessor panProcessor;
    if (!applyParameterEdits(panProcessor.parameters, panWrite.edits)
        || !parameterValueMatches(panProcessor.parameters, "transmod.4.source", randomOnNoteValue)
        || !parameterValueMatches(panProcessor.parameters, "transmod.4.scaler", 0.0f)
        || !parameterValueMatches(panProcessor.parameters, "transmod.4.pan", -0.25f))
    {
        std::cerr << "Applied modulation route edits did not preserve choice-valued source/scaler indices.\n";
        return false;
    }

    const auto clear = synth::buildModulationSlotClear(3);
    if (!clear.ok
        || clear.edits.size() != 9
        || !applyParameterEdits(processor.parameters, clear.edits)
        || !parameterValueMatches(processor.parameters, "transmod.3.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.source", 0.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.scaler", 0.0f)
        || !parameterValueMatches(processor.parameters, "transmod.3.filter_cutoff_semitones", 0.0f))
    {
        std::cerr << "Modulation slot clear did not compile/apply expected edits.\n";
        return false;
    }

    if (synth::buildModulationRouteWrite({ 0, "lfo.1", "none", "filter.cutoff", 1.0f }).ok
        || synth::buildModulationRouteWrite({ 1, "none", "none", "filter.cutoff", 1.0f }).ok
        || synth::buildModulationRouteWrite({ 1, "lfo.1", "bogus", "filter.cutoff", 1.0f }).ok
        || synth::buildModulationRouteWrite({ 1, "lfo.1", "none", "missing.destination", 1.0f }).ok
        || synth::buildModulationRouteWrite({ 1, "lfo.1", "none", "filter.cutoff", 0.0f }).ok
        || synth::buildModulationSlotClear(9).ok)
    {
        std::cerr << "Modulation route write accepted invalid input.\n";
        return false;
    }

    return true;
}

void removeParameterChildrenStartingWith(juce::ValueTree& state, const char* prefix)
{
    for (auto childIndex = state.getNumChildren(); --childIndex >= 0;)
    {
        const auto child = state.getChild(childIndex);
        if (!child.hasType("PARAM"))
            continue;

        if (child.getProperty("id").toString().startsWith(prefix))
            state.removeChild(childIndex, nullptr);
    }
}

bool checkHostStateDefaultMerge()
{
    StateRoundTripProcessor source;
    if (!setPhysicalParameter(source.parameters, "filter.cutoff_semitones", 58.0f))
        return false;

    auto oldState = source.parameters.copyState();
    oldState.setProperty("schema_version", 1, nullptr);
    oldState.setProperty("current_preset", "Old Host State", nullptr);
    removeParameterChildrenStartingWith(oldState, "layer.");
    removeParameterChildrenStartingWith(oldState, "arp.");
    removeParameterChildrenStartingWith(oldState, "chord.");
    removeParameterChildrenStartingWith(oldState, "fx.distortion_");
    removeParameterChildrenStartingWith(oldState, "fx.phaser_");
    removeParameterChildrenStartingWith(oldState, "fx.eq_");
    removeParameterChildrenStartingWith(oldState, "fx.compressor_");

    StateRoundTripProcessor destination;
    if (!setPhysicalParameter(destination.parameters, "layer.1.enabled", 0.0f)
        || !setPhysicalParameter(destination.parameters, "layer.2.enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "layer.2.osc.2.voices", 8.0f)
        || !setPhysicalParameter(destination.parameters, "layer.2.osc.2.invert", 1.0f)
        || !setPhysicalParameter(destination.parameters, "arp.enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "arp.step.1.tie", 1.0f)
        || !setPhysicalParameter(destination.parameters, "chord.enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "chord.voice.3.enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "fx.distortion_mode", 2.0f)
        || !setPhysicalParameter(destination.parameters, "fx.phaser_enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "fx.phaser_mix", 1.0f)
        || !setPhysicalParameter(destination.parameters, "fx.eq_enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "fx.eq_low_gain_db", 6.0f)
        || !setPhysicalParameter(destination.parameters, "fx.compressor_enabled", 1.0f)
        || !setPhysicalParameter(destination.parameters, "fx.compressor_mix", 1.0f))
    {
        return false;
    }

    const auto migratedState = synth::mergeParameterStateWithDefaults(destination.parameters, oldState);
    destination.parameters.replaceState(migratedState);

    return parameterValueMatches(destination.parameters, "filter.cutoff_semitones", 58.0f)
        && parameterValueMatches(destination.parameters, "layer.1.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "layer.2.enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "layer.1.osc.1.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "layer.1.osc.1.voices", 1.0f)
        && parameterValueMatches(destination.parameters, "layer.2.osc.2.voices", 0.0f)
        && parameterValueMatches(destination.parameters, "layer.2.osc.2.invert", 0.0f)
        && parameterValueMatches(destination.parameters, "arp.enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "arp.step.1.tie", 0.0f)
        && parameterValueMatches(destination.parameters, "chord.enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "chord.voice.1.enabled", 1.0f)
        && parameterValueMatches(destination.parameters, "chord.voice.3.enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.distortion_mode", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.phaser_enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.phaser_mix", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.eq_enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.eq_low_gain_db", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.compressor_enabled", 0.0f)
        && parameterValueMatches(destination.parameters, "fx.compressor_mix", 0.0f);
}

bool checkLayerOscillatorVoiceCost()
{
    synth::SynthParameters parameters;
    if (synth::layerOscillatorVoiceCost(parameters) != 1)
    {
        std::cerr << "Default layer voice cost should be one enabled Layer A oscillator.\n";
        return false;
    }

    parameters.layers[0].oscillators[1].enabled = true;
    parameters.layers[0].oscillators[1].voices = 3;
    parameters.layers[0].oscillators[1].level = 1.0f;
    parameters.layers[1].enabled = true;
    parameters.layers[1].oscillators[0].enabled = true;
    parameters.layers[1].oscillators[0].voices = 8;
    parameters.layers[1].oscillators[0].level = 1.0f;
    if (synth::layerOscillatorVoiceCost(parameters) != 12)
    {
        std::cerr << "Layer voice cost did not include all active unsoloed slots.\n";
        return false;
    }

    parameters.layers[0].solo = true;
    if (synth::layerOscillatorVoiceCost(parameters) != 4)
    {
        std::cerr << "Layer solo should isolate the soloed layer voice cost.\n";
        return false;
    }

    parameters.layers[0].mute = true;
    if (synth::layerOscillatorVoiceCost(parameters) != 0)
    {
        std::cerr << "Muted solo layer should have zero voice cost.\n";
        return false;
    }

    return true;
}

bool checkPatchCostEstimate()
{
    synth::SynthParameters parameters;
    const auto init = synth::estimatePatchCost(parameters);
    // Init defaults render the filter without oversampling (0457fe4 tuned the
    // default quality for realtime cost), so the filter multiplier is 1.0.
    if (init.noteLimit != 8
        || init.unisonVoices != 1
        || init.maxActiveVoices != 8
        || init.oscillatorSlotVoices != 1
        || init.voiceUnits != 8
        || init.filterOversampling != 1
        || init.activeFxModules != 0
        || std::abs(init.totalUnits - 8.0f) > 0.001f
        || init.loadPercent != 3
        || init.elevated
        || init.high
        || init.overBudget)
    {
        std::cerr << "Default patch cost estimate did not match init voice math.\n";
        return false;
    }

    parameters.layers[0].oscillators[1].enabled = true;
    parameters.layers[0].oscillators[1].voices = 3;
    parameters.layers[0].oscillators[1].level = 0.0f;
    if (synth::estimatePatchCost(parameters).oscillatorSlotVoices != 1)
    {
        std::cerr << "Patch cost should ignore zero-level oscillator slots.\n";
        return false;
    }

    parameters.osc.stackCount = 5;
    const auto stackedCore = synth::estimatePatchCost(parameters);
    if (stackedCore.oscillatorSlotVoices != 5
        || stackedCore.voiceUnits != 40
        || stackedCore.loadPercent != 17)
    {
        std::cerr << "Patch cost should include live A1 core oscillator stack count.\n";
        return false;
    }

    parameters.layers[0].oscillators[1].level = 1.0f;
    parameters.layers[1].enabled = true;
    parameters.layers[1].oscillators[0].enabled = true;
    parameters.layers[1].oscillators[0].voices = 8;
    parameters.layers[1].oscillators[0].level = 0.75f;
    parameters.osc.stackCount = 1;
    parameters.voiceMode = synth::VoiceMode::Poly;
    parameters.polyphony = 32;
    parameters.unisonCount = 8;
    parameters.filter.oversampling = 3;
    parameters.fx.enabled = true;
    parameters.fx.phaserEnabled = true;
    parameters.fx.chorusEnabled = true;
    parameters.fx.eqEnabled = true;
    parameters.fx.compressorEnabled = true;

    const auto large = synth::estimatePatchCost(parameters);
    if (large.noteLimit != 32
        || large.unisonVoices != 8
        || large.maxActiveVoices != 32
        || large.oscillatorSlotVoices != 12
        || large.voiceUnits != 384
        || large.filterOversampling != 8
        || large.activeFxModules != 7
        || !large.elevated
        || !large.high
        || !large.overBudget
        || large.loadPercent <= 100)
    {
        std::cerr << "High-cost patch estimate did not include voice, filter, and FX factors.\n";
        return false;
    }

    parameters.voiceMode = synth::VoiceMode::MonoLegato;
    parameters.unisonCount = 8;
    const auto mono = synth::estimatePatchCost(parameters);
    if (mono.noteLimit != 1 || mono.unisonVoices != 1 || mono.maxActiveVoices != 1)
    {
        std::cerr << "Mono patch cost should not multiply by polyphony or unison.\n";
        return false;
    }

    parameters.voiceMode = synth::VoiceMode::Unison;
    const auto unison = synth::estimatePatchCost(parameters);
    if (unison.noteLimit != 1 || unison.unisonVoices != 8 || unison.maxActiveVoices != 8)
    {
        std::cerr << "Unison patch cost should use one note multiplied by unison voices.\n";
        return false;
    }

    parameters.layers[0].solo = true;
    parameters.layers[0].mute = true;
    if (synth::estimatePatchCost(parameters).oscillatorSlotVoices != 0)
    {
        std::cerr << "Patch cost should follow solo/mute layer voice math.\n";
        return false;
    }

    return true;
}

bool checkMidiControllerMap()
{
    auto normalized = synth::normalizeMidiControllerAssignments({
        { 74, "filter.cutoff_semitones" },
        { -1, "macro.motion" },
        { 7, "" },
        { 74, "amp.level_db" },
        { 1, "amp.level_db" },
        { 91, "fx.reverb_mix" }
    });

    if (normalized.size() != 2
        || normalized[0].controllerNumber != 1
        || normalized[0].parameterId != "amp.level_db"
        || normalized[1].controllerNumber != 91
        || normalized[1].parameterId != "fx.reverb_mix")
    {
        std::cerr << "MIDI controller map normalization did not dedupe by latest CC/parameter assignment.\n";
        return false;
    }

    const auto mapDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-midi-controller-map-contract");
    mapDirectory.deleteRecursively();

    const auto mapFile = mapDirectory.getChildFile("MidiControllerMap.json");
    std::string error;
    if (!synth::writeMidiControllerAssignments(mapFile.getFullPathName().toStdString(),
                                               {
                                                   { 91, "fx.reverb_mix" },
                                                   { 1, "macro.motion" },
                                                   { 74, "filter.cutoff_semitones" }
                                               },
                                               error))
    {
        std::cerr << error << "\n";
        return false;
    }

    const auto assignments = synth::readMidiControllerAssignments(mapFile.getFullPathName().toStdString());
    mapDirectory.deleteRecursively();

    if (assignments.size() != 3
        || assignments[0].controllerNumber != 1
        || assignments[0].parameterId != "macro.motion"
        || assignments[1].controllerNumber != 74
        || assignments[1].parameterId != "filter.cutoff_semitones"
        || assignments[2].controllerNumber != 91
        || assignments[2].parameterId != "fx.reverb_mix")
    {
        std::cerr << "MIDI controller map read/write round-trip failed.\n";
        return false;
    }

    return true;
}

bool checkPresetManagerLoadAndSave()
{
    StateRoundTripProcessor processor;
    const auto pluckPresetPath = "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset";
    auto presetPayloadObject = [](const juce::var& parsed) -> const juce::DynamicObject* {
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr)
            return nullptr;

        const auto fileType = root->getProperty(juce::Identifier("fileType"));
        if (!fileType.isString() || fileType.toString() != "SynthiaPreset")
            return nullptr;

        const auto preset = root->getProperty(juce::Identifier("preset"));
        return preset.isObject() ? preset.getDynamicObject() : nullptr;
    };

    if (!parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 96.0f))
    {
        std::cerr << "Unexpected default filter cutoff before preset prepare.\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "layer.1.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "layer.2.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "layer.1.osc.1.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "layer.1.osc.1.voices", 1.0f)
        || !parameterValueMatches(processor.parameters, "layer.1.osc.2.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "layer.2.osc.2.voices", 0.0f)
        || !parameterValueMatches(processor.parameters, "arp.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "arp.step_count", 16.0f)
        || !parameterValueMatches(processor.parameters, "arp.step.1.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "chord.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "chord.voice.1.enabled", 1.0f))
    {
        std::cerr << "Layer/oscillator defaults do not match the Phase 1 backbone contract.\n";
        return false;
    }

    const auto prepared = synth::preparePresetState(processor.parameters, pluckPresetPath);
    if (!prepared.loaded || !prepared.state.isValid())
    {
        std::cerr << prepared.message << "\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 96.0f))
    {
        std::cerr << "Preset prepare mutated live parameters before replaceState.\n";
        return false;
    }

    const auto load = synth::loadPresetIntoState(processor.parameters, pluckPresetPath);
    if (!load.loaded)
    {
        std::cerr << load.message << "\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 58.0f))
    {
        std::cerr << "Preset manager did not apply filter cutoff.\n";
        return false;
    }

    const auto cleanDirtyState = synth::comparePresetDirtyState(processor.parameters, load.fingerprint);
    if (!cleanDirtyState.current.valid
        || !cleanDirtyState.baseline.valid
        || cleanDirtyState.dirty)
    {
        std::cerr << "Preset dirty state should be clean immediately after loading baseline fingerprint.\n";
        return false;
    }

    StateRoundTripProcessor halfStepProcessor;
    auto negativeHalfStepState = halfStepProcessor.parameters.copyState();
    if (!setStateParameterValue(negativeHalfStepState, "amp.pan", -0.00005f))
        return false;

    halfStepProcessor.parameters.replaceState(negativeHalfStepState);
    const auto negativeHalfStepFingerprint = synth::fingerprintPresetState(negativeHalfStepState);
    const auto halfStepDirtyState = synth::comparePresetDirtyState(halfStepProcessor.parameters,
                                                                   negativeHalfStepFingerprint);
    if (halfStepDirtyState.dirty)
    {
        std::cerr << "Preset dirty fingerprint mismatched JUCE snapping for a negative half-step value.\n";
        return false;
    }

    const auto compareSlotA = synth::capturePresetCompareSlot(processor.parameters, "A");
    if (!setPhysicalParameter(processor.parameters, "filter.cutoff_semitones", 72.0f))
        return false;
    const auto flushedStateAfterEdit = processor.parameters.copyState();
    (void) flushedStateAfterEdit;

    const auto changedDirtyState = synth::comparePresetDirtyState(processor.parameters, load.fingerprint);
    if (!changedDirtyState.dirty)
    {
        std::cerr << "Preset dirty state did not detect a parameter edit.\n";
        return false;
    }

    const auto compareSlotB = synth::capturePresetCompareSlot(processor.parameters, "B");
    const auto preparedSlotA = synth::preparePresetCompareSlotState(processor.parameters, compareSlotA);
    const auto preparedSlotB = synth::preparePresetCompareSlotState(processor.parameters, compareSlotB);
    if (!compareSlotA.captured
        || !compareSlotB.captured
        || !preparedSlotA.loaded
        || !preparedSlotB.loaded
        || preparedSlotA.displayName != "A"
        || preparedSlotB.displayName != "B"
        || std::abs(stateParameterValue(preparedSlotA.state, "filter.cutoff_semitones") - 58.0f) > 0.001f
        || std::abs(stateParameterValue(preparedSlotB.state, "filter.cutoff_semitones") - 72.0f) > 0.001f
        || !parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 72.0f))
    {
        std::cerr << "Preset compare slot capture/prepare failed.\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "layer.1.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "layer.2.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "layer.1.osc.1.enabled", 1.0f)
        || !parameterValueMatches(processor.parameters, "layer.2.osc.1.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "arp.enabled", 0.0f)
        || !parameterValueMatches(processor.parameters, "chord.enabled", 0.0f))
    {
        std::cerr << "Legacy preset load did not preserve new layer defaults.\n";
        return false;
    }

    const auto scanInputFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-preset-scan-input");
    scanInputFile.replaceWithText("not a directory");
    const auto scannedFile = synth::scanPresetDirectory(scanInputFile.getFullPathName().toStdString(), false);
    scanInputFile.deleteFile();
    if (!scannedFile.empty())
    {
        std::cerr << "Preset directory scan should ignore non-directory paths.\n";
        return false;
    }

    const auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-preset-manager-test.SynthiaPreset");
    tempFile.deleteFile();

    std::string error;
    if (!synth::writeCurrentPreset(processor.parameters, tempFile.getFullPathName().toStdString(),
                                   "Preset Manager Test", error))
    {
        std::cerr << error << "\n";
        return false;
    }

    const auto validation = synth::validatePresetFile(tempFile.getFullPathName().toStdString());
    if (!validation.passed())
    {
        std::cerr << "Preset manager saved invalid preset.\n";
        for (const auto& validationError : validation.errors)
            std::cerr << "  " << validationError << "\n";
        tempFile.deleteFile();
        return false;
    }

    const auto savedPreset = juce::JSON::parse(tempFile);
    const auto* savedObject = presetPayloadObject(savedPreset);
    const auto savedParameters = savedObject != nullptr
        ? savedObject->getProperty(juce::Identifier("parameters"))
        : juce::var();
    const auto* savedParameterObject = savedParameters.getDynamicObject();
    if (savedParameterObject == nullptr
        || !savedParameterObject->hasProperty(juce::Identifier("layer.1.osc.1.voices"))
        || !savedParameterObject->hasProperty(juce::Identifier("layer.2.enabled"))
        || !savedParameterObject->hasProperty(juce::Identifier("layer.2.osc.2.invert"))
        || !savedParameterObject->hasProperty(juce::Identifier("arp.enabled"))
        || !savedParameterObject->hasProperty(juce::Identifier("arp.step.16.tie"))
        || !savedParameterObject->hasProperty(juce::Identifier("chord.enabled"))
        || !savedParameterObject->hasProperty(juce::Identifier("chord.voice.8.pitch_semitones")))
    {
        std::cerr << "Saved preset omitted layer/oscillator state.\n";
        tempFile.deleteFile();
        return false;
    }

    const auto savedMetadata = savedObject != nullptr
        ? savedObject->getProperty(juce::Identifier("metadata"))
        : juce::var();
    const auto* savedMetadataObject = savedMetadata.getDynamicObject();
    const auto savedBrowserMetadata = savedMetadataObject != nullptr
        ? savedMetadataObject->getProperty(juce::Identifier("browser"))
        : juce::var();
    const auto* savedBrowserObject = savedBrowserMetadata.getDynamicObject();
    if (savedBrowserObject == nullptr
        || savedBrowserObject->getProperty(juce::Identifier("bank")).toString() != "User"
        || savedBrowserObject->getProperty(juce::Identifier("category")).toString() != "User"
        || savedBrowserObject->getProperty(juce::Identifier("source")).toString() != "user")
    {
        std::cerr << "Saved preset omitted browser metadata.\n";
        tempFile.deleteFile();
        return false;
    }
    tempFile.deleteFile();

    const auto metadataFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-preset-metadata-test.SynthiaPreset");
    metadataFile.deleteFile();
    synth::PresetWriteOptions writeOptions;
    writeOptions.mode = synth::PresetWriteMode::CreateNew;
    writeOptions.metadata.displayName = "Metadata Save Test";
    writeOptions.metadata.author = "Lab";
    writeOptions.metadata.description = "Metadata write contract.";
    writeOptions.metadata.tags = { "lead", "bright" };
    writeOptions.metadata.bank = "Lab Bank";
    writeOptions.metadata.category = "Leads";
    if (!synth::writeCurrentPreset(processor.parameters, metadataFile.getFullPathName().toStdString(),
                                   writeOptions, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    writeOptions.metadata.displayName = "Should Not Overwrite";
    if (synth::writeCurrentPreset(processor.parameters, metadataFile.getFullPathName().toStdString(),
                                  writeOptions, error))
    {
        std::cerr << "CreateNew preset write should reject an existing destination.\n";
        metadataFile.deleteFile();
        return false;
    }

    const auto metadataPreset = juce::JSON::parse(metadataFile);
    const auto* metadataObject = presetPayloadObject(metadataPreset);
    const auto metadataRoot = metadataObject != nullptr
        ? metadataObject->getProperty(juce::Identifier("metadata"))
        : juce::var();
    const auto* metadataRootObject = metadataRoot.getDynamicObject();
    const auto metadataBrowser = metadataRootObject != nullptr
        ? metadataRootObject->getProperty(juce::Identifier("browser"))
        : juce::var();
    const auto* metadataBrowserObject = metadataBrowser.getDynamicObject();
    const auto metadataTags = metadataObject != nullptr
        ? metadataObject->getProperty(juce::Identifier("tags"))
        : juce::var();
    const auto* metadataTagArray = metadataTags.getArray();
    auto writtenTags = std::vector<std::string> {};
    if (metadataTagArray != nullptr)
    {
        for (const auto& tag : *metadataTagArray)
            writtenTags.push_back(tag.toString().toStdString());
    }

    if (metadataObject == nullptr
        || metadataObject->getProperty(juce::Identifier("display_name")).toString() != "Metadata Save Test"
        || metadataObject->getProperty(juce::Identifier("author")).toString() != "Lab"
        || metadataObject->getProperty(juce::Identifier("description")).toString() != "Metadata write contract."
        || !containsString(writtenTags, "lead")
        || !containsString(writtenTags, "bright")
        || metadataBrowserObject == nullptr
        || metadataBrowserObject->getProperty(juce::Identifier("bank")).toString() != "Lab Bank"
        || metadataBrowserObject->getProperty(juce::Identifier("category")).toString() != "Leads")
    {
        std::cerr << "Preset metadata write options were not serialized correctly.\n";
        metadataFile.deleteFile();
        return false;
    }

    writeOptions.mode = synth::PresetWriteMode::OverwriteExisting;
    writeOptions.metadata.displayName = "Metadata Overwrite Test";
    if (!synth::writeCurrentPreset(processor.parameters, metadataFile.getFullPathName().toStdString(),
                                   writeOptions, error))
    {
        std::cerr << error << "\n";
        metadataFile.deleteFile();
        return false;
    }

    const auto overwrittenPreset = juce::JSON::parse(metadataFile);
    const auto* overwrittenObject = presetPayloadObject(overwrittenPreset);
    metadataFile.deleteFile();
    if (overwrittenObject == nullptr
        || overwrittenObject->getProperty(juce::Identifier("display_name")).toString() != "Metadata Overwrite Test")
    {
        std::cerr << "OverwriteExisting preset write did not replace existing destination.\n";
        return false;
    }

    const auto factoryPresets = synth::scanPresetDirectory("presets/factory", synth::PresetSource::Factory, {});
    const auto pluckPreset = std::find_if(factoryPresets.begin(), factoryPresets.end(), [](const auto& preset) {
        return preset.id == "pluck-core-01";
    });
    if (pluckPreset == factoryPresets.end()
        || pluckPreset->bank != "Factory"
        || pluckPreset->category != "Pluck"
        || !containsString(pluckPreset->tags, "pluck")
        || pluckPreset->favorite
        || pluckPreset->favoriteKey != "factory:pluck-core-01")
    {
        std::cerr << "Factory preset browser metadata scan failed.\n";
        return false;
    }

    const auto browserDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-preset-browser-contract");
    browserDirectory.deleteRecursively();
    if (!browserDirectory.createDirectory())
    {
        std::cerr << "Could not create preset browser test directory.\n";
        return false;
    }

    const auto browserPresetFile = browserDirectory.getChildFile("browser-favorite-test.SynthiaPreset");
    if (!synth::writeCurrentPreset(processor.parameters, browserPresetFile.getFullPathName().toStdString(),
                                   "Browser Favorite Test", error))
    {
        std::cerr << error << "\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    const auto favoriteKey = synth::presetFavoriteKey(synth::PresetSource::User,
                                                      "browser-favorite-test",
                                                      browserPresetFile.getFullPathName().toStdString());
    const auto favoritesFile = browserDirectory.getChildFile("PresetFavorites.json");
    if (!synth::setPresetFavorite(favoritesFile.getFullPathName().toStdString(), favoriteKey, true, error))
    {
        std::cerr << error << "\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    const auto favoriteKeys = synth::readFavoritePresetKeys(favoritesFile.getFullPathName().toStdString());
    const auto browserPresets = synth::scanPresetDirectory(browserDirectory.getFullPathName().toStdString(),
                                                           synth::PresetSource::User,
                                                           favoriteKeys);
    if (browserPresets.size() != 1
        || !browserPresets.front().favorite
        || browserPresets.front().bank != "User"
        || browserPresets.front().category != "User"
        || !containsString(browserPresets.front().tags, "user"))
    {
        std::cerr << "Preset browser scan did not apply sidecar favorite metadata.\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    const auto duplicateBrowserPresetFile = browserDirectory.getChildFile("browser-favorite-test-duplicate.SynthiaPreset");
    if (!synth::writeCurrentPreset(processor.parameters, duplicateBrowserPresetFile.getFullPathName().toStdString(),
                                   "Browser Favorite Test", error))
    {
        std::cerr << error << "\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    const auto duplicateBrowserPresets = synth::scanPresetDirectory(browserDirectory.getFullPathName().toStdString(),
                                                                    synth::PresetSource::User,
                                                                    favoriteKeys);
    const auto duplicateFavoriteCount = std::count_if(duplicateBrowserPresets.begin(),
                                                      duplicateBrowserPresets.end(),
                                                      [](const auto& preset) {
                                                          return preset.favorite;
                                                      });
    if (duplicateBrowserPresets.size() != 2 || duplicateFavoriteCount != 1)
    {
        std::cerr << "Preset browser favorite keys must not collide for duplicate user preset IDs.\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    synth::PresetBrowserFilter filter;
    filter.searchText = "favorite";
    filter.category = "User";
    filter.tags = { "user" };
    filter.favoritesOnly = true;
    const auto filteredFavorites = synth::filterPresetSummaries(browserPresets, filter);
    if (filteredFavorites.size() != 1 || filteredFavorites.front().id != "browser-favorite-test")
    {
        std::cerr << "Preset browser filter failed to match favorite user preset.\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    filter.includeUser = false;
    if (!synth::filterPresetSummaries(browserPresets, filter).empty())
    {
        std::cerr << "Preset browser source filter did not exclude user preset.\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    const auto catalog = synth::buildPresetBrowserCatalog(browserPresets);
    if (!containsString(catalog.banks, "User")
        || !containsString(catalog.categories, "User")
        || !containsString(catalog.tags, "user"))
    {
        std::cerr << "Preset browser catalog facets missing expected values.\n";
        browserDirectory.deleteRecursively();
        return false;
    }

    if (!synth::setPresetFavorite(favoritesFile.getFullPathName().toStdString(), favoriteKey, false, error)
        || !synth::readFavoritePresetKeys(favoritesFile.getFullPathName().toStdString()).empty())
    {
        std::cerr << "Preset favorite sidecar remove failed.\n";
        browserDirectory.deleteRecursively();
        return false;
    }
    browserDirectory.deleteRecursively();

    const auto extensionlessFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("synthia-preset-manager-extension-test");
    const auto extensionlessPresetFile = extensionlessFile.withFileExtension(".SynthiaPreset");
    extensionlessFile.deleteFile();
    extensionlessPresetFile.deleteFile();
    if (!synth::writeCurrentPreset(processor.parameters, extensionlessFile.getFullPathName().toStdString(),
                                   "Preset Extension Test", error))
    {
        std::cerr << error << "\n";
        return false;
    }

    const auto extensionValidation = synth::validatePresetFile(extensionlessPresetFile.getFullPathName().toStdString());
    extensionlessFile.deleteFile();
    extensionlessPresetFile.deleteFile();
    if (!extensionValidation.passed())
    {
        std::cerr << "Preset manager did not normalize extensionless save path.\n";
        return false;
    }

    return true;
}

bool checkPresetCommandStatePreparation()
{
    StateRoundTripProcessor processor;
    if (!setPhysicalParameter(processor.parameters, "filter.cutoff_semitones", 42.0f)
        || !setPhysicalParameter(processor.parameters, "osc.stack_count", 5.0f)
        || !setPhysicalParameter(processor.parameters, "fx.enabled", 1.0f))
    {
        return false;
    }

    const auto init = synth::prepareInitPresetState(processor.parameters);
    if (!init.loaded
        || !init.state.isValid()
        || init.displayName != "Init"
        || init.state.getProperty("current_preset").toString() != "Init"
        || std::abs(stateParameterValue(init.state, "filter.cutoff_semitones") - 96.0f) > 0.001f
        || std::abs(stateParameterValue(init.state, "osc.stack_count") - 1.0f) > 0.001f
        || std::abs(stateParameterValue(init.state, "fx.enabled")) > 0.001f)
    {
        std::cerr << "Init preset command did not prepare default state.\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 42.0f)
        || !parameterValueMatches(processor.parameters, "osc.stack_count", 5.0f))
    {
        std::cerr << "Init preset preparation mutated live parameters before replaceState.\n";
        return false;
    }

    const auto randomA = synth::prepareRandomizedPresetState(processor.parameters, 12345u);
    const auto randomB = synth::prepareRandomizedPresetState(processor.parameters, 12345u);
    const auto randomC = synth::prepareRandomizedPresetState(processor.parameters, 67890u);
    if (!randomA.loaded
        || !randomB.loaded
        || !randomC.loaded
        || randomA.displayName != "Randomized 12345"
        || randomA.state.getProperty("current_preset").toString() != "Randomized 12345")
    {
        std::cerr << "Randomize preset command did not prepare named state.\n";
        return false;
    }

    const auto stackA = stateParameterValue(randomA.state, "osc.stack_count");
    const auto stackB = stateParameterValue(randomB.state, "osc.stack_count");
    const auto stackC = stateParameterValue(randomC.state, "osc.stack_count");
    const auto cutoffA = stateParameterValue(randomA.state, "filter.cutoff_semitones");
    const auto cutoffB = stateParameterValue(randomB.state, "filter.cutoff_semitones");
    const auto cutoffC = stateParameterValue(randomC.state, "filter.cutoff_semitones");
    if (std::abs(stackA - stackB) > 0.001f
        || std::abs(cutoffA - cutoffB) > 0.001f
        || (std::abs(stackA - stackC) <= 0.001f && std::abs(cutoffA - cutoffC) <= 0.001f)
        || stackA < 1.0f
        || stackA > 5.0f
        || cutoffA < 42.0f
        || cutoffA > 122.0f
        || stateParameterValue(randomA.state, "layer.1.enabled") < 0.5f
        || stateParameterValue(randomA.state, "layer.1.osc.1.enabled") < 0.5f
        || stateParameterValue(randomA.state, "layer.1.osc.1.level") < 0.99f)
    {
        std::cerr << "Randomize preset command is not deterministic or within safe bounds.\n";
        return false;
    }

    if (!parameterValueMatches(processor.parameters, "filter.cutoff_semitones", 42.0f)
        || !parameterValueMatches(processor.parameters, "osc.stack_count", 5.0f))
    {
        std::cerr << "Randomize preset preparation mutated live parameters before replaceState.\n";
        return false;
    }

    processor.parameters.replaceState(randomA.state);
    if (!parameterValueMatches(processor.parameters, "osc.stack_count", stackA)
        || !parameterValueMatches(processor.parameters, "filter.cutoff_semitones", cutoffA, 0.01f)
        || !parameterValueMatches(processor.parameters, "layer.1.osc.1.level", 1.0f))
    {
        const auto* stackValue = processor.parameters.getRawParameterValue("osc.stack_count");
        const auto* cutoffValue = processor.parameters.getRawParameterValue("filter.cutoff_semitones");
        const auto* levelValue = processor.parameters.getRawParameterValue("layer.1.osc.1.level");
        std::cerr << "Prepared random preset state did not apply to APVTS parameters: "
                  << "stack expected " << stackA << " got " << (stackValue != nullptr ? stackValue->load() : -1.0f)
                  << ", cutoff expected " << cutoffA << " got " << (cutoffValue != nullptr ? cutoffValue->load() : -1.0f)
                  << ", A1 level got " << (levelValue != nullptr ? levelValue->load() : -1.0f) << ".\n";
        return false;
    }

    return true;
}
} // namespace

int main()
{
    const auto errors = synth::validateParameterSpecs();
    if (!errors.empty())
    {
        for (const auto& error : errors)
            std::cerr << error << "\n";
        return 1;
    }

    const auto& specs = synth::getParameterSpecs();
    if (specs.size() < 200)
    {
        std::cerr << "Expected broad v1 parameter inventory, got " << specs.size() << "\n";
        return 1;
    }

    if (synth::findParameterSpec("filter.cutoff_semitones") == nullptr
        || synth::findParameterSpec("transmod.8.depth") == nullptr
        || synth::findParameterSpec("macro.space") == nullptr
        || synth::findParameterSpec("layer.1.enabled") == nullptr
        || synth::findParameterSpec("layer.2.enabled") == nullptr
        || synth::findParameterSpec("layer.1.osc.1.voices") == nullptr
        || synth::findParameterSpec("layer.2.osc.2.invert") == nullptr
        || synth::findParameterSpec("arp.enabled") == nullptr
        || synth::findParameterSpec("arp.step.16.tie") == nullptr
        || synth::findParameterSpec("chord.enabled") == nullptr
        || synth::findParameterSpec("chord.voice.8.velocity") == nullptr
        || synth::findParameterSpec("fx.distortion_mode") == nullptr
        || synth::findParameterSpec("fx.phaser_mix") == nullptr
        || synth::findParameterSpec("fx.eq_low_gain_db") == nullptr
        || synth::findParameterSpec("fx.compressor_mix") == nullptr)
    {
        std::cerr << "Required parameter missing.\n";
        return 1;
    }

    const auto* transModAmp = synth::findParameterSpec("transmod.1.amp_level_db");
    if (transModAmp == nullptr
        || std::abs(transModAmp->minimum + 24.0f) > 0.0001f
        || std::abs(transModAmp->maximum - 24.0f) > 0.0001f)
    {
        std::cerr << "TransMod amp-level range mismatch.\n";
        return 1;
    }

    const auto* layerAEnabled = synth::findParameterSpec("layer.1.enabled");
    const auto* layerBEnabled = synth::findParameterSpec("layer.2.enabled");
    const auto* layerAOscillatorVoices = synth::findParameterSpec("layer.1.osc.1.voices");
    const auto* layerBOscillatorInvert = synth::findParameterSpec("layer.2.osc.2.invert");
    const auto* arpEnabled = synth::findParameterSpec("arp.enabled");
    const auto* arpStepCount = synth::findParameterSpec("arp.step_count");
    const auto* arpStepPitch = synth::findParameterSpec("arp.step.16.pitch_semitones");
    const auto* chordVoiceCount = synth::findParameterSpec("chord.voice_count");
    const auto* chordVoiceOneEnabled = synth::findParameterSpec("chord.voice.1.enabled");
    const auto* fxDistortionMode = synth::findParameterSpec("fx.distortion_mode");
    const auto* fxPhaserMix = synth::findParameterSpec("fx.phaser_mix");
    const auto* fxEqLowGain = synth::findParameterSpec("fx.eq_low_gain_db");
    const auto* fxCompressorMix = synth::findParameterSpec("fx.compressor_mix");
    if (layerAEnabled == nullptr
        || layerBEnabled == nullptr
        || layerAOscillatorVoices == nullptr
        || layerBOscillatorInvert == nullptr
        || arpEnabled == nullptr
        || arpStepCount == nullptr
        || arpStepPitch == nullptr
        || chordVoiceCount == nullptr
        || chordVoiceOneEnabled == nullptr
        || fxDistortionMode == nullptr
        || fxPhaserMix == nullptr
        || fxEqLowGain == nullptr
        || fxCompressorMix == nullptr
        || std::abs(layerAEnabled->defaultValue - 1.0f) > 0.0001f
        || std::abs(layerBEnabled->defaultValue) > 0.0001f
        || std::abs(layerAOscillatorVoices->minimum) > 0.0001f
        || std::abs(layerAOscillatorVoices->maximum - 8.0f) > 0.0001f
        || std::abs(layerAOscillatorVoices->defaultValue - 1.0f) > 0.0001f
        || std::abs(layerBOscillatorInvert->defaultValue) > 0.0001f
        || std::abs(arpEnabled->defaultValue) > 0.0001f
        || std::abs(arpStepCount->defaultValue - 16.0f) > 0.0001f
        || std::abs(arpStepPitch->minimum + 24.0f) > 0.0001f
        || std::abs(arpStepPitch->maximum - 24.0f) > 0.0001f
        || std::abs(chordVoiceCount->defaultValue - 1.0f) > 0.0001f
        || std::abs(chordVoiceOneEnabled->defaultValue - 1.0f) > 0.0001f
        || arpEnabled->auVersionHint < 2
        || arpStepPitch->auVersionHint < 2
        || chordVoiceCount->auVersionHint < 2
        || chordVoiceOneEnabled->auVersionHint < 2
        || fxDistortionMode->auVersionHint < 3
        || fxPhaserMix->auVersionHint < 3
        || fxEqLowGain->auVersionHint < 3
        || fxCompressorMix->auVersionHint < 3)
    {
        std::cerr << "Layer/oscillator/arp/chord/FX registry defaults, ranges, or AU version hints mismatch.\n";
        return 1;
    }

    if (!checkStateRoundTrip())
    {
        std::cerr << "APVTS state round-trip failed.\n";
        return 1;
    }

    if (!checkAutomationParameterExposure())
        return 1;

    if (!checkModulationRouteModel())
        return 1;

    if (!checkModulationRouteWriteAdapter())
        return 1;

    if (!checkHostStateDefaultMerge())
    {
        std::cerr << "Host state default merge failed.\n";
        return 1;
    }

    if (!checkLayerOscillatorVoiceCost())
        return 1;

    if (!checkPatchCostEstimate())
        return 1;

    if (!checkMidiControllerMap())
        return 1;

    if (!checkPresetManagerLoadAndSave())
        return 1;

    if (!checkPresetCommandStatePreparation())
        return 1;

    const auto presetResults = synth::validatePresetDirectory("presets/factory");
    for (const auto& result : presetResults)
    {
        if (!result.passed())
        {
            std::cerr << "Preset failed: " << result.path << "\n";
            for (const auto& error : result.errors)
                std::cerr << "  " << error << "\n";
            return 1;
        }
    }

    const auto invalidModDepth = synth::validatePresetFile("fixtures/presets/invalid-mod-slot-depth.SynthiaPreset");
    if (invalidModDepth.passed())
    {
        std::cerr << "Invalid mod slot depth fixture passed validation.\n";
        return 1;
    }

    return 0;
}
