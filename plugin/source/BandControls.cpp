#include "MultibandReverb/BandControls.h"
#include "MultibandReverb/PluginProcessor.h"

BandControls::BandControls(int bandIndex,
                            MultibandReverbAudioProcessor &processor,
                            std::function<void(int)> onDelete)
    : bandIdx(bandIndex),
      processorRef(processor),
      deleteCallback(std::move(onDelete))
{
    // Name label.
    addAndMakeVisible(nameLabel);
    nameLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    nameLabel.setJustificationType(juce::Justification::centredLeft);

    // IR load button.
    addAndMakeVisible(irLoadButton);
    irLoadButton.onClick = [this] { loadIRButtonClicked(); };

    // Delete button.
    addAndMakeVisible(deleteButton);
    deleteButton.setColour(juce::TextButton::buttonColourId,
                           juce::Colours::darkred.withAlpha(0.6f));
    deleteButton.onClick = [this] {
        if (deleteCallback) deleteCallback(bandIdx);
    };

    // Solo / Mute.
    addAndMakeVisible(soloButton);
    addAndMakeVisible(muteButton);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    soloButton.setClickingTogglesState(true);
    muteButton.setClickingTogglesState(true);

    // Volume slider.
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(volumeLabel);
    volumeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 55, 16);
    volumeSlider.setTextValueSuffix(" dB");
    volumeLabel.setText("Vol", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

    // Mix slider.
    addAndMakeVisible(mixSlider);
    addAndMakeVisible(mixLabel);
    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 55, 16);
    mixSlider.setTextValueSuffix(" %");
    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    mixLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

    setBandIndex(bandIndex);
}

BandControls::~BandControls() {}

void BandControls::setBandIndex(int newIndex) {
    bandIdx = newIndex;

    // Detach old APVTS attachments before re-attaching.
    volumeAttachment.reset();
    mixAttachment.reset();
    soloAttachment.reset();
    muteAttachment.reset();

    nameLabel.setText("Band " + juce::String(bandIdx + 1), juce::dontSendNotification);

    juce::String idx(bandIdx);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "band" + idx + "_vol", volumeSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "band" + idx + "_mix", mixSlider);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, "band" + idx + "_solo", soloButton);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, "band" + idx + "_mute", muteButton);
}

void BandControls::loadIRButtonClicked() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an IR file...", juce::File{}, "*.wav;*.aif;*.aiff");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                processorRef.loadImpulseResponse(bandIdx, file);
                irLoadButton.setButtonText(file.getFileNameWithoutExtension());
            }
        });
}

void BandControls::paint(juce::Graphics &g) {
    const auto b  = getLocalBounds().toFloat();

    // Panel background: slightly lighter than the editor navy.
    g.setColour(juce::Colour(0xff111128));
    g.fillRoundedRectangle(b, 8.0f);

    // Coloured border that matches the band's spectrum colour.
    const juce::Colour bandColours[] = {
        juce::Colour(0xff6ec6f5), // light blue
        juce::Colour(0xfff5a623), // orange
        juce::Colour(0xff7ed321), // green
        juce::Colour(0xffbd10e0), // purple
        juce::Colour(0xffe91e63), // pink
        juce::Colour(0xff00bcd4), // cyan
        juce::Colour(0xffffeb3b), // yellow
        juce::Colour(0xff4caf50), // mid green
    };
    const juce::Colour col = bandColours[static_cast<size_t>(bandIdx) % 8];
    g.setColour(col.withAlpha(0.35f));
    g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.2f);
}

void BandControls::resized() {
    auto area = getLocalBounds().reduced(8);

    // Top row: name | S | M | X
    auto topRow = area.removeFromTop(22);
    nameLabel.setBounds(topRow.removeFromLeft(topRow.getWidth() - 72));
    deleteButton.setBounds(topRow.removeFromRight(22));
    topRow.removeFromRight(2);
    muteButton.setBounds(topRow.removeFromRight(22));
    topRow.removeFromRight(2);
    soloButton.setBounds(topRow.removeFromRight(22));

    area.removeFromTop(4);

    // IR button.
    irLoadButton.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);

    // Sliders side by side with labels above.
    const int halfW = area.getWidth() / 2;

    auto volArea = area.removeFromLeft(halfW);
    volumeLabel.setBounds(volArea.removeFromBottom(14));
    volumeSlider.setBounds(volArea);

    mixLabel.setBounds(area.removeFromBottom(14));
    mixSlider.setBounds(area);
}
