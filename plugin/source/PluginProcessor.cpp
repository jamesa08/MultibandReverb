#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/BandControls.h"
#include "MultibandReverb/PluginEditor.h"

// ---------------------------------------------------------------------------
// Parameter layout: pre-allocate MAX_BANDS slots for all per-band params.
// Crossover frequencies are NOT in the APVTS; they live in crossoverFrequencies
// and are serialised manually so we can handle a variable number of them.
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
MultibandReverbAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Band count (1..MAX_BANDS). Used only for state save/restore.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "bandCount", "Band Count", 1, MAX_BANDS, 1));

    for (int i = 0; i < MAX_BANDS; ++i) {
        juce::String idx(i);

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "band" + idx + "_vol", "Band " + idx + " Volume",
            juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "band" + idx + "_mix", "Band " + idx + " Mix",
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "band" + idx + "_solo", "Band " + idx + " Solo", false));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "band" + idx + "_mute", "Band " + idx + " Mute", false));
    }

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
MultibandReverbAudioProcessor::MultibandReverbAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
    // Cache parameter pointers.
    for (size_t i = 0; i < MAX_BANDS; ++i) {
        juce::String idx(static_cast<int>(i));
        bandVolume[i] = parameters.getRawParameterValue("band" + idx + "_vol");
        bandMix[i]    = parameters.getRawParameterValue("band" + idx + "_mix");
        bandSolo[i]   = parameters.getRawParameterValue("band" + idx + "_solo");
        bandMute[i]   = parameters.getRawParameterValue("band" + idx + "_mute");
    }

    // Start with 1 band, no crossovers.
    numActiveBands = 1;
    crossoverFrequencies.clear();
}

MultibandReverbAudioProcessor::~MultibandReverbAudioProcessor() {}

// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    transportComponent.prepareToPlay(samplesPerBlock, sampleRate);

    const int numCh = getTotalNumOutputChannels();
    prepareConvolutions(sampleRate, samplesPerBlock, numCh);
    rebuildCrossoverFilters(sampleRate, samplesPerBlock, numCh);

    if (auto *a = analyzer.load())
        a->setSampleRate(sampleRate);
}

void MultibandReverbAudioProcessor::releaseResources() {
    transportComponent.releaseResources();
}

// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::prepareConvolutions(double sampleRate, int blockSize, int numChannels) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(blockSize);
    spec.numChannels      = static_cast<uint32>(numChannels);

    for (size_t i = 0; i < MAX_BANDS; ++i)
        bandReverbs[i].convolution->prepare(spec);
}

void MultibandReverbAudioProcessor::rebuildCrossoverFilters(double sampleRate, int blockSize, int numChannels) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<uint32>(blockSize);
    spec.numChannels      = static_cast<uint32>(numChannels);

    const int numCross = numActiveBands - 1;
    crossoverFilters.resize(static_cast<size_t>(numCross));

    for (auto &cf : crossoverFilters) {
        cf.lowpass.prepare(spec);
        cf.lowpass.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        cf.highpass.prepare(spec);
        cf.highpass.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    }

    updateCrossoverFilters();
}

void MultibandReverbAudioProcessor::updateCrossoverFilters() {
    juce::SpinLock::ScopedLockType lock(crossoverLock);
    const int numCross = juce::jmin((int)crossoverFrequencies.size(),
                                    (int)crossoverFilters.size());
    for (int i = 0; i < numCross; ++i) {
        float freq = crossoverFrequencies[static_cast<size_t>(i)];
        crossoverFilters[static_cast<size_t>(i)].lowpass.setCutoffFrequency(freq);
        crossoverFilters[static_cast<size_t>(i)].highpass.setCutoffFrequency(freq);
    }
}

