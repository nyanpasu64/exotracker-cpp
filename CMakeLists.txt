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

option(QT6 "Force building with Qt 6 (not 5)" OFF)

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
        -Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits
        -Wmissing-declarations  # See !52 for explanation.
        -Wsuggest-override
        -Wno-unused-function -Wno-unused-parameter
        -Werror=return-type
    >
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
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
if (QT6)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
else ()
    find_package(Qt5 COMPONENTS Widgets)
    if (NOT Qt5_FOUND)
        find_package(Qt6 COMPONENTS Widgets REQUIRED)
    endif()
endif ()

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
    # Libraries
    src/util/box_array.h
    src/util/compare.h
    src/util/compare_impl.h
    src/util/constinit.h
    src/util/copy_move.h
    src/util/defer.h
    src/util/distance.h
    src/util/enumerate.h
    src/util/enum_flags.h
    src/util/enum_map.h
    src/util/expr.h
    src/util/loop.h
    src/util/macros.h
    src/util/math.h
    src/util/release_assert.h
    src/util/reverse.h
    src/util/safe_typedef.h
    src/util/typeid_cast.h
    src/util/unwrap.h
    src/util/variant_cast.h

    # Document
    src/chip_common.h
    src/chip_common.cpp
    src/chip_kinds.h
    src/doc_common.h
    src/doc.h
    src/doc.cpp
    src/doc/accidental_common.h
    src/doc/event_list.h
    src/doc/events.h
    src/doc/events.cpp
    src/doc/instr.h
    src/doc/sample.h
    src/doc/timed_events.h
    src/doc/timed_events.cpp
    src/doc/timeline.h

    # Document-based types and libraries
    src/audio/tempo_calc.h
    src/audio/tempo_calc.cpp
    src/doc/effect_names.h
    src/doc/gui_traits.h
    src/doc/gui_traits.cpp
    src/doc_util/track_util.h
    src/doc_util/track_util.cpp
    src/timing_common.h
    src/timing_common.cpp
    src/util/format.h
    src/util/format.cpp

    # GUI/audio communication
    src/audio/callback.h
    src/audio/audio_common.h
    src/audio/audio_common.cpp

    # Synth files
    src/audio/event_queue.h
    src/audio/event_queue.cpp
    src/audio/synth_common.h
    src/audio/synth.h
    src/audio/synth.cpp
    src/audio/synth/chip_instance_common.h
    src/audio/synth/chip_instance_common.cpp
    src/audio/synth/impl_chip_common.h
    src/audio/synth/envelope.h
    src/audio/synth/envelope.cpp
    src/audio/synth/music_driver_common.h
    src/audio/synth/music_driver_common.cpp
    src/audio/synth/sequencer_driver_common.h
    src/audio/synth/sequencer.h
    src/audio/synth/sequencer.cpp

    # SPC700 driver and synth
    src/audio/synth/spc700.h
    src/audio/synth/spc700.cpp
    src/audio/synth/spc700_driver.h
    src/audio/synth/spc700_driver.cpp
    src/audio/synth/spc700_math.h
    src/audio/synth/spc700_synth.h
    src/audio/synth/spc700_synth.cpp

    # Module loading files
    src/doc/validate_common.h
    src/doc/validate.h
    src/doc/validate.cpp
    src/serialize.h
    src/serialize.cpp
    src/serialize/document.capnp.h
    src/serialize/document.capnp.c++

    # SPC export files
    src/spc_export.h
    src/spc_export.cpp
    src/spc_export/driver.h
    src/spc_export/driver.cpp
    src/spc_export/link.h
    src/spc_export/link.cpp
)
target_compile_options(exotracker-core PRIVATE "${options}")
target_link_libraries(exotracker-core
    PRIVATE snes9x-dsp
    PRIVATE capnp kj  # Don't #include Cap'n Proto in the public interface!
    PUBLIC fmt::fmt samplerate
)

