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
    src/gui/layout_stack.cpp \
    src/main.cpp \
    src/mainwindow.cpp \

HEADERS += \
    src/gui/layout_stack.h \
    src/mainwindow.h \
    src/macros.h
