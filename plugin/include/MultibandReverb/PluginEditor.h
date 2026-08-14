#pragma once
#include "MultibandReverb/BandControls.h"
#include "MultibandReverb/PluginProcessor.h"
#include "MultibandReverb/SpectrumAnalyzer.h"
#include <JuceHeader.h>

// Inner component that holds all band panels side by side.
// Its width grows with the number of bands; it lives inside a Viewport.
class BandStrip : public juce::Component {
  public:
    BandStrip() = default;

    void resized() override {
        const int n       = static_cast<int>(bandControls.size());
        const int bandW   = 160;
        const int spacing = 8;
        setSize(n * bandW + (n - 1) * spacing, getHeight());
        int x = 0;
        for (auto &bc : bandControls) {
            bc->setBounds(x, 0, bandW, getHeight());
            x += bandW + spacing;
        }
    }

    std::vector<std::unique_ptr<BandControls>> bandControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandStrip)
};

class MultibandReverbAudioProcessorEditor : public juce::AudioProcessorEditor {
  public:
    explicit MultibandReverbAudioProcessorEditor(MultibandReverbAudioProcessor &);
    ~MultibandReverbAudioProcessorEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    void rebuildBandControls();
    void onAddBand();
    void onDeleteBand(int bandIndex);

    MultibandReverbAudioProcessor &processorRef;

    SpectrumAnalyzer analyzer;

    // Fixed-size viewport: bands scroll horizontally inside it.
    BandStrip     bandStrip;
    juce::Viewport bandViewport;

    juce::TextButton addBandButton { "+" };

    // FFT smoothing control in the toolbar.
    juce::Slider     smoothingSlider;
    juce::Label      smoothingLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandReverbAudioProcessorEditor)
};
