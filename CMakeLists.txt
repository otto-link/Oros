cmake_minimum_required(VERSION 3.20)

project(
  Oros
  VERSION 0.1.0
  DESCRIPTION "Oros 3D rendering Qt C++ library and test suite"
  LANGUAGES CXX)

# --- Global project settings

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Build options

option(OROS_BUILD_TESTS "Build Oros tests" ON)
option(OROS_BUILD_UNITTESTS "Build Oros unit tests" ON)
option(OROS_BUILD_UI_TESTS "Build Oros UI manual test" ON)

# --- Dependencies

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(spdlog)
endif()

include(FetchContent)

# --- Core library

add_subdirectory(Oros)

# --- Tests

if(OROS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
