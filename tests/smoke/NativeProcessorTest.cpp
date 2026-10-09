#include "../../src/plugin/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

namespace
{
bool writeParameter(SynthAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.getValueTreeState().getParameter(id);
    if (parameter == nullptr)
    {
        std::cerr << "Processor parameter absent: " << id << "\n";
        return false;
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    return true;
}

bool parameterMatches(SynthAudioProcessor& processor, const char* id, float value)
{
    const auto* parameter = processor.getValueTreeState().getRawParameterValue(id);
    const auto matches = parameter != nullptr && std::abs(parameter->load() - value) < 0.001f;
    if (!matches)
        std::cerr << "Processor parameter mismatch: " << id << " expected " << value << " actual "
                  << (parameter != nullptr ? parameter->load() : -9999.0f) << " program " << processor.getCurrentProgram() << "\n";
    return matches;
}

juce::AccessibilityHandler* findAccessibleControl(juce::Component& component, const juce::String& title)
{
    if (component.getTitle() == title)
        return component.getAccessibilityHandler();
    for (auto* child : component.getChildren())
        if (auto* handler = findAccessibleControl(*child, title))
            return handler;
    return nullptr;
}

class AccessibilityGestureRecorder final : private juce::AudioProcessorParameter::Listener
{
public:
    explicit AccessibilityGestureRecorder(juce::AudioProcessorParameter& observed) : parameter(observed)
    {
        parameter.addListener(this);
    }
    ~AccessibilityGestureRecorder() override { parameter.removeListener(this); }
    bool completed(int count) const { return valid && !active && starts == count && ends == count && changes >= count; }

private:
    void parameterValueChanged(int, float) override
    {
        valid = valid && active;
        ++changes;
    }
    void parameterGestureChanged(int, bool beginning) override
    {
        valid = valid && (beginning != active);
        active = beginning;
        if (beginning) ++starts;
        else ++ends;
    }
    juce::AudioProcessorParameter& parameter;
    int starts = 0, ends = 0, changes = 0;
    bool active = false, valid = true;
};

bool editorAccessibilityOperatesNativeControls()
{
    SynthAudioProcessor processor;
    const auto* masterSpec = synth::findParameterSpec("master.level_db");
    const auto* waveformSpec = synth::findParameterSpec("layer.1.osc.1.waveform");
    const auto* retriggerSpec = synth::findParameterSpec("layer.1.osc.1.retrigger");
    const auto* octaveBSpec = synth::findParameterSpec("layer.2.osc.1.octave");
    if (masterSpec == nullptr || waveformSpec == nullptr || retriggerSpec == nullptr || octaveBSpec == nullptr)
        return false;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
        return false;
    editor->setVisible(false);
    // JUCE accessibility handlers require a native peer; no visible window is needed.
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    auto* master = findAccessibleControl(*editor, masterSpec->name);
    auto* parameter = processor.getValueTreeState().getParameter(masterSpec->id);
    if (master == nullptr || parameter == nullptr || master->getRole() != juce::AccessibilityRole::slider)
        return false;
    auto* value = master->getValueInterface();
    if (value == nullptr || value->isReadOnly())
        return false;
    const auto range = value->getRange();
    if (!range.isValid() || std::abs(range.getMinimumValue() - masterSpec->minimum) > 0.0
        || std::abs(range.getMaximumValue() - masterSpec->maximum) > 0.0)
        return false;
    {
        AccessibilityGestureRecorder gestures(*parameter);
        value->setValue(-18.0);
        if (!parameterMatches(processor, "master.level_db", -18.0f) || !gestures.completed(1))
            return false;
        value->setValue(range.getMaximumValue() + 10.0);
        if (!parameterMatches(processor, "master.level_db", masterSpec->maximum) || !gestures.completed(2))
            return false;
        value->setValue(range.getMinimumValue() - 10.0);
        if (!parameterMatches(processor, "master.level_db", masterSpec->minimum) || !gestures.completed(3))
            return false;
        value->setValue(std::numeric_limits<double>::quiet_NaN());
        value->setValue(std::numeric_limits<double>::infinity());
        value->setValueAsString("invalid numeric value");
        if (!parameterMatches(processor, "master.level_db", masterSpec->minimum) || !gestures.completed(3))
            return false;
    }
    if (!writeParameter(processor, "master.level_db", -9.0f) || std::abs(value->getCurrentValue() + 9.0) > 0.001)
        return false;
    auto* waveform = findAccessibleControl(*editor, waveformSpec->name);
    if (waveform == nullptr || waveform->getRole() != juce::AccessibilityRole::comboBox
        || waveform->getValueInterface() == nullptr)
        return false;
    auto* waveformValue = waveform->getValueInterface();
    auto* waveformParameter = processor.getValueTreeState().getParameter(waveformSpec->id);
    if (waveformValue->isReadOnly() || waveformParameter == nullptr)
        return false;
    {
        AccessibilityGestureRecorder gestures(*waveformParameter);
        waveformValue->setValueAsString("Sine");
        if (!parameterMatches(processor, "layer.1.osc.1.waveform", static_cast<float>(synth::OscillatorSlotWaveform::Sine))
            || !gestures.completed(1))
            return false;
        waveformValue->setValueAsString("invalid waveform");
        if (!parameterMatches(processor, "layer.1.osc.1.waveform", static_cast<float>(synth::OscillatorSlotWaveform::Sine))
            || !gestures.completed(1))
            return false;
    }
    auto* retrigger = findAccessibleControl(*editor, retriggerSpec->name);
    if (retrigger == nullptr || retrigger->getRole() != juce::AccessibilityRole::toggleButton
        || !retrigger->getActions().invoke(juce::AccessibilityActionType::toggle)
        || !parameterMatches(processor, "layer.1.osc.1.retrigger", 0.0f))
        return false;
    auto* partB = findAccessibleControl(*editor, "Part B");
    if (partB == nullptr || partB->getRole() != juce::AccessibilityRole::radioButton
        || !partB->getActions().invoke(juce::AccessibilityActionType::press))
        return false;
    auto passed = false;
    if (!juce::MessageManager::callAsync([&] {
            auto* octave = findAccessibleControl(*editor, octaveBSpec->name);
            auto* selected = findAccessibleControl(*editor, "Part B");
            if (processor.getSelectedPart() == 1 && octave != nullptr && octave->getValueInterface() != nullptr
                && selected != nullptr && selected->getCurrentState().isChecked())
            {
                octave->getValueInterface()->setValue(1.0);
                passed = parameterMatches(processor, "layer.2.osc.1.octave", 1.0f)
                    && parameterMatches(processor, "layer.1.osc.1.octave", 0.0f)
                    && findAccessibleControl(*editor, waveformSpec->name) == nullptr;
            }
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }))
        return false;
    // This is the final test: one dispatch loop applies the deferred Part B action safely.
    juce::MessageManager::getInstance()->runDispatchLoop();
    return passed;
}

bool programPersistence()
{
    SynthAudioProcessor source;
    juce::String message;
    const auto verify = [&message](bool success, const char* operation) {
        if (!success)
            std::cerr << "Program operation failed: " << operation << ": " << message << "\n";
        return success;
    };
    if (!verify(source.getNumPrograms() == 512, "program count")
        || !verify(source.selectProgram(127, message), "select 127")
        || !verify(writeParameter(source, "layer.1.osc.1.octave", 2.0f), "write octave"))
        return false;
    source.changeProgramName(127, "Octave Persistence");
    if (!verify(source.nextProgram(message) && source.getCurrentProgram() == 128, "advance across sub-bank boundary")
        || !verify(writeParameter(source, "layer.2.amp_env.attack_ms", 500.0f), "write B attack"))
        return false;
    source.changeProgramName(128, "Envelope Persistence");
    if (!verify(source.previousProgram(message) && source.getCurrentProgram() == 127, "return across sub-bank boundary")
        || !verify(parameterMatches(source, "layer.1.osc.1.octave", 2.0f), "recall octave"))
        return false;
    if (!verify(source.copyCurrentProgram(message), "copy 127")
        || !verify(source.selectProgram(511, message), "select 511")
        || !verify(source.pasteCurrentProgram(message), "paste 511")
        || !verify(parameterMatches(source, "layer.1.osc.1.octave", 2.0f), "pasted octave")
        || !verify(!source.selectProgram(-1, message) && !source.selectProgram(512, message) && source.getCurrentProgram() == 511, "reject invalid indices"))
        return false;

    juce::MemoryBlock state;
    source.getStateInformation(state);
    SynthAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!verify(restored.getCurrentProgram() == 511, "restore selected program")
        || !verify(parameterMatches(restored, "layer.1.osc.1.octave", 2.0f), "restore selected values")
        || !verify(restored.selectProgram(128, message), "restore select 128")
        || !verify(restored.getProgramName(128) == "Envelope Persistence", "restore program 128 name")
        || !verify(parameterMatches(restored, "layer.2.amp_env.attack_ms", 500.0f), "restore program 128 values")
        || !verify(restored.selectProgram(127, message), "restore select 127")
        || !verify(restored.getProgramName(127) == "Octave Persistence", "restore program 127 name")
        || !verify(parameterMatches(restored, "layer.1.osc.1.octave", 2.0f), "restore program 127 values"))
        return false;
    if (!verify(restored.selectProgram(126, message), "select insert neighbor")
        || !verify(writeParameter(restored, "layer.1.osc.1.octave", -1.0f), "write insert neighbor"))
        return false;
    restored.changeProgramName(126, "Previous Program");
    if (!verify(restored.selectProgram(127, message) && restored.copyCurrentProgram(message), "copy insert source")
        || !verify(restored.selectProgram(126, message) && restored.insertCurrentProgram(message), "insert clipboard at 126")
        || !verify(restored.getProgramName(126) == "Octave Persistence" && parameterMatches(restored, "layer.1.osc.1.octave", 2.0f), "inserted clipboard values")
        || !verify(restored.selectProgram(127, message) && restored.getProgramName(127) == "Previous Program", "insert shifts neighbor within sub-bank")
        || !verify(parameterMatches(restored, "layer.1.osc.1.octave", -1.0f), "insert retains neighbor values")
        || !verify(restored.selectProgram(128, message) && restored.getProgramName(128) == "Envelope Persistence", "insert preserves next sub-bank")
        || !verify(restored.selectProgram(126, message) && restored.deleteCurrentProgram(message), "delete inserted program")
        || !verify(restored.getProgramName(126) == "Previous Program" && parameterMatches(restored, "layer.1.osc.1.octave", -1.0f), "delete restores neighbor")
        || !verify(restored.selectProgram(128, message) && restored.getProgramName(128) == "Envelope Persistence", "delete preserves next sub-bank"))
        return false;

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("synthia-native-program-test", ".SynthiaBank");
    if (!verify(source.saveProgramBank(file, false, message), "save bank")
        || !verify(!source.saveProgramBank(file, false, message), "no-clobber bank"))
    {
        file.deleteFile();
        return false;
    }
    SynthAudioProcessor loaded;
    const auto loadedBank = verify(loaded.loadProgramBank(file, message), "load bank");
    file.deleteFile();
    const auto malformed = file.replaceWithText("{\"fileType\":\"SynthiaBank\",\"programs\":[false]}");
    const auto currentBeforeInvalid = loaded.getCurrentProgram();
    const auto nameBeforeInvalid = loaded.getProgramName(currentBeforeInvalid);
    const auto malformedRejected = verify(!loaded.loadProgramBank(file, message), "reject malformed bank");
    const auto unchangedAfterInvalid = verify(loaded.getCurrentProgram() == currentBeforeInvalid
                                                  && loaded.getProgramName(currentBeforeInvalid) == nameBeforeInvalid,
                                              "malformed bank preserves selection");
    file.deleteFile();
    return malformed && malformedRejected && unchangedAfterInvalid && loadedBank
        && verify(loaded.selectProgram(128, message) && loaded.getProgramName(128) == "Envelope Persistence", "loaded bank program name")
        && verify(parameterMatches(loaded, "layer.2.amp_env.attack_ms", 500.0f), "loaded bank program values");
}

bool presetResetPreservesStoredBaseline()
{
    SynthAudioProcessor source;
    juce::String message;
    juce::TemporaryFile temporary(".SynthiaPreset");
    const auto file = temporary.getFile();
    if (!writeParameter(source, "layer.1.osc.1.octave", 2.0f)
        || !source.savePresetFile(file, "Reset Baseline", message)
        || !source.loadPresetFile(file, message)
        || !writeParameter(source, "layer.1.osc.1.octave", 3.0f))
    {
        std::cerr << "Reset fixture preparation failed: " << message << "\n";
        return false;
    }
    auto payload = juce::JSON::parse(file);
    auto* preset = payload.getProperty("preset", {}).getDynamicObject();
    auto* values = preset != nullptr ? preset->getProperty("parameters").getDynamicObject() : nullptr;
    if (values == nullptr)
        return false;
    values->setProperty("layer.1.osc.1.octave", 4.0f);
    if (!file.replaceWithText(juce::JSON::toString(payload))
        || !source.resetCurrentPreset(message) || !parameterMatches(source, "layer.1.osc.1.octave", 2.0f))
    {
        std::cerr << "Reset reread modified preset instead of its stored baseline: " << message << "\n";
        return false;
    }
    if (!writeParameter(source, "layer.1.osc.1.octave", 3.0f) || !source.getPresetWorkflowSnapshot().dirty)
        return false;
    juce::MemoryBlock state;
    source.getStateInformation(state);
    file.deleteFile();
    SynthAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!parameterMatches(restored, "layer.1.osc.1.octave", 3.0f) || !restored.getPresetWorkflowSnapshot().dirty)
    {
        std::cerr << "Host restore lost the edited preset or its dirty baseline.\n";
        return false;
    }
    if (!restored.resetCurrentPreset(message) || !parameterMatches(restored, "layer.1.osc.1.octave", 2.0f)
        || restored.getPresetWorkflowSnapshot().dirty)
    {
        std::cerr << "Reset failed after the preset file was removed: " << message << "\n";
        return false;
    }
    return true;
}

