# --- Qt auto-generation

set(CMAKE_AUTOMOC ON)

# --- Unit test executable

add_executable(oros_unittests logger_test.cpp ui/viewport_widget_test.cpp)

target_link_libraries(oros_unittests PRIVATE Oros::Oros GTest::gtest_main
                                             Qt6::Widgets)

target_compile_features(oros_unittests PRIVATE cxx_std_20)

gtest_discover_tests(oros_unittests)
