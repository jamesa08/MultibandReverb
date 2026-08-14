#pragma once

#include "AudioTransport.h"
#include "SpectrumAnalyzer.h"
#include <JuceHeader.h>

class SpectrumAnalyzer;

// Maximum number of bands supported. Parameter slots are pre-allocated up to
// this limit so the host always sees a fixed parameter list.
static constexpr int MAX_BANDS = 8;

class MultibandReverbAudioProcessor : public juce::AudioProcessor,
                                      public juce::AudioProcessorValueTreeState::Listener {
  public:
    MultibandReverbAudioProcessor();
    ~MultibandReverbAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    std::atomic<SpectrumAnalyzer *> analyzer { nullptr };
    juce::SpinLock analyzerLock; // held briefly when accessing analyzer pointer

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    // 10 seconds covers most practical IR tails.
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram([[maybe_unused]] int index) override {}
    const juce::String getProgramName([[maybe_unused]] int index) override { return {}; }
    void changeProgramName([[maybe_unused]] int index, [[maybe_unused]] const juce::String &newName) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    void parameterChanged(const juce::String &parameterID, float newValue) override;

    // -------------------------------------------------------------------------
    // Band management (call from UI thread only)
    // -------------------------------------------------------------------------
    void addBand();
    void removeBand(int bandIndex);
    int getNumBands() const { return numActiveBands; }

    // -------------------------------------------------------------------------
    // IR loading
    // -------------------------------------------------------------------------
    void loadImpulseResponse(int bandIndex, const juce::File &irFile);

    // -------------------------------------------------------------------------
    // Crossover frequency access (UI thread reads, audio thread reads via atomic copy)
    // -------------------------------------------------------------------------
    // Returns the upper crossover frequency for band [bandIndex].
    // The last active band has no upper crossover (returns 20000 Hz).
    float getCrossoverFrequency(int crossoverIndex) const;
    int getNumCrossovers() const { return juce::jmax(0, numActiveBands - 1); }

    // Called by SpectrumAnalyzer when user drags a crossover handle.
    void setCrossoverFrequency(int crossoverIndex, float freq);

    // Called by SpectrumAnalyzer to read per-band volume for the overlay.
    float getBandVolumeDb(int bandIndex) const {
        if (bandIndex >= 0 && bandIndex < MAX_BANDS)
            return bandVolume[static_cast<size_t>(bandIndex)]->load();
        return 0.0f;
    }

    bool wasStateLoaded() const { return stateWasLoaded; }

    juce::String getBandIRName(int bandIndex) const {
        if (bandIndex >= 0 && bandIndex < numActiveBands)
            return bandReverbs[static_cast<size_t>(bandIndex)].irName;
        return {};
    }

    // -------------------------------------------------------------------------
    // Public DSP state (accessed by BandControls via UI thread)
    // -------------------------------------------------------------------------
    struct BandReverb {
        std::unique_ptr<juce::dsp::Convolution> convolution;
        juce::AudioBuffer<float> irBuffer;
        juce::String irName; // display name of the loaded IR file, empty if none

        BandReverb() : convolution(std::make_unique<juce::dsp::Convolution>()) {}

        BandReverb(const BandReverb &) = delete;
        BandReverb &operator=(const BandReverb &) = delete;
        BandReverb(BandReverb &&) = default;
        BandReverb &operator=(BandReverb &&) = default;
    };

    // Fixed-size array; only indices [0, numActiveBands) are active.
    std::array<BandReverb, MAX_BANDS> bandReverbs;

    juce::AudioProcessorValueTreeState parameters;
    AudioTransportComponent transportComponent;

    // Notifies editor to rebuild its band controls.
    std::function<void()> onBandLayoutChanged;

  private:
    struct CrossoverFilter {
        juce::dsp::LinkwitzRileyFilter<float> lowpass;
        juce::dsp::LinkwitzRileyFilter<float> highpass;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareConvolutions(double sampleRate, int blockSize, int numChannels);
    void updateCrossoverFilters();
    void rebuildCrossoverFilters(double sampleRate, int blockSize, int numChannels);
    void notifyAnalyzerOfCrossovers();

    int numActiveBands = 1;

    // Crossover frequencies between adjacent bands. Size = numActiveBands - 1.
    // Stored as plain floats; written on UI thread, read on audio thread.
    // Access is protected by a simple spinlock since writes are infrequent.
    juce::SpinLock crossoverLock;
    std::vector<float> crossoverFrequencies; // size: numActiveBands - 1

    // Crossover filters; rebuilt when band count changes.
    // Size = numActiveBands - 1; each splits one frequency.
    std::vector<CrossoverFilter> crossoverFilters;

    // Per-band parameter pointers into APVTS (pre-allocated for MAX_BANDS slots).
    std::array<std::atomic<float> *, MAX_BANDS> bandVolume  {};
    std::array<std::atomic<float> *, MAX_BANDS> bandMix     {};
    std::array<std::atomic<float> *, MAX_BANDS> bandSolo    {};
    std::array<std::atomic<float> *, MAX_BANDS> bandMute    {};

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool stateWasLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandReverbAudioProcessor)
};
