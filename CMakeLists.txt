cmake_minimum_required(VERSION 3.8.0)

project(exotracker)

#Initialize environment
set(portaudio "portaudio_static")
set(portaudiocpp "portaudiocpp_static")

include(cmake_user_override.txt OPTIONAL)
#End initialize environment


set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)
if(find_portaudio)
    find_package(portaudio REQUIRED)
else()
    add_subdirectory(
        "3rdparty/portaudio"
        EXCLUDE_FROM_ALL
    )
    add_subdirectory(
        "3rdparty/portaudio/bindings/cpp"
        EXCLUDE_FROM_ALL
    )
endif()

# Configure global settings
include_directories(src 3rdparty)
set(CMAKE_CXX_STANDARD 20)
if (WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    if (MSVC)
        add_compile_options(/EHsc)
    endif ()
endif ()

# The immer library creates some extremely noisy warnings.
add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)

# Core library, does not use Qt
add_library(exotracker-lib
        ## Audio

        # Library (widely used, uncoupled)
        3rdparty/Blip_Buffer/Blip_Buffer.cpp
        src/audio/event_queue.cpp

        # Common files (tightly coupled)
        src/audio/audio_common.cpp
        src/audio/synth_common.cpp
        src/audio/synth/music_engine_common.cpp

        # Implementation files
        src/audio/output.cpp
        src/audio/synth.cpp
        src/audio/synth/music_engine.cpp

        # 2A03 audio
        3rdparty/nsfplay/xgm/devices/Sound/nes_apu.cpp
        3rdparty/nsfplay/xgm/devices/Sound/nes_dmc.cpp
        src/audio/synth/nes_2a03.cpp
        src/audio/synth/music_engine/nes_2a03.cpp

        # Etc.
        src/document.cpp
        src/sequencer/sequencer.cpp
        )
target_link_libraries(exotracker-lib PRIVATE ${portaudio} ${portaudiocpp})


# GUI editor, uses Qt
add_executable(exotracker-bin
        # GUI
        src/gui/history.cpp
        src/gui/main_window.cpp
        src/gui/pattern_editor/pattern_editor_panel.cpp
        src/main.cpp
        )
target_link_libraries(exotracker-bin PRIVATE exotracker-lib ${portaudio} ${portaudiocpp} Qt5::Widgets)

# Unit tests
add_executable(exotracker-tests
        tests/run_tests.cpp
        tests/test_blip_buffer.cpp
        tests/test_document.cpp
        tests/audio/test_event_queue.cpp

        src/gui/history.cpp
        )
target_include_directories(exotracker-tests PUBLIC tests)
target_link_libraries(exotracker-tests PRIVATE exotracker-lib)