bool editorClosePreservesHostAudio()
{
    SynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    processor.processBlock(buffer, midi);
    midi.clear();
    for (int block = 0; block < 8; ++block)
        processor.processBlock(buffer, midi);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor == nullptr)
            return false;
    }
    processor.processBlock(buffer, midi);
    if (processor.getDiagnosticsSnapshot().activeVoices == 0 || buffer.getMagnitude(0, buffer.getNumSamples()) <= 1.0e-4f)
        return false;
    processor.releaseResources();
    return true;
}

bool idlePeriodsPreserveArpScheduler()
{
    SynthAudioProcessor processor;
    if (!writeParameter(processor, "arp.enabled", 1.0f)
        || !writeParameter(processor, "global.sync", 0.0f)
        || !writeParameter(processor, "arp.time_ms", 6000.0f)
        || !writeParameter(processor, "arp.gate", 0.05f)
        || !writeParameter(processor, "layer.1.amp_env.release_ms", 1.0f))
        return false;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    processor.processBlock(buffer, midi);
    midi.clear();
    auto observedSilence = false;
    auto restarted = false;
    for (int block = 1; block < 2500; ++block)
    {
        processor.processBlock(buffer, midi);
        const auto active = processor.getDiagnosticsSnapshot().activeVoices;
        if (block > 200 && active == 0)
            observedSilence = true;
        if (observedSilence && block >= 2250 && active > 0 && buffer.getMagnitude(0, buffer.getNumSamples()) > 1.0e-4f)
            restarted = true;
    }
    processor.releaseResources();
    return observedSilence && restarted;
}

