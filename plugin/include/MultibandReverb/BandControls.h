#pragma once
#include "PluginProcessor.h"
#include <JuceHeader.h>

class BandControls : public juce::Component
{
public:
    BandControls(const juce::String& bandName, size_t bandIndex, MultibandReverbAudioProcessor& processor);
    ~BandControls() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void loadIRButtonClicked();

private:
    juce::Label nameLabel{"", "Band"};
    juce::TextButton irLoadButton{"Load IR"};
    juce::Slider mixSlider;
    juce::Label mixLabel;
    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    juce::Slider crossoverSlider;
    juce::Label crossoverLabel;
    juce::TextButton soloButton{"S"};
    juce::TextButton muteButton{"M"};

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossoverAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    juce::String name;
    size_t bandIdx;
    bool isSoloed = false;
    bool isMuted = false;

    MultibandReverbAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControls)
};