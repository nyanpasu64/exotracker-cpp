cmake_minimum_required(VERSION 3.8.0)

project(exotracker)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# it's OFF by default, on 3.0.0 and 3.15.3. why enable on old?
#if(CMAKE_VERSION VERSION_LESS "3.7.0")
#    set(CMAKE_INCLUDE_CURRENT_DIR ON)
#endif()

find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_executable(exotracker-bin
    src/document.cpp
    src/gui/pattern_editor/pattern_editor_panel.cpp
    src/main.cpp
    src/gui/main_window.cpp
    src/audio/output.cpp
    src/audio/synth.cpp

    src/document.h
    src/gui/main_window.h
    src/gui/pattern_editor/pattern_editor_panel.h
    src/util/macros.h
    src/gui/lib/lightweight.h
    src/audio/output.h
    src/audio/synth.h
)

target_include_directories(exotracker-bin PUBLIC src boost)

if (WIN32)
    target_compile_definitions(exotracker-bin PRIVATE UNICODE _UNICODE)
endif()

target_compile_features(exotracker-bin PUBLIC cxx_std_20)

target_link_libraries(exotracker-bin Qt5::Widgets)
