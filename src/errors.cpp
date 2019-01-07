/****************************************************************
**errors.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-12-04.
*
* Description: Error handling code.
*
*****************************************************************/
#include "errors.hpp"

// Revolution Now
#include "fmt-helper.hpp"

// backward
#ifdef STACK_TRACE_ON
#  include "backward.hpp"
#endif

// C++ standard library
#include <string>

// C standard library
#include <stdio.h>

namespace rn {

namespace {} // namespace

/****************************************************************
**Stack Trace Reporting
*****************************************************************/

#ifdef STACK_TRACE_ON
StackTrace::StackTrace() {}

StackTrace::~StackTrace() {}

StackTrace::StackTrace(
    std::unique_ptr<backward::StackTrace>&& st_ )
  : st( std::move( st_ ) ) {}

StackTrace::StackTrace( StackTrace&& st_ )
  : st( std::move( st_.st ) ) {}
#endif

ND StackTrace stack_trace_here() {
#ifdef STACK_TRACE_ON
  auto st = std::make_unique<backward::StackTrace>();
  st->load_here( 32 );
  return StackTrace( std::move( st ) );
#else
  return StackTrace{};
#endif
}

void print_stack_trace( StackTrace const& st_, int skip ) {
#ifdef STACK_TRACE_ON
  backward::StackTrace st = *( st_.st );
  // Skip uninteresting stack frames
  st.skip_n_firsts( skip );
  backward::Printer p;
  p.print( st, stderr );
#else
  (void)st_;
  (void)skip;
  std::cerr << "(stack trace unavailable: binary built without "
               "support for it)\n";
#endif
}

void die( char const* file, int line, std::string_view msg ) {
#ifdef STACK_TRACE_ON
  (void)file;
  (void)line;
  std::string result( msg );
#else
  std::string result =
      fmt::format( "\n{}:{}:\n{}", file, line, msg );
#endif
  auto st = stack_trace_here();
  throw exception_with_bt( result, std::move( st ) );
}

namespace detail {

std::string check_msg( char const*        expr,
                       std::string const& msg ) {
  std::string suffix = msg.empty() ? "." : ( ": " + msg );
  return fmt::format( "CHECK( {} ) failed{}", expr, suffix );
}

} // namespace detail

} // namespace rn
