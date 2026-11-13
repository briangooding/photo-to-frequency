#pragma once

#include "PluginProcessor.h"

class PhotoToFrequencyEditor final : public juce::AudioProcessorEditor,
                                     public juce::FileDragAndDropTarget,
                                     private juce::ChangeListener,
                                     private juce::Timer
{
public:
    explicit PhotoToFrequencyEditor (PhotoToFrequencyProcessor&);
    ~PhotoToFrequencyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void addKnob (Knob&, const juce::String& paramId, const juce::String& title);
    void addChoice (Choice&, const juce::String& paramId, const juce::String& title);
    void chooseFile();
    void loadFile (const juce::File&);
    void refreshImage();
    juce::Rectangle<int> getImageArea() const;

    PhotoToFrequencyProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    juce::TextButton loadButton { "Load Image..." };
    juce::TextButton playButton { "Play" };
    juce::ToggleButton invertButton { "Invert" };
    std::unique_ptr<APVTS::ButtonAttachment> invertAttachment;

    Choice loopMode, tuning, partials;
    Knob scan, range, contrast, threshold, tilt, width, attack, release, gain;

    juce::Image image;
    juce::String imageName, statusMessage;
    float scanPosition = -1.0f;
    bool dragHighlight = false;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhotoToFrequencyEditor)
};
