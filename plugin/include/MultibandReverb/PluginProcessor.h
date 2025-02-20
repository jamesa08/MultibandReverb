#pragma once

#include <JuceHeader.h>
#include "AudioTransport.h"

class SpectrumAnalyzer;

class MultibandReverbAudioProcessor : public juce::AudioProcessor, 
                                    public juce::AudioProcessorValueTreeState::Listener
{
public:
    MultibandReverbAudioProcessor();
    ~MultibandReverbAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    SpectrumAnalyzer* analyzer = nullptr;
    
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram ([[maybe_unused]] int index) override {}
    const juce::String getProgramName ([[maybe_unused]] int index) override { return {}; }
    void changeProgramName ([[maybe_unused]] int index, [[maybe_unused]] const juce::String& newName) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged(const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState parameters;
    AudioTransportComponent transportComponent;

    void updateCrossoverFrequencies();
    void loadImpulseResponse(size_t bandIndex, const juce::File& irFile);
    
    struct CrossoverFilter {
        juce::dsp::LinkwitzRileyFilter<float> lowpass;
        juce::dsp::LinkwitzRileyFilter<float> highpass;
    };
    
    struct BandReverb {
        std::unique_ptr<juce::dsp::Convolution> convolution;
        juce::AudioBuffer<float> irBuffer;
        float mix = 0.5f;

        BandReverb() : convolution(std::make_unique<juce::dsp::Convolution>()) {}
        
        // Explicitly delete copy operations
        BandReverb(const BandReverb&) = delete;
        BandReverb& operator=(const BandReverb&) = delete;
        
        // Enable move operations
        BandReverb(BandReverb&&) = default;
        BandReverb& operator=(BandReverb&&) = default;
    };

    std::vector<CrossoverFilter> crossovers;
    std::vector<BandReverb> bandReverbs;



private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
        
    std::atomic<float>* lowCrossoverFreq = nullptr;
    std::atomic<float>* midCrossoverFreq = nullptr;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandReverbAudioProcessor)
};