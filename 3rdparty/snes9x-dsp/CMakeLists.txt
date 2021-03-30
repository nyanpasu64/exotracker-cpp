cmake_minimum_required(VERSION 3.12)

project(snes9x-dsp LANGUAGES CXX)

add_library(snes9x-dsp STATIC
    SPC_DSP.cpp
)
target_include_directories(snes9x-dsp
    PUBLIC .
)
