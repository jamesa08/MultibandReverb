#pragma once
#include "PluginProcessor.h"
#include "SpectrumAnalyzer.h"

class BandControls : public juce::Component
{
public:
    BandControls(const juce::String& bandName, size_t bandIndex, 
                 MultibandReverbAudioProcessor& processor, 
                 juce::AudioProcessorValueTreeState& params);
    
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void loadIRButtonClicked();

    juce::String name;
    size_t bandIdx;
    MultibandReverbAudioProcessor& processorRef;
    
    juce::TextButton irLoadButton;
    juce::Slider mixSlider;
    juce::Slider volumeSlider;
    
    juce::Label nameLabel;
    juce::Label mixLabel;
    juce::Label volumeLabel;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    
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
    
    // Initialize components in the correct order (same as declaration)
    SpectrumAnalyzer analyzer;
    BandControls lowBand;
    BandControls highBand;
    juce::Slider crossoverSlider;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossoverAttachment;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandReverbAudioProcessorEditor)
};