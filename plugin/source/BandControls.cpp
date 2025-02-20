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
    nameLabel.setBounds(area.removeFromTop(20));

    auto controlArea = area.reduced(10);
    irLoadButton.setBounds(controlArea.removeFromTop(30));

    controlArea.removeFromTop(10);
    mixSlider.setBounds(controlArea);
}
