cmake_minimum_required(VERSION 3.22)

# static MSVC runtime -> the .vst3 does not need the Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Stretchy VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected in ./JUCE (the GitHub workflow clones it, locally: git clone --branch 8.0.12 https://github.com/juce-framework/JUCE.git)
add_subdirectory(JUCE)

juce_add_plugin(Stretchy
    COMPANY_NAME "Stretchy"
    PLUGIN_MANUFACTURER_CODE Strc
    PLUGIN_CODE Stry
    FORMATS VST3
    PRODUCT_NAME "Stretchy"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Stretchy PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/StretchEngine.h)

target_compile_definitions(Stretchy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Stretchy
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless test of the processor (no host needed): cmake -DSTRETCHY_BUILD_TESTS=ON ..
option(STRETCHY_BUILD_TESTS "Build processor test" OFF)
if(STRETCHY_BUILD_TESTS)
    add_executable(proc_test tests/proc_test.cpp Source/PluginProcessor.cpp)
    target_compile_definitions(proc_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(proc_test PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
endif()
