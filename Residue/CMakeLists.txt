cmake_minimum_required(VERSION 3.22)

# static MSVC runtime -> the .vst3 does not need the Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# macOS: universal binary (Apple Silicon + Intel), minimum macOS 10.13
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(Residue VERSION 1.0.0)

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

juce_add_plugin(Residue
    COMPANY_NAME "Residue"
    PLUGIN_MANUFACTURER_CODE Rsdu
    PLUGIN_CODE Rsec
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Residue"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Delay
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Residue PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/EchoEngine.h)

target_compile_definitions(Residue PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Residue
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless tests (no host needed): cmake -DRESIDUE_BUILD_TESTS=ON ..
option(RESIDUE_BUILD_TESTS "Build processor/editor tests" OFF)
if(RESIDUE_BUILD_TESTS)
    foreach(t proc_test render_editor)
        add_executable(${t} tests/${t}.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_compile_definitions(${t} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(${t} PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    endforeach()
endif()
