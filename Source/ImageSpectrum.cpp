#include "ImageSpectrum.h"

namespace ImageSpectrum
{

//==============================================================================
int readExifOrientation (const void* rawData, size_t size)
{
    const auto* d = static_cast<const uint8_t*> (rawData);

    if (size < 4 || d[0] != 0xff || d[1] != 0xd8)
        return 1;

    size_t pos = 2;

    while (pos + 4 <= size)
    {
        if (d[pos] != 0xff)
            return 1;

        const auto marker = d[pos + 1];

        if (marker == 0xff)   { ++pos; continue; }            // padding
        if (marker == 0xda || marker == 0xd9)  return 1;       // image data starts: no EXIF

        const size_t segmentLength = ((size_t) d[pos + 2] << 8) | d[pos + 3];

        if (segmentLength < 2 || pos + 2 + segmentLength > size)
            return 1;

        if (marker == 0xe1 && segmentLength >= 16
             && std::memcmp (d + pos + 4, "Exif\0\0", 6) == 0)
        {
            const auto* tiff = d + pos + 10;
            const size_t tiffSize = segmentLength - 8;

            const bool littleEndian = (tiff[0] == 'I' && tiff[1] == 'I');
            const bool bigEndian    = (tiff[0] == 'M' && tiff[1] == 'M');

            if (! (littleEndian || bigEndian))
                return 1;

            auto read16 = [&] (size_t o) -> uint32_t
            {
                return littleEndian ? (uint32_t) (tiff[o] | (tiff[o + 1] << 8))
                                    : (uint32_t) ((tiff[o] << 8) | tiff[o + 1]);
            };

            auto read32 = [&] (size_t o) -> uint32_t
            {
                return littleEndian ? (read16 (o) | (read16 (o + 2) << 16))
                                    : ((read16 (o) << 16) | read16 (o + 2));
            };

            if (tiffSize < 8 || read16 (2) != 42)
                return 1;

            const size_t ifd = read32 (4);

            if (ifd + 2 > tiffSize)
                return 1;

            const size_t numEntries = read16 (ifd);

            for (size_t i = 0; i < numEntries; ++i)
            {
                const size_t entry = ifd + 2 + i * 12;

                if (entry + 12 > tiffSize)
                    return 1;

                if (read16 (entry) == 0x0112)
                {
                    const auto value = (int) read16 (entry + 8);
                    return (value >= 1 && value <= 8) ? value : 1;
                }
            }

            return 1;
        }

        pos += 2 + segmentLength;
    }

    return 1;
}

juce::Image applyOrientation (const juce::Image& source, int orientation)
{
    if (! source.isValid() || orientation <= 1 || orientation > 8)
        return source;

    const int w = source.getWidth();
    const int h = source.getHeight();
    const bool swapsAxes = orientation >= 5;

    juce::Image result (source.getFormat(), swapsAxes ? h : w, swapsAxes ? w : h, true);

    const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (result, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < result.getHeight(); ++y)
    {
        for (int x = 0; x < result.getWidth(); ++x)
        {
            int sx = x, sy = y;

            switch (orientation)
            {
                case 2: sx = w - 1 - x;                 break;   // mirrored
                case 3: sx = w - 1 - x; sy = h - 1 - y; break;   // upside down
                case 4:                 sy = h - 1 - y; break;   // flipped
                case 5: sx = y;         sy = x;         break;   // transposed
                case 6: sx = y;         sy = h - 1 - x; break;   // needs 90 degrees clockwise
                case 7: sx = w - 1 - y; sy = h - 1 - x; break;   // transverse
                case 8: sx = w - 1 - y; sy = x;         break;   // needs 90 degrees anticlockwise
                default: break;
            }

            dst.setPixelColour (x, y, src.getPixelColour (sx, sy));
        }
    }

    return result;
}

static juce::Image shrinkToFit (const juce::Image& image, int maxSize)
{
    const int w = image.getWidth();
    const int h = image.getHeight();

    if (w <= maxSize && h <= maxSize)
        return image;

    const double scale = (double) maxSize / (double) juce::jmax (w, h);

    return image.rescaled (juce::jmax (1, juce::roundToInt (w * scale)),
                           juce::jmax (1, juce::roundToInt (h * scale)),
                           juce::Graphics::highResamplingQuality);
}

juce::Image loadFile (const juce::File& file)
{
    juce::MemoryBlock data;

    if (! file.existsAsFile() || ! file.loadFileAsData (data) || data.isEmpty())
        return {};

    auto image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());

    if (! image.isValid())
        return {};

    // Shrink first so rotating a 40-megapixel photo stays cheap.
    image = shrinkToFit (image, maxPreviewSize);

    return applyOrientation (image, readExifOrientation (data.getData(), data.getSize()));
}

