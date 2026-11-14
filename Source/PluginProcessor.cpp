#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier imageDataProperty ("imageData");
    const juce::Identifier imageNameProperty ("imageName");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PhotoToFrequencyProcessor::createParameterLayout()
{
    using Range = juce::NormalisableRange<float>;
    using FloatParam = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    auto skewed = [] (float low, float high, float interval, float centre)
    {
        Range r (low, high, interval);
        r.setSkewForCentre (centre);
        return r;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "scan", 1 }, "Scan Time",
                                              skewed (0.05f, 60.0f, 0.01f, 4.0f), 4.0f, Attributes().withLabel ("s")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "loop", 1 }, "Scan Mode",
                                                              juce::StringArray { "Loop", "Ping-Pong", "One-Shot" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "tuning", 1 }, "Tuning",
                                                              juce::StringArray { "Harmonic", "Free", "Chromatic", "Major", "Minor", "Pentatonic" }, 0));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "range", 1 }, "Range",
                                              Range (1.0f, 8.0f, 0.01f), 5.0f, Attributes().withLabel ("oct")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "partials", 1 }, "Partials",
                                                              juce::StringArray { "16", "32", "64", "128" }, 2));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "contrast", 1 }, "Contrast",
                                              skewed (0.5f, 6.0f, 0.01f, 2.0f), 2.0f));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "threshold", 1 }, "Threshold",
                                              Range (0.0f, 0.9f, 0.01f), 0.1f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "invert", 1 }, "Invert", false));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "tilt", 1 }, "Tilt",
                                              Range (0.0f, 2.0f, 0.01f), 0.5f));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "width", 1 }, "Colour Width",
                                              Range (0.0f, 1.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "attack", 1 }, "Attack",
                                              skewed (0.001f, 5.0f, 0.001f, 0.2f), 0.02f, Attributes().withLabel ("s")));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "release", 1 }, "Release",
                                              skewed (0.01f, 10.0f, 0.01f, 1.0f), 0.5f, Attributes().withLabel ("s")));
    layout.add (std::make_unique<FloatParam> (juce::ParameterID { "gain", 1 }, "Gain",
                                              Range (-48.0f, 12.0f, 0.1f), -6.0f, Attributes().withLabel ("dB")));

    if (PluginConfig::isSplit)
    {
        // Oscillator envelope: attack and release above, plus these two.
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "decay", 1 }, "Decay",
                                                  skewed (0.005f, 10.0f, 0.001f, 0.5f), 0.3f, Attributes().withLabel ("s")));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "sustain", 1 }, "Sustain",
                                                  Range (0.0f, 1.0f, 0.01f), 1.0f));

        // Part of the picture that is played.
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "regionLeft", 1 }, "Region Left", Range (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "regionRight", 1 }, "Region Right", Range (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "regionBottom", 1 }, "Region Bottom", Range (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "regionTop", 1 }, "Region Top", Range (0.0f, 1.0f), 1.0f));

        // Filter and its envelope.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "filterType", 1 }, "Filter Type",
                                                                  juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass" }, 0));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "cutoff", 1 }, "Cutoff",
                                                  skewed (20.0f, 20000.0f, 1.0f, 1000.0f), 20000.0f, Attributes().withLabel ("Hz")));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "resonance", 1 }, "Resonance",
                                                  Range (0.0f, 1.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "filterEnvAmount", 1 }, "Filter Env Amount",
                                                  Range (-8.0f, 8.0f, 0.01f), 0.0f, Attributes().withLabel ("oct")));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "filterAttack", 1 }, "Filter Attack",
                                                  skewed (0.001f, 5.0f, 0.001f, 0.2f), 0.01f, Attributes().withLabel ("s")));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "filterDecay", 1 }, "Filter Decay",
                                                  skewed (0.005f, 10.0f, 0.001f, 0.5f), 0.4f, Attributes().withLabel ("s")));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "filterSustain", 1 }, "Filter Sustain",
                                                  Range (0.0f, 1.0f, 0.01f), 0.3f));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "filterRelease", 1 }, "Filter Release",
                                                  skewed (0.01f, 10.0f, 0.01f, 1.0f), 0.5f, Attributes().withLabel ("s")));
    }

    if (PluginConfig::isFreq)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "subOn", 1 }, "Sub On", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "subOctave", 1 }, "Sub Octave",
                                                                  juce::StringArray { "-1 oct", "-2 oct" }, 0));
        layout.add (std::make_unique<FloatParam> (juce::ParameterID { "subLevel", 1 }, "Sub Level",
                                                  Range (0.0f, 1.0f, 0.01f), 0.5f));
    }

    return layout;
}

