#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class BandControls : public juce::Component {
  public:
    BandControls(const juce::String &bandName, size_t bandIndex, MultibandReverbAudioProcessor &processor);
    ~BandControls() override;
    void paint(juce::Graphics &g) override;
    void resized() override;

  private:
    void loadIRButtonClicked();

    juce::String name;
    size_t bandIdx;
    MultibandReverbAudioProcessor &processorRef;

    juce::TextButton irLoadButton{"Load IR"};
    juce::Slider mixSlider;
    juce::Label nameLabel;
    juce::Label mixLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControls)
};