// PluginProcessor.cpp
#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MultibandReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCross", 
        "Low Crossover",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.5f),
        200.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "midCross", 
        "Mid Crossover",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 1.0f, 0.5f),
        2000.0f));

    return { params.begin(), params.end() };
}

MultibandReverbAudioProcessor::MultibandReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                    .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize crossovers
    crossovers.resize(2);  // We need 2 crossover points for 3 bands
    
    // Initialize reverb bands
    bandReverbs.reserve(3);  // Reserve space for 3 bands
    for (int i = 0; i < 3; ++i) {
        bandReverbs.emplace_back();  // Use emplace_back to construct in-place
    }
    
    // Get parameter pointers
    lowCrossoverFreq = parameters.getRawParameterValue("lowCross");
    midCrossoverFreq = parameters.getRawParameterValue("midCross");
    
    // Listen to parameter changes
    parameters.addParameterListener("lowCross", this);
    parameters.addParameterListener("midCross", this);
}

MultibandReverbAudioProcessor::~MultibandReverbAudioProcessor()
{
    parameters.removeParameterListener("lowCross", this);
    parameters.removeParameterListener("midCross", this);
}

void MultibandReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(samplesPerBlock);
    spec.numChannels = static_cast<uint32>(getTotalNumOutputChannels());

    // Prepare transport
    transportComponent.prepareToPlay(samplesPerBlock, sampleRate);

    // Prepare crossover filters
    for (auto& crossover : crossovers) {
        crossover.lowpass.prepare(spec);
        crossover.highpass.prepare(spec);
    }

    // Prepare convolution engines
    for (auto& reverb : bandReverbs) {
        if (reverb.convolution) {
            reverb.convolution->prepare(spec);
        }
    }

    updateCrossoverFrequencies();
}

void MultibandReverbAudioProcessor::releaseResources()
{
    // Release any allocated resources when playback stops
    transportComponent.releaseResources();

}

void MultibandReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               [[maybe_unused]] juce::MidiBuffer& midiMessages)
{    
    juce::ScopedNoDenormals noDenormals;
    // Get the next block from the transport source
    juce::AudioSourceChannelInfo info(buffer);
    transportComponent.getNextAudioBlock(info);
    
    // Now process through your reverb chain
    juce::dsp::AudioBlock<float> block(buffer);

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that don't contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear(i, 0, buffer.getNumSamples());
    }
    
    // TODO: Implement multiband processing:
    // 1. Split the signal using crossover filters
    // 2. Process each band through its reverb
    // 3. Mix and sum the bands back together
}

juce::AudioProcessorEditor* MultibandReverbAudioProcessor::createEditor()
{
    return new MultibandReverbAudioProcessorEditor(*this);
}

void MultibandReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MultibandReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr) {
        if (xmlState->hasTagName(parameters.state.getType())) {
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

void MultibandReverbAudioProcessor::updateCrossoverFrequencies()
{
    if (lowCrossoverFreq && midCrossoverFreq) {
        auto lowFreq = lowCrossoverFreq->load();
        auto midFreq = midCrossoverFreq->load();
        
        // Update the crossover filters with new frequencies
        if (!crossovers.empty()) {
            crossovers[0].lowpass.setCutoffFrequency(lowFreq);
            crossovers[0].highpass.setCutoffFrequency(lowFreq);
        }
        if (crossovers.size() > 1) {
            crossovers[1].lowpass.setCutoffFrequency(midFreq);
            crossovers[1].highpass.setCutoffFrequency(midFreq);
        }
    }
}

void MultibandReverbAudioProcessor::loadImpulseResponse(size_t bandIndex, 
                                                       [[maybe_unused]] const juce::File& irFile)
{
    if (bandIndex < bandReverbs.size()) {
        // TODO: Implement IR loading for the specified band
    }
}

void MultibandReverbAudioProcessor::parameterChanged(const juce::String& parameterID,
                                                    [[maybe_unused]] float newValue)
{
    if (parameterID == "lowCross" || parameterID == "midCross") {
        updateCrossoverFrequencies();
    }
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultibandReverbAudioProcessor();
}