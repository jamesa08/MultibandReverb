// PluginEditor.cpp
#include "MultibandReverb/PluginEditor.h"

//==============================================================================
MultibandReverbAudioProcessorEditor::MultibandReverbAudioProcessorEditor(MultibandReverbAudioProcessor &p) : AudioProcessorEditor(&p), processorRef(p) {

    // Connect analyzer (atomic store so processBlock sees it safely)
    processorRef.analyzer.store(&analyzer);
    analyzer.setProcessor(&processorRef);

    // Transport controls
    addAndMakeVisible(processorRef.transportComponent);
    if (!JUCEApplication::getInstance()->isStandaloneApp()) {
        setSize(800, 530);
        processorRef.transportComponent.setVisible(false);
    } else {
        setSize(800, 600);
    }

    // Set up crossover frequency sliders

    addAndMakeVisible(lowCrossoverSlider);
    addAndMakeVisible(midCrossoverSlider);
    lowCrossoverSlider.setVisible(false);
    midCrossoverSlider.setVisible(false);

    // Configure Low Crossover Knob
    lowCrossoverSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    lowCrossoverSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    lowCrossoverSlider.setRange(20.0, 20000.0, 1.0);
    lowCrossoverSlider.setSkewFactorFromMidPoint(1000.0); // Make the knob more natural for frequency control
    lowCrossoverSlider.setTextValueSuffix(" Hz");

    // Configure Mid Crossover Knob
    midCrossoverSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    midCrossoverSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    midCrossoverSlider.setRange(250.0, 20000.0, 1.0);
    midCrossoverSlider.setSkewFactorFromMidPoint(2500.0); // Make the knob more natural for frequency control
    midCrossoverSlider.setTextValueSuffix(" Hz");

    // Connect parameters
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.parameters, "lowCross", lowCrossoverSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.parameters, "midCross", midCrossoverSlider));

    // Add band controls
    addAndMakeVisible(lowBand);
    addAndMakeVisible(midBand);
    addAndMakeVisible(highBand);

    // Add spectrum analyzer
    addAndMakeVisible(analyzer);
}

MultibandReverbAudioProcessorEditor::~MultibandReverbAudioProcessorEditor() { processorRef.analyzer.store(nullptr); }

void MultibandReverbAudioProcessorEditor::paint(juce::Graphics &g) { g.fillAll(juce::Colours::darkgrey); }

void MultibandReverbAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds().reduced(20);

    // Transport controls at the very top
    if (JUCEApplication::getInstance()->isStandaloneApp()) {
        processorRef.transportComponent.setBounds(bounds.removeFromTop(70));
    }

    bounds.removeFromTop(20); // Spacing

    // Spectrum analyzer below transport
    analyzer.setBounds(bounds.removeFromTop(200));

    bounds.removeFromTop(20); // Spacing

    // // Crossover controls
    // auto crossoverBounds = bounds.removeFromTop(60);
    // lowCrossoverSlider.setBounds(crossoverBounds.removeFromTop(25));
    // midCrossoverSlider.setBounds(crossoverBounds.removeFromTop(25));

    // Band controls
    auto bandWidth = bounds.getWidth() / 3;
    lowBand.setBounds(bounds.removeFromLeft(bandWidth).reduced(5));
    midBand.setBounds(bounds.removeFromLeft(bandWidth).reduced(5));
    highBand.setBounds(bounds.reduced(5));
}
