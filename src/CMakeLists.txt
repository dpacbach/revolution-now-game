# === subdirectories ==============================================

add_subdirectory( fb )

# === this directory ==============================================

find_sdl2_lib( SDL2       )
find_sdl2_lib( SDL2_ttf   )
find_sdl2_lib( SDL2_image )
find_sdl2_lib( SDL2_mixer )

# We need threading.
set( THREADS_PREFER_PTHREAD_FLAG ON )
find_package( Threads REQUIRED )

file( GLOB sources "[a-zA-Z]*.cpp" )

add_library(
  rn
  STATIC
  ${sources}
)

target_compile_features( rn PUBLIC cxx_std_20 )
set_target_properties( rn PROPERTIES CXX_EXTENSIONS OFF )
set_warning_options( rn )

target_link_libraries(
    rn
    PUBLIC
    absl::container
    absl::flat_hash_map
    absl::flat_hash_set
    absl::hash
    absl::strings
    base-util
    better-enums::better-enums
    expected-lite
    flatbuffers
    fmt::fmt
    function-ref
    lua::lua
    magic_enum::magic_enum
    midifile
    observer-ptr::observer-ptr
    range-v3
    rtmidi
    scelta::headers
    sol2::sol2
    spdlog::spdlog
    Threads::Threads
    ucl
    ${SDL2_LIBRARY}
    ${SDL2_TTF_LIBRARY}
    ${SDL2_IMAGE_LIBRARY}
    ${SDL2_MIXER_LIBRARY} )

add_dependencies( rn rn-flatbuf )
get_target_property( FLATBUFF_INCLUDE_DIR rn-flatbuf INCLUDE_DIR )

target_include_directories(
    rn
    PUBLIC
    ${PROJECT_SOURCE_DIR} # top-level folder
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FLATBUFF_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIR}
    ${SDL2_TTF_INCLUDE_DIR}
    ${SDL2_IMAGE_INCLUDE_DIR}
    ${SDL2_MIXER_INCLUDE_DIR} )

# If this is a debug build then link with the backward-cpp
# library which provides stack traces.
if( CMAKE_BUILD_TYPE STREQUAL Debug )
  if( CMAKE_SYSTEM_NAME STREQUAL Linux )
    find_library( DW dw )
    if( DW STREQUAL "DW-NOTFOUND" )
      message( WARNING "libdw not found; not linking to backward-cpp library" )
    else()
      message( STATUS "RN Found libdw." )
      target_link_libraries( rn PUBLIC dw )
      target_compile_definitions(
        rn
        PUBLIC
        BACKWARD_HAS_DW=1 )
    endif()
  endif()
  target_link_libraries( rn PUBLIC backward backward_object )
endif()

if( DEBUG_TRACE )
  message( STATUS "Enabling trace-level logging" )
  target_compile_definitions( rn PUBLIC RN_TRACE=1 )
endif()

target_compile_definitions(
  rn
  PUBLIC
  FMT_STRING_ALIAS=1
  BETTER_ENUMS_STRICT_CONVERSION=1
  SCELTA_SUPPORT_VARIANT_BOOST_DISABLE
  SCELTA_SUPPORT_OPTIONAL_BOOST_DISABLE
  LUA_VERSION_NUM=503
  SOL_USING_CXX_LUA=1
  SOL_CXX17_FEATURES=1  # won't need this in future versions.
  SOL_ALL_SAFETIES_ON=1
  SOL_EXCEPTIONS_ALWAYS_UNSAFE=1
  SOL_PRINT_ERRORS=0
  MAGIC_ENUM_RANGE_MAX=512
  )

clang_tidy( rn )

target_precompile_headers( rn PRIVATE pch.hpp )
