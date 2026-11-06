cmake_minimum_required(VERSION 3.22)
project(ZCRUSH VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# статическая среда выполнения: плагину не нужны доп. DLL на компьютере пользователя
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ZCRUSH
    COMPANY_NAME "ZCRUSH Lab"
    PLUGIN_MANUFACTURER_CODE Zcrl
    PLUGIN_CODE Zcr1
    FORMATS VST3
    PRODUCT_NAME "ZCRUSH"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

juce_add_binary_data(ZCrushFonts SOURCES
    Fonts/LiberationMono-Bold.ttf
    Fonts/LiberationSans-Bold.ttf)

target_sources(ZCRUSH PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ZCRUSH PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ZCRUSH
    PRIVATE
        juce::juce_audio_utils
        ZCrushFonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
