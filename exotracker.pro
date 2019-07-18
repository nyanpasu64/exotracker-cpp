#-------------------------------------------------
#
# Project created by QtCreator 2019-07-14T01:04:27
#
#-------------------------------------------------

QT += core gui widgets

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

# Begin file lists

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/gui/mainwindow.cpp \
    src/audio/output.cpp \

HEADERS += \
    src/gui/mainwindow.h \
    src/util/macros.h \
    src/gui/lib/lightweight.h \
    src/audio/output.h \
