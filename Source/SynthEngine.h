#pragma once

#include "ImageSpectrum.h"
#include <juce_audio_basics/juce_audio_basics.h>

/** Snapshot of the user parameters, taken once per audio block.

    The defaults for everything from `decay` down describe the original plugin:
    whole picture, no decay, no filter. The Split variant exposes them. */
struct EngineParams
{
    enum LoopMode   { loop = 0, pingPong, oneShot };
    enum Tuning     { harmonic = 0, freeLog, chromatic, major, minor, pentatonic };
    enum FilterType { lowPass = 0, highPass, bandPass };

    float scanSeconds  = 4.0f;
    int   loopMode     = loop;
    int   tuning       = harmonic;
    float rangeOctaves = 5.0f;
    int   level        = 2;        // 16 << level partials
    float contrast     = 2.0f;
    float threshold    = 0.1f;
    bool  invert       = false;
    float tilt         = 0.5f;
    float width        = 1.0f;

    // Oscillator (amplitude) envelope
    float attack       = 0.02f;
    float decay        = 0.1f;
    float sustain      = 1.0f;
    float release      = 0.5f;

    // Part of the picture that is played, as fractions of its size.
    // Left/right are in scan direction; bottom/top are low/high pitch.
    float regionLeft = 0.0f, regionRight = 1.0f, regionBottom = 0.0f, regionTop = 1.0f;

    // Filter
    bool  filterOn        = false;
    int   filterType      = lowPass;
    float cutoff          = 20000.0f;   // Hz
    float resonance       = 0.0f;       // 0..1
    float filterEnvAmount = 0.0f;       // octaves, either direction
    float filterAttack    = 0.01f;
    float filterDecay     = 0.3f;
    float filterSustain   = 0.5f;
    float filterRelease   = 0.5f;

    // The picture oscillator (all the partials). Off leaves only the sub.
    bool  oscOn      = true;

    // Sub oscillator: a simple waveform below the played note. It follows the
    // oscillator envelope but not the picture or the filter.
    enum SubWave { subSine = 0, subTriangle, subSaw, subSquare };

    bool  subOn      = false;
    int   subWave    = subSine;
    int   subOctaves = 1;       // octaves below the played note, 1..3
    float subFine    = 0.0f;    // cents, -100..100
    float subDrive   = 0.0f;    // 0..1 saturation
    float subLevel   = 0.5f;    // 0..1
};

/**
    Additive synth that plays a SpectrumTable.

    Every row of the selected part of the picture is one sine partial. A scan
    head travels across it; the brightness under it sets each partial's level
    and the colour sets its pan. The played note sets the pitch of the bottom row.

    The filter works on the partials themselves: each one is scaled by the
    filter's response at its frequency, which costs almost nothing and gives
    every voice its own filter.
*/
class SynthEngine
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate);
    void reset();

    void noteOn (int noteId, float midiPitch, float velocity);
    void noteOff (int noteId);
    void allNotesOff();

    /** Adds audio to left/right. The table may be null (renders nothing). */
    void render (float* left, float* right, int numSamples,
                 const SpectrumTable* table, const EngineParams& params);

    /** Where the newest sounding voice's scan head is, as a fraction of the
        whole picture's width, or -1 when nothing is sounding. */
    float getScanPosition() const noexcept;

private:
    static constexpr int subBlockSize = 32;

    struct Envelope
    {
        enum class Stage { idle, attack, decay, release };

        struct Rates { float attackStep, decayCoeff, sustain, releaseCoeff; };

        static Rates makeRates (float attack, float decay, float sustain, float release, double sampleRate);

        void start() noexcept          { value = 0.0f; stage = Stage::attack; }
        void startRelease() noexcept   { if (stage != Stage::idle) stage = Stage::release; }

        float tick (const Rates& r) noexcept
        {
            switch (stage)
            {
                case Stage::attack:
                    value += r.attackStep;
                    if (value >= 1.0f) { value = 1.0f; stage = Stage::decay; }
                    break;

                case Stage::decay:     value = r.sustain + (value - r.sustain) * r.decayCoeff; break;
                case Stage::release:   value *= r.releaseCoeff; break;
                case Stage::idle:      break;
            }

            return value;
        }

        float value = 0.0f;
        Stage stage = Stage::idle;
    };

    /** Per-partial data shared by all voices, rebuilt when a relevant parameter changes. */
    struct Shared
    {
        int numPartials = 0;
        uint32_t version = 0;                                   // bumped when ratios change
        std::array<float, SpectrumTable::maxRows> ratio {};     // frequency relative to the played note
        std::array<float, SpectrumTable::maxRows> tiltGain {};
        float normalise = 0.0f;                                 // auto-level for the current picture

        // Which part of the table is played.
        int tableLevel = 0;
        std::array<int, SpectrumTable::maxRows> row0 {}, row1 {};
        std::array<float, SpectrumTable::maxRows> rowBlend {};
        float columnStart = 0.0f, columnLength = (float) SpectrumTable::numColumns;
        float regionLeft = 0.0f, regionWidth = 1.0f;
    };

    void updateShared (const SpectrumTable&, const EngineParams&);

    struct Voice
    {
        void start (int id, float midiPitch, float vel, uint64_t startOrder, juce::Random&);
        void release() noexcept;
        bool isHeld() const noexcept   { return active && ampEnvelope.stage != Envelope::Stage::release; }

        void updatePartials (const Shared&, double sampleRate);
        void render (float* left, float* right, int numSamples,
                     const SpectrumTable&, const EngineParams&, const Shared&, double sampleRate);

        bool active = false;
        int noteId = -1;
        uint64_t order = 0;
        float baseFrequency = 440.0f;
        float velocity = 1.0f;
        double position = 0.0;
        double direction = 1.0;
        Envelope ampEnvelope, filterEnvelope;

        // Cache key for the per-partial oscillator data.
        uint32_t cachedVersion = 0;
        int cachedPartials = -1;
        double cachedSampleRate = 0.0;

        std::array<float, SpectrumTable::maxRows> oscRe {}, oscIm {};      // oscillator state
        std::array<float, SpectrumTable::maxRows> rotCos {}, rotSin {};    // per-sample rotation
        std::array<float, SpectrumTable::maxRows> frequency {};            // Hz
        std::array<bool, SpectrumTable::maxRows> audible {};               // false above Nyquist
        std::array<float, SpectrumTable::maxRows> ampL {}, ampR {};        // current levels

        double subPhase = 0.0;                                             // sub oscillator, 0..1
        float subAmp = 0.0f;
    };

    Voice* findVoiceToStart();

    std::array<Voice, maxVoices> voices;
    Shared shared;

    // Cache keys for Shared.
    int cachedTuning = -1, cachedLevel = -1;
    float cachedRange = -1.0f, cachedTilt = -1.0f, cachedContrast = -1.0f, cachedThreshold = -1.0f;
    float cachedRegion[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    bool cachedInvert = false;
    uint32_t cachedTableSerial = 0;

    double sampleRate = 44100.0;
    uint64_t noteCounter = 0;
    juce::Random random;
};
