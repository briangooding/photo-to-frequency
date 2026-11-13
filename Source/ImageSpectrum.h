#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

/**
    The picture, reduced to what the synth needs.

    The image is read like a spectrogram: x is time, y is pitch (bottom = low),
    brightness (the strongest colour channel) is loudness and the red/blue balance is the stereo position.

    The same picture is stored at several vertical resolutions so the number of
    partials can be changed while playing without re-analysing anything.
*/
struct SpectrumTable final : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SpectrumTable>;

    static constexpr int numColumns = 512;
    static constexpr int numLevels  = 4;
    static constexpr int maxRows    = 128;

    static constexpr int rowsForLevel (int level) noexcept   { return 16 << level; }

    /** [row * numColumns + column]; row 0 is the bottom of the picture. */
    std::array<std::vector<float>, numLevels> brightness;   // 0..1
    std::array<std::vector<float>, numLevels> pan;          // -1 (red, left) .. +1 (blue, right)

    /** Reduced copy of the picture, used for the display and saved with the project. */
    juce::Image preview;
    juce::String name;

    /** Unique per analysis, so the audio thread can tell tables apart cheaply. */
    uint32_t serial = 0;
};

namespace ImageSpectrum
{
    /** Largest edge of the stored preview, in pixels. */
    constexpr int maxPreviewSize = 1024;

    /** Loads a JPEG/PNG/GIF, honouring the EXIF orientation of camera JPEGs.
        Returns an invalid image if the file can't be decoded. */
    juce::Image loadFile (const juce::File& file);

    /** Reads the EXIF orientation (1..8) from JPEG data, or 1 if there is none. */
    int readExifOrientation (const void* data, size_t size);

    /** Returns a copy with the EXIF orientation applied. */
    juce::Image applyOrientation (const juce::Image& source, int orientation);

    /** Analyses an image. Never returns null; an invalid image gives silence. */
    SpectrumTable::Ptr analyse (const juce::Image& image, const juce::String& name);

    /** The picture the plugin starts with, so it makes a sound before anything is loaded. */
    juce::Image createDefaultImage();
}
