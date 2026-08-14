#pragma once
#include <JuceHeader.h>

class MultibandReverbAudioProcessor;

class SpectrumAnalyzer : public juce::Component, public juce::Timer {
  public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() override;

    void paint(juce::Graphics &g) override;
    void resized() override;
    void timerCallback() override;

    void pushInputBuffer(const float *data, int size);
    void pushBuffer(const float *data, int size);

    void setCrossoverFrequencies(const std::vector<float> &freqs);
    void setSampleRate(double sr) { sampleRate = sr; }
    void setProcessor(MultibandReverbAudioProcessor *p) { audioProcessor = p; }
    void setDecayRate(float dbPerTick) { decayDbPerTick = juce::jlimit(0.1f, 12.0f, dbPerTick); }

    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseMove(const juce::MouseEvent &e) override;

  private:
    static constexpr int   FFT_ORDER     = 11;
    static constexpr int   FFT_SIZE      = 1 << FFT_ORDER; // 2048
    static constexpr int   HISTORY_FRAMES = 4;
    static constexpr int   HISTORY_DROP  = 6;
    static constexpr float HISTORY_FADE  = 0.5f;

    juce::dsp::FFT                      fft;
    juce::dsp::WindowingFunction<float> window;

    // All large buffers on the heap to avoid Rosetta layout issues with
    // large inline arrays (104KB+ of class data caused SIGSEGV under Rosetta).
    std::vector<float> outFifo;
    std::vector<float> outFftData;
    std::vector<float> outIn;
    std::vector<float> outDisp;

    std::vector<float> inFifo;
    std::vector<float> inFftData;
    std::vector<float> inIn;
    std::vector<float> inDisp;

    // History ring buffer: vector of vectors.
    std::vector<std::vector<float>> historyFrames;

    // Pre-rendered + blurred ghost images, one per history slot.
    // Rebuilt only when a new FFT frame arrives (not every paint call).
    std::vector<juce::Image> ghostImages;
    bool ghostImagesDirty = true; // force rebuild on first paint

    int   outFifoIndex = 0;
    bool  outFftReady  = false;
    int   inFifoIndex  = 0;
    bool  inFftReady   = false;
    int   historyHead  = 0;
    int   historyCount = 0;

    float decayDbPerTick = 1.5f;
    float grainTime      = 0.0f; // advances each paint, drives grain drift animation
    double sampleRate    = 44100.0;

    juce::CriticalSection crossoverMutex;
    std::vector<float>    crossoverFreqs;

    int   draggedIndex      = -1;
    int   draggedVolumeBand = -1;
    float lastDragY         = 0.0f;

    float getFrequencyForX(float x) const;
    float getXForFrequency(float freq) const;
    int   hitTestCrossover(float x) const;
    int   hitTestVolumeLine(float mx, float my) const;
    float getBinValue(const std::vector<float> &data, float freq) const;
    juce::Colour getBandColourForFrequency(float freq,
                                           const std::vector<float> &bandBounds) const;

    MultibandReverbAudioProcessor *audioProcessor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
