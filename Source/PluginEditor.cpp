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
    constexpr int choiceRowHeight = 46;
    constexpr int knobRowHeight = 104;
    constexpr int groupTitleHeight = 22;

    constexpr float minRegionSize = 0.02f;
    constexpr float grabDistance = 8.0f;

    bool isImageFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("jpg;jpeg;jpe;png;gif");
    }

    juce::Font uiFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
PictureView::PictureView (PhotoToFrequencyProcessor& p) : processor (p)
{
    if (PluginConfig::isSplit)
    {
        const char* ids[] = { "regionLeft", "regionRight", "regionBottom", "regionTop" };

        for (size_t i = 0; i < 4; ++i)
            regionParams[i] = processor.apvts.getParameter (ids[i]);

        shownRegion = readRegion();
    }

    scanPosition = processor.getScanPosition();
}

void PictureView::setImage (const juce::Image& newImage)
{
    image = newImage;
    repaint();
}

void PictureView::setDragHighlight (bool shouldHighlight)
{
    if (dragHighlight != shouldHighlight)
    {
        dragHighlight = shouldHighlight;
        repaint();
    }
}

void PictureView::refresh()
{
    const float newPosition = processor.getScanPosition();
    const auto newRegion = readRegion();

    if (! juce::approximatelyEqual (newPosition, scanPosition) || ! (newRegion == shownRegion))
    {
        scanPosition = newPosition;
        shownRegion = newRegion;
        repaint();
    }
}

PictureView::Region PictureView::readRegion() const
{
    Region r;

    if (regionParams[0] == nullptr)
        return r;

    r.left   = regionParams[0]->getValue();
    r.right  = regionParams[1]->getValue();
    r.bottom = regionParams[2]->getValue();
    r.top    = regionParams[3]->getValue();

    if (r.right < r.left)  std::swap (r.left, r.right);
    if (r.top < r.bottom)  std::swap (r.bottom, r.top);

    return r;
}

void PictureView::writeRegion (const Region& r)
{
    const float values[] = { r.left, r.right, r.bottom, r.top };

    for (size_t i = 0; i < 4; ++i)
        if (regionParams[i] != nullptr && ! juce::exactlyEqual (regionParams[i]->getValue(), values[i]))
            regionParams[i]->setValueNotifyingHost (values[i]);

    shownRegion = r;
    repaint();
}

void PictureView::setGesture (bool active)
{
    if (gestureActive == active)
        return;

    gestureActive = active;

    for (auto* param : regionParams)
    {
        if (param != nullptr)
        {
            if (active)  param->beginChangeGesture();
            else         param->endChangeGesture();
        }
    }
}

juce::Rectangle<float> PictureView::regionToBounds (const Region& r) const
{
    const auto w = (float) getWidth(), h = (float) getHeight();
    return { r.left * w, (1.0f - r.top) * h, (r.right - r.left) * w, (r.top - r.bottom) * h };
}

int PictureView::edgesAt (juce::Point<float> point) const
{
    const auto box = regionToBounds (shownRegion);

    if (! box.expanded (grabDistance).contains (point))
        return noEdge;

    int edges = noEdge;

    if (std::abs (point.x - box.getX()) <= grabDistance)            edges |= leftEdge;
    else if (std::abs (point.x - box.getRight()) <= grabDistance)   edges |= rightEdge;

    if (std::abs (point.y - box.getY()) <= grabDistance)            edges |= topEdge;
    else if (std::abs (point.y - box.getBottom()) <= grabDistance)  edges |= bottomEdge;

    return edges != noEdge ? edges : moveAll;
}

void PictureView::mouseMove (const juce::MouseEvent& e)
{
    if (! PluginConfig::isSplit)
        return;

    const int edges = edgesAt (e.position);
    const bool horizontal = (edges & (leftEdge | rightEdge)) != 0;
    const bool vertical = (edges & (topEdge | bottomEdge)) != 0;

    if (edges == moveAll)                 setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else if (horizontal && vertical)      setMouseCursor (((edges & leftEdge) != 0) == ((edges & topEdge) != 0)
                                                            ? juce::MouseCursor::TopLeftCornerResizeCursor
                                                            : juce::MouseCursor::TopRightCornerResizeCursor);
    else if (horizontal)                  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (vertical)                    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else                                  setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void PictureView::mouseDown (const juce::MouseEvent& e)
{
    if (! PluginConfig::isSplit || getWidth() <= 0 || getHeight() <= 0)
        return;

    dragEdges = edgesAt (e.position);
    dragStartRegion = shownRegion;
    setGesture (true);

    if (dragEdges == noEdge)
    {
        // Clicked outside the region: start drawing a new one from here.
        const float x = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / (float) getHeight());
        dragStartRegion = { x, x, y, y };
        dragEdges = rightEdge | bottomEdge;
    }
}

