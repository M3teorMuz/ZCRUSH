cmake_minimum_required(VERSION 3.22)

# Статическая среда выполнения MSVC: плагин запускается без установки VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Pitcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Pitcher
    COMPANY_NAME              "PitcherAudio"
    PLUGIN_MANUFACTURER_CODE  Pchr
    PLUGIN_CODE               Pit1
    FORMATS                   VST3
    PRODUCT_NAME              "Pitcher"
    VST3_CATEGORIES           Fx "Pitch Shift"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(Pitcher PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# ---- Дизайн (подключается позже) --------------------------------------------
# Когда в папке Assets появятся картинки/шрифты, раскомментируй блок ниже:
#
# file(GLOB PITCHER_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png"
#                                            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.svg"
#                                            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.ttf")
# juce_add_binary_data(PitcherAssets SOURCES ${PITCHER_ASSETS})
# target_link_libraries(Pitcher PRIVATE PitcherAssets)
# -----------------------------------------------------------------------------

target_compile_definitions(Pitcher PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Pitcher
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)
