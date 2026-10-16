cmake_minimum_required(VERSION 3.22)
project(PTZR_FORGE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")      # universal: Apple Silicon + Intel
option(PTZR_ENABLE_MIDI "Accept MIDI CC / program change for parameter control" ON)
option(PTZR_COPY_AFTER_BUILD "Copy plugins to the user plugin folders after building" OFF)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(PTZR_ENABLE_MIDI)
  set(PTZR_AU_TYPE kAudioUnitType_MusicEffect)   # AU effects that take MIDI must be 'aumf'
else()
  set(PTZR_AU_TYPE kAudioUnitType_Effect)
endif()
if(APPLE)
  set(PTZR_FORMATS VST3 AU Standalone)
else()
  set(PTZR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(PTZRForge
  COMPANY_NAME "PTZR.PLUGINS"
  BUNDLE_ID com.ptzr.forge
  PLUGIN_MANUFACTURER_CODE Ptzr
  PLUGIN_CODE Frg1
  FORMATS ${PTZR_FORMATS}
  PRODUCT_NAME "PTZR FORGE"
  PLUGIN_NAME "PTZR.FORGE"   # name shown by the host; the bundle file name avoids a dot (breaks the VST3 manifest tool)
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT ${PTZR_ENABLE_MIDI}
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Fx Distortion
  AU_MAIN_TYPE ${PTZR_AU_TYPE}
  COPY_PLUGIN_AFTER_BUILD ${PTZR_COPY_AFTER_BUILD}
  NEEDS_WEB_BROWSER FALSE
  NEEDS_CURL FALSE)

juce_add_binary_data(PTZRForgeAssets SOURCES
  assets/bg.png assets/tape_tube.png assets/tape_transistor.png assets/tape_opto.png assets/tape_diode.png)

target_sources(PTZRForge PRIVATE
  Source/PluginProcessor.cpp Source/PluginEditor.cpp)

target_compile_definitions(PTZRForge PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PTZRForge
  PRIVATE PTZRForgeAssets juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offline DSP tests (no JUCE):  cmake --build build --target dsp_test && ./build/dsp_test
add_executable(dsp_test tests/dsp_test.cpp)
target_include_directories(dsp_test PRIVATE Source/DSP)
