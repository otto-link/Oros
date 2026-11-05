# --- Filament dependency setup

include(FetchContent)

set(FILAMENT_VERSION "v1.77.1")
set(FILAMENT_URL "https://github.com/google/filament/releases/download/${FILAMENT_VERSION}/filament-${FILAMENT_VERSION}-linux.tgz")

FetchContent_Declare(
  filament_binaries
  URL ${FILAMENT_URL}
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(filament_binaries)

# locate filament directories
set(FILAMENT_INCLUDE_DIR "${filament_binaries_SOURCE_DIR}/include")
set(FILAMENT_LIB_DIR "${filament_binaries_SOURCE_DIR}/lib/x86_64")
set(FILAMENT_BIN_DIR "${filament_binaries_SOURCE_DIR}/bin")

# create imported target for Filament
add_library(filament::filament INTERFACE IMPORTED GLOBAL)

# find required system libraries on Linux
find_package(Threads REQUIRED)

target_include_directories(
  filament::filament
  INTERFACE
  ${FILAMENT_INCLUDE_DIR}
)

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

# expose matc executable
if(EXISTS "${FILAMENT_BIN_DIR}/matc")
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
      COMMAND ${FILAMENT_MATC_EXECUTABLE} -f header -p desktop -a opengl -o "${HEADER_OUT}" "${MAT_FILE}"
      DEPENDS "${MAT_FILE}"
      COMMENT "Compiling Filament material ${MAT_NAME}.mat"
      VERBATIM
    )
    list(APPEND GENERATED_HEADERS "${HEADER_OUT}")
  endforeach()

  target_sources(${TARGET} PRIVATE ${GENERATED_HEADERS})
  target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
