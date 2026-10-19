# --- Dependencies

FetchContent_Declare(
  fastnoiselite
  GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
  GIT_TAG v1.1.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(fastnoiselite)

add_library(fastnoiselite INTERFACE)
target_include_directories(fastnoiselite
                           INTERFACE ${fastnoiselite_SOURCE_DIR}/Cpp)

# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)

# --- CDLOD visual test executable

add_executable(ui_cdlod_test main.cpp cdlod_view_widget.cpp
                             cdlod_view_widget.hpp)

target_link_libraries(ui_cdlod_test PRIVATE Oros::Oros Qt6::Widgets
                                            fastnoiselite)

target_compile_features(ui_cdlod_test PRIVATE cxx_std_20)
