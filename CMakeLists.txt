# Aseprite FLIC Library
# Copyright (c) 2015 David Capello
cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

add_library(flic-lib decoder.cpp encoder.cpp stdio.cpp)
target_compile_features(flic-lib PRIVATE cxx_auto_type)

