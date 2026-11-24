# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# --- Library definition

add_library(
  Oros
  include/oros/logger.hpp
  include/oros/oros.hpp
  include/oros/scene/scene_types.hpp
  include/oros/terrain/async_lod_selector.hpp
  include/oros/terrain/cdlod_quadtree.hpp
  include/oros/terrain/grid_mesh_factory.hpp
  include/oros/terrain/heightmap_data.hpp
  include/oros/terrain/heightmap_patch.hpp
  include/oros/ui/viewport_widget.hpp
  src/logger.cpp
  src/oros.cpp
  src/scene/scene_types.cpp
  src/terrain/async_lod_selector.cpp
  src/terrain/cdlod_quadtree.cpp
  src/terrain/grid_mesh_factory.cpp
  src/terrain/heightmap_data.cpp
  src/terrain/heightmap_patch.cpp
  src/ui/viewport_widget.cpp)

add_library(Oros::Oros ALIAS Oros)

filament_compile_materials(
  Oros
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/cdlod_terrain_unlit.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/cdlod_terrain_lit.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/ground_plane_lit.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/instanced_object_lit.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/sky_atmosphere.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/materials/water_lit.mat)

# --- Include directories

target_include_directories(
  Oros
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Dependencies

target_link_libraries(
  Oros
  PUBLIC spdlog::spdlog Qt6::Core Qt6::Gui Qt6::Widgets
  PRIVATE filament::filament)

# --- Compiler warnings and features

target_compile_features(Oros PUBLIC cxx_std_20)
