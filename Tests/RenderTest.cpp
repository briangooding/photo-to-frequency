// Headless check of the whole plugin: loads a JPEG, plays notes, verifies the
// audio, round-trips the saved state and (optionally) writes a WAV and a
// screenshot of the editor.
//
//   PhotoToFrequencyRenderTest [image.jpg] [output-directory]

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    /** Env Amount in the plugin's own unit: octaves, or percent in Photo Freq 2. */
    float envAmount (float octaves)
    {
       #if PTF_VARIANT >= 3
        return octaves / PhotoToFrequencyProcessor::envAmountFullOctaves * 100.0f;
       #else
        return octaves;
       #endif
    }

    /** Correlation between the two channels: 1 is mono, 0 unrelated, -1 opposite. */
    float channelCorrelation (const juce::AudioBuffer<float>& b, int start, int length)
    {
        double lr = 0, ll = 0, rr = 0;

        for (int n = start; n < start + length; ++n)
        {
            const double l = b.getSample (0, n), r = b.getSample (1, n);
            lr += l * r;  ll += l * l;  rr += r * r;
        }

        return (float) (lr / std::sqrt (juce::jmax (1.0e-20, ll * rr)));
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

   #if PTF_VARIANT >= 1
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
        setParam (plugin, "filterEnvAmount", envAmount (6.0f));
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
        setParam (plugin, "filterEnvAmount", envAmount (-6.0f));
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

   #if PTF_VARIANT >= 2
    //==========================================================================
    std::cout << "Freq: sub oscillator" << std::endl;
    {
        constexpr int sr = 48000;

        // A black picture is silent, so anything heard is the sub oscillator.
        juce::Image black (juce::Image::RGB, 64, 64, true);
        plugin.setImage (black, "black");
        setParam (plugin, "loop", (float) EngineParams::loop);

        auto crossings = [&] (const juce::AudioBuffer<float>& audio)
        {
            int count = 0;
            for (int n = sr / 2 + 1; n < sr / 2 + sr; ++n)
                if ((audio.getSample (0, n - 1) < 0.0f) != (audio.getSample (0, n) < 0.0f))
                    ++count;
            return count;
        };

        check (measure (render (plugin, { 48 }, 2.0, 0.2)).peak < 1.0e-4f, "off by default: a black picture is silent");

        // C3 is 130.8 Hz, so one octave down is 65.4 Hz: about 131 zero crossings a second.
        setParam (plugin, "subOn", 1.0f);
        setParam (plugin, "subLevel", 1.0f);
        auto audio = render (plugin, { 48 }, 2.0, 0.2);
        const int oneDown = crossings (audio);
        const float level = audio.getRMSLevel (0, sr / 2, sr);
        std::cout << "    one octave down: " << oneDown << " zero crossings a second, rms " << level << std::endl;
        check (oneDown >= 129 && oneDown <= 133, "plays one octave below the note");
        check (level > 0.1f, "is clearly audible");
        check (std::abs (audio.getRMSLevel (0, sr / 2, sr) - audio.getRMSLevel (1, sr / 2, sr)) < 1.0e-4f, "is centred");

        setParam (plugin, "subOctave", 1.0f);
        const int twoDown = crossings (render (plugin, { 48 }, 2.0, 0.2));
        std::cout << "    two octaves down: " << twoDown << " zero crossings a second" << std::endl;
        check (twoDown >= 64 && twoDown <= 67, "or two octaves below");

        setParam (plugin, "subLevel", 0.5f);
        const float halfLevel = render (plugin, { 48 }, 2.0, 0.2).getRMSLevel (0, sr / 2, sr);
        check (halfLevel > 0.45f * level && halfLevel < 0.55f * level, "level control scales it");

        // It is not filtered: a high-pass well above it leaves it untouched.
        setParam (plugin, "filterType", (float) EngineParams::highPass);
        setParam (plugin, "cutoff", 5000.0f);
        const float highPassed = render (plugin, { 48 }, 2.0, 0.2).getRMSLevel (0, sr / 2, sr);
        check (std::abs (highPassed - halfLevel) < 0.02f * halfLevel, "ignores the filter");
        setParam (plugin, "filterType", (float) EngineParams::lowPass);
        setParam (plugin, "cutoff", 20000.0f);

        // It follows the oscillator envelope.
        setParam (plugin, "sustain", 0.0f);
        setParam (plugin, "decay", 0.2f);
        check (render (plugin, { 48 }, 2.0, 0.2).getRMSLevel (0, sr, sr / 2) < 1.0e-4f, "follows the oscillator envelope");
        setParam (plugin, "sustain", 1.0f);

        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        std::unique_ptr<juce::AudioProcessor> otherBase (createPluginFilter());
        auto& other = dynamic_cast<PhotoToFrequencyProcessor&> (*otherBase);
        other.setStateInformation (state.getData(), (int) state.getSize());
        check (other.apvts.getRawParameterValue ("subOn")->load() > 0.5f
                && juce::roundToInt (other.apvts.getRawParameterValue ("subOctave")->load()) == 1,
               "sub oscillator settings are restored from saved state");

        // Steady part of a held note: from 0.5 s to 1.5 s.
        auto steady = [&] (const juce::AudioBuffer<float>& rendered)
        {
            juce::AudioBuffer<float> part (1, sr);
            part.copyFrom (0, 0, rendered, 0, sr / 2, sr);
            return part;
        };

        auto crest = [&] (const juce::AudioBuffer<float>& part)
        {
            return part.getMagnitude (0, 0, part.getNumSamples()) / juce::jmax (1.0e-9f, part.getRMSLevel (0, 0, part.getNumSamples()));
        };

        setParam (plugin, "subOctave", 0.0f);
        setParam (plugin, "subLevel", 1.0f);

        setParam (plugin, "subOctave", 2.0f);
        const int threeDown = crossings (render (plugin, { 48 }, 2.0, 0.2));
        std::cout << "    three octaves down: " << threeDown << " zero crossings a second" << std::endl;
        check (threeDown >= 31 && threeDown <= 34, "or three octaves below");
        setParam (plugin, "subOctave", 0.0f);

        // 65.4 Hz up a semitone is 69.3 Hz: about 139 zero crossings a second.
        setParam (plugin, "subFine", 100.0f);
        const int fineUp = crossings (render (plugin, { 48 }, 2.0, 0.2));
        std::cout << "    fine +100 cents: " << fineUp << " zero crossings a second" << std::endl;
        check (fineUp >= 137 && fineUp <= 141, "fine tune moves it by up to a semitone");
        setParam (plugin, "subFine", 0.0f);

        // Each wave shape is about as loud as the sine, but shaped differently.
        float waveRms[4] = {}, waveCrest[4] = {};

        for (int wave = 0; wave < 4; ++wave)
        {
            setParam (plugin, "subWave", (float) wave);
            const auto waveAudio = render (plugin, { 48 }, 2.0, 0.2);
            const auto part = steady (waveAudio);
            waveRms[wave] = part.getRMSLevel (0, 0, sr);
            waveCrest[wave] = crest (part);
            check (measure (waveAudio).finite, "wave " + juce::String (wave) + " is finite");
        }

        std::cout << "    rms sine " << waveRms[0] << ", triangle " << waveRms[1] << ", saw " << waveRms[2] << ", square " << waveRms[3]
                  << "; crest " << waveCrest[0] << ", " << waveCrest[1] << ", " << waveCrest[2] << ", " << waveCrest[3] << std::endl;

        for (int wave = 1; wave < 4; ++wave)
            check (std::abs (waveRms[wave] - waveRms[0]) < 0.15f * waveRms[0], "wave " + juce::String (wave) + " matches the sine's loudness");

        check (std::abs (waveCrest[0] - 1.414f) < 0.08f, "sine wave is a sine");
        check (waveCrest[1] > 1.6f && waveCrest[2] > 1.6f, "triangle and saw are peakier than a sine");
        check (waveCrest[3] < 1.25f, "square is flat-topped");
        setParam (plugin, "subWave", 0.0f);

        // Drive squares the sine off.
        setParam (plugin, "subDrive", 1.0f);
        const auto drivenAudio = render (plugin, { 48 }, 2.0, 0.2);
        const float drivenCrest = crest (steady (drivenAudio));
        std::cout << "    driven sine crest " << drivenCrest << std::endl;
        check (measure (drivenAudio).finite && drivenCrest < 1.3f, "drive saturates the sub");
        setParam (plugin, "subDrive", 0.0f);

        // The picture oscillator can be switched off, leaving only the sub.
        juce::Image white (juce::Image::RGB, 64, 64, true);
        white.clear (white.getBounds(), juce::Colours::white);
        plugin.setImage (white, "white");
        setParam (plugin, "subOn", 0.0f);
        check (measure (render (plugin, { 48 }, 1.0, 0.2)).rmsL > 0.01f, "picture oscillator is on by default");
        setParam (plugin, "oscOn", 0.0f);
        check (measure (render (plugin, { 48 }, 1.0, 0.2)).peak < 1.0e-4f, "picture oscillator can be switched off");
        setParam (plugin, "subOn", 1.0f);
        const int subOnly = crossings (render (plugin, { 48 }, 2.0, 0.2));
        check (subOnly >= 129 && subOnly <= 133, "with it off the sub still plays on its own");
        setParam (plugin, "oscOn", 1.0f);
        setParam (plugin, "subOn", 0.0f);
        setParam (plugin, "subLevel", 0.5f);
    }

    std::cout << "Freq: picture panel follows the picture's shape" << std::endl;
    {
        auto panelAspect = [&] (int width, int height)
        {
            juce::Image image (juce::Image::RGB, width, height, true);
            plugin.setImage (image, "shape");
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());

            for (auto* child : editor->getChildren())
                if (auto* view = dynamic_cast<PictureView*> (child))
                    return (float) view->getWidth() / (float) view->getHeight();

            return 0.0f;
        };

        const float wide = panelAspect (900, 300), standard = panelAspect (600, 400), tall = panelAspect (400, 600), square = panelAspect (500, 500);
        std::cout << "    panel width/height for 3:1 " << wide << ", 3:2 " << standard << ", 1:1 " << square << ", 2:3 " << tall << std::endl;
        check (std::abs (wide - 3.0f) < 0.05f, "3:1 picture gets a 3:1 panel");
        check (std::abs (standard - 1.5f) < 0.03f, "3:2 picture gets a 3:2 panel");
        check (std::abs (square - 1.0f) < 0.02f, "square picture gets a square panel");
        check (std::abs (tall - 0.667f) < 0.02f, "2:3 portrait picture gets a 2:3 panel");

        // And it keeps that shape when the window is resized.
        juce::Image image (juce::Image::RGB, 600, 400, true);
        plugin.setImage (image, "shape");
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
        editor->setSize (1500, 1300);

        for (auto* child : editor->getChildren())
            if (auto* view = dynamic_cast<PictureView*> (child))
                check (std::abs ((float) view->getWidth() / (float) view->getHeight() - 1.5f) < 0.03f
                        && editor->getLocalBounds().contains (view->getBounds()),
                       "panel keeps the picture's shape when the window is resized");

        // Every control explains itself on hover.
        int controls = 0, withTooltip = 0;
        bool hasTooltipWindow = false;

        for (auto* child : editor->getChildren())
        {
            hasTooltipWindow = hasTooltipWindow || dynamic_cast<juce::TooltipWindow*> (child) != nullptr;

            if (dynamic_cast<juce::Slider*> (child) != nullptr || dynamic_cast<juce::ComboBox*> (child) != nullptr
                 || dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<PictureView*> (child) != nullptr)
            {
                ++controls;

                auto* client = dynamic_cast<juce::TooltipClient*> (child);

                if (client != nullptr && client->getTooltip().length() > 20)
                    ++withTooltip;
                else
                    std::cout << "    no tooltip: " << child->getName() << " \"" << (client != nullptr ? client->getTooltip() : juce::String()) << "\"" << std::endl;
            }
        }

        std::cout << "    " << withTooltip << " of " << controls << " controls have a tooltip" << std::endl;
        check (hasTooltipWindow, "editor shows tooltips");
        check (controls >= 27 && withTooltip == controls, "every control has a tooltip");

        // Tooltips are white on black.
        auto& laf = editor->getLookAndFeel();
        check (laf.findColour (juce::TooltipWindow::backgroundColourId) == juce::Colours::black
                && laf.findColour (juce::TooltipWindow::textColourId) == juce::Colours::white,
               "tooltips are white text on black");

        // Knobs with only a few steps turn by a short drag.
        int steppedKnobs = 0;
        bool steppedKnobsTurn = true;

        for (auto* child : editor->getChildren())
        {
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                const auto steps = slider->getRange().getLength() / juce::jmax (1.0e-9, slider->getInterval());

                if (steps >= 1.0 && steps <= 4.0)
                {
                    ++steppedKnobs;
                    const double pixelsPerStep = slider->getMouseDragSensitivity() / steps;
                    steppedKnobsTurn = steppedKnobsTurn && pixelsPerStep <= 50.0;
                }
            }
        }

        check (steppedKnobs == 2 && steppedKnobsTurn, "sub Wave and Octave knobs move a step every 40 pixels");

        // The header switch turns tooltips off and on, and the choice is saved.
        juce::ToggleButton* tooltipsSwitch = nullptr;
        bool hasOscSwitch = false;

        for (auto* child : editor->getChildren())
        {
            if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
            {
                if (toggle->getButtonText() == "Tooltips")  tooltipsSwitch = toggle;
                if (toggle->getButtonText() == "Osc On")    hasOscSwitch = true;
            }
        }

        auto countTooltipWindows = [] (juce::Component& c)
        {
            int count = 0;
            for (auto* child : c.getChildren())
                if (dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                    ++count;
            return count;
        };

        check (hasOscSwitch, "picture oscillator has an on/off switch");
        check (tooltipsSwitch != nullptr && tooltipsSwitch->getToggleState(), "tooltips switch is in the header, on by default");

        if (tooltipsSwitch != nullptr)
        {
            tooltipsSwitch->setToggleState (false, juce::sendNotificationSync);
            check (countTooltipWindows (*editor) == 0 && ! plugin.areTooltipsEnabled(), "tooltips switch turns them off");

            juce::MemoryBlock state;
            plugin.getStateInformation (state);
            std::unique_ptr<juce::AudioProcessor> otherBase (createPluginFilter());
            auto& other = dynamic_cast<PhotoToFrequencyProcessor&> (*otherBase);
            other.setStateInformation (state.getData(), (int) state.getSize());
            std::unique_ptr<juce::AudioProcessorEditor> otherEditor (other.createEditor());
            check (countTooltipWindows (*otherEditor) == 0, "tooltips stay off when the project is reopened");

            tooltipsSwitch->setToggleState (true, juce::sendNotificationSync);
            check (countTooltipWindows (*editor) == 1 && plugin.areTooltipsEnabled(), "and back on");
        }

        setParam (plugin, "subOn", 0.0f);
    }
   #endif

   #if PTF_VARIANT >= 3
    //==========================================================================
    std::cout << "Freq 2: reverse scan" << std::endl;
    {
        constexpr int sr = 48000;

        // Bright on the left half only, all the way up.
        juce::Image halfLit (juce::Image::RGB, 400, 200, true);
        {
            juce::Graphics g (halfLit);
            g.fillAll (juce::Colours::black);
            g.setColour (juce::Colours::white);
            g.fillRect (0, 0, 200, 200);
        }
        plugin.setImage (halfLit, "half");

        setParam (plugin, "regionLeft", 0.0f);   setParam (plugin, "regionRight", 1.0f);
        setParam (plugin, "regionBottom", 0.0f); setParam (plugin, "regionTop", 1.0f);
        setParam (plugin, "loop", (float) EngineParams::oneShot);
        setParam (plugin, "scan", 2.0f);
        setParam (plugin, "attack", 0.005f);
        setParam (plugin, "sustain", 1.0f);
        setParam (plugin, "cutoff", 20000.0f);
        setParam (plugin, "filterEnvAmount", 0.0f);

        auto halves = [&] (float& first, float& second)
        {
            const auto audio = render (plugin, { 48 }, 2.0, 0.2);
            first  = audio.getRMSLevel (0, sr / 5, sr / 2);
            second = audio.getRMSLevel (0, sr + sr / 5, sr / 2);
        };

        float forwardFirst, forwardSecond, reverseFirst, reverseSecond;
        check (plugin.apvts.getRawParameterValue ("reverse")->load() < 0.5f, "off by default");
        halves (forwardFirst, forwardSecond);
        setParam (plugin, "reverse", 1.0f);
        halves (reverseFirst, reverseSecond);
        std::cout << "    forward: first second " << forwardFirst << ", second " << forwardSecond
                  << "   reversed: first " << reverseFirst << ", second " << reverseSecond << std::endl;
        check (forwardFirst > 0.01f && forwardSecond < 0.1f * forwardFirst, "forward plays the left half first");
        check (reverseSecond > 0.01f && reverseFirst < 0.1f * reverseSecond, "reversed plays the right half first");

        render (plugin, { 48 }, 0.1, 0.0);
        const float startPosition = plugin.getScanPosition();
        std::cout << "    reversed scan head after 0.1 s: " << startPosition << std::endl;
        check (startPosition > 0.9f && startPosition < 1.0f, "reversed scan head starts at the right and moves left");

        // Within a selected part the reversed head stays inside it.
        setParam (plugin, "regionLeft", 0.5f);
        setParam (plugin, "regionRight", 0.75f);
        render (plugin, { 48 }, 0.1, 0.0);
        const float regionStart = plugin.getScanPosition();
        check (regionStart > 0.72f && regionStart <= 0.75f, "reversed head starts at the right edge of the selected part");
        setParam (plugin, "regionLeft", 0.0f);
        setParam (plugin, "regionRight", 1.0f);

        for (const auto mode : { EngineParams::loop, EngineParams::pingPong })
        {
            setParam (plugin, "loop", (float) mode);
            setParam (plugin, "scan", 0.3f);
            const auto s = measure (render (plugin, { 48, 55 }, 1.5, 0.2));
            check (s.finite && s.rmsL > 0.005f, juce::String (mode == EngineParams::loop ? "loop" : "ping-pong") + " reversed sounds");
        }

        setParam (plugin, "reverse", 0.0f);
        setParam (plugin, "loop", (float) EngineParams::loop);
        setParam (plugin, "scan", 2.0f);
    }

    std::cout << "Freq 2: stereo" << std::endl;
    {
        constexpr int sr = 48000;

        // A white picture has no colour, so the Colour source leaves it in the middle.
        juce::Image white (juce::Image::RGB, 64, 64, true);
        white.clear (white.getBounds(), juce::Colours::white);
        plugin.setImage (white, "white");
        setParam (plugin, "tuning", (float) EngineParams::freeLog);

        auto* width = plugin.apvts.getParameter ("width");
        check (width->getName (100) == "Stereo Width" && width->getLabel() == "%"
                && std::abs (plugin.apvts.getRawParameterValue ("width")->load() - 100.0f) < 0.01f,
               "Stereo Width is in percent, full by default");
        check (juce::roundToInt (plugin.apvts.getRawParameterValue ("stereoSource")->load()) == EngineParams::stereoColour,
               "Stereo Source is Colour by default");

        auto correlation = [&] (int source, float widthPercent)
        {
            setParam (plugin, "stereoSource", (float) source);
            setParam (plugin, "width", widthPercent);
            return channelCorrelation (render (plugin, { 48 }, 1.0, 0.0), sr / 4, sr / 2);
        };

        const float colour = correlation (EngineParams::stereoColour, 100.0f);
        const float alternate = correlation (EngineParams::stereoAlternate, 100.0f);
        const float alternateMono = correlation (EngineParams::stereoAlternate, 0.0f);
        const float pitch = correlation (EngineParams::stereoPitch, 100.0f);
        std::cout << "    channel correlation of a white picture: colour " << colour << ", alternate " << alternate
                  << " (at 0% " << alternateMono << "), pitch " << pitch << std::endl;
        check (colour > 0.99f, "Colour source: a picture with no colour is centred");
        check (alternate < 0.3f, "Alternate source: spreads any picture wide");
        check (pitch < 0.7f, "Pitch source: spreads it too");
        check (alternateMono > 0.999f, "Stereo Width 0% is mono");

        // Pitch: a stripe at the bottom of the picture plays on the left, one at the top on the right.
        auto stripeBalance = [&] (int y)
        {
            juce::Image stripe (juce::Image::RGB, 64, 64, true);
            {
                juce::Graphics g (stripe);
                g.fillAll (juce::Colours::black);
                g.setColour (juce::Colours::white);
                g.fillRect (0, y, 64, 6);
            }
            plugin.setImage (stripe, "stripe");
            const auto audio = render (plugin, { 48 }, 1.0, 0.0);
            return audio.getRMSLevel (0, sr / 4, sr / 2) / juce::jmax (1.0e-6f, audio.getRMSLevel (1, sr / 4, sr / 2));
        };

        setParam (plugin, "stereoSource", (float) EngineParams::stereoPitch);
        setParam (plugin, "width", 100.0f);
        const float lowBalance = stripeBalance (56), highBalance = stripeBalance (2);
        std::cout << "    pitch source, left/right level: bottom stripe " << lowBalance << ", top stripe " << highBalance << std::endl;
        check (lowBalance > 4.0f && highBalance < 0.25f, "Pitch source: low rows on the left, high rows on the right");

        setParam (plugin, "stereoSource", (float) EngineParams::stereoColour);
        setParam (plugin, "tuning", (float) EngineParams::harmonic);
    }

    std::cout << "Freq 2: filter Env Amount in percent" << std::endl;
    {
        auto* amount = plugin.apvts.getParameter ("filterEnvAmount");
        check (amount->getLabel() == "%"
                && juce::exactlyEqual (amount->convertFrom0to1 (0.0f), -100.0f)
                && juce::exactlyEqual (amount->convertFrom0to1 (1.0f), 100.0f),
               "ranges from -100% to +100%");
        check (amount->getText (amount->convertTo0to1 (50.0f), 10) == "+50"
                && amount->getText (amount->convertTo0to1 (-25.0f), 10) == "-25"
                && amount->getText (amount->convertTo0to1 (0.0f), 10) == "0",
               "shows a sign, like +50 %");
        check (juce::exactlyEqual (PhotoToFrequencyProcessor::envAmountFullOctaves, 10.0f), "100% is the whole audible range, 10 octaves");
    }

    std::cout << "Freq 2: patches" << std::endl;
    {
        // A picture with a little detail, so a lossy round trip would show.
        juce::Image picture (juce::Image::RGB, 300, 150, true);
        {
            juce::Graphics g (picture);
            g.fillAll (juce::Colour (0xff102030));
            g.setColour (juce::Colour (0xffc04080));
            g.fillRect (30, 20, 90, 40);
            g.setColour (juce::Colour (0xff20e0a0));
            g.fillEllipse (160.0f, 60.0f, 100.0f, 70.0f);
        }
        plugin.setImage (picture, "garden.jpg");

        setParam (plugin, "regionLeft", 0.2f);   setParam (plugin, "regionRight", 0.7f);
        setParam (plugin, "regionBottom", 0.1f); setParam (plugin, "regionTop", 0.8f);
        setParam (plugin, "reverse", 1.0f);
        setParam (plugin, "stereoSource", (float) EngineParams::stereoAlternate);
        setParam (plugin, "width", 40.0f);
        setParam (plugin, "filterEnvAmount", 35.0f);
        setParam (plugin, "cutoff", 3000.0f);
        setParam (plugin, "subOn", 1.0f);
        plugin.setTooltipsEnabled (false);

        const auto patchFile = tempDir.getChildFile ("Patches").getChildFile (juce::String ("Warm Garden") + PluginConfig::patchExtension);
        patchFile.getParentDirectory().deleteRecursively();
        check (plugin.savePatch (patchFile) && patchFile.existsAsFile(), "saves a patch file (and makes its folder)");
        check (plugin.getPatchName() == "Warm Garden", "the patch takes the file's name");
        std::cout << "    patch file: " << patchFile.getSize() << " bytes" << std::endl;

        // Change everything, then bring it back.
        juce::Image black (juce::Image::RGB, 64, 64, true);
        plugin.setImage (black, "black");
        setParam (plugin, "regionLeft", 0.0f);   setParam (plugin, "regionRight", 1.0f);
        setParam (plugin, "regionBottom", 0.0f); setParam (plugin, "regionTop", 1.0f);
        setParam (plugin, "reverse", 0.0f);
        setParam (plugin, "stereoSource", (float) EngineParams::stereoColour);
        setParam (plugin, "width", 100.0f);
        setParam (plugin, "filterEnvAmount", 0.0f);
        setParam (plugin, "cutoff", 20000.0f);
        setParam (plugin, "subOn", 0.0f);
        plugin.setTooltipsEnabled (true);

        auto value = [&] (const char* id) { return plugin.apvts.getRawParameterValue (id)->load(); };

        check (plugin.loadPatch (patchFile), "loads the patch");
        check (plugin.getImageName() == "garden.jpg", "patch brings back the picture's name");

        const auto restored = plugin.getPreviewImage();
        bool samePixels = restored.getWidth() == 300 && restored.getHeight() == 150;

        for (int y = 0; samePixels && y < 150; y += 7)
            for (int x = 0; samePixels && x < 300; x += 7)
                samePixels = restored.getPixelAt (x, y).getARGB() == picture.getPixelAt (x, y).getARGB();

        check (samePixels, "patch brings back the picture itself, pixel for pixel");
        check (std::abs (value ("regionLeft") - 0.2f) < 0.001f && std::abs (value ("regionRight") - 0.7f) < 0.001f
                && std::abs (value ("regionBottom") - 0.1f) < 0.001f && std::abs (value ("regionTop") - 0.8f) < 0.001f,
               "patch brings back the selected part of the picture");
        check (value ("reverse") > 0.5f && juce::roundToInt (value ("stereoSource")) == EngineParams::stereoAlternate
                && std::abs (value ("width") - 40.0f) < 0.01f && std::abs (value ("filterEnvAmount") - 35.0f) < 0.01f
                && std::abs (value ("cutoff") - 3000.0f) < 1.0f && value ("subOn") > 0.5f,
               "patch brings back the settings");
        check (plugin.areTooltipsEnabled(), "loading a patch leaves the Tooltips switch alone");
        check (plugin.getPatchName() == "Warm Garden", "the patch name shows after loading");
        check (measure (render (plugin, { 48 }, 1.0, 0.2)).finite, "plays after loading a patch");

        // The patch name is kept in the project too.
        {
            juce::MemoryBlock state;
            plugin.getStateInformation (state);
            std::unique_ptr<juce::AudioProcessor> otherBase (createPluginFilter());
            auto& other = dynamic_cast<PhotoToFrequencyProcessor&> (*otherBase);
            other.setStateInformation (state.getData(), (int) state.getSize());
            check (other.getPatchName() == "Warm Garden", "the project remembers which patch was loaded");
        }

        // Files that aren't patches are refused and change nothing.
        const auto junk = tempDir.getChildFile (juce::String ("junk") + PluginConfig::patchExtension);
        junk.replaceWithText ("not a patch");
        check (! plugin.loadPatch (junk) && plugin.getImageName() == "garden.jpg", "a file that isn't a patch is refused");
        check (! plugin.loadPatch (tempDir.getChildFile ("missing.pfpatch")), "a missing patch is refused");

        juce::ValueTree otherPlugin ("PhotoFreq");
        juce::MemoryOutputStream otherData;
        otherPlugin.writeToStream (otherData);
        const auto otherFile = tempDir.getChildFile (juce::String ("other") + PluginConfig::patchExtension);
        otherFile.replaceWithData (otherData.getData(), otherData.getDataSize());
        check (! plugin.loadPatch (otherFile) && std::abs (value ("regionLeft") - 0.2f) < 0.001f,
               "a state from another plugin is refused");

        // A folder inside an ordinary file can't be created on any system.
        const auto blocker = tempDir.getChildFile ("not-a-folder.txt");
        blocker.replaceWithText ("x");
        check (! plugin.savePatch (blocker.getChildFile ("cannot").getChildFile ("save.pfpatch")) && plugin.getPatchName() == "Warm Garden",
               "a failed save is reported and keeps the old patch name");

        // The editor has the patch buttons, the Reverse switch and the Stereo Source menu.
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
        bool hasSave = false, hasLoad = false, hasReverse = false, hasStereoSource = false;

        for (auto* child : editor->getChildren())
        {
            if (auto* button = dynamic_cast<juce::Button*> (child))
            {
                hasSave    = hasSave    || (button->getButtonText() == "Save Patch..." && button->isVisible());
                hasLoad    = hasLoad    || (button->getButtonText() == "Load Patch..." && button->isVisible());
                hasReverse = hasReverse || (button->getButtonText() == "Reverse" && button->getToggleState());
            }

            if (auto* box = dynamic_cast<juce::ComboBox*> (child))
                hasStereoSource = hasStereoSource || box->getText() == "Alternate";
        }

        check (hasSave && hasLoad, "header has Save Patch and Load Patch buttons");
        check (hasReverse, "Reverse switch shows the loaded setting");
        check (hasStereoSource, "Stereo Source menu shows the loaded setting");

        // Leave things neutral for the stress run.
        setParam (plugin, "reverse", 0.0f);
        setParam (plugin, "stereoSource", (float) EngineParams::stereoColour);
        setParam (plugin, "width", 100.0f);
        setParam (plugin, "filterEnvAmount", 0.0f);
        setParam (plugin, "cutoff", 20000.0f);
        setParam (plugin, "subOn", 0.0f);
        setParam (plugin, "regionLeft", 0.0f);   setParam (plugin, "regionRight", 1.0f);
        setParam (plugin, "regionBottom", 0.0f); setParam (plugin, "regionTop", 1.0f);
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

               #if PTF_VARIANT >= 1
                // Show a selected region in the screenshot.
                setParam (fresh, "regionLeft", 0.45f);
                setParam (fresh, "regionRight", 0.95f);
                setParam (fresh, "regionBottom", 0.3f);
                setParam (fresh, "regionTop", 0.9f);
               #endif

               #if PTF_VARIANT >= 2
                setParam (fresh, "subOn", 1.0f);
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
