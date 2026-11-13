cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

project(PhotoToFrequency VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PTF_BUILD_TESTS "Build the headless render test" ON)

# JUCE is fetched at configure time. Point JUCE_SOURCE_DIR at a local checkout
# (-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE) to build offline.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(PTF_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PTF_FORMATS AU)
endif()

juce_add_plugin(PhotoToFrequency
    PRODUCT_NAME              "Photo To Frequency"
    COMPANY_NAME              "Photo To Frequency"
    BUNDLE_ID                 "com.phototofrequency.plugin"
    PLUGIN_MANUFACTURER_CODE  Ptfq
    PLUGIN_CODE               Ptf1
    FORMATS                   ${PTF_FORMATS}
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES           Instrument Synth
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(PhotoToFrequency PRIVATE
    Source/ImageSpectrum.cpp
    Source/SynthEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PhotoToFrequency PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PhotoToFrequency
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(PTF_BUILD_TESTS)
    juce_add_console_app(PhotoToFrequencyRenderTest)
    target_sources(PhotoToFrequencyRenderTest PRIVATE Tests/RenderTest.cpp)
    target_include_directories(PhotoToFrequencyRenderTest PRIVATE Source)
    target_link_libraries(PhotoToFrequencyRenderTest PRIVATE
        PhotoToFrequency
        juce::juce_audio_utils)

    # Loads the built .vst3 through JUCE's VST3 host, the way a DAW would.
    juce_add_console_app(PhotoToFrequencyHostCheck)
    target_sources(PhotoToFrequencyHostCheck PRIVATE Tests/HostCheck.cpp)
    target_compile_definitions(PhotoToFrequencyHostCheck PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PhotoToFrequencyHostCheck PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    add_dependencies(PhotoToFrequencyHostCheck PhotoToFrequency_VST3)
endif()