PhotoToFrequencyProcessor::PhotoToFrequencyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, PluginConfig::stateType, createParameterLayout())
{
    params.scan      = apvts.getRawParameterValue ("scan");
    params.loopMode  = apvts.getRawParameterValue ("loop");
    params.tuning    = apvts.getRawParameterValue ("tuning");
    params.range     = apvts.getRawParameterValue ("range");
    params.partials  = apvts.getRawParameterValue ("partials");
    params.contrast  = apvts.getRawParameterValue ("contrast");
    params.threshold = apvts.getRawParameterValue ("threshold");
    params.invert    = apvts.getRawParameterValue ("invert");
    params.tilt      = apvts.getRawParameterValue ("tilt");
    params.width     = apvts.getRawParameterValue ("width");
    params.attack    = apvts.getRawParameterValue ("attack");
    params.release   = apvts.getRawParameterValue ("release");
    params.gain      = apvts.getRawParameterValue ("gain");

    if (PluginConfig::isSplit)
    {
        params.decay           = apvts.getRawParameterValue ("decay");
        params.sustain         = apvts.getRawParameterValue ("sustain");
        params.regionLeft      = apvts.getRawParameterValue ("regionLeft");
        params.regionRight     = apvts.getRawParameterValue ("regionRight");
        params.regionBottom    = apvts.getRawParameterValue ("regionBottom");
        params.regionTop       = apvts.getRawParameterValue ("regionTop");
        params.filterType      = apvts.getRawParameterValue ("filterType");
        params.cutoff          = apvts.getRawParameterValue ("cutoff");
        params.resonance       = apvts.getRawParameterValue ("resonance");
        params.filterEnvAmount = apvts.getRawParameterValue ("filterEnvAmount");
        params.filterAttack    = apvts.getRawParameterValue ("filterAttack");
        params.filterDecay     = apvts.getRawParameterValue ("filterDecay");
        params.filterSustain   = apvts.getRawParameterValue ("filterSustain");
        params.filterRelease   = apvts.getRawParameterValue ("filterRelease");
    }

    if (PluginConfig::isFreq)
    {
        params.subOn     = apvts.getRawParameterValue ("subOn");
        params.subOctave = apvts.getRawParameterValue ("subOctave");
        params.subLevel  = apvts.getRawParameterValue ("subLevel");
    }

    setImage (ImageSpectrum::createDefaultImage(), {});
}

//==============================================================================
bool PhotoToFrequencyProcessor::loadImageFile (const juce::File& file)
{
    const auto image = ImageSpectrum::loadFile (file);

    if (! image.isValid())
        return false;

    setImage (image, file.getFileName());
    return true;
}

void PhotoToFrequencyProcessor::setImage (const juce::Image& image, const juce::String& name)
{
    auto newTable = ImageSpectrum::analyse (image, name);

    {
        const juce::ScopedLock sl (imageLock);

        // Drop tables that only this list still refers to.
        for (int i = retired.size(); --i >= 0;)
            if (retired.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
                retired.remove (i);

        const juce::SpinLock::ScopedLockType tl (tableLock);

        if (currentTable != nullptr)
            retired.add (currentTable);

        currentTable = newTable;
    }

    sendChangeMessage();
}

juce::Image PhotoToFrequencyProcessor::getPreviewImage() const
{
    const juce::SpinLock::ScopedLockType tl (tableLock);
    return currentTable != nullptr ? currentTable->preview : juce::Image();
}

juce::String PhotoToFrequencyProcessor::getImageName() const
{
    const juce::SpinLock::ScopedLockType tl (tableLock);
    return currentTable != nullptr ? currentTable->name : juce::String();
}

//==============================================================================
void PhotoToFrequencyProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    previewPlaying = false;
    lastGain = juce::Decibels::decibelsToGain (params.gain->load());
    scanPosition.store (-1.0f);
}

bool PhotoToFrequencyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

