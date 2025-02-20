#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/PluginEditor.h"

//==============================================================================
// BandControls Implementation
BandControls::BandControls(const juce::String& bandName, size_t bandIndex, 
                         MultibandReverbAudioProcessor& processor,
                         juce::AudioProcessorValueTreeState& params)
    : name(bandName)
    , bandIdx(bandIndex)
    , processorRef(processor)
{
    // IR Load Button
    addAndMakeVisible(irLoadButton);
    irLoadButton.setButtonText("Load IR");
    irLoadButton.onClick = [this] { loadIRButtonClicked(); };

    // Mix Slider
    addAndMakeVisible(mixSlider);
    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    
    // Volume Slider
    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

    // Labels
    addAndMakeVisible(nameLabel);
    nameLabel.setText(name + " Band", juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centred);
    auto font = juce::Font(16.0f);
    font.setBold(true);
    nameLabel.setFont(font);

    addAndMakeVisible(mixLabel);
    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(volumeLabel);
    volumeLabel.setText("Volume", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centred);

    // Create attachments
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "mix" + juce::String(bandIdx), mixSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        params, "vol" + juce::String(bandIdx), volumeSlider);
}

void BandControls::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::darkgrey);
    g.fillRoundedRectangle(bounds, 10.0f);
    
    g.setColour(juce::Colours::white);
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
}

void BandControls::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    // Band name at top
    nameLabel.setBounds(bounds.removeFromTop(25));
    
    bounds.removeFromTop(5); // Spacing
    
    // IR Load button
    irLoadButton.setBounds(bounds.removeFromTop(30));
    
    bounds.removeFromTop(10); // Spacing
    
    // Create a horizontal layout for mix and volume
    auto controlArea = bounds;
    auto mixArea = controlArea.removeFromLeft(controlArea.getWidth() / 2);
    
    // Mix controls
    mixLabel.setBounds(mixArea.removeFromTop(20));
    mixArea.removeFromTop(5);
    mixSlider.setBounds(mixArea);
    
    // Volume controls
    volumeLabel.setBounds(controlArea.removeFromTop(20));
    controlArea.removeFromTop(5);
    volumeSlider.setBounds(controlArea);
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

//==============================================================================
// SpectrumAnalyzer Implementation
SpectrumAnalyzer::SpectrumAnalyzer() 
    : fft(11),  // 2048 points
      window(2048, juce::dsp::WindowingFunction<float>::hann)
{
    std::cout << "SpectrumAnalyzer created" << std::endl;
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
    
    auto bounds = getLocalBounds();
    auto width = bounds.getWidth();
    auto height = bounds.getHeight();
    
    // Create path for spectrum
    juce::Path spectrumPath;
    
    auto minFreq = 20.0f;
    auto maxFreq = 20000.0f;
    const float minDb = -100.0f;
    const float maxDb = 12.0f;  // Extend range to +12dB
    
    // Start the path at the bottom-left
    spectrumPath.startNewSubPath(0, (float)height);
    
    // Create points for the curve
    for (int x = 0; x < width; x += 2)
    {
        auto freq = std::exp(std::log(minFreq) + (std::log(maxFreq) - std::log(minFreq)) * x / width);
        auto level = getSmoothedValueForFrequency(freq, minFreq, maxFreq, width);
        auto dbLevel = juce::Decibels::gainToDecibels(level, minDb);
        auto normalizedLevel = juce::jmap(dbLevel, minDb, maxDb, 0.0f, 0.7f);
        
        spectrumPath.lineTo(x, height * (1.0f - normalizedLevel));
    }
    
    // Complete the path
    spectrumPath.lineTo(width, (float)height);
    spectrumPath.closeSubPath();
    
    // Draw spectrum with gradient fill
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::lightblue.withAlpha(0.8f), 0, 0,
        juce::Colours::lightblue.withAlpha(0.2f), 0, (float)height,
        false));
        
    // Smooth the path
    auto smoothedPath = spectrumPath.createPathWithRoundedCorners(5.0f);
    g.fillPath(smoothedPath);
    
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
    const int levels[] = {12, 0, -12, -24, -36, -48, -60};
    for (int level : levels)
    {
        float normalizedY = juce::jmap((float)level, minDb, maxDb, 1.0f, 0.0f);
        float y = height * normalizedY;
        g.drawHorizontalLine((int)y, 0.0f, (float)width);
        
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawText(juce::String(level) + "dB", 
                  width - 35, (int)y - 10, 30, 20, 
                  juce::Justification::right);
    }
}

void SpectrumAnalyzer::timerCallback()
{
    if (nextFFTBlockReady)
    {
        window.multiplyWithWindowingTable(fftData.data(), fftData.size());
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        
        // Apply temporal smoothing
        for (size_t i = 0; i < fftData.size(); ++i)
        {
            smoothedFFTData[i] = smoothedFFTData[i] * temporalSmoothing + 
                                fftData[i] * (1.0f - temporalSmoothing);
        }
        
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
            
            if (fifoIndex >= fifo.size())
            {
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;
                fifoIndex = 0;
            }
        }
    }
}

float SpectrumAnalyzer::getSmoothedValueForFrequency(float freq, float minFreq, float maxFreq, int width)
{
    auto binFreq = freq * 2048.0f / 44100.0f;  // Assuming 44.1kHz sample rate
    int bin = juce::jlimit(0, 1024, (int)binFreq);
    
    float sum = 0.0f;
    int count = 0;
    
    // Average over nearby bins for smoother display
    for (int i = -spectralAveraging; i <= spectralAveraging; ++i)
    {
        if (bin + i >= 0 && bin + i < 1024)
        {
            sum += smoothedFFTData[bin + i];
            ++count;
        }
    }
    
    return count > 0 ? sum / count : 0.0f;
}

void SpectrumAnalyzer::resized()
{
}


//==============================================================================
MultibandReverbAudioProcessorEditor::MultibandReverbAudioProcessorEditor(MultibandReverbAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processorRef(p)
    , analyzer()
    , lowBand("Low", 0, p, p.parameters)
    , highBand("High", 1, p, p.parameters)
{
    setSize(800, 700);
    
    processorRef.analyzer = &analyzer;
    
    // Add and make visible all components
    addAndMakeVisible(lowBand);
    addAndMakeVisible(highBand);
    addAndMakeVisible(crossoverSlider);
    
    // Set up crossover slider
    crossoverSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    crossoverSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    crossoverSlider.setRange(20.0, 20000.0, 1.0);
    
    // Create attachment for crossover
    crossoverAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "crossover", crossoverSlider);
        
    // Connect analyzer
    addAndMakeVisible(analyzer);
}

MultibandReverbAudioProcessorEditor::~MultibandReverbAudioProcessorEditor()
{
    processorRef.analyzer = nullptr;
}

void MultibandReverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void MultibandReverbAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Set the bounds for the spectrum analyzer
    analyzer.setBounds(bounds.removeFromTop(200));

    // Set the bounds for the crossover slider
    auto crossoverBounds = bounds.removeFromTop(50);
    crossoverBounds.reduce(10, 0);
    crossoverSlider.setBounds(crossoverBounds);

    // Set the bounds for the band controls
    auto bandControlsWidth = bounds.getWidth() / 2;
    auto bandControlsHeight = bounds.getHeight();

    auto lowBandBounds = bounds.removeFromLeft(bandControlsWidth);
    lowBandBounds.reduce(10, 10);
    lowBand.setBounds(lowBandBounds);

    auto highBandBounds = bounds;
    highBandBounds.reduce(10, 10);
    highBand.setBounds(highBandBounds);
}