void PictureView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragEdges == noEdge)
        return;

    const float dx = (float) e.getDistanceFromDragStartX() / (float) getWidth();
    const float dy = (float) -e.getDistanceFromDragStartY() / (float) getHeight();
    auto r = dragStartRegion;

    if (dragEdges == moveAll)
    {
        const float moveX = juce::jlimit (-r.left, 1.0f - r.right, dx);
        const float moveY = juce::jlimit (-r.bottom, 1.0f - r.top, dy);
        r.left += moveX;    r.right += moveX;
        r.bottom += moveY;  r.top += moveY;
    }
    else
    {
        if ((dragEdges & leftEdge) != 0)    r.left   = juce::jlimit (0.0f, 1.0f, r.left + dx);
        if ((dragEdges & rightEdge) != 0)   r.right  = juce::jlimit (0.0f, 1.0f, r.right + dx);
        if ((dragEdges & bottomEdge) != 0)  r.bottom = juce::jlimit (0.0f, 1.0f, r.bottom + dy);
        if ((dragEdges & topEdge) != 0)     r.top    = juce::jlimit (0.0f, 1.0f, r.top + dy);

        // Dragging an edge past its opposite simply flips the box.
        if (r.right < r.left)  std::swap (r.left, r.right);
        if (r.top < r.bottom)  std::swap (r.bottom, r.top);

        auto widen = [] (float& low, float& high)
        {
            if (high - low < minRegionSize)
            {
                const float centre = juce::jlimit (minRegionSize * 0.5f, 1.0f - minRegionSize * 0.5f, 0.5f * (low + high));
                low = centre - minRegionSize * 0.5f;
                high = centre + minRegionSize * 0.5f;
            }
        };

        widen (r.left, r.right);
        widen (r.bottom, r.top);
    }

    writeRegion (r);
}

void PictureView::mouseUp (const juce::MouseEvent&)
{
    dragEdges = noEdge;
    setGesture (false);
}

void PictureView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! PluginConfig::isSplit)
        return;

    setGesture (true);
    writeRegion ({});
    setGesture (false);
}

void PictureView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    g.fillAll (juce::Colours::black);

    // Stretched to fill, because x is time and y is pitch.
    if (image.isValid())
        g.drawImage (image, area.toFloat(), juce::RectanglePlacement::stretchToFit);

    const auto box = regionToBounds (shownRegion);
    const bool regionIsWholePicture = shownRegion == Region();

    if (PluginConfig::isSplit && ! regionIsWholePicture)
    {
        // Dim everything that isn't played.
        juce::RectangleList<float> outside (area.toFloat());
        outside.subtract (box);
        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.fillRectList (outside);
    }

    // Axis hints, with a dark edge so they read over bright pictures too.
    g.setFont (uiFont (11.0f));

    for (const bool shadow : { true, false })
    {
        const auto textArea = area.reduced (6, 4).translated (shadow ? 1 : 0, shadow ? 1 : 0);
        g.setColour (shadow ? juce::Colours::black.withAlpha (0.8f) : textColour);
        g.drawText ("high", textArea, juce::Justification::topLeft);
        g.drawText ("low", textArea, juce::Justification::bottomLeft);
        g.drawText (PluginConfig::isSplit ? "drag to choose the part that plays - double-click for all" : "time >",
                    textArea, juce::Justification::bottomRight);
    }

    if (scanPosition >= 0.0f)
    {
        const float x = scanPosition * (float) (getWidth() - 1);
        const float top = PluginConfig::isSplit ? box.getY() : 0.0f;
        const float height = PluginConfig::isSplit ? box.getHeight() : (float) getHeight();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (x - 1.5f, top, 4.0f, height);
        g.setColour (accent);
        g.fillRect (x - 0.5f, top, 2.0f, height);
    }

    if (PluginConfig::isSplit)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRect (box.expanded (1.0f), 1.0f);
        g.setColour (textColour);
        g.drawRect (box, 1.0f);

        // Corner handles
        for (const auto corner : { box.getTopLeft(), box.getTopRight(), box.getBottomLeft(), box.getBottomRight() })
        {
            const auto handle = juce::Rectangle<float> (7.0f, 7.0f).withCentre (corner).constrainedWithin (area.toFloat());
            g.setColour (juce::Colours::black);
            g.fillRect (handle.expanded (1.0f));
            g.setColour (textColour);
            g.fillRect (handle);
        }
    }

    g.setColour (dragHighlight ? accent : outline);
    g.drawRect (area, dragHighlight ? 2 : 1);
}