bool wheelCoalescingPreservesTerminalValues()
{
    SynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
    processor.processBlock(buffer, midi);
    midi.clear();
    for (int event = 0; event < 2048; ++event)
        processor.submitUiMidiMessage(juce::MidiMessage::controllerEvent(1, 2, event % 128));
    if (processor.submitUiMidiMessage(juce::MidiMessage::controllerEvent(1, 2, 0)))
        return false;
    for (int event = 0; event < 2048; ++event)
        if (!processor.submitUiMidiMessage(juce::MidiMessage::pitchWheel(1, event % 16384))
            || !processor.submitUiMidiMessage(juce::MidiMessage::controllerEvent(1, 1, event % 128)))
            return false;
    if (!processor.submitUiMidiMessage(juce::MidiMessage::pitchWheel(1, 8192))
        || !processor.submitUiMidiMessage(juce::MidiMessage::controllerEvent(1, 1, 0)))
        return false;
    processor.processBlock(buffer, midi);
    const auto snapshot = processor.getUiVisualSnapshot();
    processor.releaseResources();
    return snapshot.pitchBend == 0.0f && snapshot.modWheel == 0.0f;
}

bool freeLfoAdvancesAcrossSilentCallbacks()
{
    const auto& destinations = synth::modulationDestinationCatalog();
    const auto pitch = std::find_if(destinations.begin(), destinations.end(), [](const auto& destination) {
        return destination.id == "layer.1.osc.1.note";
    });
    if (pitch == destinations.end())
        return false;
    const auto selection = static_cast<float>(std::distance(destinations.begin(), pitch) + 1);
    const auto configure = [selection](SynthAudioProcessor& processor, bool free, bool second) {
        const std::string lfoPrefix = second ? "lfo.2." : "lfo.";
        const std::string routePrefix = second ? "transmod.4.route.1." : "transmod.3.route.1.";
        return writeParameter(processor, (lfoPrefix + "free").c_str(), free ? 1.0f : 0.0f)
            && writeParameter(processor, "global.sync", 0.0f)
            && writeParameter(processor, (lfoPrefix + "shape").c_str(), 0.0f)
            && writeParameter(processor, (lfoPrefix + "rate_hz").c_str(), 2.0f)
            && writeParameter(processor, (lfoPrefix + "phase_degrees").c_str(), 0.0f)
            && writeParameter(processor, (routePrefix + "destination").c_str(), selection)
            && writeParameter(processor, (routePrefix + "amount").c_str(), 0.25f);
    };
    SynthAudioProcessor silent, continuous;
    if (!configure(silent, true, false) || !configure(continuous, true, false))
        return false;
    silent.prepareToPlay(48000.0, 128);
    continuous.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> silentAudio(2, 128), continuousAudio(2, 128);
    juce::MidiBuffer silentMidi, continuousMidi;
    continuousMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    for (int block = 0; block < 100; ++block)
    {
        silent.processBlock(silentAudio, silentMidi);
        continuous.processBlock(continuousAudio, continuousMidi);
        continuousMidi.clear();
    }
    silentMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    silent.processBlock(silentAudio, silentMidi);
    continuous.processBlock(continuousAudio, continuousMidi);
    const auto silentPhase = silent.getUiVisualSnapshot().lfoPhase;
    const auto continuousPhase = continuous.getUiVisualSnapshot().lfoPhase;
    constexpr auto twoSamplePhaseTolerance = 2.0f * 2.0f / 48000.0f;
    if (std::abs(silentPhase - continuousPhase) > twoSamplePhaseTolerance || silentPhase < 0.5f || silentPhase > 0.6f)
    {
        std::cerr << "FREE LFO silent phase " << silentPhase << " continuous phase " << continuousPhase << "\n";
        return false;
    }
    if (!writeParameter(silent, "lfo.rate_hz", 3.0f) || !writeParameter(continuous, "lfo.rate_hz", 3.0f))
        return false;
    for (int block = 0; block < 50; ++block)
    {
        silentMidi.clear();
        silent.processBlock(silentAudio, silentMidi);
        continuous.processBlock(continuousAudio, continuousMidi);
    }
    const auto editedSilent = silent.getUiVisualSnapshot().lfoPhase;
    const auto editedContinuous = continuous.getUiVisualSnapshot().lfoPhase;
    if (std::abs(editedSilent - editedContinuous) > 2.0f * 3.0f / 48000.0f || editedSilent < 0.9f || editedSilent > 0.98f)
        return false;
    silent.releaseResources();
    continuous.releaseResources();

    for (const auto second : { false, true })
        for (const auto free : { false, true })
        {
            SynthAudioProcessor gap, immediate;
            if (!configure(gap, free, second) || !configure(immediate, free, second))
                return false;
            gap.prepareToPlay(48000.0, 128);
            immediate.prepareToPlay(48000.0, 128);
            juce::AudioBuffer<float> gapAudio(2, 128), immediateAudio(2, 128);
            juce::MidiBuffer midi;
            for (int block = 0; block < 100; ++block)
            {
                if (block == 50)
                {
                    const auto rateId = second ? "lfo.2.rate_hz" : "lfo.rate_hz";
                    if (!writeParameter(gap, rateId, 3.0f) || !writeParameter(immediate, rateId, 3.0f))
                        return false;
                }
                gap.processBlock(gapAudio, midi);
            }
            if (free && !second)
            {
                const auto idlePhase = gap.getUiVisualSnapshot().lfoPhase;
                constexpr auto expectedIdlePhase = (6400.0f * 2.0f + 6400.0f * 3.0f) / 48000.0f;
                if (std::abs(idlePhase - expectedIdlePhase) > 2.0f * 3.0f / 48000.0f)
                {
                    std::cerr << "FREE LFO ignored an idle rate edit: phase " << idlePhase << "\n";
                    return false;
                }
            }
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            gap.processBlock(gapAudio, midi);
            midi.clear();
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            immediate.processBlock(immediateAudio, midi);
            auto maximum = 0.0f;
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 128; ++sample)
                    maximum = std::max(maximum, std::abs(gapAudio.getSample(channel, sample) - immediateAudio.getSample(channel, sample)));
            gap.releaseResources();
            immediate.releaseResources();
            if ((!free && maximum > 1.0e-7f) || (free && maximum <= 1.0e-4f))
            {
                std::cerr << "LFO note-reset comparison second=" << second << " free=" << free << " max difference=" << maximum << "\n";
                return false;
            }
        }
    return true;
}