EngineParams PhotoToFrequencyProcessor::readParams() const
{
    EngineParams p;
    p.scanSeconds  = params.scan->load();
    p.loopMode     = juce::roundToInt (params.loopMode->load());
    p.tuning       = juce::roundToInt (params.tuning->load());
    p.rangeOctaves = params.range->load();
    p.level        = juce::roundToInt (params.partials->load());
    p.contrast     = params.contrast->load();
    p.threshold    = params.threshold->load();
    p.invert       = params.invert->load() > 0.5f;
    p.tilt         = params.tilt->load();
    p.width        = params.width->load();
    p.attack       = params.attack->load();
    p.release      = params.release->load();

    if (PluginConfig::isSplit)
    {
        p.decay           = params.decay->load();
        p.sustain         = params.sustain->load();
        p.regionLeft      = params.regionLeft->load();
        p.regionRight     = params.regionRight->load();
        p.regionBottom    = params.regionBottom->load();
        p.regionTop       = params.regionTop->load();
        p.filterOn        = true;
        p.filterType      = juce::roundToInt (params.filterType->load());
        p.cutoff          = params.cutoff->load();
        p.resonance       = params.resonance->load();
        p.filterEnvAmount = params.filterEnvAmount->load();
        p.filterAttack    = params.filterAttack->load();
        p.filterDecay     = params.filterDecay->load();
        p.filterSustain   = params.filterSustain->load();
        p.filterRelease   = params.filterRelease->load();
    }

    if (PluginConfig::isFreq)
    {
        p.subOn      = params.subOn->load() > 0.5f;
        p.subOctaves = 1 + juce::roundToInt (params.subOctave->load());
        p.subLevel   = params.subLevel->load();
    }

    return p;
}

void PhotoToFrequencyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (buffer.getNumChannels() < 2)
        return;

    // Pick up a newly loaded picture if the message thread isn't mid-swap.
    {
        const juce::SpinLock::ScopedTryLockType tl (tableLock);

        if (tl.isLocked() && audioTable != currentTable)
            audioTable = currentTable;
    }

    const auto engineParams = readParams();

    const bool wantPreview = previewHeld.load();

    if (wantPreview != previewPlaying)
    {
        if (wantPreview)  engine.noteOn (previewNoteId, previewPitch, 0.8f);
        else              engine.noteOff (previewNoteId);

        previewPlaying = wantPreview;
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int rendered = 0;

    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (rendered, numSamples, metadata.samplePosition);

        if (eventPosition > rendered)
        {
            engine.render (left + rendered, right + rendered, eventPosition - rendered, audioTable.get(), engineParams);
            rendered = eventPosition;
        }

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            engine.noteOn (message.getNoteNumber(), (float) message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            engine.noteOff (message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }

    if (rendered < numSamples)
        engine.render (left + rendered, right + rendered, numSamples - rendered, audioTable.get(), engineParams);

    midi.clear();

    const float gain = juce::Decibels::decibelsToGain (params.gain->load());
    buffer.applyGainRamp (0, numSamples, lastGain, gain);
    lastGain = gain;

    // Soft limit: dense pictures can stack up a lot of partials.
    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int n = 0; n < numSamples; ++n)
            data[n] = std::tanh (data[n]);
    }

    scanPosition.store (engine.getScanPosition(), std::memory_order_relaxed);
}

//==============================================================================
void PhotoToFrequencyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    const auto preview = getPreviewImage();
    const auto name = getImageName();

    // The picture travels with the project, so it still plays if the file moves.
    if (preview.isValid() && name.isNotEmpty())
    {
        juce::MemoryOutputStream imageStream;
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.9f);

        if (jpeg.writeImageToStream (preview, imageStream))
        {
            state.setProperty (imageDataProperty, juce::var (imageStream.getData(), imageStream.getDataSize()), nullptr);
            state.setProperty (imageNameProperty, name, nullptr);
        }
    }

    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void PhotoToFrequencyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    if (! state.isValid() || ! state.hasType (apvts.state.getType()))
        return;

    juce::Image image;
    const auto name = state.getProperty (imageNameProperty).toString();

    if (const auto* block = state.getProperty (imageDataProperty).getBinaryData())
        image = juce::ImageFileFormat::loadFrom (block->getData(), block->getSize());

    state.removeProperty (imageDataProperty, nullptr);
    state.removeProperty (imageNameProperty, nullptr);
    apvts.replaceState (state);

    if (image.isValid())
        setImage (image, name);
    else
        setImage (ImageSpectrum::createDefaultImage(), {});
}

//==============================================================================
juce::AudioProcessorEditor* PhotoToFrequencyProcessor::createEditor()
{
    return new PhotoToFrequencyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhotoToFrequencyProcessor();
}
