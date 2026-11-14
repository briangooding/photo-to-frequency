#pragma once

#include "PluginProcessor.h"

/** Shows the picture and the scan head. In the Split plugin it also shows the
    part of the picture that is played and lets you drag it. */
class PictureView final : public juce::Component
{
public:
    explicit PictureView (PhotoToFrequencyProcessor&);

    void setImage (const juce::Image&);
    void setDragHighlight (bool);

    /** Called from the editor's timer; repaints if the scan head or region moved. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    /** Region in picture fractions: x from the left, y from the bottom. */
    struct Region
    {
        float left = 0.0f, right = 1.0f, bottom = 0.0f, top = 1.0f;

        bool operator== (const Region& o) const noexcept
        {
            return juce::exactlyEqual (left, o.left) && juce::exactlyEqual (right, o.right)
                && juce::exactlyEqual (bottom, o.bottom) && juce::exactlyEqual (top, o.top);
        }
    };

    enum Edge { noEdge = 0, leftEdge = 1, rightEdge = 2, bottomEdge = 4, topEdge = 8, moveAll = 16 };

    Region readRegion() const;
    void writeRegion (const Region&);
    juce::Rectangle<float> regionToBounds (const Region&) const;
    int edgesAt (juce::Point<float>) const;
    void setGesture (bool active);

    PhotoToFrequencyProcessor& processor;
    juce::RangedAudioParameter* regionParams[4] = {};   // left, right, bottom, top

    juce::Image image;
    float scanPosition = -1.0f;
    bool dragHighlight = false;

    Region shownRegion, dragStartRegion;
    int dragEdges = noEdge;
    bool gestureActive = false;
};

//==============================================================================
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

    /** A titled run of knobs in the lower row of the Split plugin. */
    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    Knob* addKnob (const juce::String& paramId, const juce::String& title);
    Choice* addChoice (const juce::String& paramId, const juce::String& title);
    void chooseFile();
    void loadFile (const juce::File&);
    void refreshImage();
    int getControlsHeight() const;

    PhotoToFrequencyProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    PictureView picture;
    juce::TextButton loadButton { "Load Image..." };
    juce::TextButton playButton { "Play" };
    juce::ToggleButton invertButton { "Invert" };
    std::unique_ptr<APVTS::ButtonAttachment> invertAttachment;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Choice> choices;
    std::vector<Knob*> mainRow;
    std::vector<Group> groups;

    juce::String imageName, statusMessage;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhotoToFrequencyEditor)
};
