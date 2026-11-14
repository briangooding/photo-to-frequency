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

## Photo To Frequency Split

A second plugin, built from the same code and installed alongside the first. It
does everything above and adds:

- **Part of the picture.** Drag a box on the picture to choose what plays. The
  scan head travels only between the box's left and right edges, and only the
  rows between its bottom and top become partials (the bottom edge is the note
  you play). Drag inside the box to move it, drag an edge or corner to resize
  it, drag outside it to draw a new one, double-click to select everything
  again. **Scan Time** is the time to cross the box, and the automatic level
  matching follows the box.
- **Oscillator envelope** - Attack, Decay, Sustain, Release for the volume of
  each note. With Sustain at 0 the picture plays as a plucked sound.
- **Filter** - Low-Pass, High-Pass or Band-Pass with **Cutoff** and
  **Resonance**. It is applied to each partial individually, so every note has
  its own filter.
- **Filter envelope** - its own Attack, Decay, Sustain, Release, moving the
  cutoff by **Env Amount** octaves (negative amounts sweep downwards).

With the filter fully open (Low-Pass at 20000 Hz, Env Amount 0, the defaults)
and the whole picture selected, it sounds the same as the original.

## Getting the plugin

Every push builds Windows, macOS (universal) and Linux versions: open the
latest run under the repository's **Actions** tab and download the artifact for
your system. Each download contains both plugins. Copy
`Photo To Frequency.vst3` and `Photo To Frequency Split.vst3` to:

| System | VST3 folder |
| --- | --- |
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` (the `.component` goes in `~/Library/Audio/Plug-Ins/Components`) |
| Linux | `~/.vst3` |

The builds are not signed with a developer certificate. On macOS, clear the
download quarantine once before rescanning in your DAW:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Photo To Frequency"*.vst3

Standalone apps are included for trying them without a DAW.

## Building

Needs CMake 3.22+ and a C++17 compiler (Visual Studio 2022, Xcode, or GCC/Clang).
JUCE 8 is downloaded during configuration.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The plugins end up in `build/PhotoToFrequency_artefacts/Release/VST3` and
`build/PhotoToFrequencySplit_artefacts/Release/VST3`.

Linux also needs the development packages for ALSA, X11 (Xext, Xrandr, Xinerama,
Xcursor, Xcomposite), FreeType and Fontconfig.

### Tests

Headless checks are built alongside the plugins:

    build/PhotoToFrequencyRenderTest_artefacts/Release/PhotoToFrequencyRenderTest [photo.jpg] [output-dir]
    build/PhotoToFrequencySplitRenderTest_artefacts/Release/PhotoToFrequencySplitRenderTest [photo.jpg] [output-dir]
    build/PhotoToFrequencyHostCheck_artefacts/Release/PhotoToFrequencyHostCheck "build/PhotoToFrequency_artefacts/Release/VST3/Photo To Frequency.vst3"

The render tests exercise image analysis, the synth, EXIF handling and state
saving (and, for Split, the region, envelopes and filter); given a photo and an
output directory they also write `render.wav` and a screenshot of the editor.
The host check loads a built VST3 through a VST3 host and plays a note.

## Layout

- `Source/PluginConfig.h` - which of the two plugins is being built
- `Source/ImageSpectrum.*` - loads the picture and reduces it to brightness/pan grids
- `Source/SynthEngine.*` - the additive synth
- `Source/PluginProcessor.*` - parameters, MIDI, state
- `Source/PluginEditor.*` - the interface

## Licence note

The plugin is built on [JUCE](https://juce.com), which is dual-licensed: AGPLv3
or a commercial JUCE licence. Distributing binaries means complying with one of
those.
