#include "MultibandReverb/PluginEditor.h"

// Fixed window dimensions. Bands scroll horizontally inside the viewport.
static constexpr int WINDOW_W    = 800;
static constexpr int WINDOW_H    = 680;
static constexpr int TRANSPORT_H =  70;
static constexpr int ANALYZER_H  = 290;
static constexpr int TOOLBAR_H   =  36;
static constexpr int BAND_H      = 200;
static constexpr int MARGIN      =  12;

MultibandReverbAudioProcessorEditor::MultibandReverbAudioProcessorEditor(
    MultibandReverbAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(WINDOW_W, WINDOW_H);
    setResizable(false, false);

    processorRef.analyzer.store(&analyzer);
    analyzer.setProcessor(&processorRef);
    addAndMakeVisible(analyzer);

    addAndMakeVisible(processorRef.transportComponent);
    if (!juce::JUCEApplication::getInstance()->isStandaloneApp())
        processorRef.transportComponent.setVisible(false);

    // Add band button.
    addAndMakeVisible(addBandButton);
    addBandButton.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::darkgreen.withAlpha(0.8f));
    addBandButton.setTooltip("Add band");
    addBandButton.onClick = [this] { onAddBand(); };

    // Speed slider: controls FFT smoothing time constant in milliseconds.
    // Lower ms = faster/rawer. Higher ms = slower/smoother.
    addAndMakeVisible(smoothingSlider);
    addAndMakeVisible(smoothingLabel);
    smoothingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothingSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    smoothingSlider.setRange(20.0, 600.0, 10.0);
    smoothingSlider.setValue(150.0, juce::dontSendNotification);
    smoothingSlider.setTooltip("Speed: left = faster/rawer (20ms), right = slower/smoother (600ms)");
    smoothingSlider.onValueChange = [this] {
        analyzer.setSmoothingTime(static_cast<float>(smoothingSlider.getValue()));
    };
    smoothingLabel.setText("Speed", juce::dontSendNotification);
    smoothingLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    smoothingLabel.setColour(juce::Label::textColourId,
                             juce::Colours::white.withAlpha(0.6f));
    smoothingLabel.setJustificationType(juce::Justification::centredRight);

    // Only apply the 3-band default on a fresh instantiation.
    // If the host loaded saved state first, stateWasLoaded is true
    // and we leave whatever bands were restored untouched.
    if (!processorRef.wasStateLoaded()) {
        const int defaultBands = 3;
        for (int i = processorRef.getNumBands(); i < defaultBands; ++i)
            processorRef.addBand();
    }

    // Viewport for scrollable band strip.
    bandViewport.setViewedComponent(&bandStrip, false);
    bandViewport.setScrollBarsShown(false, true);
    bandViewport.setScrollBarThickness(8);
    addAndMakeVisible(bandViewport);

    // Processor notifies us when band layout changes (e.g. setStateInformation).
    processorRef.onBandLayoutChanged = [this] {
        juce::MessageManager::callAsync([this] { rebuildBandControls(); });
    };

    // Build the initial band panel layout.
    rebuildBandControls();
}

MultibandReverbAudioProcessorEditor::~MultibandReverbAudioProcessorEditor() {
    processorRef.analyzer.store(nullptr);
    processorRef.onBandLayoutChanged = nullptr;
}

// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessorEditor::rebuildBandControls() {
    // Detach all existing band controls from the strip.
    for (auto &bc : bandStrip.bandControls)
        bandStrip.removeChildComponent(bc.get());
    bandStrip.bandControls.clear();

    const int numBands = processorRef.getNumBands();
    for (int i = 0; i < numBands; ++i) {
        auto bc = std::make_unique<BandControls>(
            i, processorRef,
            [this](int idx) { onDeleteBand(idx); });
        bandStrip.addAndMakeVisible(*bc);
        bandStrip.bandControls.push_back(std::move(bc));
    }

    resized(); // reflow so the strip and viewport get correct sizes
}

void MultibandReverbAudioProcessorEditor::onAddBand() {
    processorRef.addBand();
}

void MultibandReverbAudioProcessorEditor::onDeleteBand(int bandIndex) {
    if (processorRef.getNumBands() <= 1)
        return;
    processorRef.removeBand(bandIndex);
}

// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessorEditor::paint(juce::Graphics &g) {
    const auto bounds = getLocalBounds();
    const float fw = static_cast<float>(bounds.getWidth());
    const float fh = static_cast<float>(bounds.getHeight());

    // Deep navy base matching the analyzer background.
    g.fillAll(juce::Colour(0xff0a0a1a));

    // Subtle radial glow in the upper area behind the analyzer.
    juce::ColourGradient glow(
        juce::Colour(0xff1a2a5a).withAlpha(0.45f), fw * 0.35f, fh * 0.15f,
        juce::Colour(0xff0a0a1a).withAlpha(0.0f),  fw * 0.85f, fh * 0.55f,
        true);
    g.setGradientFill(glow);
    g.fillRect(bounds);
}

void MultibandReverbAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(MARGIN);

    const bool isStandalone = juce::JUCEApplication::getInstance()->isStandaloneApp();

    if (isStandalone)
        processorRef.transportComponent.setBounds(area.removeFromTop(TRANSPORT_H));

    area.removeFromTop(MARGIN);
    analyzer.setBounds(area.removeFromTop(ANALYZER_H));
    area.removeFromTop(MARGIN);

    // Toolbar: [+] button on left, smoothing slider on right.
    auto toolbar = area.removeFromTop(TOOLBAR_H);
    addBandButton.setBounds(toolbar.removeFromLeft(36).withSizeKeepingCentre(32, 26));
    toolbar.removeFromLeft(8);
    smoothingLabel.setBounds(toolbar.removeFromLeft(48).withSizeKeepingCentre(48, 16));
    smoothingSlider.setBounds(toolbar.removeFromLeft(120).withSizeKeepingCentre(120, 20));

    area.removeFromTop(MARGIN);

    // The viewport fills the remaining space. The band strip inside it
    // sizes itself wide enough for all bands and scrolls if needed.
    bandViewport.setBounds(area.removeFromTop(BAND_H));

    const int numBands = static_cast<int>(bandStrip.bandControls.size());
    const int bandW    = 160;
    const int spacing  = 8;
    const int stripW   = juce::jmax(bandViewport.getWidth(),
                                    numBands * bandW + (numBands - 1) * spacing);
    bandStrip.setSize(stripW, BAND_H);

    // Layout the band controls inside the strip.
    int x = 0;
    for (auto &bc : bandStrip.bandControls) {
        bc->setBounds(x, 0, bandW, BAND_H);
        x += bandW + spacing;
    }
}
