#pragma once
#include <JuceHeader.h>

// SpectrumAnalyzer.h
class SpectrumAnalyzer : public juce::Component,
                        public juce::Timer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
    void pushBuffer(const float* data, int size);
    
private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    
    std::array<float, 2048> fftData;
    std::array<float, 2048> fifo;
    std::array<float, 2048> smoothedFFTData;  // For temporal smoothing
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    
    float temporalSmoothing = 0.8f;    // Smoothing factor (0 to 1)
    int spectralAveraging = 3;         // Number of bins to average
    
    float getSmoothedValueForFrequency(float freq, float minFreq, float maxFreq, int width);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
