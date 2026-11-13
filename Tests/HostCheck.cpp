// Loads the built VST3 the way a DAW would and checks that it plays.
//
//   PhotoToFrequencyHostCheck "path/to/Photo To Frequency.vst3"

#include <juce_audio_utils/juce_audio_utils.h>

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cout << "usage: PhotoToFrequencyHostCheck <plugin.vst3>" << std::endl;
        return 2;
    }

    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);

    if (found.isEmpty())
    {
        std::cout << "FAIL  no VST3 plugin found in " << path << std::endl;
        return 1;
    }

    const auto& description = *found.getFirst();
    std::cout << "found  \"" << description.name << "\" by \"" << description.manufacturerName
              << "\"  category " << description.category
              << "  instrument " << (description.isInstrument ? "yes" : "no")
              << "  outputs " << description.numOutputChannels << std::endl;

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    juce::String error;
    auto instance = format.createInstanceFromDescription (description, sampleRate, blockSize, error);

    if (instance == nullptr)
    {
        std::cout << "FAIL  couldn't create an instance: " << error << std::endl;
        return 1;
    }

    std::cout << "       " << instance->getParameters().size() << " parameters, editor "
              << (instance->hasEditor() ? "yes" : "no") << std::endl;

    instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    instance->prepareToPlay (sampleRate, blockSize);

    const int channels = juce::jmax (2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (channels, blockSize);
    float peak = 0.0f;
    double sumSquares = 0.0;
    bool finite = true;

    for (int block = 0; block < 200; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (block == 0)    midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.8f), 0);
        if (block == 150)  midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);

        instance->processBlock (buffer, midi);

        for (int n = 0; n < blockSize; ++n)
        {
            const float sample = buffer.getSample (0, n);
            finite = finite && std::isfinite (sample);
            peak = juce::jmax (peak, std::abs (sample));
            sumSquares += (double) sample * sample;
        }
    }

    const double rms = std::sqrt (sumSquares / (200.0 * blockSize));
    std::cout << "       played C3 through the VST3: peak " << peak << "  rms " << rms << std::endl;

    // State must survive a save/restore through the host interface.
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    instance->setStateInformation (state.getData(), (int) state.getSize());
    std::cout << "       state round trip: " << state.getSize() << " bytes" << std::endl;

    instance->releaseResources();
    instance.reset();

    const bool ok = finite && rms > 0.005 && peak <= 1.0f && state.getSize() > 0;
    std::cout << (ok ? "OK" : "FAIL") << std::endl;
    return ok ? 0 : 1;
}
