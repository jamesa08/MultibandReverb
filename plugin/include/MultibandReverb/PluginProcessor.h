#pragma once
#include <JuceHeader.h>

class SpectrumAnalyzer;

// Forward declare our Band structure
struct ReverbBand;

class MultibandReverbAudioProcessor : public juce::AudioProcessor,
                                    public juce::AudioProcessorValueTreeState::Listener
{
public:
    MultibandReverbAudioProcessor();
    ~MultibandReverbAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Audio processing value tree state
    juce::AudioProcessorValueTreeState parameters;
    
    // Spectrum analyzer
    SpectrumAnalyzer* analyzer = nullptr;

    // Structure to hold band information
    struct ReverbBand {
        std::unique_ptr<juce::dsp::Convolution> convolution;
        juce::AudioBuffer<float> irBuffer;
        float mix = 0.0f;  // 0 = dry, 1 = wet
        float volume = 1.0f;
        
        // Filters for this band
        juce::dsp::LinkwitzRileyFilter<float> lowpass;
        juce::dsp::LinkwitzRileyFilter<float> highpass;
        
        // Band frequency range
        float lowCutoff = 20.0f;  // Hz
        float highCutoff = 20000.0f;  // Hz

        // Constructor
        ReverbBand() : convolution(std::make_unique<juce::dsp::Convolution>()) {}
    };

    // Vector to hold our bands
    std::vector<ReverbBand> bands;

    // Methods for band management
    void createTwoBandSetup();
    void setBandCrossover(float frequency);
    void loadImpulseResponse(size_t bandIndex, const juce::File& irFile);
    void updateBandSettings();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareFilters(const juce::dsp::ProcessSpec& spec);
    
    std::atomic<float>* crossoverFreq = nullptr;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandReverbAudioProcessor)
};