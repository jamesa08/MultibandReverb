#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MultibandReverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Crossover frequency parameter (for 2-band setup)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crossover", 
        "Crossover",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        1000.0f));

    // Mix parameters for each band (starting with 2 bands)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix0", "Low Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix1", "High Band Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f));

    // Volume parameters for each band
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vol0", "Low Band Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "vol1", "High Band Volume",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        1.0f));

    return { params.begin(), params.end() };
}

MultibandReverbAudioProcessor::MultibandReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                    .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    crossoverFreq = parameters.getRawParameterValue("crossover");
    parameters.addParameterListener("crossover", this);
    
    // Initialize with 2 bands
    createTwoBandSetup();
}

MultibandReverbAudioProcessor::~MultibandReverbAudioProcessor() {
    for (auto& band : bands)
    {
        if (band.convolution)
            band.convolution->reset();
    }
}

void MultibandReverbAudioProcessor::createTwoBandSetup()
{
    bands.clear();
    bands.resize(2); // Two bands

    // Set up initial frequency ranges
    bands[0].lowCutoff = 20.0f;
    bands[0].highCutoff = crossoverFreq->load();
    
    bands[1].lowCutoff = crossoverFreq->load();
    bands[1].highCutoff = 20000.0f;
}

void MultibandReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(samplesPerBlock);
    spec.numChannels = static_cast<uint32>(getTotalNumOutputChannels());

    prepareFilters(spec);

    // Prepare convolution engines
    for (auto& band : bands)
    {
        if (band.convolution)
        {
            band.convolution->prepare(spec);
        }
    }
}

void MultibandReverbAudioProcessor::prepareFilters(const juce::dsp::ProcessSpec& spec)
{
    for (auto& band : bands)
    {
        band.lowpass.prepare(spec);
        band.highpass.prepare(spec);
        
        band.lowpass.setCutoffFrequency(band.highCutoff);
        band.highpass.setCutoffFrequency(band.lowCutoff);
    }
}

void MultibandReverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, 
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Create temporary buffers for each band
    std::vector<juce::AudioBuffer<float>> bandBuffers;
    for (size_t i = 0; i < bands.size(); ++i)
    {
        bandBuffers.emplace_back(numChannels, numSamples);
        // Copy input to each band buffer
        for (int channel = 0; channel < numChannels; ++channel)
        {
            bandBuffers[i].copyFrom(channel, 0, buffer, channel, 0, numSamples);
        }
    }

    // Process each band
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& band = bands[i];
        auto& bandBuffer = bandBuffers[i];
        
        // Create audio block for the band
        juce::dsp::AudioBlock<float> block(bandBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        // Apply filters
        if (i == 0) // Low band
        {
            band.lowpass.process(context);
        }
        else if (i == bands.size() - 1) // High band
        {
            band.highpass.process(context);
        }
        else // Mid bands (for future expansion)
        {
            band.lowpass.process(context);
            band.highpass.process(context);
        }

        // Process reverb if we have an IR loaded
        if (band.convolution)
        {
            // Create wet buffer
            juce::AudioBuffer<float> wetBuffer(numChannels, numSamples);
            wetBuffer.copyFrom(0, 0, bandBuffer, 0, 0, numSamples);
            if (numChannels > 1)
                wetBuffer.copyFrom(1, 0, bandBuffer, 1, 0, numSamples);

            // Process reverb
            juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
            juce::dsp::ProcessContextReplacing<float> wetContext(wetBlock);
            band.convolution->process(wetContext);

            // Mix wet and dry
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* dry = bandBuffer.getWritePointer(channel);
                auto* wet = wetBuffer.getReadPointer(channel);

                for (int sample = 0; sample < numSamples; ++sample)
                {
                    dry[sample] = dry[sample] * (1.0f - band.mix) + 
                                 wet[sample] * band.mix;
                    
                    // Apply band volume
                    dry[sample] *= band.volume;
                }
            }
        }

    }

    // Clear the main buffer before summing
    buffer.clear();

    // Sum all bands back to main buffer
    for (auto& bandBuffer : bandBuffers)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            buffer.addFrom(channel, 0, bandBuffer, channel, 0, numSamples);
        }
    }

    // Update analyzer if present
    if (analyzer != nullptr)
    {
        std::cout << "Pushing buffer to analyzer" << std::endl;
        const float* channelData = buffer.getReadPointer(0);
        analyzer->pushBuffer(channelData, numSamples);
    }
}

