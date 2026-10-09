#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>

// The classic instrument uses a fixed logical canvas. JUCE scales the complete
// component tree together so labels, controls and hit targets stay aligned.
class SynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SynthAudioProcessorEditor(SynthAudioProcessor&);
    ~SynthAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ClassicSurface;
    std::unique_ptr<ClassicSurface> surface;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthAudioProcessorEditor)
};
