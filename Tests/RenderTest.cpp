// Headless check of the whole plugin: loads a JPEG, plays notes, verifies the
// audio, round-trips the saved state and (optionally) writes a WAV and a
// screenshot of the editor.
//
//   PhotoToFrequencyRenderTest [image.jpg] [output-directory]

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << what << std::endl;

        if (! condition)
            ++failures;
    }

    struct Stats { float peak = 0, rmsL = 0, rmsR = 0; bool finite = true; };

    Stats measure (const juce::AudioBuffer<float>& b)
    {
        Stats s;
        s.peak = b.getMagnitude (0, b.getNumSamples());
        s.rmsL = b.getRMSLevel (0, 0, b.getNumSamples());
        s.rmsR = b.getRMSLevel (1, 0, b.getNumSamples());

        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int n = 0; n < b.getNumSamples(); ++n)
                if (! std::isfinite (b.getSample (c, n)))
                    s.finite = false;

        return s;
    }

    /** Plays the given notes for holdSeconds, then lets them ring for tailSeconds. */
    juce::AudioBuffer<float> render (juce::AudioProcessor& plugin, std::initializer_list<int> notes,
                                     double holdSeconds, double tailSeconds, double sampleRate = 48000.0)
    {
        constexpr int blockSize = 441;   // deliberately not a multiple of the engine's sub-block
        plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
        plugin.prepareToPlay (sampleRate, blockSize);

        const int holdSamples = (int) (holdSeconds * sampleRate);
        const int total = holdSamples + (int) (tailSeconds * sampleRate);
        juce::AudioBuffer<float> out (2, total), block (2, blockSize);

        for (int pos = 0; pos < total; pos += blockSize)
        {
            const int n = juce::jmin (blockSize, total - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;

            if (pos == 0)
                for (int note : notes)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 7);

            if (holdSamples >= pos && holdSamples < pos + n)
                for (int note : notes)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), holdSamples - pos);

            plugin.processBlock (view, midi);
            out.copyFrom (0, pos, view, 0, 0, n);
            out.copyFrom (1, pos, view, 1, 0, n);
        }

        return out;
    }

    void setParam (PhotoToFrequencyProcessor& p, const char* id, float realValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    /** Wraps JPEG data in a minimal EXIF header carrying the given orientation. */
    juce::MemoryBlock withExifOrientation (const juce::MemoryBlock& jpeg, uint8_t orientation)
    {
        const uint8_t app1[] = { 0xff, 0xe1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
                                 'M', 'M', 0x00, 0x2a, 0x00, 0x00, 0x00, 0x08,      // TIFF header, IFD at 8
                                 0x00, 0x01,                                        // one entry
                                 0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,    // orientation, SHORT, count 1
                                 0x00, orientation, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00 };                          // no next IFD
        juce::MemoryBlock out;
        out.append (jpeg.getData(), 2);
        out.append (app1, sizeof (app1));
        out.append (juce::addBytesToPointer (jpeg.getData(), 2), jpeg.getSize() - 2);
        return out;
    }
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;

    const juce::File userImage = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    const juce::File outDir = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]) : juce::File();
    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ptf-render-test");
    tempDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& plugin = dynamic_cast<PhotoToFrequencyProcessor&> (*base);

    //==========================================================================
    std::cout << "Built-in picture" << std::endl;
    {
        const auto audio = render (plugin, { 48 }, 2.0, 1.5);
        const auto s = measure (audio);
        std::cout << "    peak " << s.peak << "  rms " << s.rmsL << " / " << s.rmsR << std::endl;
        check (s.finite, "output is finite");
        check (s.rmsL > 0.005f && s.rmsR > 0.005f, "makes sound out of the box");
        check (s.peak <= 1.0f, "stays within full scale");

        const int tail = 4800;
        check (audio.getMagnitude (audio.getNumSamples() - tail, tail) < 1.0e-3f, "note dies away after release");
    }

    //==========================================================================
    std::cout << "Image analysis" << std::endl;
    {
        // Left half: bright red stripe near the bottom. Right half: bright blue stripe near the top.
        juce::Image img (juce::Image::RGB, 400, 200, true);

        {
            // Scoped: on Windows the drawing only reaches the image when the Graphics is destroyed.
            juce::Graphics g (img);
            g.fillAll (juce::Colours::black);
            g.setColour (juce::Colours::red);
            g.fillRect (0, 170, 200, 20);
            g.setColour (juce::Colours::blue);
            g.fillRect (200, 10, 200, 20);
        }

        const auto table = ImageSpectrum::analyse (img, "test");
        const int level = 2, rows = SpectrumTable::rowsForLevel (level);
        auto at = [&] (const std::vector<float>& v, int row, int col) { return v[(size_t) (row * SpectrumTable::numColumns + col)]; };
        const int lowRow = rows / 10, highRow = rows - 1 - rows / 10;

        check (at (table->brightness[level], lowRow, 100) > 0.15f, "bottom-left stripe lands in the low rows");
        check (at (table->brightness[level], highRow, 100) < 0.01f, "top-left is dark");
        check (at (table->brightness[level], highRow, 400) > 0.05f, "top-right stripe lands in the high rows");
        check (at (table->pan[level], lowRow, 100) < -0.9f, "red pans left");
        check (at (table->pan[level], highRow, 400) > 0.9f, "blue pans right");

        // Save as JPEG, load through the plugin, and listen to each half.
        const auto jpegFile = tempDir.getChildFile ("stripes.jpg");
        jpegFile.deleteFile();
        {
            juce::FileOutputStream stream (jpegFile);
            juce::JPEGImageFormat jpeg;
            jpeg.setQuality (0.95f);
            check (stream.openedOk() && jpeg.writeImageToStream (img, stream), "wrote test JPEG");
        }

        check (plugin.loadImageFile (jpegFile), "plugin loads a JPEG");
        check (plugin.getImageName() == "stripes.jpg", "image name is kept");
        check (! plugin.loadImageFile (tempDir.getChildFile ("missing.jpg")), "missing file is rejected");
        check (plugin.getImageName() == "stripes.jpg", "failed load keeps the current picture");

        setParam (plugin, "scan", 2.0f);
        setParam (plugin, "loop", (float) EngineParams::oneShot);
        setParam (plugin, "tuning", (float) EngineParams::freeLog);
        setParam (plugin, "attack", 0.005f);

        const auto audio = render (plugin, { 48 }, 2.0, 0.5);
        const int sr = 48000;
        const float firstL  = audio.getRMSLevel (0, sr / 5, sr / 2), firstR  = audio.getRMSLevel (1, sr / 5, sr / 2);
        const float secondL = audio.getRMSLevel (0, sr + sr / 5, sr / 2), secondR = audio.getRMSLevel (1, sr + sr / 5, sr / 2);
        std::cout << "    first half L/R " << firstL << " / " << firstR
                  << "   second half L/R " << secondL << " / " << secondR << std::endl;
        check (firstL > 0.01f && firstL > 8.0f * firstR, "red half plays on the left");
        check (secondR > 0.001f && secondR > 8.0f * secondL, "blue half plays on the right");

        // The low stripe should sound lower than the high one: compare zero crossings.
        auto crossings = [&] (int channel, int start, int length)
        {
            int count = 0;
            for (int n = start + 1; n < start + length; ++n)
                if ((audio.getSample (channel, n - 1) < 0.0f) != (audio.getSample (channel, n) < 0.0f))
                    ++count;
            return count;
        };
        const int lowCrossings = crossings (0, sr / 5, sr / 2), highCrossings = crossings (1, sr + sr / 5, sr / 2);
        std::cout << "    zero crossings: bottom stripe " << lowCrossings << ", top stripe " << highCrossings << std::endl;
        check (highCrossings > 4 * lowCrossings, "top of the picture is higher in pitch");

        // EXIF orientation
        juce::MemoryBlock jpegData;
        jpegFile.loadFileAsData (jpegData);
        check (ImageSpectrum::readExifOrientation (jpegData.getData(), jpegData.getSize()) == 1, "no EXIF means orientation 1");

        const auto rotatedData = withExifOrientation (jpegData, 6);
        check (ImageSpectrum::readExifOrientation (rotatedData.getData(), rotatedData.getSize()) == 6, "EXIF orientation is read");

        const auto rotatedFile = tempDir.getChildFile ("rotated.jpg");
        rotatedFile.replaceWithData (rotatedData.getData(), rotatedData.getSize());
        const auto rotated = ImageSpectrum::loadFile (rotatedFile);
        check (rotated.getWidth() == 200 && rotated.getHeight() == 400, "orientation 6 swaps width and height");
        // Stored bottom-left red ends up top-left after a clockwise turn; stored top-right blue ends up bottom-right.
        check (rotated.getPixelAt (20, 100).getRed() > 150, "rotated picture: red at top-left");
        check (rotated.getPixelAt (180, 300).getBlue() > 150, "rotated picture: blue at bottom-right");
    }

    //==========================================================================
    std::cout << "State" << std::endl;
    {
        setParam (plugin, "range", 3.5f);
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        std::cout << "    state size " << state.getSize() << " bytes" << std::endl;

        std::unique_ptr<juce::AudioProcessor> otherBase (createPluginFilter());
        auto& other = dynamic_cast<PhotoToFrequencyProcessor&> (*otherBase);
        other.setStateInformation (state.getData(), (int) state.getSize());

        check (other.getImageName() == "stripes.jpg", "picture is restored from saved state");
        check (other.getPreviewImage().getWidth() == 400, "restored picture has the right size");
        check (std::abs (other.apvts.getRawParameterValue ("range")->load() - 3.5f) < 0.01f, "parameters are restored");
        check (juce::roundToInt (other.apvts.getRawParameterValue ("loop")->load()) == EngineParams::oneShot, "choices are restored");

        const juce::String garbage ("not a state");
        other.setStateInformation (garbage.toRawUTF8(), (int) garbage.getNumBytesAsUTF8());
        check (other.getImageName() == "stripes.jpg", "garbage state is ignored");
    }

   #if PTF_SPLIT
    //==========================================================================
    // The stripes picture is still loaded: red low stripe on the left half,
    // blue high stripe on the right half.
    std::cout << "Split: part of the picture" << std::endl;
    {
        constexpr int sr = 48000;
        auto setRegion = [&] (float left, float right, float bottom, float top)
        {
            setParam (plugin, "regionLeft", left);
            setParam (plugin, "regionRight", right);
            setParam (plugin, "regionBottom", bottom);
            setParam (plugin, "regionTop", top);
        };
        auto levels = [&] (const juce::AudioBuffer<float>& audio, const char* label)
        {
            const float l = audio.getRMSLevel (0, sr / 5, 3 * sr / 2), r = audio.getRMSLevel (1, sr / 5, 3 * sr / 2);
            std::cout << "    " << label << "  L/R " << l << " / " << r << std::endl;
            return std::make_pair (l, r);
        };

        setParam (plugin, "scan", 2.0f);
        setParam (plugin, "range", 5.0f);

        setRegion (0.0f, 0.5f, 0.0f, 1.0f);
        auto [leftL, leftR] = levels (render (plugin, { 48 }, 2.0, 0.5), "left half ");
        check (leftL > 0.01f && leftL > 8.0f * leftR, "left half of the picture: only the red stripe plays, all scan long");

        setRegion (0.5f, 1.0f, 0.0f, 1.0f);
        auto [rightL, rightR] = levels (render (plugin, { 48 }, 2.0, 0.5), "right half");
        check (rightR > 0.01f && rightR > 8.0f * rightL, "right half of the picture: only the blue stripe plays");

        setRegion (0.0f, 1.0f, 0.0f, 0.5f);
        {
            const auto audio = render (plugin, { 48 }, 2.0, 0.5);
            const float first = audio.getRMSLevel (0, sr / 5, sr / 2);
            const float second = juce::jmax (audio.getRMSLevel (0, sr + sr / 5, sr / 2), audio.getRMSLevel (1, sr + sr / 5, sr / 2));
            std::cout << "    bottom half  first " << first << "  second " << second << std::endl;
            check (first > 0.01f && second < 1.0e-4f, "bottom half of the picture: the top stripe is left out");
        }

        setRegion (1.0f, 0.0f, 1.0f, 0.0f);
        check (measure (render (plugin, { 48 }, 0.5, 0.2)).finite, "reversed region edges are handled");

        setRegion (0.3f, 0.3f, 0.9f, 0.9f);
        check (measure (render (plugin, { 48 }, 0.5, 0.2)).finite, "empty region is handled");
    }

    std::cout << "Split: oscillator envelope" << std::endl;
    {
        constexpr int sr = 48000;
        setParam (plugin, "regionLeft", 0.0f);
        setParam (plugin, "regionRight", 0.5f);
        setParam (plugin, "regionBottom", 0.0f);
        setParam (plugin, "regionTop", 1.0f);
        setParam (plugin, "loop", (float) EngineParams::loop);
        setParam (plugin, "decay", 0.2f);
        setParam (plugin, "sustain", 0.0f);

        auto audio = render (plugin, { 48 }, 2.0, 0.2);
        float early = audio.getRMSLevel (0, sr / 100, sr / 20), late = audio.getRMSLevel (0, sr, sr / 2);
        std::cout << "    sustain 0:   early " << early << "  late " << late << std::endl;
        check (early > 0.01f && late < 1.0e-4f, "with sustain at 0 a held note decays to silence");

        setParam (plugin, "sustain", 0.5f);
        const float half = render (plugin, { 48 }, 2.0, 0.2).getRMSLevel (0, sr, sr / 2);
        setParam (plugin, "sustain", 1.0f);
        const float full = render (plugin, { 48 }, 2.0, 0.2).getRMSLevel (0, sr, sr / 2);
        std::cout << "    held level: sustain 0.5 " << half << "  sustain 1 " << full << std::endl;
        check (half > 0.45f * full && half < 0.55f * full, "sustain sets the held level");
    }

    std::cout << "Split: filter" << std::endl;
    {
        constexpr int sr = 48000;
        juce::Image white (juce::Image::RGB, 64, 64, true);
        white.clear (white.getBounds(), juce::Colours::white);
        plugin.setImage (white, "white");

        setParam (plugin, "regionRight", 1.0f);
        setParam (plugin, "tuning", (float) EngineParams::harmonic);
        setParam (plugin, "tilt", 0.0f);

        auto crossings = [&] (const juce::AudioBuffer<float>& audio, int start, int length)
        {
            int count = 0;
            for (int n = start + 1; n < start + length; ++n)
                if ((audio.getSample (0, n - 1) < 0.0f) != (audio.getSample (0, n) < 0.0f))
                    ++count;
            return count;
        };

        // C2: 64 harmonics from 65 Hz to 4.2 kHz.
        const int open = crossings (render (plugin, { 36 }, 1.0, 0.1), sr / 4, sr / 2);

        setParam (plugin, "cutoff", 200.0f);
        const int lowPassed = crossings (render (plugin, { 36 }, 1.0, 0.1), sr / 4, sr / 2);

        setParam (plugin, "filterType", (float) EngineParams::highPass);
        setParam (plugin, "cutoff", 3000.0f);
        const int highPassed = crossings (render (plugin, { 36 }, 1.0, 0.1), sr / 4, sr / 2);

        setParam (plugin, "filterType", (float) EngineParams::bandPass);
        setParam (plugin, "cutoff", 1000.0f);
        setParam (plugin, "resonance", 1.0f);
        const int bandPassed = crossings (render (plugin, { 36 }, 1.0, 0.1), sr / 4, sr / 2);

        std::cout << "    zero crossings in 0.5 s: open " << open << ", low-pass 200 Hz " << lowPassed
                  << ", high-pass 3 kHz " << highPassed << ", band-pass 1 kHz " << bandPassed << std::endl;
        check (lowPassed * 3 < open, "low-pass removes the high partials");
        check (highPassed > open, "high-pass removes the low partials");
        // 1 kHz alone would give 1000; neighbouring partials leak through and the
        // random start phases move the count around a little.
        check (bandPassed > 800 && bandPassed < 1600, "resonant band-pass rings at its cutoff");

        // Filter envelope: opens on the attack, then closes while the note is held.
        setParam (plugin, "filterType", (float) EngineParams::lowPass);
        setParam (plugin, "resonance", 0.0f);
        setParam (plugin, "cutoff", 100.0f);
        setParam (plugin, "filterEnvAmount", 6.0f);
        setParam (plugin, "filterAttack", 0.001f);
        setParam (plugin, "filterDecay", 1.0f);
        setParam (plugin, "filterSustain", 0.0f);

        // Compared well apart in time: the oscillators start at random phases,
        // so the counts wander a little from run to run.
        const auto swept = render (plugin, { 36 }, 2.0, 0.1);
        const int bright = crossings (swept, sr / 100, sr / 20), dark = crossings (swept, 9 * sr / 5, sr / 20);
        std::cout << "    filter envelope: zero crossings in 50 ms at the start " << bright << ", after 1.8 s " << dark << std::endl;
        check (bright > 3 * dark, "filter envelope sweeps the cutoff down while the note is held");

        // A slower decay here, so the filter is still nearly shut in the first 50 ms.
        setParam (plugin, "filterEnvAmount", -6.0f);
        setParam (plugin, "cutoff", 6400.0f);
        setParam (plugin, "filterDecay", 2.0f);
        const auto sweptUp = render (plugin, { 36 }, 2.0, 0.1);
        const int closedStart = crossings (sweptUp, sr / 100, sr / 20), openLater = crossings (sweptUp, sr, sr / 20);
        std::cout << "    negative amount: at the start " << closedStart << ", after 1 s " << openLater
                  << "  (cutoff " << plugin.apvts.getRawParameterValue ("cutoff")->load()
                  << ", amount " << plugin.apvts.getRawParameterValue ("filterEnvAmount")->load() << ")" << std::endl;
        check (openLater > 3 * closedStart, "negative amount sweeps it the other way");

        // State keeps the new parameters.
        setParam (plugin, "regionTop", 0.6f);
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        std::unique_ptr<juce::AudioProcessor> otherBase (createPluginFilter());
        auto& other = dynamic_cast<PhotoToFrequencyProcessor&> (*otherBase);
        other.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (other.apvts.getRawParameterValue ("regionTop")->load() - 0.6f) < 0.001f
                && std::abs (other.apvts.getRawParameterValue ("cutoff")->load() - 6400.0f) < 1.0f,
               "region and filter settings are restored from saved state");

        // Back to neutral for the stress run.
        setParam (plugin, "regionTop", 1.0f);
        setParam (plugin, "cutoff", 20000.0f);
        setParam (plugin, "filterEnvAmount", 0.0f);
    }
   #endif

    //==========================================================================
    std::cout << "Stress" << std::endl;
    {
        // White picture, every voice, most partials, loudest settings.
        juce::Image white (juce::Image::RGB, 64, 64, true);
        white.clear (white.getBounds(), juce::Colours::white);
        plugin.setImage (white, "white");

        setParam (plugin, "loop", (float) EngineParams::pingPong);
        setParam (plugin, "partials", 3.0f);
        setParam (plugin, "contrast", 0.5f);
        setParam (plugin, "threshold", 0.0f);
        setParam (plugin, "tilt", 0.0f);
        setParam (plugin, "gain", 12.0f);
        setParam (plugin, "scan", 0.05f);

        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto audio = render (plugin, { 24, 31, 36, 40, 43, 48, 52, 55, 60, 64 }, 4.0, 1.0);
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        const auto s = measure (audio);
        std::cout << "    peak " << s.peak << "  rms " << s.rmsL << "  cpu " << juce::String (100.0 * elapsed / 5.0, 1)
                  << "% of real time (10 notes, 128 partials)" << std::endl;
        check (s.finite, "output is finite");
        check (s.peak <= 1.0f, "limiter keeps the output within full scale");
        check (elapsed < 5.0, "renders faster than real time");

        for (auto tuningMode : { EngineParams::harmonic, EngineParams::chromatic, EngineParams::major,
                                 EngineParams::minor, EngineParams::pentatonic })
        {
            setParam (plugin, "tuning", (float) tuningMode);
            const auto st = measure (render (plugin, { 60 }, 0.3, 0.1, 44100.0));
            check (st.finite && st.rmsL > 0.001f, "tuning mode " + juce::String ((int) tuningMode) + " sounds");
        }
    }

    //==========================================================================
    if (userImage != juce::File())
    {
        std::cout << "User picture: " << userImage.getFileName() << std::endl;
        std::unique_ptr<juce::AudioProcessor> freshBase (createPluginFilter());
        auto& fresh = dynamic_cast<PhotoToFrequencyProcessor&> (*freshBase);
        const bool loaded = fresh.loadImageFile (userImage);
        check (loaded, "loads");

        if (loaded)
        {
            const auto audio = render (fresh, { 36, 48, 55 }, 8.0, 1.0);
            const auto s = measure (audio);
            std::cout << "    peak " << s.peak << "  rms " << s.rmsL << " / " << s.rmsR << std::endl;
            check (s.finite && s.rmsL > 0.001f, "sounds");

            if (outDir != juce::File())
            {
                outDir.createDirectory();
                const auto wavFile = outDir.getChildFile ("render.wav");
                wavFile.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);
                const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16);

                if (auto writer = wav.createWriterFor (stream, options))
                    check (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()), "wrote " + wavFile.getFullPathName());

               #if PTF_SPLIT
                // Show a selected region in the screenshot.
                setParam (fresh, "regionLeft", 0.45f);
                setParam (fresh, "regionRight", 0.95f);
                setParam (fresh, "regionBottom", 0.3f);
                setParam (fresh, "regionTop", 0.9f);
               #endif

                fresh.setPreviewHeld (true);
                render (fresh, {}, 1.3, 0.0);
                std::unique_ptr<juce::AudioProcessorEditor> editor (fresh.createEditor());
                                const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
                const auto pngFile = outDir.getChildFile ("editor.png");
                pngFile.deleteFile();
                juce::FileOutputStream pngStream (pngFile);
                juce::PNGImageFormat png;
                check (pngStream.openedOk() && png.writeImageToStream (snapshot, pngStream), "wrote " + pngFile.getFullPathName());
            }
        }
    }

    std::cout << (failures == 0 ? "\nAll checks passed" : "\n" + juce::String (failures) + " check(s) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
