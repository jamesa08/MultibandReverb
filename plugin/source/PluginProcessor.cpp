// PluginProcessor.cpp
#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MultibandReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lowCross", 
        "Low Crossover",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.5f),
        20000.0f));

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
    transportComponent.releaseResources();
}

void MultibandReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               [[maybe_unused]] juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Get audio from transport if it's active
    juce::AudioSourceChannelInfo info(buffer);
    transportComponent.getNextAudioBlock(info);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Create temporary buffers for band processing
    juce::AudioBuffer<float> lowBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> midBuffer(numChannels, numSamples);
    juce::AudioBuffer<float> highBuffer(numChannels, numSamples);

    // Copy input to all bands initially
    for (int channel = 0; channel < numChannels; ++channel)
    {
        lowBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);
        midBuffer.copyFrom(channel, 0, buffer, channel, 0, numSamples);
    }

    // Create AudioBlocks for DSP processing
    juce::dsp::AudioBlock<float> lowBlock(lowBuffer);
    juce::dsp::AudioBlock<float> midBlock(midBuffer);
    juce::dsp::AudioBlock<float> highBlock(highBuffer);
    
    // Process crossovers
    if (crossovers.size() >= 2)
    {
        // Split into low and mid-high first
        juce::dsp::ProcessContextReplacing<float> lowContext(lowBlock);
        juce::dsp::ProcessContextReplacing<float> midHighContext(midBlock);
        crossovers[0].lowpass.process(lowContext);
        crossovers[0].highpass.process(midHighContext);

        // Copy mid-high into high buffer before second split
        for (int channel = 0; channel < numChannels; ++channel) {
            highBuffer.copyFrom(channel, 0, midBuffer, channel, 0, numSamples);
        }

        // Now split mid-high into mid and high
        juce::dsp::ProcessContextReplacing<float> midContext(midBlock);
        juce::dsp::ProcessContextReplacing<float> highContext(highBlock);
        crossovers[1].lowpass.process(midContext);
        crossovers[1].highpass.process(highContext);
    }

    // Process each band through its reverb
    for (size_t i = 0; i < bandReverbs.size(); ++i)
    {
        auto& reverb = bandReverbs[i];
        if (reverb.convolution)
        {
            juce::AudioBuffer<float>* bandBuffer = nullptr;
            switch (i)
            {
                case 0: bandBuffer = &lowBuffer; break;
                case 1: bandBuffer = &midBuffer; break;
                case 2: bandBuffer = &highBuffer; break;
                default: break;
            }

            if (bandBuffer != nullptr)
            {
                // Create wet buffer for reverb
                juce::AudioBuffer<float> wetBuffer(numChannels, numSamples);
                for (int channel = 0; channel < numChannels; ++channel) {
                    wetBuffer.copyFrom(channel, 0, *bandBuffer, channel, 0, numSamples);
                }

                // Process through convolution
                juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
                juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
                reverb.convolution->process(wetContext);

                // Mix wet and dry
                const float wetGain = reverb.mix;
                const float dryGain = 1.0f - wetGain;

                for (int channel = 0; channel < numChannels; ++channel)
                {
                    auto* dry = bandBuffer->getWritePointer(channel);
                    auto* wet = wetBuffer.getReadPointer(channel);

                    for (int sample = 0; sample < numSamples; ++sample)
                    {
                        dry[sample] = dry[sample] * dryGain + wet[sample] * wetGain;
                    }
                }
            }
        }
    }

    // Clear the output buffer before mixing
    buffer.clear();

    // Mix all bands back together with equal gain
    const float bandGain = 1.0f / 3.0f; // Normalize the output
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* output = buffer.getWritePointer(channel);
        auto* low = lowBuffer.getReadPointer(channel);
        auto* mid = midBuffer.getReadPointer(channel);
        auto* high = highBuffer.getReadPointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            output[sample] = (low[sample] + mid[sample] + high[sample]) * bandGain;
        }
    }

    // Now push the processed audio to the analyzer
    if (analyzer != nullptr)
    {
        float analysisBuf[2048];
        const float* channelData = buffer.getReadPointer(0);
        
        if (buffer.getNumChannels() > 1)
        {
            const float* channel2Data = buffer.getReadPointer(1);
            for (int i = 0; i < numSamples; ++i)
            {
                analysisBuf[i] = (channelData[i] + channel2Data[i]) * 0.5f;
            }
        }
        else
        {
            std::memcpy(analysisBuf, channelData, numSamples * sizeof(float));
        }
        
        analyzer->pushBuffer(analysisBuf, numSamples);
    }
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
            
            if (crossovers.size() > 1) {
                crossovers[1].lowpass.setCutoffFrequency(midFreq);
                crossovers[1].highpass.setCutoffFrequency(midFreq);
            }
        }
        
        DBG("Crossover frequencies updated - Low: " << lowFreq << " Hz, Mid: " << midFreq << " Hz");
    }
}

void MultibandReverbAudioProcessor::loadImpulseResponse(size_t bandIndex, 
                                                       const juce::File& irFile)
{
    if (bandIndex < bandReverbs.size())
    {
        auto& reverb = bandReverbs[bandIndex];
        
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));
        
        if (reader != nullptr)
        {
            DBG("Loading IR file: " << irFile.getFullPathName());
            DBG("Sample rate: " << reader->sampleRate);
            DBG("Length in samples: " << reader->lengthInSamples);
            
            const auto numSamples = static_cast<int>(reader->lengthInSamples);
            
            reverb.irBuffer.setSize(1, numSamples);
            reader->read(&reverb.irBuffer, 0, numSamples, 0, true, false);
            
            reverb.convolution = std::make_unique<juce::dsp::Convolution>();
            reverb.convolution->prepare({ getSampleRate(), 
                                        static_cast<uint32>(getBlockSize()), 
                                        static_cast<uint32>(getTotalNumOutputChannels()) });
            
            reverb.convolution->loadImpulseResponse(
                std::move(reverb.irBuffer),
                getSampleRate(),
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::yes
            );
            
            DBG("IR loaded successfully into band " << bandIndex);
        }
        else
        {
            DBG("Failed to read IR file");
        }
    }
}

void MultibandReverbAudioProcessor::parameterChanged(const juce::String& parameterID,
                                                    [[maybe_unused]] float newValue)
{
    if (parameterID == "lowCross" || parameterID == "midCross") {
        updateCrossoverFrequencies();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultibandReverbAudioProcessor();
}