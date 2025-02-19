// PluginEditor.cpp
#include "MultibandReverb/PluginEditor.h"

//==============================================================================
BandControls::BandControls(const juce::String& bandName, size_t bandIndex, 
                          MultibandReverbAudioProcessor& processor)
    : name(bandName), bandIdx(bandIndex), processorRef(processor)
{
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
}

void BandControls::loadIRButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an IR file...",
        juce::File{},
        "*.wav;*.aif;*.aiff"
    );

    auto folderChooserFlags = juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            processorRef.loadImpulseResponse(bandIdx, file);
            irLoadButton.setButtonText(file.getFileNameWithoutExtension());
        }
    });
}

void BandControls::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
}

void BandControls::resized()
{
    auto area = getLocalBounds().reduced(10);
    nameLabel.setBounds(area.removeFromTop(20));
    
    auto controlArea = area.reduced(10);
    irLoadButton.setBounds(controlArea.removeFromTop(30));
    
    controlArea.removeFromTop(10);
    mixSlider.setBounds(controlArea);
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer() 
    : fft(11),  // 2048 points
      window(2048, juce::dsp::WindowingFunction<float>::hann)
{
    startTimerHz(60); // 60 fps refresh rate
    setOpaque(true);
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
}

void SpectrumAnalyzer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    
    auto bounds = getLocalBounds();
    auto width = bounds.getWidth();
    auto height = bounds.getHeight();
    
    // Draw frequency spectrum
    g.setColour(juce::Colours::lightblue);
    
    auto minFreq = 20.0f;
    auto maxFreq = 20000.0f;
    
    for (int i = 1; i < width; ++i)
    {
        auto freq = std::exp(std::log(minFreq) + (std::log(maxFreq) - std::log(minFreq)) * i / width);
        auto fftIndex = juce::jlimit(0, 1024, (int)(freq * 2048.0 / 44100.0));
        
        auto level = juce::Decibels::gainToDecibels(fftData[static_cast<size_t>(fftIndex)]);
        auto normalizedLevel = juce::jmap(level, -100.0f, 0.0f, 0.0f, 1.0f);
        
        g.drawVerticalLine(i, height * (1.0f - normalizedLevel), (float)height);
    }
    
    // Draw grid lines
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    
    // Frequency grid lines
    const int freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int freq : freqs)
    {
        auto x = std::log(freq / minFreq) / std::log(maxFreq / minFreq) * width;
        g.drawVerticalLine((int)x, 0.0f, (float)height);
        
        g.setFont(12.0f);
        g.drawText(juce::String(freq) + "Hz", 
                  (int)x - 20, height - 20, 40, 20, 
                  juce::Justification::centred);
    }
    
    // Level grid lines
    const int levels[] = {0, -12, -24, -36, -48};
    for (int level : levels)
    {
        float normalizedY = (level + 100.0f) / 100.0f;  // normalize between 0 and 1
        float y = height * (1.0f - normalizedY);
        g.drawHorizontalLine((int)y, 0.0f, (float)width);
        
        g.drawText(juce::String(level) + "dB", 
                  width - 35, (int)y - 10, 30, 20, 
                  juce::Justification::right);
    }
}

void SpectrumAnalyzer::timerCallback()
{
    if (nextFFTBlockReady)
    {
        window.multiplyWithWindowingTable(fftData.data(), 2048);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        nextFFTBlockReady = false;
        repaint();
    }
}

void SpectrumAnalyzer::pushBuffer(const float* data, int size)
{
    if (size > 0)
    {
        for (int i = 0; i < size; ++i)
        {
            fifo[static_cast<size_t>(fifoIndex)] = data[i];
            ++fifoIndex;
            
            if (fifoIndex >= 2048)
            {
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;
                fifoIndex = 0;
            }
        }
    }
}

void SpectrumAnalyzer::resized()
{
}

//==============================================================================
MultibandReverbAudioProcessorEditor::MultibandReverbAudioProcessorEditor(MultibandReverbAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(800, 700);  // Made taller
    
    // Transport controls
    addAndMakeVisible(processorRef.transportComponent);
    
    // Set up crossover frequency sliders
    addAndMakeVisible(lowCrossoverSlider);
    addAndMakeVisible(midCrossoverSlider);
    
    lowCrossoverSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lowCrossoverSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    lowCrossoverSlider.setRange(20.0, 2000.0, 1.0);
    
    midCrossoverSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    midCrossoverSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    midCrossoverSlider.setRange(1000.0, 20000.0, 1.0);
    
    // Connect parameters
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "lowCross", lowCrossoverSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "midCross", midCrossoverSlider));
    
    // Add band controls
    addAndMakeVisible(lowBand);
    addAndMakeVisible(midBand);
    addAndMakeVisible(highBand);
    
    // Add spectrum analyzer
    addAndMakeVisible(analyzer);
}

MultibandReverbAudioProcessorEditor::~MultibandReverbAudioProcessorEditor()
{
}

void MultibandReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void MultibandReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    // Transport controls at the very top
    processorRef.transportComponent.setBounds(bounds.removeFromTop(70));
    
    bounds.removeFromTop(20); // Spacing
    
    // Spectrum analyzer below transport
    analyzer.setBounds(bounds.removeFromTop(200));
    
    bounds.removeFromTop(20); // Spacing
    
    // Crossover controls
    auto crossoverBounds = bounds.removeFromTop(60);
    lowCrossoverSlider.setBounds(crossoverBounds.removeFromTop(25));
    midCrossoverSlider.setBounds(crossoverBounds.removeFromTop(25));
    
    bounds.removeFromTop(20); // Spacing
    
    // Band controls
    auto bandWidth = bounds.getWidth() / 3;
    lowBand.setBounds(bounds.removeFromLeft(bandWidth).reduced(5));
    midBand.setBounds(bounds.removeFromLeft(bandWidth).reduced(5));
    highBand.setBounds(bounds.reduced(5));
}

