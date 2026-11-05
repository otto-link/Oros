# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)

# --- Filament CDLOD visual test executable

add_executable(
  ui_cdlod_filament_test
  main.cpp
  cdlod_filament_widget.hpp
  cdlod_filament_widget.cpp)

filament_compile_materials(
  ui_cdlod_filament_test
  ${CMAKE_CURRENT_SOURCE_DIR}/materials/cdlod_terrain_unlit.mat
  ${CMAKE_CURRENT_SOURCE_DIR}/materials/cdlod_terrain_lit.mat)

target_link_libraries(
  ui_cdlod_filament_test
  PRIVATE
    Oros::Oros
    filament::filament
    Qt6::Widgets
    Qt6::Gui
    Qt6::Core
    fastnoiselite)

target_compile_features(ui_cdlod_filament_test PRIVATE cxx_std_20)
