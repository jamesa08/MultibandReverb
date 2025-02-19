    cmake_minimum_required(VERSION 3.21)

    project(MultibandReverb VERSION 0.1.0)

    juce_add_plugin(${PROJECT_NAME}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        PLUGIN_MANUFACTURER_CODE Jalt
        PLUGIN_CODE AUPL
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "MultibandReverb"
    )

    # Collect all source files
    file(GLOB_RECURSE SOURCES 
        "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
    )

    # Collect all header files
    file(GLOB_RECURSE HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/MultibandReverb/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/MultibandReverb/*.hpp"
    )

    target_sources(MultibandReverb
        PRIVATE
            ${SOURCES}
            ${HEADERS}
    )

    # The key change is here - we need the parent 'include' directory
    target_include_directories(MultibandReverb
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    juce_generate_juce_header(${PROJECT_NAME})


    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_basics
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_audio_plugin_client
            juce::juce_audio_processors
            juce::juce_core
            juce::juce_data_structures
            juce::juce_dsp  # Since you're using DSP features
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )


    target_compile_definitions(${PROJECT_NAME}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    if (MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/..)