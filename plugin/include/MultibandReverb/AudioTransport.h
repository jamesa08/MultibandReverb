// AudioTransport.h
#pragma once
#include <JuceHeader.h>

class AudioTransportComponent : public juce::Component,
                              public juce::Timer,
                              public juce::ChangeListener
{
public:
    AudioTransportComponent()
    {
        addAndMakeVisible(loadButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(positionSlider);
        
        loadButton.setButtonText("Load File");
        playButton.setButtonText("Play");
        stopButton.setButtonText("Stop");
        
        loadButton.onClick = [this] { loadButtonClicked(); };
        playButton.onClick = [this] { playButtonClicked(); };
        stopButton.onClick = [this] { stopButtonClicked(); };
        
        positionSlider.setRange(0.0, 1.0);
        positionSlider.onValueChange = [this] {
            if (formatManager.getNumKnownFormats() > 0 && transportSource.isPlaying()) {
                const double position = positionSlider.getValue() * transportSource.getLengthInSeconds();
                transportSource.setPosition(position);
            }
        };
        
        formatManager.registerBasicFormats();
        transportSource.addChangeListener(this);
        startTimer(20); // Update slider 50 times per second
    }
    
    ~AudioTransportComponent() override
    {
        stopTimer();
        transportSource.setSource(nullptr);
        transportSource.removeChangeListener(this);
    }
    
    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }
    
    void resized() override
    {
        auto area = getLocalBounds();
        auto buttonHeight = 30;
        auto margin = 5;
        
        auto buttonArea = area.removeFromTop(buttonHeight);
        loadButton.setBounds(buttonArea.removeFromLeft(100).reduced(margin));
        playButton.setBounds(buttonArea.removeFromLeft(100).reduced(margin));
        stopButton.setBounds(buttonArea.removeFromLeft(100).reduced(margin));
        
        area.removeFromTop(margin);
        positionSlider.setBounds(area.removeFromTop(buttonHeight).reduced(margin));
    }
    
    void loadButtonClicked()
    {
        chooser = std::make_unique<juce::FileChooser>("Select an audio file...",
                                                     juce::File{},
                                                     "*.wav;*.mp3;*.aiff");
        auto folderChooserFlags = juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles;
        
        chooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            
            if (file != juce::File{})
            {
                auto* reader = formatManager.createReaderFor(file);
                
                if (reader != nullptr)
                {
                    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
                    transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
                    readerSource.reset(newSource.release());
                }
            }
        });
    }
    
    void playButtonClicked()
    {
        if (readerSource.get() == nullptr)
        {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                            "No File Loaded",
                                            "Please load an audio file first!");
            return;
        }
        
        if (transportSource.isPlaying())
        {
            transportSource.stop();
        }
        else
        {
            transportSource.start();
        }
        
        updatePlayButtonText();
    }
    
    void stopButtonClicked()
    {
        transportSource.stop();
        transportSource.setPosition(0.0);
        updatePlayButtonText();
    }
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }
    
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
    {
        if (readerSource.get() == nullptr)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }
        
        transportSource.getNextAudioBlock(bufferToFill);
    }
    
    void releaseResources()
    {
        transportSource.releaseResources();
    }
    
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        updatePlayButtonText();
    }
    
    void timerCallback() override
    {
        if (transportSource.isPlaying())
        {
            const double position = transportSource.getCurrentPosition();
            const double length = transportSource.getLengthInSeconds();
            
            if (length > 0.0)
                positionSlider.setValue(position / length, juce::dontSendNotification);
        }
    }
    
private:
    void updatePlayButtonText()
    {
        playButton.setButtonText(transportSource.isPlaying() ? "Pause" : "Play");
    }
    
    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Slider positionSlider;
    
    std::unique_ptr<juce::FileChooser> chooser;
    
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTransportComponent)
};