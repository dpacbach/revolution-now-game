/****************************************************************
**lua.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-09-18.
*
* Description: Unit tests for lua integration.
*
*****************************************************************/
#include "testing.hpp"

#define LUA_MODULE_NAME_OVERRIDE "testing"

// Revolution Now
#include "coord.hpp"
#include "lua.hpp"

// luapp
#include "luapp/as.hpp"
#include "luapp/error.hpp"
#include "luapp/ext-base.hpp"
#include "luapp/rstring.hpp"
#include "luapp/state.hpp"

// Must be last.
#include "catch-common.hpp"

namespace {

using namespace std;
using namespace rn;

using Catch::Contains;
using Catch::Equals;

TEST_CASE( "[lua] run trivial script" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    local x = 5+6
  )";
  REQUIRE( st.script.run_safe( script ) == valid );
}

TEST_CASE( "[lua] syntax error" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    local x =
  )";

  auto xp = st.script.run_safe( script );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT( xp.error(), Contains( "unexpected symbol" ) );
}

TEST_CASE( "[lua] semantic error" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    local a, b
    local x = a + b
  )";

  auto xp = st.script.run_safe( script );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT( xp.error(),
                Contains( "attempt to perform arithmetic" ) );
}

TEST_CASE( "[lua] has base lib" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    return tostring( 5 ) .. type( function() end )
  )";
  REQUIRE( st.script.run_safe<lua::rstring>( script ) ==
           "5function" );
}

TEST_CASE( "[lua] no implicit conversions from double to int" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    return 5+8.5
  )";
  REQUIRE( st.script.run_safe<maybe<int>>( script ) == nothing );
}

TEST_CASE( "[lua] returns double" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    return 5+8.5
  )";
  REQUIRE( st.script.run_safe<double>( script ) == 13.5 );
}

TEST_CASE( "[lua] returns string" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    local function f( s )
      return s .. '!'
    end
    return f( 'hello' )
  )";
  REQUIRE( st.script.run_safe<lua::rstring>( script ) ==
           "hello!" );
}

// FIXME: need to implement some kind of "from string" method for
// lua enums when registering them, then we can re-enable this
// test.
// TEST_CASE( "[lua] enums exist" ) {
//  auto script = R"(
//    return tostring( e.nation.dutch ) .. type( e.nation.dutch )
//  )";
//  REQUIRE( st.script.run_safe<string>( script ) ==
//  "dutchuserdata" );
//}

TEST_CASE( "[lua] enums no assign" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    e.nation.dutch = 3
  )";

  auto xp = st.script.run_safe( script );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT( xp.error(),
                Contains( "modify a read-only table" ) );
}

TEST_CASE( "[lua] enums from string" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    return e.nation.dutch == e.nation["dutch"]
  )";

  REQUIRE( st.script.run_safe<bool>( script ) == true );
}

TEST_CASE( "[lua] has startup.run" ) {
  lua::state& st = lua_global_state();
  lua_reload();
  auto script = R"(
    return tostring( startup.main )
  )";

  auto xp = st.script.run_safe<lua::rstring>( script );
  REQUIRE( xp.has_value() );
  REQUIRE_THAT( xp.value().as_cpp(), Contains( "function" ) );
}

TEST_CASE( "[lua] C++ function binding" ) {
  lua::state& st = lua_global_state();
  lua_reload();
  auto script = R"(
    local soldier_type =
        utype.UnitType.create( e.unit_type.soldier )
    local soldier_comp = unit_composer
                        .UnitComposition
                        .create_with_type_obj( soldier_type )
    local id1 = old_world.create_unit_in_port( e.nation.dutch, soldier_comp )
    local id2 = old_world.create_unit_in_port( e.nation.dutch, soldier_comp )
    local id3 = old_world.create_unit_in_port( e.nation.dutch, soldier_comp )
    return id3-id1
  )";

  REQUIRE( st.script.run_safe<int>( script ) == 2 );
}

TEST_CASE( "[lua] frozen globals" ) {
  lua::state& st = lua_global_state();
  auto        xp = st.script.run_safe( "e = 1" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "attempt to modify a read-only global" ) );

  xp = st.script.run_safe( "startup = 1" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "attempt to modify a read-only global" ) );

  xp = st.script.run_safe( "startup.x = 1" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "attempt to modify a read-only table:" ) );

  xp = st.script.run_safe( "id = 1" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "attempt to modify a read-only global" ) );

  xp = st.script.run_safe( "id.x = 1" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "attempt to modify a read-only table:" ) );

  REQUIRE( st.script.run_safe<int>( "x = 1; return x" ) == 1 );

  REQUIRE( st.script.run_safe<int>(
               "d = {}; d.x = 1; return d.x" ) == 1 );
}

TEST_CASE( "[lua] rawset is locked down" ) {
  lua::state& st = lua_global_state();
  // `id` is locked down.
  auto xp = st.script.run_safe( "rawset( _ENV, 'id', 3 )" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT( xp.error(), Contains( "nil value" ) );

  // `xxx` is not locked down, but rawset should fail for any
  // key.
  xp = st.script.run_safe( "rawset( _ENV, 'xxx', 3 )" );
  REQUIRE( !xp.valid() );
  REQUIRE_THAT( xp.error(), Contains( "nil value" ) );
}

LUA_FN( throwing, int, int x ) {
  lua::cthread L = lua_global_state().thread.main().cthread();
  if( x >= 10 )
    lua::throw_lua_error(
        L, "x (which is {}) must be less than 10.", x );
  return x + 1;
};

TEST_CASE( "[lua] throwing" ) {
  lua::state& st     = lua_global_state();
  auto        script = "return testing.throwing( 5 )";
  REQUIRE( st.script.run_safe<int>( script ) == 6 );

  script  = "return testing.throwing( 11 )";
  auto xp = st.script.run_safe<int>( script );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error(),
      Contains( "x (which is 11) must be less than 10." ) );
}

