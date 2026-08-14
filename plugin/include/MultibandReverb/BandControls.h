#pragma once
#include <JuceHeader.h>

class MultibandReverbAudioProcessor;

class BandControls : public juce::Component {
  public:
    BandControls(int bandIndex,
                 MultibandReverbAudioProcessor &processor,
                 std::function<void(int)> onDelete);
    ~BandControls() override;

    void paint(juce::Graphics &g) override;
    void resized() override;

    void setBandIndex(int newIndex);
    int  getBandIndex() const { return bandIdx; }

  private:
    void loadIRButtonClicked();

    int bandIdx;
    MultibandReverbAudioProcessor &processorRef;
    std::function<void(int)> deleteCallback;

    juce::Label     nameLabel;
    juce::TextButton irLoadButton  { "Load IR" };
    juce::TextButton deleteButton  { "X" };
    juce::TextButton soloButton    { "S" };
    juce::TextButton muteButton    { "M" };

    juce::Slider volumeSlider;
    juce::Label  volumeLabel;

    juce::Slider mixSlider;
    juce::Label  mixLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControls)
};