bool uiMidiQueue()
{
    SynthAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer hostMidi;
    if (!processor.submitUiMidiMessage(juce::MidiMessage::noteOn(1, 60, 1.0f)))
        return false;
    auto maximum = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        buffer.clear();
        processor.processBlock(buffer, hostMidi);
        maximum = std::max(maximum, buffer.getMagnitude(0, buffer.getNumSamples()));
    }
    if (maximum <= 1.0e-4f || processor.getDiagnosticsSnapshot().activeVoices < 1)
        return false;
    if (!processor.submitUiMidiMessage(juce::MidiMessage::noteOff(1, 60)))
        return false;
    for (int block = 0; block < 512; ++block)
    {
        buffer.clear();
        processor.processBlock(buffer, hostMidi);
    }
    if (processor.getDiagnosticsSnapshot().activeVoices != 0)
        return false;

    // Fixed storage must reject overflow instead of allocating or dropping silently.
    auto accepted = 0;
    for (int event = 0; event < 2048; ++event)
        accepted += processor.submitUiMidiMessage(juce::MidiMessage::controllerEvent(1, 2, event % 128)) ? 1 : 0;
    if (accepted == 0 || accepted >= 2048)
        return false;
    buffer.clear();
    processor.processBlock(buffer, hostMidi);
    const auto resumed = processor.submitUiMidiMessage(juce::MidiMessage::noteOn(1, 64, 0.8f));
    buffer.clear();
    processor.processBlock(buffer, hostMidi);
    for (int event = 0; event < 2048; ++event)
        processor.submitUiMidiMessage(juce::MidiMessage::noteOn(1, 64, 0.8f));
    const auto releaseRejected = !processor.submitUiMidiMessage(juce::MidiMessage::noteOff(1, 64));
    buffer.clear();
    processor.processBlock(buffer, hostMidi);
    const auto noStuckNotes = processor.getDiagnosticsSnapshot().activeVoices == 0;
    processor.releaseResources();
    return resumed && releaseRejected && noStuckNotes;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (!programPersistence())
    {
        std::cerr << "Program boundary, copy/paste, host-state and bank-file persistence failed.\n";
        return 1;
    }
    if (!presetResetPreservesStoredBaseline())
    {
        std::cerr << "Stored preset baseline/dirty restore contract failed.\n";
        return 1;
    }
    if (!editorClosePreservesHostAudio())
    {
        std::cerr << "Closing the editor silenced a held host note.\n";
        return 1;
    }
    if (!idlePeriodsPreserveArpScheduler())
    {
        std::cerr << "The arpeggiator stopped scheduling after an idle interval.\n";
        return 1;
    }
    if (!wheelCoalescingPreservesTerminalValues())
    {
        std::cerr << "Full MIDI queue lost the final pitch/mod wheel values.\n";
        return 1;
    }
    if (!freeLfoAdvancesAcrossSilentCallbacks())
    {
        std::cerr << "FREE LFO stopped advancing during silent processor callbacks.\n";
        return 1;
    }
    if (!uiMidiQueue())
    {
        std::cerr << "UI MIDI note lifecycle or bounded queue overflow behavior failed.\n";
        return 1;
    }
    if (!editorAccessibilityOperatesNativeControls())
    {
        std::cerr << "Editor accessibility roles, parameter gestures or Part B action failed.\n";
        return 1;
    }
    std::cout << "Native processor persistence and UI MIDI checks passed.\n";
    return 0;
}
