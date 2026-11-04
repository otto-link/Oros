# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)

# --- OpenGL CDLOD visual test executable

add_executable(
  ui_cdlod_gl_test
  main.cpp
  cdlod_gl_widget.hpp
  cdlod_gl_widget.cpp)

target_link_libraries(
  ui_cdlod_gl_test
  PRIVATE
    Oros::Oros
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    fastnoiselite)

target_compile_features(ui_cdlod_gl_test PRIVATE cxx_std_20)
