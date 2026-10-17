cmake_minimum_required(VERSION 3.24)

# Before project(): on Apple platforms project() creates an empty cache entry
# for the deployment target, and a set(... CACHE ...) after it never applies,
# so the plugin only loaded on the macOS it was built on (releases said minos
# 26.0). FORCE also fills that empty entry in existing build dirs; an explicit
# -DCMAKE_OSX_DEPLOYMENT_TARGET still wins. Check: otool -l <binary> | grep minos
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Release builds pass the tag's version, e.g. -DBUILDAPP_VERSION=1.2.0.
set(BUILDAPP_VERSION "0.2.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(BuildApp VERSION ${BUILDAPP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(BUILDAPP_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(BUILDAPP_BUILD_TESTS "Build the DSP unit tests" ON)
option(BUILDAPP_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: the buildup chain and the spectrum analysis. Plain C++20, no JUCE, so
# the tests build in seconds.
# ---------------------------------------------------------------------------
add_library(buildapp_core STATIC
    src/core/dsp/Fft.cpp
    src/core/dsp/Halfband.cpp
    src/core/fx/FilterStyles.cpp
    src/core/fx/CharacterFilter.cpp
    src/core/fx/CutFilters.cpp
    src/core/fx/PitchShifter.cpp
    src/core/fx/NoiseSource.cpp
    src/core/fx/Reverb.cpp
    src/core/fx/Buildup.cpp
    src/core/analysis/Spectrum.cpp
)
target_include_directories(buildapp_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(buildapp_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(buildapp_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(BUILDAPP_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(buildapp_assets SOURCES
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-Medium.ttf)

    juce_add_plugin(BuildApp
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.buildapp"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE BdAp
        FORMATS VST3 AU
        PRODUCT_NAME "BuildApp"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Filter
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${BUILDAPP_COPY_PLUGIN})

    # Listed once, used by the plugin and the dev tools.
    set(BUILDAPP_UI_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/Controls.cpp
        src/ui/SpectrumView.cpp
        src/ui/BuildTimeline.cpp)

    target_sources(BuildApp PRIVATE ${BUILDAPP_UI_SOURCES})
    target_include_directories(BuildApp PRIVATE src)

    target_compile_definitions(BuildApp PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(BuildApp
        PRIVATE
            buildapp_core
            buildapp_assets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    juce_add_console_app(buildapp_snapshot PRODUCT_NAME "buildapp_snapshot")
    target_sources(buildapp_snapshot PRIVATE tools/Snapshot.cpp ${BUILDAPP_UI_SOURCES})
    target_include_directories(buildapp_snapshot PRIVATE src tests)
    target_compile_definitions(buildapp_snapshot PRIVATE
        JucePlugin_Name="BuildApp"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(buildapp_snapshot PRIVATE
        buildapp_core buildapp_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: the plugin in a window with a built-in psytrance loop (or a
    # file) playing through it. ./start.sh runs it.
    juce_add_gui_app(buildapp_demo PRODUCT_NAME "BuildApp Demo")
    target_sources(buildapp_demo PRIVATE tools/Demo.cpp ${BUILDAPP_UI_SOURCES})
    target_include_directories(buildapp_demo PRIVATE src tests)
    target_compile_definitions(buildapp_demo PRIVATE
        JucePlugin_Name="BuildApp"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(buildapp_demo PRIVATE
        buildapp_core buildapp_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILDAPP_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(buildapp_tests
        tests/TestFilters.cpp
        tests/TestPitch.cpp
        tests/TestChain.cpp
        tests/TestPresets.cpp)
    target_include_directories(buildapp_tests PRIVATE tests)
    target_link_libraries(buildapp_tests PRIVATE buildapp_core Catch2::Catch2WithMain)

    # Dev tool: every preset's buildup rendered to WAV, with per-bar loudness,
    # brightness and width (how the presets were tuned).
    add_executable(buildapp_render tools/RenderPresets.cpp)
    target_include_directories(buildapp_render PRIVATE tests)
    target_link_libraries(buildapp_render PRIVATE buildapp_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(buildapp_tests)
endif()