if(USE_PCH)
    target_precompile_headers(exotracker-core PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()


## Qt-independent GUI libraries, used/tested by GUI and unit tests
add_library(exotracker-headless
    # Audio output, GUI/audio communication
    src/audio/output.h
    src/audio/output.cpp
    src/cmd_queue.h
    src/cmd_queue.cpp

    # Document editing code
    src/doc_util/event_builder.h
    src/doc_util/event_search.h
    src/doc_util/event_search.cpp
    src/doc_util/sample_instrs.h
    src/doc_util/sample_instrs.cpp
    src/doc_util/time_util.h
    src/doc_util/time_util.cpp
    src/edit_common.h
    src/edit/edit_impl.h
    src/edit/modified_common.h
    src/edit/modified.h

    # Document edit types
    src/sample_docs.h
    src/sample_docs.cpp
    src/edit/edit_doc.h
    src/edit/edit_doc.cpp
    src/edit/edit_instr.h
    src/edit/edit_instr.cpp
    src/edit/edit_instr_list.h
    src/edit/edit_instr_list.cpp
    src/edit/edit_pattern.h
    src/edit/edit_pattern.cpp
    src/edit/edit_sample.h
    src/edit/edit_sample.cpp
    src/edit/edit_sample_list.h
    src/edit/edit_sample_list.cpp

    # Qt-independent GUI libraries
    src/gui/history.h
    src/gui/history.cpp

    # GUI timing code
    src/gui/gui_time.h
    src/gui/gui_time.cpp
    src/gui/move_cursor.h
    src/gui/move_cursor.cpp
)
target_compile_options(exotracker-headless PRIVATE "${options}")
target_link_libraries(exotracker-headless
    PUBLIC exotracker-core
    PRIVATE rtaudio
)
if(USE_PCH)
    target_precompile_headers(exotracker-headless PRIVATE
        precompiled_headers/exotracker_lib.h
    )
endif()


## GUI editor, uses Qt
add_executable(exotracker-qt
    # GUI libraries and components
    src/gui_common.h
    src/gui/config.h
    src/gui/config.cpp
    src/gui/config/block_config.h
    src/gui/config/cursor_config.h
    src/gui/cursor.h
    src/gui/lib/color.h
    src/gui/lib/color.cpp
    src/gui/lib/docs_palette.h
    src/gui/lib/docs_palette.cpp
    src/gui/lib/dpi.h
    src/gui/lib/format.h
    src/gui/lib/format.cpp  # Unfortunately tied to QString.
    src/gui/lib/icons.h
    src/gui/lib/icons.cpp
    src/gui/lib/instr_warnings.h
    src/gui/lib/instr_warnings.cpp
    src/gui/lib/layout_macros.h
    src/gui/lib/list_warnings.h
    src/gui/lib/list_warnings.cpp
    src/gui/lib/parse_note.cpp
    src/gui/lib/painter_ext.h
    src/gui/lib/painter_ext.cpp
    src/gui/lib/parse_note.h
    src/gui/lib/persistent_dialog.h
    src/gui/lib/persistent_dialog.cpp
    src/gui/lib/qt6.h
    src/gui/lib/sample_text.h
    src/gui/lib/sample_text.cpp
    src/gui/lib/small_button.h
    src/gui/lib/trace.h

    # Widget components
    src/gui/lib/hv_line.h
    src/gui/lib/icon_toolbar.h
    src/gui/lib/icon_toolbar.cpp
    src/gui/lib/note_spinbox.h
    src/gui/lib/note_spinbox.cpp

    # Panels
    src/gui/instrument_list.h
    src/gui/instrument_list.cpp
    src/gui/pattern_editor.h
    src/gui/pattern_editor.cpp
    src/gui/timeline_editor.h
    src/gui/timeline_editor.cpp

    # Dialogs
    src/gui/instrument_dialog.h
    src/gui/instrument_dialog.cpp
    src/gui/instrument_dialog/adsr_graph.h
    src/gui/instrument_dialog/adsr_graph.cpp
    src/gui/sample_dialog.h
    src/gui/sample_dialog.cpp
    src/gui/tempo_dialog.h
    src/gui/tempo_dialog.cpp

    # GUI interfaces and implementation (sadly coupled)
    src/main.cpp
    src/gui/app.h
    src/gui/app.cpp
    src/gui/main_window.h
    src/gui/main_window.cpp

    # Resources
    res/exotracker.qrc
)
target_compile_options(exotracker-qt PRIVATE "${options}")
target_link_libraries(exotracker-qt PRIVATE exotracker-headless rtaudio Qt::Widgets qkeycode)

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
#    tests/audio/test_sequencer.cpp
    tests/audio/test_synth.cpp
    tests/test_utils/test_parameterize.cpp

    # for #ifdef UNITTEST
    src/doc.cpp
    src/serialize.cpp
    src/gui/move_cursor.cpp
    # src/audio/synth/envelope.cpp
    src/audio/synth/spc700_driver.cpp
    src/doc_util/track_util.cpp
    src/spc_export.cpp
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
