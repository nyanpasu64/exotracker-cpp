#-------------------------------------------------
#
# Project created by QtCreator 2019-07-14T01:04:27
#
#-------------------------------------------------

QT += core gui widgets

win32:LIBS += -luser32 -lgdi32

TARGET = exotracker
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

# `CONFIG += c++17` was added in Qt 5.12. I'm on 5.9.
win32-msvc* {
    # Compiling on MSVC is untested.
    QMAKE_CXXFLAGS += /std:c++latest
}
else {
    QMAKE_CXXFLAGS += -std=c++17
}

# Begin linking dependencies
# https://doc.qt.io/qt-5/qmake-variable-reference.html#libs
# If you use the Unix -l (library) and -L (library path) flags, qmake handles the libraries correctly on Windows (that is, passes the full path of the library to the linker).

#LIBS += -L/usr/lib -lSDL2

# Begin file lists

INCLUDEPATH += src boost

SOURCES += \
    src/gui/pattern_editor/pattern_editor_panel.cpp \
    src/main.cpp \
    src/gui/main_window.cpp \
    src/audio/output.cpp \
    src/audio/synth.cpp \

HEADERS += \
    src/gui/main_window.h \
    src/gui/pattern_editor/pattern_editor_panel.h \
    src/util/macros.h \
    src/gui/lib/lightweight.h \
    src/audio/output.h \
    src/audio/synth.h \
