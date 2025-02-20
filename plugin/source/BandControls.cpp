#include "MultibandReverb/BandControls.h"
#include "MultibandReverb/PluginProcessor.h"

//==============================================================================
BandControls::BandControls(const juce::String &bandName, size_t bandIndex, MultibandReverbAudioProcessor &processor) : name(bandName), bandIdx(bandIndex), processorRef(processor) {
    addAndMakeVisible(nameLabel);
    nameLabel.setText(name + " Band", juce::dontSendNotification);
    auto font = juce::Font(16.0f);
    font.setBold(true);
    nameLabel.setFont(font);

    addAndMakeVisible(irLoadButton);
    irLoadButton.onClick = [this] { loadIRButtonClicked(); };

    // Volume Slider setup
    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    volumeSlider.setTextValueSuffix(" dB");

    addAndMakeVisible(volumeLabel);
    volumeLabel.setText("Volume", juce::dontSendNotification);
    volumeLabel.attachToComponent(&volumeSlider, false);

    // Set up volume parameter attachment based on band
    juce::String volParamID = bandIdx == 0 ? "lowVol" : (bandIdx == 1 ? "midVol" : "highVol");
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.parameters, volParamID, volumeSlider);

    // Mix Slider setup
    addAndMakeVisible(mixSlider);
    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    mixSlider.setRange(0.0, 100.0, 1.0);
    mixSlider.setValue(50.0);

    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix %", juce::dontSendNotification);
    mixLabel.attachToComponent(&mixSlider, false);

    mixSlider.onValueChange = [this] {
        if (bandIdx < processorRef.bandReverbs.size()) {
            processorRef.bandReverbs[bandIdx].mix = mixSlider.getValue() / 100.0f;
        }
    };

    // Crossover Slider setup
    addAndMakeVisible(crossoverSlider);
    crossoverSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    crossoverSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    crossoverSlider.setTextValueSuffix(" Hz");
    crossoverSlider.setSkewFactorFromMidPoint(1000.0);

    addAndMakeVisible(crossoverLabel);

    // Configure crossover based on band
    if (bandIdx == 0) { // Low band
        crossoverLabel.setText("High Cut", juce::dontSendNotification);
        crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.parameters, "lowCross", crossoverSlider);
    } else if (bandIdx == 1) { // Mid band
        crossoverLabel.setText("High Cut", juce::dontSendNotification);
        crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.parameters, "midCross", crossoverSlider);
    } else { // High band - no crossover control
        crossoverSlider.setVisible(false);
        crossoverLabel.setVisible(false);
    }

    crossoverLabel.attachToComponent(&crossoverSlider, false);

    // Solo/Mute button setup
    addAndMakeVisible(soloButton);
    addAndMakeVisible(muteButton);

    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);

    soloButton.setClickingTogglesState(true);
    muteButton.setClickingTogglesState(true);

    soloButton.onClick = [this] {
        isSoloed = soloButton.getToggleState();
        if (isSoloed) {
            muteButton.setToggleState(false, juce::dontSendNotification);
            isMuted = false;
        }
        // Update the processor's band state directly
        processorRef.bandReverbs[bandIdx].isSoloed = isSoloed;
        processorRef.bandReverbs[bandIdx].isMuted = isMuted;
        processorRef.updateSoloMuteStates();
    };

    muteButton.onClick = [this] {
        isMuted = muteButton.getToggleState();
        if (isMuted) {
            soloButton.setToggleState(false, juce::dontSendNotification);
            isSoloed = false;
        }
        // Update the processor's band state directly
        processorRef.bandReverbs[bandIdx].isSoloed = isSoloed;
        processorRef.bandReverbs[bandIdx].isMuted = isMuted;
        processorRef.updateSoloMuteStates();
    };
}

BandControls::~BandControls() { processorRef.bandReverbs[bandIdx].convolution->reset(); }

void BandControls::loadIRButtonClicked() {
    fileChooser = std::make_unique<juce::FileChooser>("Select an IR file...", juce::File{}, "*.wav;*.aif;*.aiff");

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser &fc) {
        auto file = fc.getResult();
        if (file != juce::File{}) {
            processorRef.loadImpulseResponse(bandIdx, file);
            irLoadButton.setButtonText(file.getFileNameWithoutExtension());
        }
    });
}

void BandControls::paint(juce::Graphics &g) {
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
}

void BandControls::resized() {
    auto area = getLocalBounds().reduced(10);

    // Top row with name and solo/mute buttons
    auto topRow = area.removeFromTop(20);
    nameLabel.setBounds(topRow.removeFromLeft(topRow.getWidth() - 60));
    soloButton.setBounds(topRow.removeFromLeft(30));
    muteButton.setBounds(topRow);

    auto controlArea = area.reduced(10);
    irLoadButton.setBounds(controlArea.removeFromTop(30));

    controlArea.removeFromTop(10);

    // Split remaining area for sliders
    auto sliderArea = controlArea.removeFromTop(controlArea.getHeight() / 2);

    // Position sliders side by side if crossover is visible
    if (crossoverSlider.isVisible()) {
        auto sliderWidth = sliderArea.getWidth() / 3;
        volumeSlider.setBounds(sliderArea.removeFromLeft(sliderWidth));
        mixSlider.setBounds(sliderArea.removeFromLeft(sliderWidth));
        crossoverSlider.setBounds(sliderArea);
    } else {
        auto sliderWidth = sliderArea.getWidth() / 2;
        volumeSlider.setBounds(sliderArea.removeFromLeft(sliderWidth));
        mixSlider.setBounds(sliderArea);
    }
}