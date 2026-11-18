#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginConfig.h"
#include "ImageSpectrum.h"
#include "SynthEngine.h"

class PhotoToFrequencyProcessor final : public juce::AudioProcessor,
                                        public juce::ChangeBroadcaster
{
public:
    PhotoToFrequencyProcessor();
    ~PhotoToFrequencyProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 10.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    /** Loads a picture from disk. Returns false if it can't be decoded. */
    bool loadImageFile (const juce::File&);

    /** Replaces the picture. Listeners get a change message. */
    void setImage (const juce::Image&, const juce::String& name);

    juce::Image getPreviewImage() const;
    juce::String getImageName() const;

    /** Scan head position 0..1, or -1 when nothing is sounding. */
    float getScanPosition() const noexcept                  { return scanPosition.load (std::memory_order_relaxed); }

    /** Holds a note from the editor so the picture can be heard without a keyboard. */
    void setPreviewHeld (bool shouldPlay) noexcept          { previewHeld.store (shouldPlay); }
    bool isPreviewHeld() const noexcept                     { return previewHeld.load(); }

    /** Photo Freq: whether hovering shows tooltips. Saved with the project; message thread only. */
    bool areTooltipsEnabled() const;
    void setTooltipsEnabled (bool);

    /** Photo Freq 2: saves every setting, the picture and the selected part of
        it to a patch file. Message thread only. Returns false if it can't be written. */
    bool savePatch (const juce::File&);

    /** Photo Freq 2: loads a patch written by savePatch. The Tooltips switch is
        left as it is. Returns false if the file isn't a Photo Freq 2 patch. */
    bool loadPatch (const juce::File&);

    /** Name of the last patch saved or loaded, or empty. Message thread only. */
    juce::String getPatchName() const;

    /** Photo Freq 2: Env Amount is in percent; 100% moves the cutoff this many
        octaves, which is the whole audible range (20 Hz to 20 kHz). */
    static constexpr float envAmountFullOctaves = 10.0f;

    static constexpr int previewNoteId = 1000;
    static constexpr float previewPitch = 48.0f;   // C3

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    EngineParams readParams() const;

    /** The parameters plus the picture. Patches keep the picture lossless (PNG);
        DAW projects use a JPEG to stay small. */
    juce::ValueTree createStateTree (bool forPatch);
    bool applyStateTree (juce::ValueTree);

    SynthEngine engine;

    // The table is built off the audio thread and handed over through a
    // try-lock; retired tables are kept alive here so the audio thread never
    // frees one.
    juce::CriticalSection imageLock;
    juce::SpinLock tableLock;
    SpectrumTable::Ptr currentTable;                        // guarded by tableLock
    juce::ReferenceCountedArray<SpectrumTable> retired;     // guarded by imageLock
    SpectrumTable::Ptr audioTable;                          // audio thread only

    std::atomic<float> scanPosition { -1.0f };
    std::atomic<bool> previewHeld { false };
    bool previewPlaying = false;
    float lastGain = 0.0f;

    struct ParamPointers
    {
        std::atomic<float>* scan = nullptr, *loopMode = nullptr, *tuning = nullptr, *range = nullptr,
                          * partials = nullptr, *contrast = nullptr, *threshold = nullptr, *invert = nullptr,
                          * tilt = nullptr, *width = nullptr, *attack = nullptr, *release = nullptr, *gain = nullptr;

        // Split only; null in the original plugin.
        std::atomic<float>* decay = nullptr, *sustain = nullptr,
                          * regionLeft = nullptr, *regionRight = nullptr, *regionBottom = nullptr, *regionTop = nullptr,
                          * filterType = nullptr, *cutoff = nullptr, *resonance = nullptr, *filterEnvAmount = nullptr,
                          * filterAttack = nullptr, *filterDecay = nullptr, *filterSustain = nullptr, *filterRelease = nullptr;

        // Photo Freq only.
        std::atomic<float>* oscOn = nullptr, *subOn = nullptr, *subWave = nullptr, *subOctave = nullptr,
                          * subFine = nullptr, *subDrive = nullptr, *subLevel = nullptr;

        // Photo Freq 2 only.
        std::atomic<float>* reverse = nullptr, *stereoSource = nullptr;
    } params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhotoToFrequencyProcessor)
};
