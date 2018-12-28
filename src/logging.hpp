/****************************************************************
**logging.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-12-07.
*
* Description: Interface to logging
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

//#ifdef SPDLOG_ACTIVE_LEVEL
//#  error "SPDLOG_ACTIVE_LEVEL should not be defined!"
//#endif

// To log at INFO level and below (meaning INFO, WARN, ERROR, and
// CRITICAL) use the non-macro statements so that we can control
// those levels at runtime (e.g. in a release build).  For DEBUG
// and TRACE debugging use the macro forms so that they can be
// fully removed by the compiler when not needed:
//
//   Level               Command
//   ------------------------------------------------------------
//   CRITICAL            logger->critical(...)
//   ERROR               logger->error(...)
//   WARN                logger->warn(...)
//   INFO                logger->info(...)
//   DEBUG               LOG_DEBUG( ... )
//   TRACE               LOG_TRACE( ... )
//
// Compile-time log levels are controlled by defining the
// SPDLOG_ACTIVE_LEVEL to one of these (before including
// spdlog.h):
//
//   SPDLOG_LEVEL_TRACE,
//   SPDLOG_LEVEL_DEBUG,
//   SPDLOG_LEVEL_INFO,
//   SPDLOG_LEVEL_WARN,
//   SPDLOG_LEVEL_ERROR,
//   SPDLOG_LEVEL_CRITICAL,
//   SPDLOG_LEVEL_OFF
//
// When a particular level is chosen all logging for the levels
// above it will be disabled.  If using macros to log then the
// disabled logging statements will disappear completely.  If
// using non-macro logging statements then the disabled logging
// statements will be no-opts (though might not disappear
// completely, especially in debug builds).  However, note that
// when a given level is disabled then it will be disabled
// whether using macro or not.  The macro just allows for
// compile- time removal if it is disabled.
//
// In this code base we do not allow the build system to choose
// the log level explicitly; instead, we select it based on some
// other preprocessor variables.

// Decide how to set SPDLOG_ACTIVE_LEVEL.
#ifndef SPDLOG_ACTIVE_LEVEL
#  ifdef RN_TRACE
// If the build system has requested a trace level then abide.
// This will enable ALL logging statements.
#    define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#  else
#    ifdef NDEBUG // Release build
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#    else // Debug build
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#    endif
#  endif
#endif

// clang-format will reorder these headers which then generates
// an error because they need to be included in a certian order.
// clang-format off
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
// clang-format on

// This is needed so that we can log types using the formatter
// when there are operator<< overloads for them (spdlog uses
// the fmt library internally).
#include "fmt/ostream.h"

// c++ standard library
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

// Enabled if the log level is high enough
#define LOG_DEBUG( ... ) \
  SPDLOG_LOGGER_DEBUG( logger, __VA_ARGS__ )
#define LOG_TRACE( ... ) \
  SPDLOG_LOGGER_TRACE( logger, __VA_ARGS__ )

namespace {

// This will create one logger for each translation unit.  The
// name of the logger will be the stem of the filename of the
// cpp file.  Using __FILE__ would yield the name of this header
// file which would not be useful.
auto logger = spdlog::stdout_color_mt(
    fs::path( __BASE_FILE__ ).filename().stem() );

} // namespace

namespace rn {

// This will initialize the log level.  If a level is not
// specified then one will be chosen according to the build type.
void init_logging(
    std::optional<spdlog::level::level_enum> level );

} // namespace rn
