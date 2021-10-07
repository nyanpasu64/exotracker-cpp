cmake_minimum_required(VERSION 3.8.0)

# Remove /W3 from MSVC global flags.
cmake_policy(SET CMP0092 NEW)
project(exotracker)

include(cmake/lib.cmake)

## Initialize environment
set(RTAUDIO_BUILD_STATIC_LIBS TRUE)
set(BUILD_SHARED_LIBS FALSE CACHE BOOL "Build (rtaudio, fmt, ...?) as shared library")

# RtAudio uses string replacement to achieve static MSVCRT.
# Turn it off to avoid weirdness.
set(RTAUDIO_STATIC_MSVCRT FALSE)

# libsamplerate ships with 3 sinc tables of different qualities.
# SRC_SINC_BEST_QUALITY uses a massive 1.36 megabyte sinc table,
# and exotracker doesn't even use SRC_SINC_BEST_QUALITY.
# So turn it off to save space in exotracker binaries.
set(LIBSAMPLERATE_ENABLE_SINC_BEST_CONVERTER FALSE CACHE BOOL "[libsamplerate] Enable Best Sinc Interpolator converter")

set(USE_PCH FALSE)
if(COMMAND target_precompile_headers)
    set(USE_PCH TRUE)
endif()

# Trim down Cap'n Proto build to reduce binary size and build time.
option(BUILD_TESTING "Build unit tests and enable CTest 'check' target." OFF)
# ${EXTERNAL_CAPNP} only affects build if BUILD_TESTING is ON.
option(
    CAPNP_LITE
    "Compile Cap'n Proto in 'lite mode', in which all reflection APIs (schema.h, dynamic.h, etc.) are not included. Produces a smaller library at the cost of features. All programs built against the library must be compiled with -DCAPNP_LITE. Requires EXTERNAL_CAPNP."
    ON
)

include(cmake_user_begin.cmake OPTIONAL)
#End initialize environment


## Configure global settings
set(CMAKE_CXX_STANDARD 20)

if (WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    # rtaudio_c.cpp uses strncpy and the like. I don't actually use that file lol
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    if (MSVC)
        add_compile_options(/EHsc)
        add_compile_options(/permissive-)
    endif ()
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG=1)
endif()

# https://foonathan.net/2018/10/cmake-warnings/
# more warning flags at https://stackoverflow.com/questions/5088460

# cmake generator expressions are an esolang.
# no spaces allowed in generator expressions, since whitespace becomes semicolons.
set(options-3rdparty $<$<CXX_COMPILER_ID:MSVC>:/W3>)
set(
    options
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wconversion -Wsign-conversion
        -Wmissing-declarations  # See !52 for explanation.
        -Wsuggest-override
        -Wno-unused-function -Wno-unused-parameter
        -Werror=return-type
    >
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
        -Wtautological-unsigned-zero-compare
        -Wmissing-variable-declarations  # See !52 for explanation.
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /Zc:preprocessor
        /W3
        # I want to use /W4, but I get *far* too many false-positive C4244 warnings
        # when passing integer literals into functions.
        # And /w34244 doesn't help; it means "enable C4244 at /W3",
        # not "enable the /W3 version of C4244".
        /wd4100  # allow unused parameters
        /wd4505  # allow unused functions
        /wd4456 /wd4457  # allow shadowing
    >
    # /W4 causes lots of warnings in blip-buffer and Qt.
    # /experimental:external /external:anglebrackets causes lots of C4251 warnings:
    #   https://developercommunity.visualstudio.com/content/problem/1057450/experimentalexternal-generates-c4251-warnings-when.html
    # And /external:wd<warning> to disable warnings doesn't work:
    #   https://developercommunity.visualstudio.com/content/problem/1057493/externalwd-option-not-recognized.html
)

# Strip bloat from release Windows MinGW builds.
if (
    WIN32
    AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_BUILD_TYPE STREQUAL "Release"
)
    add_link_options(-Wl,--strip-all -Wl,--insert-timestamp -Wl,--gc-sections -Wl,--discard-all)
endif()

include_directories(src)
include_directories(SYSTEM 3rdparty)


## Add dependencies

set(CMAKE_AUTORCC ON)
find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(RTAUDIO_BUILD_TESTING FALSE)
add_subdirectory("3rdparty/rtaudio" EXCLUDE_FROM_ALL)
add_subdirectory("3rdparty/fmt_" EXCLUDE_FROM_ALL)
add_subdirectory("3rdparty/qkeycode" EXCLUDE_FROM_ALL)
add_subdirectory("3rdparty/snes9x-dsp" EXCLUDE_FROM_ALL)
add_subdirectory("3rdparty/capnproto-c++" EXCLUDE_FROM_ALL)

# libsamplerate in debug mode burns lots of CPU,
# so build with optimizations on even in debug mode.
add_subdirectory_optimized("3rdparty/libsamplerate" EXCLUDE_FROM_ALL)

# add_library(lib-3rdparty STATIC
#     3rdparty/nsfplay/xgm/devices/Sound/nes_apu.cpp
#     3rdparty/nsfplay/xgm/devices/Sound/nes_dmc.cpp
# )
# target_compile_options(lib-3rdparty PRIVATE ${options-3rdparty})

