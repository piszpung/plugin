cmake_minimum_required(VERSION 3.22)

# static MSVC runtime -> the .vst3 does not need the Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Ember VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected in ./JUCE (the GitHub workflow clones it; locally: git clone --branch 8.0.12 https://github.com/juce-framework/JUCE.git)
add_subdirectory(JUCE)

juce_add_plugin(Ember
    COMPANY_NAME "Ember"
    PLUGIN_MANUFACTURER_CODE Embr
    PLUGIN_CODE Emsa
    FORMATS VST3
    PRODUCT_NAME "Ember"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Ember PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SatEngine.h
    Source/Modulation.h)

target_compile_definitions(Ember PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Ember
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless tests (no host needed): cmake -DEMBER_BUILD_TESTS=ON ..
option(EMBER_BUILD_TESTS "Build processor/editor tests" OFF)
if(EMBER_BUILD_TESTS)
    foreach(t proc_test render_editor)
        add_executable(${t} tests/${t}.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_compile_definitions(${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(${t} PRIVATE juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
endif()
