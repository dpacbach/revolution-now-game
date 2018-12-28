# === ccache ======================================================

#find_program( CCACHE_PROGRAM ccache )
#if( CCACHE_PROGRAM )
#    set( CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" )
#else()
#    message( STATUS "ccache not found." )
#endif()

# === compiler warnings ===========================================

# Enable all warnings and treat warnings as errors.
# FIXME: disabled the generator expressions here because they
# break Cotire (see https://github.com/sakra/cotire/issues/135).
function( set_warning_options target )
    target_compile_options(
      ${target} PUBLIC
        # clang/GCC warnings
        #$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra #>
        # MSVC warnings
        #$<$<CXX_COMPILER_ID:MSVC>:
            #/Wall /WX >
    )
endfunction( set_warning_options )

# === build type ==================================================

set( default_build_type "Debug" )
if( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
    message( STATUS "Setting build type to '${default_build_type}'." )
    set( CMAKE_BUILD_TYPE "${default_build_type}" CACHE
         STRING "Choose the type of build." FORCE )
    # Set the possible values of build type for cmake-gui
    set_property( CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 "Debug" "Release" "RelWithDebInfo")
endif()
message( STATUS "Build type: ${CMAKE_BUILD_TYPE}" )

# === clang-tidy ==================================================

if( USE_CLANG_TIDY )
    find_program( CLANG_TIDY_EXE NAMES "clang-tidy" )
    if( NOT CLANG_TIDY_EXE )
        message( FATAL_ERROR "clang-tidy not found." )
    endif()
    set( DO_CLANG_TIDY "${CLANG_TIDY_EXE}" "-quiet" )
endif()

function( clang_tidy target )
    if( USE_CLANG_TIDY )
        set_target_properties( ${target} PROPERTIES
            CXX_CLANG_TIDY "${DO_CLANG_TIDY}" )
    endif()
endfunction()

# This is used not only for clang-tidy but also for YCM.
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

# === address sanitizer ===========================================

function( enable_address_sanitizer_if_requested )
  if( ENABLE_ADDRESS_SANITIZER )
    message( STATUS "Enabling AddressSanitizer" )
    set( CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=address" PARENT_SCOPE )
    set( CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address" PARENT_SCOPE )
  endif()
endfunction()

# === colors ======================================================

function( force_compiler_color_diagnostics )
	if( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
		# using Clang (either linux or apple)
    set( flag "-fcolor-diagnostics" )
	elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
		# using GCC
    set( flag "-fdiagnostics-color=always" )
	elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel" )
		# using Intel C++
	elseif( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC" )
		# using Visual Studio C++
	endif()
  set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}" PARENT_SCOPE )
endfunction()
