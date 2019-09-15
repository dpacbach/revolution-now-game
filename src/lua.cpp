/****************************************************************
**lua.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-09-13.
*
* Description: Interface to lua.
*
*****************************************************************/
#include "lua.hpp"

// Revolution Now
#include "aliases.hpp"
#include "errors.hpp"
#include "fmt-helper.hpp"
#include "init.hpp"
#include "logging.hpp"
#include "util.hpp"

// base-util
#include "base-util/string.hpp"
#include "base-util/type-map.hpp"

// Abseil
#include "absl/strings/str_replace.h"

// Won't be needed in future versions.
#define SOL_CXX17_FEATURES 1

// Maybe can turn this off at some point?
#define SOL_ALL_SAFETIES_ON 1

#ifdef L
#  undef L
#  include "sol/sol.hpp"
#  define L( a ) []( auto const& _ ) { return a; }
#else
#  include "sol/sol.hpp"
#endif

using namespace std;

namespace rn {

namespace {

sol::state g_lua;

expect<monostate> lua_script_( string_view script ) {
  auto result =
      g_lua.safe_script( script, sol::script_pass_on_error );
  if( !result.valid() ) {
    sol::error err = result;
    return UNEXPECTED( err.what() );
  }
  return monostate{};
}

using IntermediateCppTypeMap = TypeMap< //
    KV<int, double>                     //
    >;

template<typename Ret>
expect<Ret> sol_obj_convert( sol::object const& o ) {
  using intermediate_t = Get<IntermediateCppTypeMap, Ret, Ret>;
  if( !o.is<intermediate_t>() ) {
    string via =
        is_same_v<Ret, intermediate_t>
            ? ""
            : ( " (via `"s +
                demangled_typename<intermediate_t>() + "`)" );
    return UNEXPECTED( fmt::format(
        "expected type `{}`{} but got `{}`.",
        demangled_typename<Ret>(), via,
        sol::type_name( g_lua.lua_state(), o.get_type() ) ) );
  }
  auto intermediate_res = o.as<intermediate_t>();
  return static_cast<Ret>( intermediate_res );
}

template<typename Ret>
expect<Ret> lua_script( string_view script ) {
  auto result =
      g_lua.safe_script( script, sol::script_pass_on_error );
  if( !result.valid() ) {
    sol::error err = result;
    return UNEXPECTED( err.what() );
  }
  return sol_obj_convert<Ret>( result.get<sol::object>() );
}

void init_lua() {
  CHECK( g_lua["log"] == sol::nil );
  g_lua["log"].get_or_create<sol::table>();
  g_lua["log"]["info"] = []( string const& msg ) {
    lg.info( "{}", msg );
  };
  g_lua["log"]["debug"] = []( string const& msg ) {
    lg.debug( "{}", msg );
  };
  g_lua["log"]["warn"] = []( string const& msg ) {
    lg.warn( "{}", msg );
  };
  g_lua["log"]["error"] = []( string const& msg ) {
    lg.error( "{}", msg );
  };
  g_lua["log"]["critical"] = []( string const& msg ) {
    lg.critical( "{}", msg );
  };
  g_lua["print"] = g_lua["log"]["info"];
}

void cleanup_lua() { g_lua = sol::state{}; }

REGISTER_INIT_ROUTINE( lua );

} // namespace

/****************************************************************
** Public API
*****************************************************************/
template<>
expect<monostate> lua<void>( string const& script ) {
  return lua_script_( script );
}

template<>
expect<string> lua<string>( string const& script ) {
  return lua_script<string>( script );
}

Vec<Str> format_lua_error_msg( Str const& msg ) {
  Vec<Str> res;
  for( auto const& line : util::split_on_any( msg, "\n\r" ) )
    if( !line.empty() ) //
      res.push_back(
          absl::StrReplaceAll( line, {{"\t", "  "}} ) );
  return res;
}

/****************************************************************
** Testing
*****************************************************************/
void test_lua() {
  sol::state lua;
  // int        x = 0;
  // lua.open_libraries( sol::lib::base );
  // lua.set_function( "beep", [&x]() { ++x; } );
  auto result = lua_script<int>( "return 56.4" );
  lg.info( "result: {}", result );
}

} // namespace rn
