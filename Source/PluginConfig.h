#pragma once

/** The same sources build five plugins, each one adding to the one before:

    0  Photo To Frequency        plays the whole picture.
    1  Photo To Frequency Split  adds a selectable part of the picture, a full
                                 oscillator envelope, and a filter with its
                                 own envelope.
    2  Photo Freq                adds a sub-bass oscillator, tooltips, and a
                                 picture panel shaped like the picture.
    3  Photo Freq 2              adds patches (settings, picture and selected
                                 part saved to a file), reverse scanning, a
                                 Stereo Width with a choice of what drives it,
                                 and Env Amount in percent.
    4  Photo Freq 3              adds Follow: the picture under the scan head
                                 can drive the sub oscillator's level.
*/
#ifndef PTF_VARIANT
 #define PTF_VARIANT 0
#endif

namespace PluginConfig
{
    /** Region, envelopes and filter. */
    constexpr bool isSplit = (PTF_VARIANT >= 1);

    /** Sub oscillator, tooltips, picture panel that follows the picture's shape. */
    constexpr bool isFreq = (PTF_VARIANT >= 2);

    /** Patches, reverse scan, stereo source, Env Amount in percent. */
    constexpr bool isFreq2 = (PTF_VARIANT >= 3);

    /** Sub oscillator can follow the picture. */
    constexpr bool isFreq3 = (PTF_VARIANT >= 4);

    constexpr const char* displayName = PTF_VARIANT >= 4 ? "Photo Freq 3"
                                      : PTF_VARIANT == 3 ? "Photo Freq 2"
                                      : PTF_VARIANT == 2 ? "Photo Freq"
                                      : PTF_VARIANT == 1 ? "Photo To Frequency Split"
                                                         : "Photo To Frequency";

    constexpr const char* stateType = PTF_VARIANT >= 4 ? "PhotoFreq3"
                                    : PTF_VARIANT == 3 ? "PhotoFreq2"
                                    : PTF_VARIANT == 2 ? "PhotoFreq"
                                    : PTF_VARIANT == 1 ? "PhotoToFrequencySplit"
                                                       : "PhotoToFrequency";

    /** Patches from the previous version that this one can also open (same parameters, fewer of them). */
    constexpr const char* olderPatchStateType = PTF_VARIANT >= 4 ? "PhotoFreq2" : "";

    /** File extension of patches (Photo Freq 2 onwards). */
    constexpr const char* patchExtension = ".pfpatch";
}
