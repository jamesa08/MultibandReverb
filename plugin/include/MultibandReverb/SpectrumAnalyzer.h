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

    // dB decay per timer tick (called at 30Hz). Higher = faster fall.
    // 0.5 = very slow, 3.0 = fast, matches original GMPI implementation at ~4.0
    void setDecayRate(float dbPerTick) { decayDbPerTick = juce::jlimit(0.1f, 12.0f, dbPerTick); }

    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseMove(const juce::MouseEvent &e) override;

  private:
    static constexpr int FFT_SIZE       = 2048;
    static constexpr int HISTORY_FRAMES = 4;
    static constexpr int HISTORY_DROP   = 6;
    static constexpr float HISTORY_FADE = 0.5f;

    juce::dsp::FFT                      fft;
    juce::dsp::WindowingFunction<float> window;

    // Output (post-processing) pipeline.
    std::array<float, FFT_SIZE> outFifo    {};
    std::array<float, FFT_SIZE> outFftData {};
    int   outFifoIndex = 0;
    bool  outFftReady  = false;

    // Input (pre-processing / dry) pipeline.
    std::array<float, FFT_SIZE> inFifo    {};
    std::array<float, FFT_SIZE> inFftData {};
    int  inFifoIndex = 0;
    bool inFftReady  = false;

    // Two-array decay approach (from GMPI FreqAnalyser):
    //   outIn  = raw FFT magnitude values from latest frame (gain domain)
    //   outDisp = displayed values; can only fall by decayDbPerTick dB per tick,
    //             never below outIn. This gives the smooth slow-fall look.
    std::array<float, FFT_SIZE> outIn  {};
    std::array<float, FFT_SIZE> outDisp{};
    std::array<float, FFT_SIZE> inIn   {};
    std::array<float, FFT_SIZE> inDisp {};

    // dB drop per timer tick. Slider controls this.
    float decayDbPerTick = 1.5f;

    // History ring buffer for ghost trails.
    std::array<std::array<float, FFT_SIZE>, HISTORY_FRAMES> historyFrames{};
    int  historyHead  = 0;
    int  historyCount = 0;

    double sampleRate = 44100.0;

    juce::CriticalSection crossoverMutex;
    std::vector<float>    crossoverFreqs;

    int   draggedIndex      = -1;
    int   draggedVolumeBand = -1;
    float lastDragY         = 0.0f;

    float getFrequencyForX(float x) const;
    float getXForFrequency(float freq) const;
    int   hitTestCrossover(float x) const;
    int   hitTestVolumeLine(float mx, float my) const;
    float getBinValue(const std::array<float, FFT_SIZE> &data, float freq) const;
    juce::Colour getBandColourForFrequency(float freq,
                                           const std::vector<float> &bandBounds) const;

    MultibandReverbAudioProcessor *audioProcessor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