void MultibandReverbAudioProcessor::setBandCrossover(float frequency)
{
    if (bands.size() >= 2)
    {
        bands[0].highCutoff = frequency;
        bands[1].lowCutoff = frequency;
        
        for (auto& band : bands)
        {
            band.lowpass.setCutoffFrequency(band.highCutoff);
            band.highpass.setCutoffFrequency(band.lowCutoff);
        }
    }
}

void MultibandReverbAudioProcessor::loadImpulseResponse(size_t bandIndex, 
                                                       const juce::File& irFile)
{
    if (bandIndex < bands.size())
    {
        auto& band = bands[bandIndex];
        
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));
        
        if (reader != nullptr)
        {
            const auto numSamples = static_cast<int>(reader->lengthInSamples);
            
            band.irBuffer.setSize(1, numSamples);
            reader->read(&band.irBuffer, 0, numSamples, 0, true, false);
            
            band.convolution = std::make_unique<juce::dsp::Convolution>();
            band.convolution->loadImpulseResponse(
                std::move(band.irBuffer),
                reader->sampleRate,
                juce::dsp::Convolution::Stereo::no,
                juce::dsp::Convolution::Trim::no,
                juce::dsp::Convolution::Normalise::yes
            );
            
            // Prepare the convolution engine if we're already playing
            if (getSampleRate() > 0)
            {
                juce::dsp::ProcessSpec spec;
                spec.sampleRate = getSampleRate();
                spec.maximumBlockSize = static_cast<uint32>(getBlockSize());
                spec.numChannels = static_cast<uint32>(getTotalNumOutputChannels());
                band.convolution->prepare(spec);
            }
        }
    }
}

void MultibandReverbAudioProcessor::parameterChanged(const juce::String& parameterID, 
                                                    float newValue)
{
    if (parameterID == "crossover")
    {
        setBandCrossover(newValue);
    }
    else if (parameterID.startsWith("mix"))
    {
        int bandIndex = parameterID.getLastCharacter() - '0';
        if (bandIndex >= 0 && bandIndex < bands.size())
        {
            bands[bandIndex].mix = newValue / 100.0f;
        }
    }
    else if (parameterID.startsWith("vol"))
    {
        int bandIndex = parameterID.getLastCharacter() - '0';
        if (bandIndex >= 0 && bandIndex < bands.size())
        {
            bands[bandIndex].volume = newValue;
        }
    }
}

void MultibandReverbAudioProcessor::releaseResources()
{
    for (auto& band : bands)
    {
        if (band.convolution)
            band.convolution->reset();
    }
}

juce::AudioProcessorEditor* MultibandReverbAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void MultibandReverbAudioProcessor::updateBandSettings()
{
    // Update crossover frequency
    if (crossoverFreq != nullptr)
    {
        setBandCrossover(crossoverFreq->load());
    }
    
    // Update mix and volume values for each band
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto* mixParam = parameters.getRawParameterValue("mix" + juce::String(static_cast<int>(i)));
        auto* volParam = parameters.getRawParameterValue("vol" + juce::String(static_cast<int>(i)));
        
        if (mixParam != nullptr)
            bands[i].mix = mixParam->load() / 100.0f;
            
        if (volParam != nullptr)
            bands[i].volume = volParam->load();
    }
    
    // Ensure the filters are using the correct frequencies
    for (auto& band : bands)
    {
        band.lowpass.setCutoffFrequency(band.highCutoff);
        band.highpass.setCutoffFrequency(band.lowCutoff);
    }
}

void MultibandReverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    
    // Store additional band-specific data
    juce::ValueTree extraState("ExtraState");
    
    for (size_t i = 0; i < bands.size(); ++i)
    {
        juce::ValueTree bandState("Band" + juce::String(i));
        
        // Store IR file path if one is loaded
        if (bands[i].irBuffer.getNumSamples() > 0)
        {
            // In a real plugin, you'd want to store the actual IR data or a reference
            bandState.setProperty("hasIR", true, nullptr);
        }
        
        extraState.addChild(bandState, -1, nullptr);
    }
    
    state.addChild(extraState, -1, nullptr);
    
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MultibandReverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml(*xmlState);
        parameters.replaceState(tree);
        
        // Process extra state data
        auto extraState = tree.getChildWithName("ExtraState");
        if (extraState.isValid())
        {
            for (int i = 0; i < extraState.getNumChildren(); ++i)
            {
                auto bandState = extraState.getChild(i);
                
                // Handle band-specific restoration
                if (static_cast<size_t>(i) < bands.size() && bandState.isValid())
                {
                    if (bandState.getProperty("hasIR", false))
                    {
                        DBG("Band " << i << " had an IR loaded");
                    }
                }
            }
        }
        
        // Update crossover frequencies and band settings
        updateBandSettings();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultibandReverbAudioProcessor();
}