// ---------------------------------------------------------------------------
// N-band processing:
//   bandBuffers[0] = low band output
//   bandBuffers[n-1] = high band output
//
// For each crossover i (0-based):
//   bandBuffers[i]   = lowpass of whatever came in
//   remainder        = highpass, passed down the chain
// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                                 [[maybe_unused]] juce::MidiBuffer &midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Mix in transport audio if playing.
    juce::AudioBuffer<float> transportBuffer;
    transportBuffer.makeCopyOf(buffer);
    juce::AudioSourceChannelInfo info(transportBuffer);
    transportComponent.getNextAudioBlock(info);

    if (transportComponent.isTransportPlaying()) {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom(ch, 0, transportBuffer, ch, 0, buffer.getNumSamples(), 0.5f);
    }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int numBands    = numActiveBands;

    // Push the pre-processing signal to the analyzer's input pipeline.
    {
        juce::SpinLock::ScopedTryLockType lock(analyzerLock);
        if (lock.isLocked()) {
            if (auto *analyzerPtr = analyzer.load()) {
                std::vector<float> inputMono(static_cast<size_t>(numSamples));
                const float *ch0 = buffer.getReadPointer(0);
                if (numChannels > 1) {
                    const float *ch1 = buffer.getReadPointer(1);
                    for (int s = 0; s < numSamples; ++s)
                        inputMono[static_cast<size_t>(s)] = (ch0[s] + ch1[s]) * 0.5f;
                } else {
                    std::memcpy(inputMono.data(), ch0, static_cast<size_t>(numSamples) * sizeof(float));
                }
                analyzerPtr->pushInputBuffer(inputMono.data(), numSamples);
            }
        }
    }

    // Allocate per-band buffers.
    std::vector<juce::AudioBuffer<float>> bandBuffers(static_cast<size_t>(numBands));
    for (auto &b : bandBuffers)
        b.setSize(numChannels, numSamples, false, false, true);

    // --- Crossover splitting ---
    // 'remainder' starts as the full input and gets repeatedly split.
    juce::AudioBuffer<float> remainder(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        remainder.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    {
        juce::SpinLock::ScopedLockType lock(crossoverLock);
        const int numCross = juce::jmin(numBands - 1, (int)crossoverFilters.size());

        for (int i = 0; i < numCross; ++i) {
            // Low portion of remainder -> bandBuffers[i]
            for (int ch = 0; ch < numChannels; ++ch)
                bandBuffers[static_cast<size_t>(i)].copyFrom(ch, 0, remainder, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> lowBlock(bandBuffers[static_cast<size_t>(i)]);
            juce::dsp::AudioBlock<float> highBlock(remainder);

            juce::dsp::ProcessContextReplacing<float> lowCtx(lowBlock);
            juce::dsp::ProcessContextReplacing<float> highCtx(highBlock);

            crossoverFilters[static_cast<size_t>(i)].lowpass.process(lowCtx);
            crossoverFilters[static_cast<size_t>(i)].highpass.process(highCtx);
        }
    }

    // Whatever remains after all splits is the top band.
    for (int ch = 0; ch < numChannels; ++ch)
        bandBuffers[static_cast<size_t>(numBands - 1)].copyFrom(ch, 0, remainder, ch, 0, numSamples);

    // --- Solo / mute logic ---
    bool anySoloed = false;
    for (size_t i = 0; i < static_cast<size_t>(numBands); ++i)
        if (bandSolo[i]->load() > 0.5f) { anySoloed = true; break; }

    // --- Apply reverb and volume, mix to output ---
    buffer.clear();

    for (size_t i = 0; i < static_cast<size_t>(numBands); ++i) {
        const bool muted  = bandMute[i]->load() > 0.5f;
        const bool soloed = bandSolo[i]->load() > 0.5f;

        if (muted || (anySoloed && !soloed))
            continue;

        auto &bandBuf  = bandBuffers[i];
        auto &reverb   = bandReverbs[i];
        const float mix = juce::jlimit(0.0f, 1.0f, bandMix[i]->load() / 100.0f);

        // Wet path through convolution — only if an IR has been loaded.
        // Without an IR the convolution output is silence/noise, so pass dry through.
        if (reverb.irName.isNotEmpty()) {
            juce::AudioBuffer<float> wetBuf(numChannels, numSamples);
            for (int ch = 0; ch < numChannels; ++ch)
                wetBuf.copyFrom(ch, 0, bandBuf, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> wetBlock(wetBuf);
            juce::dsp::ProcessContextReplacing<float> wetCtx(wetBlock);
            reverb.convolution->process(wetCtx);

            // Mix dry + wet in place.
            const float dryGain = 1.0f - mix;
            for (int ch = 0; ch < numChannels; ++ch) {
                auto *dry = bandBuf.getWritePointer(ch);
                const auto *wet = wetBuf.getReadPointer(ch);
                for (int s = 0; s < numSamples; ++s)
                    dry[s] = dry[s] * dryGain + wet[s] * mix;
            }
        }

        // Apply band volume and sum into output.
        const float volGain = juce::Decibels::decibelsToGain(bandVolume[i]->load());
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom(ch, 0, bandBuf, ch, 0, numSamples, volGain);
    }

    // Feed output to spectrum analyzer.
    {
        juce::SpinLock::ScopedTryLockType lock(analyzerLock);
        if (lock.isLocked()) {
            if (auto *analyzerPtr = analyzer.load()) {
                std::vector<float> analysisBuf(static_cast<size_t>(numSamples));
                const float *ch0 = buffer.getReadPointer(0);
                if (numChannels > 1) {
                    const float *ch1 = buffer.getReadPointer(1);
                    for (int s = 0; s < numSamples; ++s)
                        analysisBuf[static_cast<size_t>(s)] = (ch0[s] + ch1[s]) * 0.5f;
                } else {
                    std::memcpy(analysisBuf.data(), ch0, static_cast<size_t>(numSamples) * sizeof(float));
                }
                analyzerPtr->pushBuffer(analysisBuf.data(), numSamples);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Band management
// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::addBand() {
    if (numActiveBands >= MAX_BANDS)
        return;

    // Split the last band's frequency range in half.
    const float upperFreq = 20000.0f;
    const float lowerFreq = crossoverFrequencies.empty()
                            ? 20.0f
                            : crossoverFrequencies.back();

    // Midpoint on a log scale sounds more musical than linear.
    const float newCross = std::exp((std::log(lowerFreq) + std::log(upperFreq)) * 0.5f);
    crossoverFrequencies.push_back(juce::jlimit(lowerFreq + 10.0f, upperFreq - 10.0f, newCross));

    ++numActiveBands;

    rebuildCrossoverFilters(currentSampleRate, currentBlockSize, getTotalNumOutputChannels());
    notifyAnalyzerOfCrossovers();

    if (onBandLayoutChanged)
        onBandLayoutChanged();
}

void MultibandReverbAudioProcessor::removeBand(int bandIndex) {
    if (numActiveBands <= 1 || bandIndex < 0 || bandIndex >= numActiveBands)
        return;

    // Remove this band's left crossover (index == bandIndex - 1).
    // For band 0 there is no left crossover; remove the right one instead
    // (index 0) and shift band 1 downward.
    if (bandIndex == 0) {
        // Deleting the lowest band: remove crossover[0], shift bands down.
        if (!crossoverFrequencies.empty())
            crossoverFrequencies.erase(crossoverFrequencies.begin());
    } else {
        // Deleting any other band: remove its left crossover.
        // The band below absorbs the deleted band's range.
        const int crossIdx = bandIndex - 1;
        crossoverFrequencies.erase(crossoverFrequencies.begin() + crossIdx);
    }

    // Shift band data (convolution state and IR name) down so there are no gaps.
    for (int i = bandIndex; i < numActiveBands - 1; ++i) {
        bandReverbs[static_cast<size_t>(i)].convolution.swap(
            bandReverbs[static_cast<size_t>(i + 1)].convolution);
        bandReverbs[static_cast<size_t>(i)].irName =
            bandReverbs[static_cast<size_t>(i + 1)].irName;
    }
    // Clear the now-vacated last slot.
    bandReverbs[static_cast<size_t>(numActiveBands - 1)].irName = {};

    --numActiveBands;

    rebuildCrossoverFilters(currentSampleRate, currentBlockSize, getTotalNumOutputChannels());
    notifyAnalyzerOfCrossovers();

    if (onBandLayoutChanged)
        onBandLayoutChanged();
}

float MultibandReverbAudioProcessor::getCrossoverFrequency(int index) const {
    juce::SpinLock::ScopedLockType lock(const_cast<juce::SpinLock &>(crossoverLock));
    if (index >= 0 && index < (int)crossoverFrequencies.size())
        return crossoverFrequencies[static_cast<size_t>(index)];
    return 20000.0f;
}

void MultibandReverbAudioProcessor::setCrossoverFrequency(int index, float freq) {
    {
        juce::SpinLock::ScopedLockType lock(crossoverLock);
        if (index < 0 || index >= (int)crossoverFrequencies.size())
            return;

        // Clamp so adjacent crossovers stay at least 10 Hz apart.
        const float minFreq = (index > 0)
            ? crossoverFrequencies[static_cast<size_t>(index - 1)] + 10.0f
            : 20.0f;
        const float maxFreq = (index < (int)crossoverFrequencies.size() - 1)
            ? crossoverFrequencies[static_cast<size_t>(index + 1)] - 10.0f
            : 20000.0f;

        crossoverFrequencies[static_cast<size_t>(index)] =
            juce::jlimit(minFreq, maxFreq, freq);
    }

    updateCrossoverFilters();
    notifyAnalyzerOfCrossovers();
}

void MultibandReverbAudioProcessor::notifyAnalyzerOfCrossovers() {
    if (auto *a = analyzer.load()) {
        juce::SpinLock::ScopedLockType lock(crossoverLock);
        a->setCrossoverFrequencies(crossoverFrequencies);
    }
}

// ---------------------------------------------------------------------------
// IR loading
// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::loadImpulseResponse(int bandIndex, const juce::File &irFile) {
    if (bandIndex < 0 || bandIndex >= numActiveBands)
        return;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));
    if (reader == nullptr)
        return;

    const double irLengthSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    if (irLengthSeconds > 60.0) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "IR Too Long",
            "The selected IR is " + juce::String(irLengthSeconds, 1) + "s. "
            "Please use an IR under 60 seconds.");
        return;
    }

    const int numChannels = static_cast<int>(
        juce::jmin(reader->numChannels, static_cast<unsigned int>(2)));
    const bool isStereo   = (numChannels == 2);
    const int numSamples  = static_cast<int>(reader->lengthInSamples);

    auto &reverb = bandReverbs[static_cast<size_t>(bandIndex)];
    reverb.irBuffer.setSize(numChannels, numSamples);
    reader->read(&reverb.irBuffer, 0, numSamples, 0, true, isStereo);

    reverb.irName = irFile.getFileNameWithoutExtension();

    reverb.convolution->loadImpulseResponse(
        std::move(reverb.irBuffer),
        getSampleRate(),
        isStereo ? juce::dsp::Convolution::Stereo::yes
                 : juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
    auto state = parameters.copyState();

    // Store dynamic crossover frequencies as children.
    juce::ValueTree crossovers("Crossovers");
    {
        juce::SpinLock::ScopedLockType lock(crossoverLock);
        for (float f : crossoverFrequencies) {
            juce::ValueTree node("Cross");
            node.setProperty("freq", f, nullptr);
            crossovers.appendChild(node, nullptr);
        }
    }
    state.appendChild(crossovers, nullptr);

    // Store active band count.
    state.setProperty("numActiveBands", numActiveBands, nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MultibandReverbAudioProcessor::setStateInformation(const void *data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;
    if (!xmlState->hasTagName(parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xmlState);

    // Restore band count.
    numActiveBands = juce::jlimit(1, MAX_BANDS,
                        static_cast<int>(state.getProperty("numActiveBands", 1)));

    // Restore crossover frequencies.
    {
        juce::SpinLock::ScopedLockType lock(crossoverLock);
        crossoverFrequencies.clear();
        if (auto crossovers = state.getChildWithName("Crossovers"); crossovers.isValid()) {
            for (auto child : crossovers) {
                crossoverFrequencies.push_back(
                    static_cast<float>(child.getProperty("freq", 1000.0f)));
            }
        }
        // Trim to match numActiveBands - 1.
        const size_t expected = static_cast<size_t>(juce::jmax(0, numActiveBands - 1));
        crossoverFrequencies.resize(expected, 1000.0f);
    }

    parameters.replaceState(state);
    stateWasLoaded = true;

    rebuildCrossoverFilters(currentSampleRate, currentBlockSize, getTotalNumOutputChannels());
    notifyAnalyzerOfCrossovers();

    if (onBandLayoutChanged)
        onBandLayoutChanged();
}

// ---------------------------------------------------------------------------
void MultibandReverbAudioProcessor::parameterChanged(
    [[maybe_unused]] const juce::String &parameterID,
    [[maybe_unused]] float newValue) {
    // All per-band params are read directly via raw pointers in processBlock.
    // Nothing extra needed here.
}

juce::AudioProcessorEditor *MultibandReverbAudioProcessor::createEditor() {
    return new MultibandReverbAudioProcessorEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
    return new MultibandReverbAudioProcessor();
}
