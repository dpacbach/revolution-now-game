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
#include "lua.hpp"

// Must be last.
#include "catch-common.hpp"

namespace {

using namespace std;
using namespace rn;

using Catch::Contains;
using Catch::Equals;

TEST_CASE( "[lua] run trivial script" ) {
  auto script = R"(
    local x = 5+6
  )";
  REQUIRE( lua::run<void>( script ) == valid );
}

TEST_CASE( "[lua] syntax error" ) {
  auto script = R"(
    local x =
  )";

  auto xp = lua::run<void>( script );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT( xp.error().what,
                Contains( "unexpected symbol" ) );
}

TEST_CASE( "[lua] semantic error" ) {
  auto script = R"(
    local a, b
    local x = a + b
  )";

  auto xp = lua::run<void>( script );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT( xp.error().what,
                Contains( "attempt to perform arithmetic" ) );
}

TEST_CASE( "[lua] has base lib" ) {
  auto script = R"(
    return tostring( 5 ) .. type( function() end )
  )";
  REQUIRE( lua::run<string>( script ) == "5function" );
}

TEST_CASE( "[lua] returns int" ) {
  auto script = R"(
    return 5+8.5
  )";
  REQUIRE( lua::run<int>( script ) == 13 );
}

TEST_CASE( "[lua] returns double" ) {
  auto script = R"(
    return 5+8.5
  )";
  REQUIRE( lua::run<double>( script ) == 13.5 );
}

TEST_CASE( "[lua] returns string" ) {
  auto script = R"(
    local function f( s )
      return s .. '!'
    end
    return f( 'hello' )
  )";
  REQUIRE( lua::run<string>( script ) == "hello!" );
}

// FIXME: need to implement some kind of "from string" method for
// lua enums when registering them, then we can re-enable this
// test.
// TEST_CASE( "[lua] enums exist" ) {
//  auto script = R"(
//    return tostring( e.nation.dutch ) .. type( e.nation.dutch )
//  )";
//  REQUIRE( lua::run<string>( script ) == "dutchuserdata" );
//}

TEST_CASE( "[lua] enums no assign" ) {
  auto script = R"(
    e.nation.dutch = 3
  )";

  auto xp = lua::run<void>( script );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT( xp.error().what,
                Contains( "modify a read-only table" ) );
}

TEST_CASE( "[lua] enums from string" ) {
  auto script = R"(
    return e.nation.dutch == e.nation_from_string( "dutch" )
  )";

  REQUIRE( lua::run<bool>( script ) == true );
}

TEST_CASE( "[lua] has startup.run" ) {
  lua::reload();
  auto script = R"(
    return tostring( startup.main )
  )";

  auto xp = lua::run<string>( script );
  REQUIRE( xp.has_value() );
  REQUIRE_THAT( xp.value(), Contains( "function" ) );
}

TEST_CASE( "[lua] C++ function binding" ) {
  lua::reload();
  auto script = R"(
    local id1 = europort.create_unit_in_port( e.nation.dutch, e.unit_type.soldier )
    local id2 = europort.create_unit_in_port( e.nation.dutch, e.unit_type.soldier )
    local id3 = europort.create_unit_in_port( e.nation.dutch, e.unit_type.soldier )
    return id3-id1
  )";

  REQUIRE( lua::run<int>( script ) == 2 );
}

