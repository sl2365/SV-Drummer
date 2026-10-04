cmake_minimum_required(VERSION 3.25)

project(SVDrummer VERSION 1.14.4 LANGUAGES C CXX)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../_Tools/JUCE/_8.0.15")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE 8.0.15 was not found. Expected: ${JUCE_DIR}/CMakeLists.txt")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(SVDrummer
    VERSION 1.14.4
    COMPANY_NAME "sl23"
    BUNDLE_ID "com.sl23.svdrummer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE sl23
    FORMATS VST3
    VST3_CATEGORIES Instrument Drum
    PRODUCT_NAME "SV-Drummer"
    # Permanent sl23 plug-in identifier. Do not change after release.
    PLUGIN_CODE svd1)

juce_generate_juce_header(SVDrummer)

juce_add_binary_data(SVDrummerAssets
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/../Resources/logo.png")

target_sources(SVDrummer
    PRIVATE
        PluginEditor.cpp
        PluginEditor.h
        PluginProcessor.cpp
        PluginProcessor.h)

target_compile_features(SVDrummer PRIVATE cxx_std_17)

target_compile_definitions(SVDrummer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SVDrummer
    PRIVATE
        SVDrummerAssets
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
