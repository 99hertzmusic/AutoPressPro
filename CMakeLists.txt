cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(AutoPressPro VERSION 1.0.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(AUTOPRESSPRO_COPY_PLUGIN_AFTER_BUILD "Copy built plugins into local plug-in folders" ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.13
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AutoPressPro
    COMPANY_NAME "99Hertz"
    BUNDLE_ID "com.99hertz.autopresspro"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${AUTOPRESSPRO_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Hz99
    PLUGIN_CODE APpr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "AutoPress Pro"
)

juce_add_binary_data(AutoPressProData SOURCES
    Resources/Fonts/BarlowCondensed-Bold.ttf
    Resources/Fonts/BarlowCondensed-SemiBold.ttf
    Resources/Fonts/BarlowCondensed-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)
set_target_properties(AutoPressProData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(AUTOPRESSPRO_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analysis.cpp
    Source/Licence/Licence.cpp
    Source/Licence/tweetnacl.c)

target_sources(AutoPressPro PRIVATE ${AUTOPRESSPRO_SOURCES})

target_compile_definitions(AutoPressPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_features(AutoPressPro PUBLIC cxx_std_17)

target_link_libraries(AutoPressPro
    PRIVATE AutoPressProData juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offline test: timing, clicks, state, Lite gating, licence keys, sound examples, UI snapshots (not shipped)
option(AUTOPRESSPRO_BUILD_TEST "Build the offline test app" OFF)
if(AUTOPRESSPRO_BUILD_TEST)
    juce_add_console_app(AutoPressProTest PRODUCT_NAME "AutoPressProTest")
    target_sources(AutoPressProTest PRIVATE Test/Main.cpp ${AUTOPRESSPRO_SOURCES})
    target_compile_definitions(AutoPressProTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
        AUTOPRESSPRO_TEST=1 JucePlugin_VersionString="1.0.0")
    target_compile_features(AutoPressProTest PRIVATE cxx_std_17)
    target_link_libraries(AutoPressProTest PRIVATE AutoPressProData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
