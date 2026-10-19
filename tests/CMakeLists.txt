# --- Dependencies

if(OROS_BUILD_UNITTESTS)
  # fetch googletest
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)

  # prevent overriding parent project compiler/linker settings on Windows
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  include(GoogleTest)
  add_subdirectory(unittests)
endif()

# --- UI manual test

if(OROS_BUILD_UI_TESTS)
  add_subdirectory(ui_test)
  add_subdirectory(ui_cdlod_test)
endif()
