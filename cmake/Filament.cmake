# --- Filament dependency setup

include(FetchContent)

set(FILAMENT_VERSION "v1.77.1")
if(WIN32)
  set(FILAMENT_PLATFORM "windows")
  set(FILAMENT_ARCHIVE_EXT "tgz")
  set(FILAMENT_MATC_API "opengl")
elseif(APPLE)
  set(FILAMENT_PLATFORM "mac")
  set(FILAMENT_ARCHIVE_EXT "tgz")
  set(FILAMENT_MATC_API "metal")
else()
  set(FILAMENT_PLATFORM "linux")
  set(FILAMENT_ARCHIVE_EXT "tgz")
  set(FILAMENT_MATC_API "opengl")
endif()

set(FILAMENT_URL "https://github.com/google/filament/releases/download/${FILAMENT_VERSION}/filament-${FILAMENT_VERSION}-${FILAMENT_PLATFORM}.${FILAMENT_ARCHIVE_EXT}")

FetchContent_Declare(
  filament_binaries
  URL ${FILAMENT_URL}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(filament_binaries)

# locate filament directories
set(FILAMENT_INCLUDE_DIR "${filament_binaries_SOURCE_DIR}/include")
if(WIN32)
  set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/x86_64/mt")
  if(NOT EXISTS "${FILAMENT_LIB_DIR}")
    set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/x86_64")
  endif()
elseif(APPLE)
  set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/universal")
  if(NOT EXISTS "${FILAMENT_LIB_DIR}")
    set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/arm64")
  endif()
else()
  set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/x86_64")
endif()
set(FILAMENT_BIN_DIR "${filament_binaries_SOURCE_DIR}/bin")

# create imported target for Filament
add_library(filament::filament INTERFACE IMPORTED GLOBAL)

# find required system libraries
find_package(Threads REQUIRED)

target_include_directories(
  filament::filament
  INTERFACE
  ${FILAMENT_INCLUDE_DIR}
)

if(WIN32)
  file(GLOB FILAMENT_ALL_LIBS "${FILAMENT_LIB_DIR}/*.lib")
  target_link_libraries(
    filament::filament
    INTERFACE
    ${FILAMENT_ALL_LIBS}
    Threads::Threads
    opengl32
    gdi32
    user32
  )
elseif(APPLE)
  file(GLOB FILAMENT_ALL_LIBS "${FILAMENT_LIB_DIR}/lib*.a")
  target_link_libraries(
    filament::filament
    INTERFACE
    ${FILAMENT_ALL_LIBS}
    Threads::Threads
    "-framework Metal"
    "-framework CoreVideo"
    "-framework Cocoa"
    "-framework QuartzCore"
  )
else()
  file(GLOB FILAMENT_ALL_LIBS "${FILAMENT_LIB_DIR}/lib*.a")
  target_link_libraries(
    filament::filament
    INTERFACE
    -Wl,--start-group
    ${FILAMENT_ALL_LIBS}
    -Wl,--end-group
    Threads::Threads
    ${CMAKE_DL_LIBS}
    zstd
    c++
    c++abi
    GL
    X11
  )
endif()

# expose matc executable
if(EXISTS "${FILAMENT_BIN_DIR}/matc.exe")
  set(FILAMENT_MATC_EXECUTABLE "${FILAMENT_BIN_DIR}/matc.exe" CACHE FILEPATH "Filament matc tool")
elseif(EXISTS "${FILAMENT_BIN_DIR}/matc")
  set(FILAMENT_MATC_EXECUTABLE "${FILAMENT_BIN_DIR}/matc" CACHE FILEPATH "Filament matc tool")
endif()

# helper to compile Filament materials to headers
function(filament_compile_materials TARGET)
  set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/materials")
  file(MAKE_DIRECTORY "${GEN_DIR}")

  set(GENERATED_HEADERS)
  foreach(MAT_FILE IN LISTS ARGN)
    get_filename_component(MAT_NAME ${MAT_FILE} NAME_WLE)
    set(HEADER_OUT "${GEN_DIR}/${MAT_NAME}.h")
    add_custom_command(
      OUTPUT "${HEADER_OUT}"
      COMMAND ${FILAMENT_MATC_EXECUTABLE} -f header -p desktop -a ${FILAMENT_MATC_API} -o "${HEADER_OUT}" "${MAT_FILE}"
      DEPENDS "${MAT_FILE}"
      COMMENT "Compiling Filament material ${MAT_NAME}.mat"
      VERBATIM
    )
    list(APPEND GENERATED_HEADERS "${HEADER_OUT}")
  endforeach()

  target_sources(${TARGET} PRIVATE ${GENERATED_HEADERS})
  target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
