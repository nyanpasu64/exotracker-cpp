cmake_minimum_required(VERSION 3.8.0)

project(exotracker)

set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

#vcpkg's portaudio does not support CMake targets, unfortunately.
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
find_library(PORTAUDIO_LIBRARY portaudio)

# Configure global settings
include_directories(src 3rdparty)
set(CMAKE_CXX_STANDARD 20)
if (WIN32)
    add_compile_definitions(UNICODE _UNICODE)
    if (MSVC)
        add_compile_options(/EHsc)
    endif ()
endif ()

# Core library, does not use Qt
add_library(exotracker-lib
        # Utility
        src/util/macros.h

        # Library
        src/audio/output.cpp
        src/audio/output.h
        src/audio/synth.cpp
        src/audio/synth.h
        src/document.cpp
        src/document.h
        src/sequencer/sequencer.cpp
        src/sequencer/sequencer.h
        )
target_include_directories(exotracker-lib PRIVATE ${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(exotracker-lib PRIVATE ${PORTAUDIO_LIBRARY})


# GUI editor, uses Qt
add_executable(exotracker-bin
        # GUI
        src/gui/lib/lightweight.h
        src/gui/main_window.cpp
        src/gui/main_window.h
        src/gui/pattern_editor/pattern_editor_panel.cpp
        src/gui/pattern_editor/pattern_editor_panel.h
        src/main.cpp
        )
target_link_libraries(exotracker-bin PRIVATE exotracker-lib Qt5::Widgets)

# Unit tests
add_executable(exotracker-tests
        tests/test_runner.cpp
        tests/test_document.cpp
        )
target_include_directories(exotracker-tests PUBLIC tests)
target_link_libraries(exotracker-tests PRIVATE exotracker-lib)
