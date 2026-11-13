# Photo To Frequency

A VST3 instrument that turns a picture into sound. Load a JPG (or PNG/GIF) and
the plugin plays it like a spectrogram: a scan head sweeps across the picture
and every row becomes one sine partial.

| In the picture | In the sound |
| --- | --- |
| Left to right | Time - the scan head travels across in **Scan Time** seconds |
| Bottom to top | Pitch - the bottom row is the note you play, rows above are higher |
| Brightness (strongest colour channel) | Loudness of that partial |
| Red vs. blue | Stereo position - red leans left, blue leans right |

It is polyphonic (8 voices). Each MIDI note starts its own scan, transposed to
that note. The **Play** button holds a C3 so you can listen without a keyboard.

## Controls

- **Load Image...** or drag a file onto the picture. Camera JPEGs are rotated
  according to their EXIF orientation. A reduced copy of the picture is saved
  inside the DAW project, so the project still plays if the file moves.
- **Scan Mode** - Loop, Ping-Pong (back and forth) or One-Shot (once, then release).
- **Tuning** - how rows map to pitch:
  - *Harmonic*: row *n* is the *n*-th harmonic of the played note. The picture
    becomes the changing timbre of one pitched tone.
  - *Free*: rows are spread evenly over **Range** octaves above the note. Closest
    to a true spectrogram; dense pictures sound noisy.
  - *Chromatic / Major / Minor / Pentatonic*: as Free, but each row snaps to the
    nearest note of the scale, so the picture plays chords and melodies.
- **Partials** - 16, 32, 64 or 128 rows.
- **Contrast** - higher values keep only the brightest parts. Photos are bright
  almost everywhere, so this is the main control for going from a wash to
  distinct shapes.
- **Threshold** - anything darker than this is silent.
- **Invert** - dark plays, bright is silent.
- **Tilt** - how much quieter the higher partials are.
- **Colour Width** - how far colour moves sound left and right. 0 is mono.
- **Attack / Release**, **Gain**.

The level is matched automatically so that the loudest moment of any picture
comes out at roughly the same volume.

## Getting the plugin

Every push builds Windows, macOS (universal) and Linux versions: open the
latest run under the repository's **Actions** tab and download the artifact for
your system. Copy `Photo To Frequency.vst3` to:

| System | VST3 folder |
| --- | --- |
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` (the `.component` goes in `~/Library/Audio/Plug-Ins/Components`) |
| Linux | `~/.vst3` |

The builds are not signed with a developer certificate. On macOS, clear the
download quarantine once before rescanning in your DAW:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Photo To Frequency.vst3"

A standalone app is included for trying it without a DAW.

## Building

Needs CMake 3.22+ and a C++17 compiler (Visual Studio 2022, Xcode, or GCC/Clang).
JUCE 8 is downloaded during configuration.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The plugin ends up in `build/PhotoToFrequency_artefacts/Release/VST3`.

Linux also needs the development packages for ALSA, X11 (Xext, Xrandr, Xinerama,
Xcursor, Xcomposite), FreeType and Fontconfig.

### Tests

Two headless checks are built alongside the plugin:

    build/PhotoToFrequencyRenderTest_artefacts/Release/PhotoToFrequencyRenderTest [photo.jpg] [output-dir]
    build/PhotoToFrequencyHostCheck_artefacts/Release/PhotoToFrequencyHostCheck "build/PhotoToFrequency_artefacts/Release/VST3/Photo To Frequency.vst3"

The first exercises image analysis, the synth, EXIF handling and state saving;
given a photo and an output directory it also writes `render.wav` and a
screenshot of the editor. The second loads the built VST3 through a VST3 host
and plays a note.

## Layout

- `Source/ImageSpectrum.*` - loads the picture and reduces it to brightness/pan grids
- `Source/SynthEngine.*` - the additive synth
- `Source/PluginProcessor.*` - parameters, MIDI, state
- `Source/PluginEditor.*` - the interface

## Licence note

The plugin is built on [JUCE](https://juce.com), which is dual-licensed: AGPLv3
or a commercial JUCE licence. Distributing binaries means complying with one of
those.