TEST_CASE( "[lua] frozen globals" ) {
  auto xp = lua::run<void>( "e = 1" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only global" ) );

  xp = lua::run<void>( "startup = 1" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only global" ) );

  xp = lua::run<void>( "startup.x = 1" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only table." ) );

  xp = lua::run<void>( "id = 1" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only global" ) );

  xp = lua::run<void>( "id.x = 1" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only table." ) );

  REQUIRE( lua::run<int>( "x = 1; return x" ) == 1 );

  REQUIRE( lua::run<int>( "d = {}; d.x = 1; return d.x" ) == 1 );
}

TEST_CASE( "[lua] rawset is locked down" ) {
  auto xp = lua::run<void>( "rawset( _ENV, 'xxx', 3 )" );
  REQUIRE( xp );
  REQUIRE( lua::run<int>( "return xxx" ) == 3 );

  // `id` is locked down.
  xp = lua::run<void>( "rawset( _ENV, 'id', 3 )" );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "attempt to modify a read-only global" ) );
}

TEST_CASE( "[lua] has modules" ) {
  auto script = R"lua(
    assert( modules['startup'] ~= nil )
    assert( modules['util']    ~= nil )
    assert( modules['meta']    ~= nil )
    assert( modules['utype']   ~= nil )
  )lua";
  REQUIRE( lua::run<void>( script ) == valid );
}

LUA_FN( throwing, int, int x ) {
  if( x >= 10 )
    THROW_LUA_ERROR( "x (which is {}) must be less than 10.",
                     x );
  return x + 1;
};

TEST_CASE( "[lua] throwing" ) {
  auto script = "return testing.throwing( 5 )";
  REQUIRE( lua::run<int>( script ) == 6 );

  script  = "return testing.throwing( 11 )";
  auto xp = lua::run<int>( script );
  REQUIRE( !xp.has_value() );
  REQUIRE_THAT(
      xp.error().what,
      Contains( "x (which is 11) must be less than 10." ) );
}

LUA_FN( coord_test, Coord, Coord const& coord ) {
  auto new_coord = coord;
  new_coord.x += 1_w;
  new_coord.y += 1_h;
  return new_coord;
}

TEST_CASE( "[lua] Coord" ) {
  auto script = R"(
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
  REQUIRE( lua::run<Coord>( script ) == Coord{ 4_x, 5_y } );
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
  REQUIRE( lua::run<void>( script ) == valid );

  REQUIRE( lua::run<maybe<string>>( "return nil" ) == nothing );
  REQUIRE( lua::run<maybe<string>>( "return 'hello'" ) ==
           "hello" );
  REQUIRE( lua::run<maybe<int>>( "return 'hello'" ) == nothing );

  // Coord
  auto script2 = R"(
    assert( testing.opt_test2( nil            ) == Coord{x=5,y=7} )
    assert( testing.opt_test2( Coord{x=2,y=3} ) == Coord{x=3,y=4} )
  )";
  REQUIRE( lua::run<void>( script2 ) == valid );

  REQUIRE( lua::run<maybe<Coord>>( "return nil" ) == nothing );
  REQUIRE( lua::run<maybe<Coord>>( "return Coord{x=9, y=8}" ) ==
           Coord{ 9_x, 8_y } );
  REQUIRE( lua::run<maybe<Coord>>( "return 'hello'" ) ==
           nothing );
  REQUIRE( lua::run<maybe<Coord>>( "return 5" ) == nothing );
}

// Test the o.as<maybe<?>>() constructs. This tests the custom
// handlers that we've defined for maybe<>.
TEST_CASE( "[lua] get as maybe" ) {
  sol::state st{};
  st["func"] = []( sol::object o ) -> string {
    if( o == sol::lua_nil ) return "nil";
    if( auto maybe_string = o.as<maybe<string>>();
        maybe_string.has_value() ) {
      return *maybe_string + "!";
    } else if( auto maybe_bool = o.as<maybe<bool>>();
               maybe_bool.has_value() ) {
      return fmt::format( "a bool: {}", *maybe_bool );
    } else if( auto maybe_double = o.as<maybe<double>>();
               maybe_double.has_value() ) {
      return fmt::format( "a double: {}", *maybe_double );
    } else {
      return "?";
    }
  };
  REQUIRE( st["func"]( "hello" ).get<string>() == "hello!" );
  REQUIRE( st["func"]( 5 ).get<string>() == "a double: 5.0" );
  REQUIRE( st["func"]( true ).get<string>() == "a bool: true" );

  REQUIRE( st["func"]( false ).get<maybe<string>>() ==
           "a bool: false" );
  REQUIRE( st["func"]( true ).get<maybe<int>>() == nothing );
}

TEST_CASE( "[lua] new_usertype" ) {
  auto script = R"(
    u = MyType.new()
    assert( u.x == 5 )
    assert( u:get() == "c" )
    assert( u:add( 4, 5 ) == 9 )
  )";
  REQUIRE( lua::run<void>( script ) == valid );
}

} // namespace