//==============================================================================
PhotoToFrequencyEditor::PhotoToFrequencyEditor (PhotoToFrequencyProcessor& p)
    : AudioProcessorEditor (&p), processor (p), picture (p)
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

    addAndMakeVisible (picture);

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

    addChoice ("loop", "Scan Mode");
    addChoice ("tuning", "Tuning");
    addChoice ("partials", "Partials");

    if (PluginConfig::isSplit)
        addChoice ("filterType", "Filter Type");

    addAndMakeVisible (invertButton);
    invertAttachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, "invert", invertButton);

    mainRow = { addKnob ("scan", "Scan Time"), addKnob ("range", "Range"), addKnob ("contrast", "Contrast"),
                addKnob ("threshold", "Threshold"), addKnob ("tilt", "Tilt"), addKnob ("width", "Colour Width") };

    if (PluginConfig::isSplit)
    {
        mainRow.push_back (addKnob ("gain", "Gain"));

        groups.push_back ({ "OSCILLATOR ENVELOPE",
                            { addKnob ("attack", "Attack"), addKnob ("decay", "Decay"),
                              addKnob ("sustain", "Sustain"), addKnob ("release", "Release") }, {} });
        groups.push_back ({ "FILTER",
                            { addKnob ("cutoff", "Cutoff"), addKnob ("resonance", "Resonance"),
                              addKnob ("filterEnvAmount", "Env Amount") }, {} });
        groups.push_back ({ "FILTER ENVELOPE",
                            { addKnob ("filterAttack", "Attack"), addKnob ("filterDecay", "Decay"),
                              addKnob ("filterSustain", "Sustain"), addKnob ("filterRelease", "Release") }, {} });
    }
    else
    {
        mainRow.push_back (addKnob ("attack", "Attack"));
        mainRow.push_back (addKnob ("release", "Release"));
        mainRow.push_back (addKnob ("gain", "Gain"));
    }

    processor.addChangeListener (this);
    refreshImage();

    setResizable (true, true);

    if (PluginConfig::isSplit)
    {
        setResizeLimits (940, 660, 2000, 1500);
        setSize (1020, 780);
    }
    else
    {
        setResizeLimits (760, 520, 1800, 1300);
        setSize (900, 620);
    }

    startTimerHz (30);
}

PhotoToFrequencyEditor::~PhotoToFrequencyEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

PhotoToFrequencyEditor::Knob* PhotoToFrequencyEditor::addKnob (const juce::String& paramId, const juce::String& title)
{
    auto* knob = knobs.add (new Knob());

    addAndMakeVisible (knob->slider);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob->attachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, paramId, knob->slider);

    if (auto* param = processor.apvts.getParameter (paramId))
        if (param->getLabel().isNotEmpty())
            knob->slider.setTextValueSuffix (" " + param->getLabel());

    addAndMakeVisible (knob->label);
    knob->label.setText (title, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (uiFont (13.0f));
    knob->label.setColour (juce::Label::textColourId, textColour);

    return knob;
}

PhotoToFrequencyEditor::Choice* PhotoToFrequencyEditor::addChoice (const juce::String& paramId, const juce::String& title)
{
    auto* choice = choices.add (new Choice());

    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        choice->box.addItemList (param->choices, 1);

    addAndMakeVisible (choice->box);
    choice->attachment = std::make_unique<APVTS::ComboBoxAttachment> (processor.apvts, paramId, choice->box);

    addAndMakeVisible (choice->label);
    choice->label.setText (title, juce::dontSendNotification);
    choice->label.setFont (uiFont (13.0f));
    choice->label.setColour (juce::Label::textColourId, dimText);

    return choice;
}

//==============================================================================
int PhotoToFrequencyEditor::getControlsHeight() const
{
    int height = 20 + choiceRowHeight + 8 + knobRowHeight;

    if (! groups.empty())
        height += 10 + groupTitleHeight + knobRowHeight;

    return height;
}

void PhotoToFrequencyEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Header
    auto header = getLocalBounds().reduced (margin).removeFromTop (headerHeight - 10);
    const juce::String title = PluginConfig::isSplit ? "Photo To Frequency Split" : "Photo To Frequency";
    g.setColour (textColour);
    g.setFont (uiFont (20.0f, true));
    g.drawText (title, header.removeFromLeft (PluginConfig::isSplit ? 260 : 210), juce::Justification::centredLeft);

    g.setFont (uiFont (13.0f));
    g.setColour (statusMessage.isNotEmpty() ? accent : dimText);
    const auto info = statusMessage.isNotEmpty() ? statusMessage
                    : imageName.isNotEmpty()     ? imageName
                                                 : juce::String ("Built-in picture - drop a JPG here or use Load Image");
    g.drawText (info, header.withTrimmedRight (240).reduced (8, 0), juce::Justification::centredLeft);

    // Controls panel
    const auto controls = getLocalBounds().reduced (margin).removeFromBottom (getControlsHeight());
    g.setColour (panel);
    g.fillRoundedRectangle (controls.toFloat(), 6.0f);

    // Group titles, each with a rule across its knobs
    g.setFont (uiFont (11.0f, true));

    for (auto& group : groups)
    {
        auto titleArea = group.bounds.withHeight (groupTitleHeight).reduced (6, 0);
        g.setColour (outline);
        g.fillRect (titleArea.withTop (titleArea.getBottom() - 4).withHeight (1));
        g.setColour (accent);
        g.drawText (group.title, titleArea.withTrimmedBottom (6), juce::Justification::centredLeft);
    }
}

void PhotoToFrequencyEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    auto header = bounds.removeFromTop (headerHeight - 10);
    playButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (8);
    loadButton.setBounds (header.removeFromRight (130));

    auto controls = bounds.removeFromBottom (getControlsHeight()).reduced (12, 10);
    bounds.removeFromTop (10);
    bounds.removeFromBottom (margin);
    picture.setBounds (bounds);

    // Choices
    auto topRow = controls.removeFromTop (choiceRowHeight);
    const int choiceWidth = juce::jmin (190, (topRow.getWidth() - 110) / juce::jmax (1, choices.size()));

    for (auto* choice : choices)
    {
        auto cell = topRow.removeFromLeft (choiceWidth).withTrimmedRight (12);
        choice->label.setBounds (cell.removeFromTop (18));
        choice->box.setBounds (cell.removeFromTop (26));
    }

    invertButton.setBounds (topRow.removeFromLeft (100).withTrimmedTop (18).withHeight (26));

    auto layOutKnobs = [] (const std::vector<Knob*>& row, juce::Rectangle<int> area, int knobWidth)
    {
        for (auto* knob : row)
        {
            auto cell = area.removeFromLeft (knobWidth);
            knob->label.setBounds (cell.removeFromTop (18));
            knob->slider.setBounds (cell.reduced (2, 0));
        }
    };

    // Main knob row
    controls.removeFromTop (8);
    auto mainArea = controls.removeFromTop (knobRowHeight);
    layOutKnobs (mainRow, mainArea, mainArea.getWidth() / juce::jmax (1, (int) mainRow.size()));

    // Envelope and filter groups
    if (! groups.empty())
    {
        controls.removeFromTop (10);
        auto groupArea = controls;

        int totalKnobs = 0;
        for (auto& group : groups)
            totalKnobs += (int) group.knobs.size();

        constexpr int gap = 14;
        const int knobWidth = (groupArea.getWidth() - gap * ((int) groups.size() - 1)) / juce::jmax (1, totalKnobs);

        for (auto& group : groups)
        {
            group.bounds = groupArea.removeFromLeft (knobWidth * (int) group.knobs.size());
            groupArea.removeFromLeft (gap);
            layOutKnobs (group.knobs, group.bounds.withTrimmedTop (groupTitleHeight), knobWidth);
        }
    }
}

//==============================================================================
void PhotoToFrequencyEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshImage();
}

void PhotoToFrequencyEditor::refreshImage()
{
    picture.setImage (processor.getPreviewImage());
    imageName = processor.getImageName();
    repaint();
}

void PhotoToFrequencyEditor::timerCallback()
{
    picture.refresh();
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
    picture.setDragHighlight (true);
}

void PhotoToFrequencyEditor::fileDragExit (const juce::StringArray&)
{
    picture.setDragHighlight (false);
}

void PhotoToFrequencyEditor::filesDropped (const juce::StringArray& files, int, int)
{
    picture.setDragHighlight (false);

    for (auto& f : files)
    {
        if (isImageFile (f))
        {
            loadFile (juce::File (f));
            return;
        }
    }
}
