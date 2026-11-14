#pragma once

/** The same sources build two plugins:

    - Photo To Frequency        (PTF_SPLIT=0) plays the whole picture.
    - Photo To Frequency Split  (PTF_SPLIT=1) adds a selectable part of the
      picture, a full oscillator envelope, and a filter with its own envelope.
*/
#ifndef PTF_SPLIT
 #define PTF_SPLIT 0
#endif

namespace PluginConfig
{
    constexpr bool isSplit = (PTF_SPLIT != 0);
}
