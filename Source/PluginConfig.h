#pragma once

/** The same sources build three plugins, each one adding to the one before:

    0  Photo To Frequency        plays the whole picture.
    1  Photo To Frequency Split  adds a selectable part of the picture, a full
                                 oscillator envelope, and a filter with its
                                 own envelope.
    2  Photo Freq                adds a sub-bass oscillator, tooltips, and a
                                 picture panel shaped like the picture.
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

    constexpr const char* displayName = PTF_VARIANT >= 2 ? "Photo Freq"
                                      : PTF_VARIANT == 1 ? "Photo To Frequency Split"
                                                         : "Photo To Frequency";

    constexpr const char* stateType = PTF_VARIANT >= 2 ? "PhotoFreq"
                                    : PTF_VARIANT == 1 ? "PhotoToFrequencySplit"
                                                       : "PhotoToFrequency";
}
