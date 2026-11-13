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
        v.stage = Voice::Stage::off;
}

SynthEngine::Voice* SynthEngine::findVoiceToStart()
{
    Voice* oldest = nullptr;
    Voice* oldestReleasing = nullptr;

    for (auto& v : voices)
    {
        if (v.stage == Voice::Stage::off)
            return &v;

        if (oldest == nullptr || v.order < oldest->order)
            oldest = &v;

        if (v.stage == Voice::Stage::release && (oldestReleasing == nullptr || v.order < oldestReleasing->order))
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
        if (v.noteId == noteId && (v.stage == Voice::Stage::attack || v.stage == Voice::Stage::sustain))
            v.stage = Voice::Stage::release;
}

void SynthEngine::allNotesOff()
{
    for (auto& v : voices)
        if (v.stage != Voice::Stage::off)
            v.stage = Voice::Stage::release;
}

float SynthEngine::getScanPosition() const noexcept
{
    const Voice* newest = nullptr;

    for (auto& v : voices)
        if (v.stage != Voice::Stage::off && (newest == nullptr || v.order > newest->order))
            newest = &v;

    return newest != nullptr ? (float) newest->position : -1.0f;
}

void SynthEngine::updateShared (const SpectrumTable& table, const EngineParams& p)
{
    const int level = juce::jlimit (0, SpectrumTable::numLevels - 1, p.level);
    const int numPartials = SpectrumTable::rowsForLevel (level);

    const bool ratiosChanged = cachedTuning != p.tuning || cachedLevel != level
                                || ! juce::exactlyEqual (cachedRange, p.rangeOctaves);
    const bool tiltChanged = ! juce::exactlyEqual (cachedTilt, p.tilt);
    const bool shapeChanged = cachedTableSerial != table.serial || cachedInvert != p.invert
                               || ! juce::exactlyEqual (cachedContrast, p.contrast)
                               || ! juce::exactlyEqual (cachedThreshold, p.threshold);

    if (! (ratiosChanged || tiltChanged || shapeChanged))
        return;

    cachedTuning = p.tuning;
    cachedLevel = level;
    cachedRange = p.rangeOctaves;
    cachedTilt = p.tilt;
    cachedContrast = p.contrast;
    cachedThreshold = p.threshold;
    cachedInvert = p.invert;
    cachedTableSerial = table.serial;

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

    // Auto-level: scale so the loudest moment of the picture always comes out at
    // about the same level, whatever the picture and the brightness shaping.
    // Sampling every few columns is plenty for that and keeps this cheap.
    constexpr int columnStep = 8;
    const float* brightness = table.brightness[(size_t) level].data();
    const float thresholdScale = 1.0f / juce::jmax (0.01f, 1.0f - p.threshold);
    float maxPower = 0.0f;

    for (int column = 0; column < SpectrumTable::numColumns; column += columnStep)
    {
        float power = 0.0f;

        for (int k = 0; k < numPartials; ++k)
        {
            float value = brightness[(size_t) (k * SpectrumTable::numColumns + column)];

            if (p.invert)
                value = 1.0f - value;

            value = (value - p.threshold) * thresholdScale;

            if (value > 0.0f)
            {
                const float amplitude = std::pow (value, p.contrast) * shared.tiltGain[(size_t) k];
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
            if (v.stage != Voice::Stage::off)
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
    envelope = 0.0f;
    position = 0.0;
    direction = 1.0;
    stage = Stage::attack;
    cachedPartials = -1;

    // Random phases keep the partials from piling up into one big peak.
    for (size_t k = 0; k < oscRe.size(); ++k)
    {
        const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        oscRe[k] = std::cos (phase);
        oscIm[k] = std::sin (phase);
    }

    ampL.fill (0.0f);
    ampR.fill (0.0f);
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
        const float frequency = baseFrequency * shared.ratio[i];
        audible[i] = frequency < nyquistLimit;

        const float w = audible[i] ? juce::MathConstants<float>::twoPi * frequency / (float) sr : 0.0f;
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
    const int level = juce::jlimit (0, SpectrumTable::numLevels - 1, p.level);
    constexpr int columns = SpectrumTable::numColumns;

    updatePartials (shared, sr);

    //==========================================================================
    // Move the scan head.
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

        if (stage != Stage::release)
            stage = Stage::release;
    }

    int column0, column1;
    float blend;

    if (p.loopMode == EngineParams::loop)
    {
        // The last column blends back into the first.
        const double c = position * columns;
        column0 = juce::jmin (columns - 1, (int) c);
        column1 = (column0 + 1) % columns;
        blend = (float) (c - (double) column0);
    }
    else
    {
        const double c = juce::jlimit (0.0, 1.0, position) * (columns - 1);
        column0 = juce::jmin (columns - 2, (int) c);
        column1 = column0 + 1;
        blend = (float) (c - (double) column0);
    }

    //==========================================================================
    // Sum the partials.
    float mixL[subBlockSize] = {}, mixR[subBlockSize] = {};

    const float* brightness = table.brightness[(size_t) level].data();
    const float* panTable = table.pan[(size_t) level].data();

    const float thresholdScale = 1.0f / juce::jmax (0.01f, 1.0f - p.threshold);
    const float rampScale = 1.0f / (float) numSamples;

    for (int k = 0; k < numPartials; ++k)
    {
        const auto i = (size_t) k;
        float targetL = 0.0f, targetR = 0.0f;

        if (audible[i])
        {
            const auto cell = (size_t) (k * columns);
            float value = brightness[cell + (size_t) column0]
                            + blend * (brightness[cell + (size_t) column1] - brightness[cell + (size_t) column0]);

            if (p.invert)
                value = 1.0f - value;

            value = (value - p.threshold) * thresholdScale;

            if (value > 0.0f)
            {
                const float amplitude = std::pow (value, p.contrast) * shared.tiltGain[i] * shared.normalise;

                const float panValue = panTable[cell + (size_t) column0]
                                         + blend * (panTable[cell + (size_t) column1] - panTable[cell + (size_t) column0]);
                const float angle = (panValue * p.width + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

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
    // Envelope.
    const float attackStep = 1.0f / (juce::jmax (0.0005f, p.attack) * (float) sr);
    const float releaseCoeff = std::exp (-6.9f / (juce::jmax (0.005f, p.release) * (float) sr));   // -60 dB over the release time

    for (int n = 0; n < numSamples; ++n)
    {
        if (stage == Stage::attack)
        {
            envelope += attackStep;

            if (envelope >= 1.0f)
            {
                envelope = 1.0f;
                stage = Stage::sustain;
            }
        }
        else if (stage == Stage::release)
        {
            envelope *= releaseCoeff;
        }

        const float gain = envelope * velocity;
        left[n]  += mixL[n] * gain;
        right[n] += mixR[n] * gain;
    }

    if (stage == Stage::release && envelope < 1.0e-4f)
        stage = Stage::off;
}
