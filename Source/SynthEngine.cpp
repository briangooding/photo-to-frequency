#include "SynthEngine.h"

namespace
{
    constexpr float silence = 1.0e-5f;

    /** Semitone offset of partial k for the scale-based tunings. */
    float quantiseToScale (float semitones, int tuning)
    {
        static const int majorScale[]      = { 0, 2, 4, 5, 7, 9, 11, 12 };
        static const int minorScale[]      = { 0, 2, 3, 5, 7, 8, 10, 12 };
        static const int pentatonicScale[] = { 0, 2, 4, 7, 9, 12 };

        if (tuning == EngineParams::chromatic)
            return std::round (semitones);

        const int* scale = majorScale;
        int size = 8;

        if (tuning == EngineParams::minor)            { scale = minorScale;      size = 8; }
        else if (tuning == EngineParams::pentatonic)  { scale = pentatonicScale; size = 6; }

        const float octave = std::floor (semitones / 12.0f);
        const float within = semitones - 12.0f * octave;

        float best = 0.0f, bestDistance = 100.0f;

        for (int i = 0; i < size; ++i)
        {
            const float distance = std::abs (within - (float) scale[i]);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = (float) scale[i];
            }
        }

        return 12.0f * octave + best;
    }

    /** Orders and clamps a pair of region edges, keeping at least minSize between them. */
    void sanitiseRange (float& low, float& high, float minSize)
    {
        low  = juce::jlimit (0.0f, 1.0f, low);
        high = juce::jlimit (0.0f, 1.0f, high);

        if (high < low)
            std::swap (low, high);

        if (high - low < minSize)
        {
            high = juce::jmin (1.0f, low + minSize);
            low = high - minSize;
        }
    }

    /** Correction that takes the aliasing out of a jump in a saw or square.
        phase is where the jump is (0..1), dt the phase step per sample. */
    float polyBlep (float phase, float dt)
    {
        if (phase < dt)
        {
            const float t = phase / dt;
            return t + t - t * t - 1.0f;
        }

        if (phase > 1.0f - dt)
        {
            const float t = (phase - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    /** Time for an exponential segment to fall by 60 dB. */
    float decayCoefficient (float seconds, double sampleRate)
    {
        return std::exp (-6.9f / (juce::jmax (0.001f, seconds) * (float) sampleRate));
    }
}

//==============================================================================
SynthEngine::Envelope::Rates SynthEngine::Envelope::makeRates (float attack, float decay, float sustain,
                                                               float release, double sr)
{
    return { 1.0f / (juce::jmax (0.0005f, attack) * (float) sr),
             decayCoefficient (decay, sr),
             juce::jlimit (0.0f, 1.0f, sustain),
             decayCoefficient (juce::jmax (0.005f, release), sr) };
}

//==============================================================================
void SynthEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void SynthEngine::reset()
{
    for (auto& v : voices)
        v.active = false;
}

SynthEngine::Voice* SynthEngine::findVoiceToStart()
{
    Voice* oldest = nullptr;
    Voice* oldestReleasing = nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
            return &v;

        if (oldest == nullptr || v.order < oldest->order)
            oldest = &v;

        if (! v.isHeld() && (oldestReleasing == nullptr || v.order < oldestReleasing->order))
            oldestReleasing = &v;
    }

    return oldestReleasing != nullptr ? oldestReleasing : oldest;
}

void SynthEngine::noteOn (int noteId, float midiPitch, float velocity)
{
    // Retriggering a held note lets the old one ring out.
    noteOff (noteId);

    if (auto* v = findVoiceToStart())
        v->start (noteId, midiPitch, velocity, ++noteCounter, random);
}

void SynthEngine::noteOff (int noteId)
{
    for (auto& v : voices)
        if (v.noteId == noteId && v.isHeld())
            v.release();
}

void SynthEngine::allNotesOff()
{
    for (auto& v : voices)
        if (v.active)
            v.release();
}

float SynthEngine::getScanPosition() const noexcept
{
    const Voice* newest = nullptr;

    for (auto& v : voices)
        if (v.active && (newest == nullptr || v.order > newest->order))
            newest = &v;

    return newest != nullptr ? shared.regionLeft + newest->scanPoint * shared.regionWidth : -1.0f;
}

//==============================================================================
void SynthEngine::updateShared (const SpectrumTable& table, const EngineParams& p)
{
    const int level = juce::jlimit (0, SpectrumTable::numLevels - 1, p.level);
    const int numPartials = SpectrumTable::rowsForLevel (level);
    const float region[4] = { p.regionLeft, p.regionRight, p.regionBottom, p.regionTop };

    const bool ratiosChanged = cachedTuning != p.tuning || cachedLevel != level
                                || ! juce::exactlyEqual (cachedRange, p.rangeOctaves);
    const bool tiltChanged = ! juce::exactlyEqual (cachedTilt, p.tilt);
    const bool regionChanged = cachedLevel != level || ! std::equal (region, region + 4, cachedRegion);
    const bool shapeChanged = cachedTableSerial != table.serial || cachedInvert != p.invert
                               || ! juce::exactlyEqual (cachedContrast, p.contrast)
                               || ! juce::exactlyEqual (cachedThreshold, p.threshold);

    if (! (ratiosChanged || tiltChanged || regionChanged || shapeChanged))
        return;

    cachedTuning = p.tuning;
    cachedLevel = level;
    cachedRange = p.rangeOctaves;
    cachedTilt = p.tilt;
    cachedContrast = p.contrast;
    cachedThreshold = p.threshold;
    cachedInvert = p.invert;
    cachedTableSerial = table.serial;
    std::copy (region, region + 4, cachedRegion);

    shared.numPartials = numPartials;

    if (ratiosChanged)
    {
        ++shared.version;

        for (int k = 0; k < numPartials; ++k)
        {
            float ratio = (float) (k + 1);

            if (p.tuning != EngineParams::harmonic)
            {
                float semitones = 12.0f * p.rangeOctaves * (float) k / (float) juce::jmax (1, numPartials - 1);

                if (p.tuning != EngineParams::freeLog)
                    semitones = quantiseToScale (semitones, p.tuning);

                ratio = std::pow (2.0f, semitones / 12.0f);
            }

            shared.ratio[(size_t) k] = ratio;
        }
    }

    if (ratiosChanged || tiltChanged)
        for (int k = 0; k < numPartials; ++k)
            shared.tiltGain[(size_t) k] = std::pow (shared.ratio[(size_t) k], -p.tilt);

    constexpr int columns = SpectrumTable::numColumns;

    if (regionChanged)
    {
        float left = p.regionLeft, right = p.regionRight, bottom = p.regionBottom, top = p.regionTop;
        sanitiseRange (left, right, 2.0f / (float) columns);
        sanitiseRange (bottom, top, 1.0f / (float) SpectrumTable::maxRows);

        shared.regionLeft = left;
        shared.regionWidth = right - left;
        shared.columnStart = left * (float) columns;
        shared.columnLength = (right - left) * (float) columns;

        // Use the table whose rows are about one partial tall inside the region,
        // so a small region still gets all the detail the picture has. With the
        // whole picture selected this is exactly one table row per partial.
        const float rowsWanted = (float) numPartials / (top - bottom);
        int tableLevel = SpectrumTable::numLevels - 1;

        for (int l = 0; l < SpectrumTable::numLevels; ++l)
        {
            if ((float) SpectrumTable::rowsForLevel (l) >= rowsWanted - 0.01f)
            {
                tableLevel = l;
                break;
            }
        }

        shared.tableLevel = tableLevel;
        const int rows = SpectrumTable::rowsForLevel (tableLevel);

        for (int k = 0; k < numPartials; ++k)
        {
            const float centre = bottom + (top - bottom) * ((float) k + 0.5f) / (float) numPartials;
            const float rowPosition = juce::jlimit (0.0f, (float) (rows - 1), centre * (float) rows - 0.5f);
            const int row = juce::jmin (rows - 1, (int) rowPosition);
            const auto i = (size_t) k;

            shared.row0[i] = row * columns;
            shared.row1[i] = juce::jmin (rows - 1, row + 1) * columns;
            shared.rowBlend[i] = rowPosition - (float) row;
        }
    }

    // Auto-level: scale so the loudest moment of the selected part of the picture
    // always comes out at about the same level, whatever the picture and the
    // brightness shaping. Sampling every few columns is plenty for that.
    constexpr int columnStep = 8;
    const float* brightness = table.brightness[(size_t) shared.tableLevel].data();
    const float thresholdScale = 1.0f / juce::jmax (0.01f, 1.0f - p.threshold);
    const int firstColumn = juce::jlimit (0, columns - 1, (int) shared.columnStart);
    const int endColumn = juce::jlimit (firstColumn + 1, columns, juce::roundToInt (shared.columnStart + shared.columnLength));
    float maxPower = 0.0f;

    for (int column = firstColumn; column < endColumn; column += columnStep)
    {
        float power = 0.0f;

        for (int k = 0; k < numPartials; ++k)
        {
            const auto i = (size_t) k;
            float value = brightness[(size_t) (shared.row0[i] + column)];
            value += shared.rowBlend[i] * (brightness[(size_t) (shared.row1[i] + column)] - value);

            if (p.invert)
                value = 1.0f - value;

            value = (value - p.threshold) * thresholdScale;

            if (value > 0.0f)
            {
                const float amplitude = std::pow (value, p.contrast) * shared.tiltGain[i];
                power += amplitude * amplitude;
            }
        }

        maxPower = juce::jmax (maxPower, power);
    }

    constexpr float targetAmplitude = 0.5f;   // loudest column: about -9 dB RMS per voice before Gain
    shared.normalise = maxPower > 1.0e-8f ? juce::jmin (16.0f, targetAmplitude / std::sqrt (maxPower)) : 0.0f;
}

void SynthEngine::render (float* left, float* right, int numSamples,
                          const SpectrumTable* table, const EngineParams& params)
{
    if (table == nullptr)
        return;

    updateShared (*table, params);

    for (int offset = 0; offset < numSamples; offset += subBlockSize)
    {
        const int n = juce::jmin (subBlockSize, numSamples - offset);

        for (auto& v : voices)
            if (v.active)
                v.render (left + offset, right + offset, n, *table, params, shared, sampleRate);
    }
}

//==============================================================================
void SynthEngine::Voice::start (int id, float midiPitch, float vel, uint64_t startOrder, juce::Random& rng)
{
    noteId = id;
    order = startOrder;
    baseFrequency = 440.0f * std::pow (2.0f, (midiPitch - 69.0f) / 12.0f);
    velocity = vel;
    position = 0.0;
    direction = 1.0;
    scanPoint = 0.0f;
    active = true;
    cachedPartials = -1;
    ampEnvelope.start();
    filterEnvelope.start();

    // Random phases keep the partials from piling up into one big peak.
    for (size_t k = 0; k < oscRe.size(); ++k)
    {
        const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        oscRe[k] = std::cos (phase);
        oscIm[k] = std::sin (phase);
    }

    ampL.fill (0.0f);
    ampR.fill (0.0f);

    // Phase 0 is a rising zero crossing for the sine and triangle.
    subPhase = 0.0;
    subAmp = 0.0f;
}

void SynthEngine::Voice::release() noexcept
{
    ampEnvelope.startRelease();
    filterEnvelope.startRelease();
}

void SynthEngine::Voice::updatePartials (const Shared& shared, double sr)
{
    if (cachedVersion == shared.version && cachedPartials == shared.numPartials
         && juce::exactlyEqual (cachedSampleRate, sr))
        return;

    // A different row count means the rows no longer line up with the old levels.
    if (cachedPartials != shared.numPartials)
    {
        ampL.fill (0.0f);
        ampR.fill (0.0f);
    }

    cachedVersion = shared.version;
    cachedPartials = shared.numPartials;
    cachedSampleRate = sr;

    const float nyquistLimit = 0.45f * (float) sr;

    for (int k = 0; k < shared.numPartials; ++k)
    {
        const auto i = (size_t) k;
        frequency[i] = baseFrequency * shared.ratio[i];
        audible[i] = frequency[i] < nyquistLimit;

        const float w = audible[i] ? juce::MathConstants<float>::twoPi * frequency[i] / (float) sr : 0.0f;
        rotCos[i] = std::cos (w);
        rotSin[i] = std::sin (w);
    }
}

void SynthEngine::Voice::render (float* left, float* right, int numSamples,
                                 const SpectrumTable& table, const EngineParams& p,
                                 const Shared& shared, double sr)
{
    jassert (numSamples <= subBlockSize);

    const int numPartials = shared.numPartials;
    constexpr int columns = SpectrumTable::numColumns;

    updatePartials (shared, sr);

    //==========================================================================
    // Move the scan head across the selected part of the picture.
    position += direction * (double) numSamples / (juce::jmax (0.01, (double) p.scanSeconds) * sr);

    if (p.loopMode == EngineParams::loop)
    {
        position -= std::floor (position);
    }
    else if (p.loopMode == EngineParams::pingPong)
    {
        if (position >= 1.0)      { position = juce::jmax (0.0, 2.0 - position); direction = -1.0; }
        else if (position <= 0.0) { position = juce::jmin (1.0, -position);      direction = 1.0; }
    }
    else if (position >= 1.0)
    {
        position = 1.0;

        if (isHeld())
            release();
    }

    // Reversed, the head starts at the right edge and travels left. Ping-Pong
    // and One-Shot behave the same way, just mirrored.
    double place = p.reverse ? 1.0 - position : position;

    if (p.loopMode == EngineParams::loop)
        place -= std::floor (place);   // reversed, the start (1.0) is the same point as 0.0

    scanPoint = (float) juce::jlimit (0.0, 1.0, place);

    int column0, column1;
    float blend;

    if (p.loopMode == EngineParams::loop)
    {
        // The last column of the region blends back into its first.
        const double c = (double) shared.columnStart + place * (double) shared.columnLength;
        column0 = juce::jmin (columns - 1, (int) c);
        column1 = column0 + 1;
        blend = (float) (c - (double) column0);

        if ((double) column1 >= (double) shared.columnStart + (double) shared.columnLength || column1 >= columns)
            column1 = juce::jmin (columns - 1, (int) shared.columnStart);
    }
    else
    {
        const double c = juce::jmin ((double) (columns - 1),
                                     (double) shared.columnStart
                                       + juce::jlimit (0.0, 1.0, place) * juce::jmax (0.0, (double) shared.columnLength - 1.0));
        column0 = juce::jmin (columns - 2, (int) c);
        column1 = column0 + 1;
        blend = (float) (c - (double) column0);
    }

    //==========================================================================
    // Filter: one envelope value per sub-block is smooth enough, because the
    // partial levels are ramped across the sub-block anyway.
    float filterCutoff = 0.0f, filterDamping = 0.0f, filterGain = 1.0f;

    if (p.filterOn)
    {
        const auto rates = Envelope::makeRates (p.filterAttack, p.filterDecay, p.filterSustain, p.filterRelease, sr);
        float envelope = 0.0f;

        for (int n = 0; n < numSamples; ++n)
            envelope = filterEnvelope.tick (rates);

        filterCutoff = juce::jlimit (20.0f, 20000.0f, p.cutoff * std::pow (2.0f, p.filterEnvAmount * envelope));

        // Resonance 0..1 maps to Q 0.7 (flat) .. 12 (ringing).
        const float q = 0.7071f * std::pow (17.0f, juce::jlimit (0.0f, 1.0f, p.resonance));
        filterDamping = 1.0f / (q * q);
        filterGain = std::sqrt (0.7071f / q);   // keeps the resonant peak from running away
    }

    //==========================================================================
    // Sum the partials.
    float mixL[subBlockSize] = {}, mixR[subBlockSize] = {};

    const float* brightness = table.brightness[(size_t) shared.tableLevel].data();
    const float* panTable = table.pan[(size_t) shared.tableLevel].data();

    const float thresholdScale = 1.0f / juce::jmax (0.01f, 1.0f - p.threshold);
    const float rampScale = 1.0f / (float) numSamples;

    auto sample = [&] (const float* data, size_t i)
    {
        const float* rowA = data + shared.row0[i];
        float value = rowA[column0] + blend * (rowA[column1] - rowA[column0]);

        if (shared.rowBlend[i] > 0.0f)
        {
            const float* rowB = data + shared.row1[i];
            const float other = rowB[column0] + blend * (rowB[column1] - rowB[column0]);
            value += shared.rowBlend[i] * (other - value);
        }

        return value;
    };

    for (int k = 0; k < numPartials; ++k)
    {
        const auto i = (size_t) k;
        float targetL = 0.0f, targetR = 0.0f;

        // With the picture oscillator off the partials ramp down to silence.
        if (p.oscOn && audible[i])
        {
            float value = sample (brightness, i);

            if (p.invert)
                value = 1.0f - value;

            value = (value - p.threshold) * thresholdScale;

            if (value > 0.0f)
            {
                float amplitude = std::pow (value, p.contrast) * shared.tiltGain[i] * shared.normalise;

                if (p.filterOn)
                {
                    // Magnitude response of a two-pole filter at this partial's frequency.
                    const float x = frequency[i] / filterCutoff;
                    const float x2 = x * x;
                    const float response = 1.0f / std::sqrt ((1.0f - x2) * (1.0f - x2) + x2 * filterDamping);

                    if (p.filterType == EngineParams::highPass)       amplitude *= x2 * response;
                    else if (p.filterType == EngineParams::bandPass)  amplitude *= x * std::sqrt (filterDamping) * response;
                    else                                              amplitude *= response;

                    amplitude *= filterGain;
                }

                float pan;

                if (p.stereoSource == EngineParams::stereoAlternate)
                    pan = (k & 1) != 0 ? 1.0f : -1.0f;                 // rows alternate left and right
                else if (p.stereoSource == EngineParams::stereoPitch)
                    pan = numPartials > 1 ? -1.0f + 2.0f * (float) k / (float) (numPartials - 1) : 0.0f;   // low left, high right
                else
                    pan = sample (panTable, i);                         // red left, blue right

                const float angle = (pan * p.width + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

                // Equal-power pan, scaled so a centred partial is at unity in both channels.
                targetL = amplitude * std::cos (angle) * juce::MathConstants<float>::sqrt2;
                targetR = amplitude * std::sin (angle) * juce::MathConstants<float>::sqrt2;
            }
        }

        float aL = ampL[i], aR = ampR[i];

        if (aL < silence && aR < silence && targetL < silence && targetR < silence)
        {
            ampL[i] = 0.0f;
            ampR[i] = 0.0f;
            continue;
        }

        const float stepL = (targetL - aL) * rampScale;
        const float stepR = (targetR - aR) * rampScale;
        const float c = rotCos[i], s = rotSin[i];
        float re = oscRe[i], im = oscIm[i];

        for (int n = 0; n < numSamples; ++n)
        {
            mixL[n] += re * aL;
            mixR[n] += re * aR;
            aL += stepL;
            aR += stepR;

            const float nextRe = re * c - im * s;
            im = re * s + im * c;
            re = nextRe;
        }

        // Keep the rotating oscillator from drifting away from unit amplitude.
        const float correction = 1.5f - 0.5f * (re * re + im * im);
        oscRe[i] = re * correction;
        oscIm[i] = im * correction;
        ampL[i] = targetL;
        ampR[i] = targetR;
    }

    //==========================================================================
    // Sub oscillator, centred. Its level is ramped so switching it is click-free.
    {
        constexpr float fullLevel = 0.5f;   // matches the loudest moment of the picture
        float target = p.subOn ? fullLevel * juce::jlimit (0.0f, 1.0f, p.subLevel) : 0.0f;

        // Follow: the sub swells and fades with the picture under the scan head.
        // Loudness is measured like the auto-level does it, so the loudest part
        // of the selected picture is 1 and black is 0. It is measured whether or
        // not the picture oscillator is on, so the sub can play the picture alone.
        const float follow = juce::jlimit (0.0f, 1.0f, p.subFollow);

        if (target > 0.0f && follow > 0.0f)
        {
            float power = 0.0f;

            for (int k = 0; k < numPartials; ++k)
            {
                const auto i = (size_t) k;
                float value = sample (brightness, i);

                if (p.invert)
                    value = 1.0f - value;

                value = (value - p.threshold) * thresholdScale;

                if (value > 0.0f)
                {
                    const float amplitude = std::pow (value, p.contrast) * shared.tiltGain[i] * shared.normalise;
                    power += amplitude * amplitude;
                }
            }

            const float loudness = juce::jlimit (0.0f, 1.0f, std::sqrt (power) / fullLevel);
            target *= 1.0f - follow + follow * loudness;
        }

        if (subAmp > silence || target > silence)
        {
            const float subFrequency = baseFrequency * std::pow (0.5f, (float) juce::jlimit (1, 3, p.subOctaves))
                                        * std::pow (2.0f, juce::jlimit (-100.0f, 100.0f, p.subFine) / 1200.0f);
            const double increment = juce::jmin (0.45, (double) subFrequency / sr);
            const float dt = (float) increment;
            const int wave = juce::jlimit ((int) EngineParams::subSine, (int) EngineParams::subSquare, p.subWave);

            // Every wave is scaled to the sine's loudness, so switching shape keeps the level.
            static constexpr float waveGain[] = { 1.0f, 1.2247f, 1.2247f, 0.7071f };

            const float drive = juce::jlimit (0.0f, 1.0f, p.subDrive);
            const float driveAmount = 1.0f + 9.0f * drive;
            const float driveNormal = drive > 0.0f ? 1.0f / std::tanh (driveAmount) : 1.0f;

            const float step = (target - subAmp) * rampScale;

            for (int n = 0; n < numSamples; ++n)
            {
                const float phase = (float) subPhase;
                float value;

                switch (wave)
                {
                    case EngineParams::subTriangle:
                        value = phase < 0.25f ? 4.0f * phase : phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
                        break;

                    case EngineParams::subSaw:
                        value = 2.0f * phase - 1.0f - polyBlep (phase, dt);
                        break;

                    case EngineParams::subSquare:
                    {
                        const float halfPhase = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
                        value = (phase < 0.5f ? 1.0f : -1.0f) + polyBlep (phase, dt) - polyBlep (halfPhase, dt);
                        break;
                    }

                    default:
                        value = std::sin (juce::MathConstants<float>::twoPi * phase);
                        break;
                }

                if (drive > 0.0f)
                    value = std::tanh (value * driveAmount) * driveNormal;

                value *= waveGain[wave] * subAmp;
                mixL[n] += value;
                mixR[n] += value;
                subAmp += step;

                subPhase += increment;

                if (subPhase >= 1.0)
                    subPhase -= 1.0;
            }
        }

        subAmp = target;
    }

    //==========================================================================
    // Oscillator envelope.
    const auto rates = Envelope::makeRates (p.attack, p.decay, p.sustain, p.release, sr);

    for (int n = 0; n < numSamples; ++n)
    {
        const float gain = ampEnvelope.tick (rates) * velocity;
        left[n]  += mixL[n] * gain;
        right[n] += mixR[n] * gain;
    }

    // Finished once it has faded out, either after release or by decaying to a silent sustain.
    const bool fading = ampEnvelope.stage == Envelope::Stage::release
                         || (ampEnvelope.stage == Envelope::Stage::decay && rates.sustain < 1.0e-4f);

    if (fading && ampEnvelope.value < 1.0e-4f)
        active = false;
}
