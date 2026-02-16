# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(sqlite_vec_library VERSION 0.0.1 LANGUAGES C)

add_library(sqlite_vec SHARED
  "sqlite-vec.c"
)

set_target_properties(sqlite_vec PROPERTIES
  PUBLIC_HEADER sqlite-vec.h
  OUTPUT_NAME "sqlite_vec"
)

target_compile_definitions(sqlite_vec PUBLIC DART_SHARED_LIB)
