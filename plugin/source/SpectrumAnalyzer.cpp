#include "MultibandReverb/SpectrumAnalyzer.h"

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer()
    : fft(11), // 2048 points
      window(2048, juce::dsp::WindowingFunction<float>::hann) {
    startTimerHz(60); // 60 fps refresh rate
    setOpaque(true);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { stopTimer(); }

void SpectrumAnalyzer::paint(juce::Graphics &g) {
    g.fillAll(juce::Colours::black);

    auto bounds = getLocalBounds();
    auto width = bounds.getWidth();
    auto height = bounds.getHeight();

    // Create path for spectrum
    juce::Path spectrumPath;

    auto minFreq = 20.0f;
    auto maxFreq = 20000.0f;
    const float minDb = -100.0f;
    const float maxDb = 12.0f; // Extend range to +12dB

    // Start the path at the bottom-left
    spectrumPath.startNewSubPath(0, (float)height);

    // Create points for the curve
    for (int x = 0; x < width; x += 2) {
        auto freq = std::exp(std::log(minFreq) + (std::log(maxFreq) - std::log(minFreq)) * x / width);
        auto level = getSmoothedValueForFrequency(freq, minFreq, maxFreq, width);
        auto dbLevel = juce::Decibels::gainToDecibels(level, minDb);
        auto normalizedLevel = juce::jmap(dbLevel, minDb, maxDb, 0.0f, 0.7f);

        spectrumPath.lineTo(x, height * (1.0f - normalizedLevel));
    }

    // Complete the path
    spectrumPath.lineTo(width, (float)height);
    spectrumPath.closeSubPath();

    // Draw spectrum with gradient fill
    g.setGradientFill(juce::ColourGradient(juce::Colours::lightblue.withAlpha(0.8f), 0, 0, juce::Colours::lightblue.withAlpha(0.2f), 0, (float)height, false));

    // Smooth the path
    auto smoothedPath = spectrumPath.createPathWithRoundedCorners(5.0f);
    g.fillPath(smoothedPath);

    // Draw grid lines
    g.setColour(juce::Colours::white.withAlpha(0.2f));

    // Frequency grid lines
    const int freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int freq : freqs) {
        auto x = std::log(freq / minFreq) / std::log(maxFreq / minFreq) * width;
        g.drawVerticalLine((int)x, 0.0f, (float)height);

        g.setFont(12.0f);
        g.drawText(juce::String(freq) + "Hz", (int)x - 20, height - 20, 40, 20, juce::Justification::centred);
    }

    // Level grid lines
    const int levels[] = {12, 0, -12, -24, -36, -48, -60};
    for (int level : levels) {
        float normalizedY = juce::jmap((float)level, minDb, maxDb, 1.0f, 0.0f);
        float y = height * normalizedY;
        g.drawHorizontalLine((int)y, 0.0f, (float)width);

        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawText(juce::String(level) + "dB", width - 35, (int)y - 10, 30, 20, juce::Justification::right);
    }
}

void SpectrumAnalyzer::timerCallback() {
    if (nextFFTBlockReady) {
        window.multiplyWithWindowingTable(fftData.data(), 2048);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        // Apply temporal smoothing
        for (size_t i = 0; i < fftData.size(); ++i) {
            smoothedFFTData[i] = smoothedFFTData[i] * temporalSmoothing + fftData[i] * (1.0f - temporalSmoothing);
        }

        nextFFTBlockReady = false;
        repaint();
    }
}

float SpectrumAnalyzer::getSmoothedValueForFrequency(float freq, float minFreq, float maxFreq, int width) {
    auto getNormalizedBinValue = [this](int bin) {
        if (bin >= 0 && bin < 2048)
            return smoothedFFTData[bin];
        return 0.0f;
    };

    // Calculate which FFT bin corresponds to this frequency
    float binFreq = freq * 2048.0f / 44100.0f;
    int centralBin = juce::jlimit(0, 1024, (int)binFreq);

    // Average over nearby bins for spectral smoothing
    float sum = 0.0f;
    int count = 0;

    for (int i = -spectralAveraging; i <= spectralAveraging; ++i) {
        sum += getNormalizedBinValue(centralBin + i);
        count++;
    }

    return sum / count;
}

void SpectrumAnalyzer::pushBuffer(const float *data, int size) {
    if (size > 0) {
        for (int i = 0; i < size; ++i) {
            fifo[static_cast<size_t>(fifoIndex)] = data[i];
            ++fifoIndex;

            if (fifoIndex >= 2048) {
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                nextFFTBlockReady = true;
                fifoIndex = 0;
            }
        }
    }
}

void SpectrumAnalyzer::resized() {}