## Core library, document and synth only (used for future player plugins).
# No editing or command support.
# (cmd_queue.cpp may be added later for playback commands.)
add_library(exotracker-core
    # Library (widely used, uncoupled)
    src/audio/event_queue.cpp

    # Common files (tightly coupled)
    src/audio/tempo_calc.cpp
    src/audio/audio_common.cpp
    src/audio/synth/chip_instance_common.cpp
    src/audio/synth/envelope.cpp
    src/audio/synth/sequencer.cpp
    src/audio/synth/music_driver_common.cpp

    src/chip_common.cpp

    # Implementation files
    src/audio/synth.cpp

    # SPC700 audio
    src/audio/synth/volume_calc_common.cpp
    src/audio/synth/spc700.cpp
    src/audio/synth/spc700_driver.cpp
    src/audio/synth/spc700_synth.cpp

    # Glue files
    src/doc.cpp
    src/doc/events.cpp
    src/doc/timed_events.cpp
    src/doc/timeline.cpp
    src/timeline_iter.cpp
    src/timing_common.cpp
    src/util/format.cpp
)
target_compile_options(exotracker-core PRIVATE "${options}")
target_link_libraries(exotracker-core PRIVATE snes9x-dsp)
target_link_libraries(exotracker-core PUBLIC fmt::fmt samplerate)

if(USE_PCH)
    target_precompile_headers(exotracker-core PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()


## Qt-independent GUI libraries, used/tested by GUI and unit tests
add_library(exotracker-headless
    # GUI/audio communication
    src/audio/output.cpp
    src/cmd_queue.cpp

    # Document editing code
    src/sample_docs.cpp
    src/edit/edit_doc.cpp
    src/edit/edit_instr.cpp
    src/edit/edit_instr_list.cpp
    src/edit/edit_pattern.cpp
    src/doc/gui_traits.cpp
    src/doc_util/event_search.cpp
    src/doc/validate.cpp
    src/serialize.cpp
    src/serialize/document.capnp.c++
    src/gui/history.cpp

    # Pattern editor backend
    src/gui/move_cursor.cpp
)
target_compile_options(exotracker-headless PRIVATE "${options}")
target_link_libraries(exotracker-headless
    PRIVATE rtaudio
    PRIVATE capnp kj  # Don't #include Cap'n Proto in the public interface!
    PUBLIC exotracker-core
)
if(USE_PCH)
    target_precompile_headers(exotracker-headless PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()


## GUI editor, uses Qt
add_executable(exotracker-qt
    # GUI
    src/main.cpp
    src/gui/app.cpp
    src/gui/main_window.cpp
    src/gui/config.cpp
    src/gui/lib/icons.cpp
    src/gui/lib/icon_toolbar.cpp

    src/gui/lib/color.cpp
    src/gui/lib/docs_palette.cpp
    src/gui/lib/format.cpp  # Unfortunately tied to QString.
    src/gui/lib/instr_warnings.cpp
    src/gui/lib/list_warnings.cpp
    src/gui/lib/parse_note.cpp
    src/gui/lib/painter_ext.cpp
    src/gui/pattern_editor.cpp
    src/gui/timeline_editor.cpp
    src/gui/instrument_list.cpp

    src/gui/tempo_dialog.cpp
    src/gui/instrument_dialog.cpp
    src/gui/instrument_dialog/adsr_graph.cpp

    # Resources
    res/exotracker.qrc
)
target_compile_options(exotracker-qt PRIVATE "${options}")
target_link_libraries(exotracker-qt PRIVATE exotracker-headless rtaudio Qt5::Widgets qkeycode)

if(USE_PCH)
    target_precompile_headers(exotracker-qt PRIVATE
        precompiled_headers/exotracker_lib.h
        precompiled_headers/exotracker_bin.h
    )
endif()

# Generate .pdb files for release builds (https://stackoverflow.com/a/57479289)
# The majority of end-user crashes happen in release builds,
# and debugging their memory dumps is difficult without symbols.
# So generate symbols to aid debugging.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")

    # Tell linker to include symbol data
    set_target_properties(exotracker-qt PROPERTIES
        LINK_FLAGS "/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF"
    )

    # Set file name & location
    set_target_properties(exotracker-qt PROPERTIES
        COMPILE_PDB_NAME exotracker-qt
        COMPILE_PDB_OUTPUT_DIR ${CMAKE_BINARY_DIR}
    )
endif()

# Generate .pdb files for release Windows Clang builds.
# If you're using MinGW Clang, you should remove this line to reduce binary size.
if(
    WIN32
    AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    AND CMAKE_BUILD_TYPE STREQUAL "Release"
)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
endif()


## Unit tests
add_executable(exotracker-tests
    tests/run_tests.cpp
    tests/test_utils/parameterize.cpp

    tests/test_box_array.cpp
    tests/test_document.cpp
    tests/test_edit_history.cpp
    tests/test_math.cpp
    tests/audio/test_event_queue.cpp
    tests/audio/test_sequencer.cpp
    tests/audio/test_synth.cpp
    tests/test_utils/test_parameterize.cpp

    # for #ifdef UNITTEST
    src/doc.cpp
    src/serialize.cpp
    src/gui/move_cursor.cpp
    # src/audio/synth/envelope.cpp
    src/audio/synth/spc700_driver.cpp
    src/audio/synth/volume_calc_common.cpp
    src/doc/timeline.cpp
)
target_compile_options(exotracker-tests PRIVATE "${options}")
target_include_directories(exotracker-tests PUBLIC tests)
target_link_libraries(exotracker-tests
    PRIVATE exotracker-headless
    PRIVATE capnp kj snes9x-dsp
)
target_compile_definitions(exotracker-tests PRIVATE UNITTEST)

if(USE_PCH)
    target_precompile_headers(exotracker-tests PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()


add_executable(serialize-main
    src/serialize_main.cpp
)
target_compile_options(serialize-main PRIVATE "${options}")
target_link_libraries(serialize-main
    PRIVATE exotracker-headless
)


# This can be used to enable asan (-fsanitize=memory/undefined/leak).
include(cmake_user_end.cmake OPTIONAL)
