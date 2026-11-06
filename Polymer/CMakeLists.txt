cmake_minimum_required(VERSION 3.22)

# static MSVC runtime -> the .vst3 does not need the Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# macOS: universal binary (Apple Silicon + Intel), minimum macOS 10.13
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(Polymer VERSION 1.0.0)

# formats: VST3 everywhere, plus Audio Unit on macOS
if(APPLE)
    set(PLUGIN_FORMATS VST3 AU)
else()
    set(PLUGIN_FORMATS VST3)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected in ./JUCE (the GitHub workflow clones it; locally: git clone --branch 8.0.12 https://github.com/juce-framework/JUCE.git)
add_subdirectory(JUCE)

juce_add_plugin(Polymer
    COMPANY_NAME "Polymer"
    PLUGIN_MANUFACTURER_CODE Plym
    PLUGIN_CODE Plmr
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Polymer"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Polymer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Sampler.h
    Source/Transmuter.h
    Source/Dsp.h)

target_compile_definitions(Polymer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Polymer
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless tests (no host needed): cmake -DPOLYMER_BUILD_TESTS=ON ..
option(POLYMER_BUILD_TESTS "Build processor/editor tests" OFF)
if(POLYMER_BUILD_TESTS)
    foreach(t proc_test render_editor junk_test rt_test)
        add_executable(${t} tests/${t}.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_compile_definitions(${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(${t} PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
    endforeach()
endif()