//==============================================================================
SpectrumTable::Ptr analyse (const juce::Image& sourceImage, const juce::String& name)
{
    static std::atomic<uint32_t> nextSerial { 1 };

    SpectrumTable::Ptr table (new SpectrumTable());
    table->name = name;
    table->serial = nextSerial.fetch_add (1);

    for (int level = 0; level < SpectrumTable::numLevels; ++level)
    {
        const auto cells = (size_t) (SpectrumTable::rowsForLevel (level) * SpectrumTable::numColumns);
        table->brightness[(size_t) level].assign (cells, 0.0f);
        table->pan[(size_t) level].assign (cells, 0.0f);
    }

    if (! sourceImage.isValid())
        return table;

    table->preview = shrinkToFit (sourceImage, maxPreviewSize).convertedToFormat (juce::Image::RGB);

    const int w = table->preview.getWidth();
    const int h = table->preview.getHeight();

    // Per-pixel brightness and pan, top row first.
    std::vector<float> pixelBrightness ((size_t) (w * h)), pixelPan ((size_t) (w * h));

    {
        const juce::Image::BitmapData pixels (table->preview, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
                const auto i = (size_t) (y * w + x);

                // Strongest channel rather than luminance, so a saturated blue
                // is as loud as a saturated yellow.
                const float value = juce::jmax (r, g, b);
                pixelBrightness[i] = value;
                pixelPan[i] = (b - r) / (value + 1.0e-4f);
            }
        }
    }

    // Box-average the pixels covered by each cell. Where the picture is smaller
    // than the grid, cells simply repeat the nearest pixel.
    for (int level = 0; level < SpectrumTable::numLevels; ++level)
    {
        const int rows = SpectrumTable::rowsForLevel (level);
        auto& outBrightness = table->brightness[(size_t) level];
        auto& outPan = table->pan[(size_t) level];

        for (int row = 0; row < rows; ++row)
        {
            // Row 0 is the lowest pitch, which is the bottom of the picture.
            const int imageRow = rows - 1 - row;
            const int y0 = juce::jlimit (0, h - 1, (imageRow * h) / rows);
            const int y1 = juce::jlimit (y0 + 1, h, ((imageRow + 1) * h) / rows);

            for (int col = 0; col < SpectrumTable::numColumns; ++col)
            {
                const int x0 = juce::jlimit (0, w - 1, (col * w) / SpectrumTable::numColumns);
                const int x1 = juce::jlimit (x0 + 1, w, ((col + 1) * w) / SpectrumTable::numColumns);

                float sumBrightness = 0.0f, sumPan = 0.0f, panWeight = 0.0f;

                for (int y = y0; y < y1; ++y)
                {
                    for (int x = x0; x < x1; ++x)
                    {
                        const auto i = (size_t) (y * w + x);
                        sumBrightness += pixelBrightness[i];

                        // Bright pixels decide where the cell sits in the stereo field.
                        sumPan    += pixelPan[i] * pixelBrightness[i];
                        panWeight += pixelBrightness[i];
                    }
                }

                const auto cell = (size_t) (row * SpectrumTable::numColumns + col);
                outBrightness[cell] = sumBrightness / (float) ((y1 - y0) * (x1 - x0));
                outPan[cell] = panWeight > 1.0e-6f ? juce::jlimit (-1.0f, 1.0f, sumPan / panWeight) : 0.0f;
            }
        }
    }

    return table;
}

//==============================================================================
juce::Image createDefaultImage()
{
    juce::Image image (juce::Image::RGB, 768, 384, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::black);

    const auto w = (float) image.getWidth();
    const auto h = (float) image.getHeight();

    // A few rising and falling arcs: a slow chord that opens up and closes again.
    for (int i = 0; i < 7; ++i)
    {
        const float t = (float) i / 6.0f;
        juce::Path arc;
        const float baseY = h * (0.92f - 0.11f * (float) i);

        arc.startNewSubPath (0.0f, baseY);
        arc.cubicTo (w * 0.3f, baseY - h * 0.35f * t,
                     w * 0.7f, baseY + h * 0.25f * t,
                     w, baseY - h * 0.1f * t);

        g.setColour (juce::Colour::fromHSV (0.58f + 0.45f * t, 0.75f, 1.0f, 1.0f));
        g.strokePath (arc, juce::PathStrokeType (3.0f + 5.0f * (1.0f - t)));
    }

    // Soft fade at both ends so the loop point doesn't click.
    juce::ColourGradient fade (juce::Colours::black, 0.0f, 0.0f, juce::Colours::black, w, 0.0f, false);
    fade.addColour (0.06, juce::Colours::transparentBlack);
    fade.addColour (0.94, juce::Colours::transparentBlack);
    g.setGradientFill (fade);
    g.fillAll();

    return image;
}

} // namespace ImageSpectrum
