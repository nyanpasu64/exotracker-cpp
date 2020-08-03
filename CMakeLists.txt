cmake_minimum_required(VERSION 3.8.0)

#Remove /W3 from MSVC global flags.
cmake_policy(SET CMP0092 NEW)
project(exotracker)

#Initialize environment
set(RTAUDIO_BUILD_STATIC_LIBS TRUE)
set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build (rtaudio, fmt, ...?) as shared library")

# On MSVC, CMake defaults to dynamic msvcrt (and exotracker uses that default).
# But if you link RtAudio statically, it forces static msvcrt for itself
# (but not targets including RtAudio).
# This causes LNK2038 errors: https://github.com/thestk/rtaudio/issues/256
# I patched RtAudio so you can specify whether to make msvcrt static or dynamic.
set(RTAUDIO_STATIC_MSVCRT FALSE)

include(cmake_user_begin.cmake OPTIONAL)
#End initialize environment


set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(RTAUDIO_BUILD_TESTING FALSE)
add_subdirectory(
    "3rdparty/rtaudio"
    EXCLUDE_FROM_ALL
)

add_subdirectory("3rdparty/fmt_" EXCLUDE_FROM_ALL)

add_subdirectory("3rdparty/qkeycode" EXCLUDE_FROM_ALL)

# Configure global settings
include_directories(src)
include_directories(SYSTEM 3rdparty 3rdparty/fmt_/include)
set(CMAKE_CXX_STANDARD 20)
if (WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    if (MSVC)
        add_compile_options(/EHsc)
        add_compile_options(/permissive-)
        # rtaudio_c.cpp uses strncpy and the like. I don't actually use that file lol
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    endif ()
endif ()

if (CMAKE_BUILD_TYPE STREQUAL DEBUG)
    add_compile_definitions(DEBUG=1)
endif()

# https://foonathan.net/2018/10/cmake-warnings/
# more warning flags at https://stackoverflow.com/questions/5088460
set(options-3rdparty $<$<CXX_COMPILER_ID:MSVC>:/W3>)
set(
    options
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wconversion -Wno-unused-function -Wno-unused-parameter
        -Werror=return-type
    >
    $<$<CXX_COMPILER_ID:MSVC>:
#        /W3
        /W4
        /wd4100  # allow unused parameters
        /wd4505  # allow unused functions
        /wd4456 /wd4457  # allow shadowing

        # Loosen warnings on external libraries.
        /experimental:external /external:anglebrackets
        /external:W3
        /wd4251  # Qt has DLL linkage warnings when using /external:anglebrackets
        # Omit /external:templates- since Blip_Buffer has compile-time if statements
    >
    # /W4 causes lots of warnings in blip-buffer and Qt.
    # /experimental:external /external:anglebrackets causes lots of C4251 warnings:
    #   https://developercommunity.visualstudio.com/content/problem/1057450/experimentalexternal-generates-c4251-warnings-when.html
    # And /external:wd<warning> to disable warnings doesn't work:
    #   https://developercommunity.visualstudio.com/content/problem/1057493/externalwd-option-not-recognized.html
)

add_library(lib-3rdparty STATIC
    3rdparty/nsfplay/xgm/devices/Sound/nes_apu.cpp
    3rdparty/nsfplay/xgm/devices/Sound/nes_dmc.cpp
)
target_compile_options(lib-3rdparty PRIVATE ${options-3rdparty})

# Core library, does not use Qt
add_library(exotracker-lib
    ## Audio
    3rdparty/Blip_Buffer/Blip_Buffer.cpp

    # Library (widely used, uncoupled)
    src/audio/event_queue.cpp

    # Common files (tightly coupled)
    src/audio/audio_common.cpp
    src/audio/synth_common.cpp
    src/audio/synth/envelope.cpp
    src/audio/synth/sequencer.cpp
    src/audio/synth/music_driver_common.cpp

    src/chip_common.cpp

    # Implementation files
    src/audio/synth.cpp

    # 2A03 audio
    src/audio/synth/nes_2a03.cpp
    src/audio/synth/nes_2a03_driver.cpp

    # Glue files
    src/doc.cpp
    src/doc/events.cpp
    src/doc/timed_events.cpp
    src/timing_common.cpp
)
target_compile_options(exotracker-lib PRIVATE "${options}")
target_link_libraries(exotracker-lib PRIVATE lib-3rdparty rtaudio)
target_link_libraries(exotracker-lib PUBLIC fmt::fmt)
target_compile_definitions(exotracker-lib PRIVATE EXO_LIB)

if(COMMAND target_precompile_headers)
    target_precompile_headers(exotracker-lib PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()

# GUI editor, uses Qt
add_executable(exotracker-bin
    # GUI/audio communication
    src/audio/output.cpp
    src/cmd_queue.cpp

    # GUI
    src/main.cpp
    src/gui/app.cpp
    src/gui/main_window.cpp
    src/gui/config.cpp

    src/sample_docs.cpp
    src/edit/edit_doc.cpp
    src/edit/edit_pattern.cpp
    src/edit_util/kv.cpp
    src/gui/history.cpp
    src/gui/lib/color.cpp
    src/gui/lib/format.cpp
    src/gui/lib/painter_ext.cpp
    src/gui/move_cursor.cpp
    src/gui/pattern_editor/pattern_editor_panel.cpp
)
target_compile_options(exotracker-bin PRIVATE "${options}")
target_link_libraries(exotracker-bin PRIVATE exotracker-lib rtaudio Qt5::Widgets qkeycode)
target_compile_definitions(exotracker-bin PRIVATE EXO_BIN)

if(COMMAND target_precompile_headers)
    target_precompile_headers(exotracker-bin PRIVATE
        precompiled_headers/exotracker_lib.h
        precompiled_headers/exotracker_bin.h
    )
endif()

# Unit tests
add_executable(exotracker-tests
    tests/run_tests.cpp
    tests/test_blip_buffer.cpp
    tests/test_document.cpp
    tests/test_math.cpp
    tests/audio/test_event_queue.cpp
    tests/audio/test_sequencer.cpp
    tests/audio/test_synth.cpp
    tests/test_utils/parameterize.cpp
    tests/test_utils/test_parameterize.cpp

    src/doc.cpp  # for #ifdef UNITTEST
    src/edit_util/kv.cpp
    src/gui/move_cursor.cpp
    src/cmd_queue.cpp
    src/sample_docs.cpp
    src/audio/synth/envelope.cpp
    src/audio/synth/nes_2a03_driver.cpp
)
target_compile_options(exotracker-tests PRIVATE "${options}")
target_include_directories(exotracker-tests PUBLIC tests)
target_link_libraries(exotracker-tests PRIVATE exotracker-lib)
target_compile_definitions(exotracker-tests PRIVATE UNITTEST)

if(COMMAND target_precompile_headers)
    target_precompile_headers(exotracker-tests PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()

# This can be used to enable asan (-fsanitize=memory/undefined/leak).
include(cmake_user_end.cmake OPTIONAL)
