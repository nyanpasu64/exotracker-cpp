cmake_minimum_required(VERSION 3.8.0)

project(exotracker)

#Initialize environment
set(RTAUDIO_BUILD_STATIC_LIBS TRUE)

# This can be used to enable asan (-fsanitize=memory/undefined).
include(cmake_user_override.cmake OPTIONAL)
#End initialize environment


set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(RTAUDIO_BUILD_TESTING FALSE)
add_subdirectory(
    "3rdparty/rtaudio"
    EXCLUDE_FROM_ALL
)

add_subdirectory("3rdparty/fmt_" EXCLUDE_FROM_ALL)

# Configure global settings
include_directories(src 3rdparty 3rdparty/fmt_/include)
set(CMAKE_CXX_STANDARD 20)
if (WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    if (MSVC)
        add_compile_options(/EHsc)
    endif ()
endif ()

# Core library, does not use Qt
add_library(exotracker-lib
        ## Audio

        # Library (widely used, uncoupled)
        3rdparty/Blip_Buffer/Blip_Buffer.cpp
        src/audio/event_queue.cpp

        # Common files (tightly coupled)
        src/audio/audio_common.cpp
        src/audio/synth_common.cpp
        src/audio/synth/sequencer.cpp
        src/audio/synth/music_driver_common.cpp

        src/chip_kinds.cpp

        # Implementation files
        src/audio/synth.cpp

        # 2A03 audio
        3rdparty/nsfplay/xgm/devices/Sound/nes_apu.cpp
        3rdparty/nsfplay/xgm/devices/Sound/nes_dmc.cpp
        src/audio/synth/nes_2a03.cpp
        src/audio/synth/music_driver/driver_2a03.cpp

        # Etc.
        src/doc.cpp
        src/doc/events.cpp
        )
target_link_libraries(exotracker-lib PRIVATE rtaudio)
target_link_libraries(exotracker-lib PUBLIC fmt::fmt)
target_compile_definitions(exotracker-lib PRIVATE EXO_LIB)

# GUI editor, uses Qt
add_executable(exotracker-bin
        # GUI/audio thread-safe locking
        src/util/sync.cpp
        src/audio/output.cpp

        # GUI
        src/gui/history.cpp
        src/gui/command_common.cpp
        src/gui/main_window.cpp
        src/gui/pattern_editor/pattern_editor_panel.cpp
        src/main.cpp
        )
target_link_libraries(exotracker-bin PRIVATE exotracker-lib rtaudio Qt5::Widgets)
target_compile_definitions(exotracker-bin PRIVATE EXO_BIN)

# Unit tests
add_executable(exotracker-tests
        tests/run_tests.cpp
        tests/test_blip_buffer.cpp
        tests/test_document.cpp
        tests/audio/test_event_queue.cpp
        tests/audio/test_synth.cpp
        tests/test_utils/parameterize.cpp
        tests/test_utils/test_parameterize.cpp

        src/doc/events.cpp
        src/audio/synth/sequencer.cpp
        src/audio/synth/music_driver/driver_2a03.cpp

        # No tests yet, why is it included?
        src/gui/history.cpp
        )
target_include_directories(exotracker-tests PUBLIC tests)
target_link_libraries(exotracker-tests PRIVATE exotracker-lib)
target_compile_definitions(exotracker-tests PRIVATE UNITTEST)