LUA_FN( coord_test, Coord, Coord const& coord ) {
  auto new_coord = coord;
  new_coord.x += 1_w;
  new_coord.y += 1_h;
  return new_coord;
}

TEST_CASE( "[lua] Coord" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    local coord = Coord{x=2, y=2}
    -- Test equality.
    assert_eq( coord, Coord{x=2,y=2} )
    -- Test tostring.
    assert_eq( tostring( coord ), "Coord{x=2,y=2}" )

    coord = testing.coord_test( coord )
    -- Test equality.
    assert_eq( coord, Coord{x=3,y=3} )
    -- Test tostring.
    assert_eq( tostring( coord ), "Coord{x=3,y=3}" )

    coord.x = coord.x + 1
    coord.y = coord.y + 2
    return coord
  )";
  REQUIRE( st.script.run_safe<Coord>( script ) ==
           Coord{ 4_x, 5_y } );
}

LUA_FN( opt_test, maybe<string>, maybe<int> const& maybe_int ) {
  if( !maybe_int ) return "got nothing";
  int n = *maybe_int;
  if( n < 5 ) return nothing;
  if( n < 10 ) return "less than 10";
  return to_string( n );
}

LUA_FN( opt_test2, maybe<Coord>,
        maybe<Coord> const& maybe_coord ) {
  if( !maybe_coord ) return Coord{ 5_x, 7_y };
  return Coord{ maybe_coord->x + 1_w, maybe_coord->y + 1_y };
}

TEST_CASE( "[lua] optional" ) {
  lua::state& st = lua_global_state();
  // string/int
  auto script = R"(
    assert( testing.opt_test( nil ) == "got nothing"  )
    assert( testing.opt_test( 0   ) ==  nil           )
    assert( testing.opt_test( 4   ) ==  nil           )
    assert( testing.opt_test( 5   ) == "less than 10" )
    assert( testing.opt_test( 9   ) == "less than 10" )
    assert( testing.opt_test( 10  ) == "10"           )
    assert( testing.opt_test( 100 ) == "100"          )
  )";
  REQUIRE( st.script.run_safe( script ) == valid );

  REQUIRE( st.script.run_safe<maybe<string>>( "return nil" ) ==
           nothing );
  REQUIRE( st.script.run_safe<maybe<string>>(
               "return 'hello'" ) == "hello" );
  REQUIRE( st.script.run_safe<maybe<int>>( "return 'hello'" ) ==
           nothing );

  // Coord
  auto script2 = R"(
    assert( testing.opt_test2( nil            ) == Coord{x=5,y=7} )
    assert( testing.opt_test2( Coord{x=2,y=3} ) == Coord{x=3,y=4} )
  )";
  REQUIRE( st.script.run_safe( script2 ) == valid );

  REQUIRE( st.script.run_safe<maybe<Coord>>( "return nil" ) ==
           nothing );
  REQUIRE( st.script.run_safe<maybe<Coord>>(
               "return Coord{x=9, y=8}" ) == Coord{ 9_x, 8_y } );
  REQUIRE( st.script.run_safe<maybe<Coord>>(
               "return 'hello'" ) == nothing );
  REQUIRE( st.script.run_safe<maybe<Coord>>( "return 5" ) ==
           nothing );
}

// Test the o.as<maybe<?>>() constructs. This tests the custom
// handlers that we've defined for maybe<>.
TEST_CASE( "[lua] get as maybe" ) {
  lua::state& st = lua_global_state();
  st["func"]     = []( lua::any o ) -> string {
    if( o == lua::nil ) return "nil";
    if( lua::type_of( o ) == lua::type::string ) {
      return lua::as<string>( o ) + "!";
    } else if( auto maybe_double = lua::as<maybe<double>>( o );
               maybe_double.has_value() ) {
      return fmt::format( "a double: {}", *maybe_double );
    } else if( auto maybe_bool = lua::as<maybe<bool>>( o );
               maybe_bool.has_value() ) {
      return fmt::format( "a bool: {}", *maybe_bool );
    } else {
      return "?";
    }
  };
  REQUIRE( lua::as<string>( st["func"]( "hello" ) ) ==
           "hello!" );
  REQUIRE( lua::as<string>( st["func"]( 5 ) ) == "a double: 5" );
  REQUIRE( lua::as<string>( st["func"]( true ) ) ==
           "a bool: true" );

  REQUIRE( lua::as<maybe<string>>( st["func"]( false ) ) ==
           "a bool: false" );
}

TEST_CASE( "[lua] new_usertype" ) {
  lua::state& st     = lua_global_state();
  auto        script = R"(
    u = MyType.new()
    assert( u.x == 5 )
    assert( u:get() == "c" )
    assert( u:add( 4, 5 ) == 9+5 )
  )";
  REQUIRE( st.script.run_safe( script ) == valid );
}

} // namespace
