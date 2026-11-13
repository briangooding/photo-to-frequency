#pragma once

#include "ImageSpectrum.h"
#include <juce_audio_basics/juce_audio_basics.h>

/** Snapshot of the user parameters, taken once per audio block. */
struct EngineParams
{
    enum LoopMode { loop = 0, pingPong, oneShot };
    enum Tuning   { harmonic = 0, freeLog, chromatic, major, minor, pentatonic };

    float scanSeconds  = 4.0f;
    int   loopMode     = loop;
    int   tuning       = harmonic;
    float rangeOctaves = 5.0f;
    int   level        = 2;        // index into SpectrumTable levels (16/32/64/128 partials)
    float contrast     = 2.0f;
    float threshold    = 0.1f;
    bool  invert       = false;
    float tilt         = 0.5f;
    float width        = 1.0f;
    float attack       = 0.02f;
    float release      = 0.5f;
};

/**
    Additive synth that plays a SpectrumTable.

    Every row of the picture is one sine partial. A scan head travels across the
    picture; the brightness under it sets each partial's level and the colour
    sets its pan. The played note sets the pitch of the bottom row.
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

    /** Scan position (0..1) of the most recently started sounding voice, or -1. */
    float getScanPosition() const noexcept;

private:
    static constexpr int subBlockSize = 32;

    /** Per-partial data shared by all voices, rebuilt when a relevant parameter changes. */
    struct Shared
    {
        int numPartials = 0;
        uint32_t version = 0;                                   // bumped when ratios change
        std::array<float, SpectrumTable::maxRows> ratio {};     // frequency relative to the played note
        std::array<float, SpectrumTable::maxRows> tiltGain {};
        float normalise = 0.0f;                                 // auto-level for the current picture
    };

    void updateShared (const SpectrumTable&, const EngineParams&);

    struct Voice
    {
        enum class Stage { off, attack, sustain, release };

        void start (int id, float midiPitch, float vel, uint64_t startOrder, juce::Random&);
        void updatePartials (const Shared&, double sampleRate);
        void render (float* left, float* right, int numSamples,
                     const SpectrumTable&, const EngineParams&, const Shared&, double sampleRate);

        Stage stage = Stage::off;
        int noteId = -1;
        uint64_t order = 0;
        float baseFrequency = 440.0f;
        float velocity = 1.0f;
        float envelope = 0.0f;
        double position = 0.0;
        double direction = 1.0;

        // Cache key for the per-partial oscillator data.
        uint32_t cachedVersion = 0;
        int cachedPartials = -1;
        double cachedSampleRate = 0.0;

        std::array<float, SpectrumTable::maxRows> oscRe {}, oscIm {};      // oscillator state
        std::array<float, SpectrumTable::maxRows> rotCos {}, rotSin {};    // per-sample rotation
        std::array<bool, SpectrumTable::maxRows> audible {};               // false above Nyquist
        std::array<float, SpectrumTable::maxRows> ampL {}, ampR {};        // current levels
    };

    Voice* findVoiceToStart();

    std::array<Voice, maxVoices> voices;
    Shared shared;

    // Cache keys for Shared.
    int cachedTuning = -1, cachedLevel = -1;
    float cachedRange = -1.0f, cachedTilt = -1.0f, cachedContrast = -1.0f, cachedThreshold = -1.0f;
    bool cachedInvert = false;
    uint32_t cachedTableSerial = 0;
    double sampleRate = 44100.0;
    uint64_t noteCounter = 0;
    juce::Random random;
};
