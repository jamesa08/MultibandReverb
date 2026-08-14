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

    // Push pre-processing (dry input) mono samples.
    void pushInputBuffer(const float *data, int size);
    // Push post-processing (wet output) mono samples.
    void pushBuffer(const float *data, int size);

    void setCrossoverFrequencies(const std::vector<float> &freqs);
    // smoothingMs: time constant in ms. 50=fast/raw, 150=natural, 500+=very slow.
    void setSmoothingTime(float ms) {
        smoothingTauMs = juce::jlimit(10.0f, 1000.0f, ms);
        updateLambda();
    }
    void setSampleRate(double sr) { sampleRate = sr; updateLambda(); }
    void setProcessor(MultibandReverbAudioProcessor *p) { audioProcessor = p; }

    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseMove(const juce::MouseEvent &e) override;

  private:
    // ---------------------------------------------------------------------------
    // FFT pipeline (shared size for input and output)
    // ---------------------------------------------------------------------------
    static constexpr int FFT_SIZE       = 2048;
    static constexpr int HISTORY_FRAMES = 4;     // ghost trail depth
    static constexpr int HISTORY_DROP   = 10;     // pixels each frame falls
    static constexpr float HISTORY_FADE = 1.0f; // alpha multiplier per frame - fades fast

    juce::dsp::FFT                          fft;
    juce::dsp::WindowingFunction<float>     window;

    // Output (post-processing) pipeline.
    std::array<float, FFT_SIZE> outFifo    {};
    std::array<float, FFT_SIZE> outFftData {};
    std::array<float, FFT_SIZE> outSmoothed{};
    int   outFifoIndex      = 0;
    bool  outFftReady       = false;

    // Input (pre-processing / dry) pipeline.
    std::array<float, FFT_SIZE> inFifo    {};
    std::array<float, FFT_SIZE> inFftData {};
    std::array<float, FFT_SIZE> inSmoothed{};
    int  inFifoIndex        = 0;
    bool inFftReady         = false;

    // Smoothing time constant in ms. Slider controls this directly.
    // λ is derived: λ = exp(-FFT_SIZE / (τ_ms * sampleRate / 1000))
    // Higher τ = slower/smoother. 150ms is a good musical default.
    float smoothingTauMs = 1000.0f;

    // Derived from smoothingTauMs and sampleRate. Recomputed when either changes.
    float lambda = 0.1f;

    void updateLambda() {
        const float tauSamples = smoothingTauMs * static_cast<float>(sampleRate) / 1000.0f;
        lambda = std::exp(-static_cast<float>(FFT_SIZE) / tauSamples);
    }

    // Ring buffer of recent output FFT frames for the ghost trail.
    // historyFrames[historyHead] is the slot to write into next.
    std::array<std::array<float, FFT_SIZE>, HISTORY_FRAMES> historyFrames{};
    int  historyHead = 0;
    int  historyCount = 0; // how many valid frames we have so far

    double sampleRate = 44100.0;

    // ---------------------------------------------------------------------------
    // Crossover / interaction state (UI thread)
    // ---------------------------------------------------------------------------
    juce::CriticalSection  crossoverMutex;
    std::vector<float>     crossoverFreqs;

    int   draggedIndex      = -1;
    int   draggedVolumeBand = -1;
    float lastDragY         = 0.0f;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------
    float getFrequencyForX(float x) const;
    float getXForFrequency(float freq) const;
    int   hitTestCrossover(float x) const;
    int   hitTestVolumeLine(float mx, float my) const;

    // Cubic-interpolated bin lookup.
    float getBinValue(const std::array<float, FFT_SIZE> &data, float freq) const;

    // Per-pixel band colour from crossover list (interpolated at crossovers).
    juce::Colour getBandColourForFrequency(float freq,
                                           const std::vector<float> &bandBounds) const;

    MultibandReverbAudioProcessor *audioProcessor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
