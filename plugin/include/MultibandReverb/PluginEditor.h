// PluginEditor.h
#pragma once

#include "PluginProcessor.h"
#include "SpectrumAnalyzer.h"


class BandControls : public juce::Component
{
public:
    BandControls(const juce::String& bandName, size_t bandIndex, MultibandReverbAudioProcessor& processor);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void loadIRButtonClicked();

    juce::String name;
    size_t bandIdx;
    MultibandReverbAudioProcessor& processorRef;
    
    juce::TextButton irLoadButton{"Load IR"};
    juce::Slider mixSlider;
    juce::Label nameLabel;
    juce::Label mixLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControls)
};

class MultibandReverbAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MultibandReverbAudioProcessorEditor(MultibandReverbAudioProcessor& p);
    ~MultibandReverbAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MultibandReverbAudioProcessor& processorRef;
    SpectrumAnalyzer analyzer;

    BandControls lowBand{"Low", 0, processorRef};
    BandControls midBand{"Mid", 1, processorRef};
    BandControls highBand{"High", 2, processorRef};
    juce::Slider lowCrossoverSlider;
    juce::Slider midCrossoverSlider;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

    void attachSliders();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandReverbAudioProcessorEditor)
};
