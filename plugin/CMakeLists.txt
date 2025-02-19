cmake_minimum_required(VERSION 3.21)

project(MultibandReverb VERSION 0.1.0)

juce_add_plugin(${PROJECT_NAME}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE JamesAlt
    PLUGIN_CODE AUPL
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "MultibandReverb"
)

# Sets the source files of the plugin project.
# target_sources(${PROJECT_NAME}
#     PRIVATE
#         source/PluginEditor.cpp
#         source/PluginProcessor.cpp
#         include/MultibandReverb/PluginEditor.h
#         include/MultibandReverb/PluginProcessor.h
# )

# # Sets the include directories of the plugin project.
# target_include_directories(${PROJECT_NAME}
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}/include
# )

# Collect all source files
file(GLOB_RECURSE SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
)

# Collect all header files
file(GLOB_RECURSE HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

target_sources(MultibandReverb
    PRIVATE
        ${SOURCES}
        ${HEADERS}
)

target_include_directories(MultibandReverb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)


# Links to all necessary dependencies. The present ones are recommended by JUCE.
# If you use one of the additional modules, like the DSP module, you need to specify it here.
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# These definitions are recommended by JUCE.
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Enables all warnings and treats warnings as errors.
# This needs to be set up only for your projects, not 3rd party
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# In Visual Studio this command provides a nice grouping of source files in "filters".
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/..)