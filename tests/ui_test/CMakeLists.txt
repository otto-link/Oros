# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)

# --- UI manual test executable

add_executable(oros_ui_test main.cpp)

target_link_libraries(oros_ui_test PRIVATE Oros::Oros Qt6::Widgets
                                           fastnoiselite)

target_compile_features(oros_ui_test PRIVATE cxx_std_20)
