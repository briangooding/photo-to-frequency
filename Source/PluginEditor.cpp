#include "PluginEditor.h"

namespace
{
    const juce::Colour background   { 0xff15171c };
    const juce::Colour panel        { 0xff1f232b };
    const juce::Colour outline      { 0xff343a46 };
    const juce::Colour accent       { 0xffffb454 };
    const juce::Colour textColour   { 0xffe6e8ec };
    const juce::Colour dimText      { 0xff9aa1ad };

    constexpr int margin = 16;
    constexpr int headerHeight = 44;
    constexpr int controlsHeight = 190;

    bool isImageFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("jpg;jpeg;jpe;png;gif");
    }
}

//==============================================================================
PhotoToFrequencyEditor::PhotoToFrequencyEditor (PhotoToFrequencyProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lookAndFeel.setColourScheme ({ background, panel, panel, outline, textColour, accent, textColour, accent, textColour });
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    lookAndFeel.setColour (juce::Slider::thumbColourId, textColour);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, dimText);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, accent);
    lookAndFeel.setColour (juce::TextButton::textColourOnId, background);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, accent);
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseFile(); };

    // Latching, so the picture keeps playing while you turn knobs.
    addAndMakeVisible (playButton);
    playButton.setClickingTogglesState (true);
    playButton.setTooltip ("Holds a C3 so you can hear the picture without a keyboard");
    playButton.setToggleState (processor.isPreviewHeld(), juce::dontSendNotification);
    playButton.onClick = [this]
    {
        processor.setPreviewHeld (playButton.getToggleState());
        playButton.setButtonText (playButton.getToggleState() ? "Stop" : "Play");
    };
    playButton.setButtonText (playButton.getToggleState() ? "Stop" : "Play");

    addChoice (loopMode, "loop", "Scan Mode");
    addChoice (tuning, "tuning", "Tuning");
    addChoice (partials, "partials", "Partials");

    addAndMakeVisible (invertButton);
    invertAttachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, "invert", invertButton);

    addKnob (scan, "scan", "Scan Time");
    addKnob (range, "range", "Range");
    addKnob (contrast, "contrast", "Contrast");
    addKnob (threshold, "threshold", "Threshold");
    addKnob (tilt, "tilt", "Tilt");
    addKnob (width, "width", "Colour Width");
    addKnob (attack, "attack", "Attack");
    addKnob (release, "release", "Release");
    addKnob (gain, "gain", "Gain");

    processor.addChangeListener (this);
    scanPosition = processor.getScanPosition();
    refreshImage();

    setResizable (true, true);
    setResizeLimits (760, 520, 1800, 1300);
    setSize (900, 620);

    startTimerHz (30);
}

PhotoToFrequencyEditor::~PhotoToFrequencyEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void PhotoToFrequencyEditor::addKnob (Knob& knob, const juce::String& paramId, const juce::String& title)
{
    addAndMakeVisible (knob.slider);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
    knob.attachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, paramId, knob.slider);

    if (auto* param = processor.apvts.getParameter (paramId))
        if (param->getLabel().isNotEmpty())
            knob.slider.setTextValueSuffix (" " + param->getLabel());

    addAndMakeVisible (knob.label);
    knob.label.setText (title, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::Font (juce::FontOptions (13.0f)));
    knob.label.setColour (juce::Label::textColourId, textColour);
}

void PhotoToFrequencyEditor::addChoice (Choice& choice, const juce::String& paramId, const juce::String& title)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        choice.box.addItemList (param->choices, 1);

    addAndMakeVisible (choice.box);
    choice.attachment = std::make_unique<APVTS::ComboBoxAttachment> (processor.apvts, paramId, choice.box);

    addAndMakeVisible (choice.label);
    choice.label.setText (title, juce::dontSendNotification);
    choice.label.setFont (juce::Font (juce::FontOptions (13.0f)));
    choice.label.setColour (juce::Label::textColourId, dimText);
}

//==============================================================================
juce::Rectangle<int> PhotoToFrequencyEditor::getImageArea() const
{
    return getLocalBounds().reduced (margin)
                           .withTrimmedTop (headerHeight)
                           .withTrimmedBottom (controlsHeight);
}

void PhotoToFrequencyEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Header
    auto header = getLocalBounds().reduced (margin).removeFromTop (headerHeight - 10);
    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("Photo To Frequency", header.removeFromLeft (210), juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.setColour (statusMessage.isNotEmpty() ? accent : dimText);
    const auto info = statusMessage.isNotEmpty() ? statusMessage
                    : imageName.isNotEmpty()     ? imageName
                                                 : juce::String ("Built-in picture - drop a JPG here or use Load Image");
    g.drawText (info, header.withTrimmedRight (240).reduced (8, 0), juce::Justification::centredLeft);

    // Picture: stretched to fill, because x is time and y is pitch.
    const auto area = getImageArea();
    g.setColour (juce::Colours::black);
    g.fillRect (area);

    if (image.isValid())
        g.drawImage (image, area.toFloat(), juce::RectanglePlacement::stretchToFit);

    // Axis hints, with a dark edge so they read over bright pictures too.
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (const bool shadow : { true, false })
    {
        const auto textArea = area.reduced (6, 4).translated (shadow ? 1 : 0, shadow ? 1 : 0);
        g.setColour (shadow ? juce::Colours::black.withAlpha (0.8f) : textColour);
        g.drawText ("high", textArea, juce::Justification::topLeft);
        g.drawText ("low", textArea, juce::Justification::bottomLeft);
        g.drawText ("time >", textArea, juce::Justification::bottomRight);
    }

    if (scanPosition >= 0.0f)
    {
        const float x = (float) area.getX() + scanPosition * (float) (area.getWidth() - 1);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (x - 1.5f, (float) area.getY(), 4.0f, (float) area.getHeight());
        g.setColour (accent);
        g.fillRect (x - 0.5f, (float) area.getY(), 2.0f, (float) area.getHeight());
    }

    g.setColour (dragHighlight ? accent : outline);
    g.drawRect (area.expanded (1), dragHighlight ? 2 : 1);

    // Controls panel
    const auto controls = getLocalBounds().reduced (margin).removeFromBottom (controlsHeight - margin);
    g.setColour (panel);
    g.fillRoundedRectangle (controls.toFloat(), 6.0f);
}

void PhotoToFrequencyEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    auto header = bounds.removeFromTop (headerHeight - 10);
    playButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (8);
    loadButton.setBounds (header.removeFromRight (130));

    auto controls = bounds.removeFromBottom (controlsHeight - margin).reduced (12, 10);

    auto topRow = controls.removeFromTop (46);
    const int choiceWidth = juce::jmin (190, (topRow.getWidth() - 110) / 3);

    for (auto* choice : { &loopMode, &tuning, &partials })
    {
        auto cell = topRow.removeFromLeft (choiceWidth).withTrimmedRight (12);
        choice->label.setBounds (cell.removeFromTop (18));
        choice->box.setBounds (cell.removeFromTop (26));
    }

    invertButton.setBounds (topRow.removeFromLeft (100).withTrimmedTop (18).withHeight (26));

    controls.removeFromTop (8);
    Knob* knobs[] = { &scan, &range, &contrast, &threshold, &tilt, &width, &attack, &release, &gain };
    const int knobWidth = controls.getWidth() / (int) std::size (knobs);

    for (auto* knob : knobs)
    {
        auto cell = controls.removeFromLeft (knobWidth);
        knob->label.setBounds (cell.removeFromTop (18));
        knob->slider.setBounds (cell.reduced (2, 0));
    }
}

//==============================================================================
void PhotoToFrequencyEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshImage();
}

void PhotoToFrequencyEditor::refreshImage()
{
    image = processor.getPreviewImage();
    imageName = processor.getImageName();
    repaint();
}

void PhotoToFrequencyEditor::timerCallback()
{
    const float newPosition = processor.getScanPosition();

    if (! juce::approximatelyEqual (newPosition, scanPosition))
    {
        scanPosition = newPosition;
        repaint (getImageArea().expanded (4));
    }
}

void PhotoToFrequencyEditor::chooseFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Choose a picture",
                                                       juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                       "*.jpg;*.jpeg;*.jpe;*.png;*.gif");

    const juce::Component::SafePointer<PhotoToFrequencyEditor> safeThis (this);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (safeThis != nullptr && file != juce::File())
                                      safeThis->loadFile (file);
                              });
}

void PhotoToFrequencyEditor::loadFile (const juce::File& file)
{
    statusMessage = processor.loadImageFile (file) ? juce::String()
                                                   : "Couldn't read " + file.getFileName() + " as a JPG, PNG or GIF";
    repaint();
}

bool PhotoToFrequencyEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isImageFile (f))
            return true;

    return false;
}

void PhotoToFrequencyEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight = true;
    repaint();
}

void PhotoToFrequencyEditor::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void PhotoToFrequencyEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;

    for (auto& f : files)
    {
        if (isImageFile (f))
        {
            loadFile (juce::File (f));
            return;
        }
    }

    repaint();
}
