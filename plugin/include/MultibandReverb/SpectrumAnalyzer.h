#pragma once
#include <JuceHeader.h>

class MultibandReverbAudioProcessor;

// SpectrumAnalyzer.h
class SpectrumAnalyzer : public juce::Component, public juce::Timer {
  public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;

    void paint(juce::Graphics &g) override;
    void resized() override;
    void timerCallback() override;

    void pushBuffer(const float *data, int size);
    void setCrossoverFrequencies(float lowCross, float midCross);

    // Add processor connection
    void setProcessor(MultibandReverbAudioProcessor *p) { audioProcessor = p; }

    // Add mouse interaction methods
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;

  private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    float lowCrossoverFreq = 250.0f;  // Default value
    float midCrossoverFreq = 2500.0f; // Default value

    std::array<float, 2048> fftData;
    std::array<float, 2048> fifo;
    std::array<float, 2048> smoothedFFTData; // For temporal smoothing
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    float temporalSmoothing = 0.8f; // Smoothing factor (0 to 1)
    int spectralAveraging = 3;      // Number of bins to average

    float getSmoothedValueForFrequency(float freq, float minFreq, float maxFreq, int width);

    // Helper methods for crossover dragging
    float getFrequencyForX(float x);
    float getXForFrequency(float freq);
    bool isNearCrossover(float x, float crossoverX, float tolerance = 5.0f);

    // Dragging state
    enum DraggedCrossover { None, Low, Mid };
    DraggedCrossover currentDrag = None;

    MultibandReverbAudioProcessor *audioProcessor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};