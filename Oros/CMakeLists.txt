# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# --- Library definition

add_library(
  Oros
  include/oros/logger.hpp include/oros/oros.hpp
  include/oros/ui/viewport_widget.hpp src/logger.cpp src/oros.cpp
  src/ui/viewport_widget.cpp)

add_library(Oros::Oros ALIAS Oros)

# --- Include directories

target_include_directories(
  Oros
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Dependencies

target_link_libraries(Oros PUBLIC spdlog::spdlog Qt6::Core Qt6::Gui
                                  Qt6::Widgets)

# --- Compiler warnings and features

target_compile_features(Oros PUBLIC cxx_std_20)
