/****************************************************************
**indexer.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-06-10.
*
* Description: Unit tests for the src/luapp/indexer.* module.
*
*****************************************************************/
#include "test/testing.hpp"

// Under test.
#include "src/luapp/indexer.hpp"

// Testing
#include "test/luapp/common.hpp"

// luapp
#include "src/luapp/c-api.hpp"
#include "src/luapp/ext-base.hpp"
#include "src/luapp/func-push.hpp"
#include "src/luapp/thing.hpp"

// Must be last.
#include "test/catch-common.hpp"

FMT_TO_CATCH( ::lua::thing );
FMT_TO_CATCH( ::lua::any );

namespace lua {
namespace {

using namespace std;

using ::base::maybe;
using ::base::nothing;
using ::base::valid;

template<typename Derived>
struct TableBase : reference {
  TableBase( cthread st, int ref ) : reference( st, ref ) {}

  template<typename U>
  auto operator[]( U&& idx ) noexcept {
    return indexer<U, Derived>( std::forward<U>( idx ),
                                static_cast<Derived&>( *this ) );
  }
};

struct EmptyTable : TableBase<EmptyTable> {
  using Base = TableBase<EmptyTable>;

  EmptyTable( cthread L )
    : Base( L, [L] {
        c_api C( L );
        CHECK( C.dostring( "return {}" ) );
        return C.ref_registry();
      }() ) {}
};

struct GlobalTable : TableBase<EmptyTable> {
  using Base = TableBase<EmptyTable>;

  GlobalTable( cthread L )
    : Base( L, [L] {
        c_api C( L );
        C.pushglobaltable();
        return C.ref_registry();
      }() ) {}
};

struct SomeTable : TableBase<EmptyTable> {
  using Base = TableBase<EmptyTable>;

  SomeTable( cthread L )
    : Base( L, [L] {
        c_api C( L );
        CHECK( C.dostring( R"(
          return {
            [5] = {
              [1] = {
                hello = "payload"
              }
            }
          }
        )" ) );
        return C.ref_registry();
      }() ) {}
};

LUA_TEST_CASE( "[indexer] construct" ) {
  EmptyTable mt( L );

  indexer idxr1 = mt[5];
  indexer idxr2 = mt["5"];
  int     n     = 2;
  indexer idxr3 = mt[n];

  (void)idxr1;
  (void)idxr2;
  (void)idxr3;
}

LUA_TEST_CASE( "[indexer] index" ) {
  EmptyTable mt( L );

  auto idxr = mt[5][1]["hello"]['c'][string( "hello" )];

  using expected_t = indexer<
      string,
      indexer<char,
              indexer<char const( & )[6],
                      indexer<int, indexer<int, EmptyTable>>>>>;
  static_assert( is_same_v<decltype( idxr ), expected_t> );
}

LUA_TEST_CASE( "[indexer] push" ) {
  SomeTable mt( L );

  REQUIRE( C.stack_size() == 0 );

  push( C.this_cthread(), mt[5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.get<string>( -1 ) == "payload" );

  push( C.this_cthread(), mt[5][1]["hello"] );
  REQUIRE( C.stack_size() == 2 );
  REQUIRE( C.get<string>( -1 ) == "payload" );

  C.pop( 2 );
}

LUA_TEST_CASE( "[indexer] assignment" ) {
  C.openlibs();
  cthread L = C.this_cthread();

  REQUIRE( C.stack_size() == 0 );

  EmptyTable mt( L );
  mt[5]             = EmptyTable( L );
  mt[5][1]          = EmptyTable( L );
  mt[5][1]["hello"] = "payload";

  push( C.this_cthread(), mt[5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::string );
  REQUIRE( C.get<string>( -1 ) == "payload" );
  C.pop();

  mt[5][1]["hello"] = 42;
  push( C.this_cthread(), mt[5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::number );
  REQUIRE( C.get<int>( -1 ) == 42 );
  C.pop();

  mt[5][1]["hello"] = "world";
  push( C.this_cthread(), mt[5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::string );
  REQUIRE( C.get<string>( -1 ) == "world" );
  C.pop();

  mt[5]["x"] = SomeTable( L );
  push( C.this_cthread(), mt[5]["x"][5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::string );
  REQUIRE( C.get<string>( -1 ) == "payload" );
  C.pop();

  mt[5]["x"][5][1]["hello"] = true;
  push( C.this_cthread(), mt[5]["x"][5][1]["hello"] );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::boolean );
  REQUIRE( C.get<bool>( -1 ) == true );
  C.pop();

  // NOTE: since key iteration order can be non-deterministic, we
  // should only use this with simple tables that have at most
  // one key per table.
  char const* dump_table = R"(
    function dump( o )
      if type( o ) == 'table' then
        local s = '{ '
        for k,v in pairs( o ) do
          if type( k ) ~= 'number' then k = "'" .. k .. "'" end
          s = s .. '['..k..'] = ' .. dump( v ) .. ','
        end
        return s .. '} '
      else
        return tostring( o )
      end
    end
  )";
  CHECK( C.dostring( dump_table ) == valid );

  GlobalTable{ L }["my_table"] = mt;
  CHECK( C.dostring( R"(
    return dump( my_table )
  )" ) == valid );
  REQUIRE( C.stack_size() == 1 );
  REQUIRE( C.type_of( -1 ) == type::string );
  // The following is what we're expecting, modulo some spacing.
  //  table = {
  //    [5] = {
  //      [1] = {
  //        ['hello'] = world,
  //      },
  //      ['x'] = {
  //        [5] = {
  //          [1] = {
  //            ['hello'] = true,
  //          },
  //        },
  //      },
  //    },
  //  }
  string full_table =
      "{ [5] = { [1] = { ['hello'] = world,} ,['x'] = { [5] = { "
      "[1] = { ['hello'] = true,} ,} ,} ,} ,} ";
  REQUIRE( *C.get<string>( -1 ) == full_table );
  C.pop();
}

LUA_TEST_CASE( "[indexer] equality" ) {
  EmptyTable mt( L );
  mt[5]             = EmptyTable( L );
  mt[5][1]          = EmptyTable( L );
  mt[5][1]["hello"] = "payload";

  // Use extra parens here because Catch's expression template
  // mechanism messes with our equality.
  REQUIRE( ( mt[5] == mt[5] ) );
  REQUIRE( ( mt[5] != 0 ) );
  REQUIRE( ( mt[5] != nil ) );
  REQUIRE( ( mt[6] == nil ) );
  REQUIRE( ( mt[5][1] != nil ) );
  REQUIRE( ( mt[5][1]["hello"] != "hello" ) );
  REQUIRE( ( mt[5][1]["hello"] == "payload" ) );

  REQUIRE( ( mt[5][1]["hello"] == mt[5][1]["hello"] ) );
}

LUA_TEST_CASE( "[indexer] assignment to maybe" ) {
  EmptyTable mt( L );
  mt[5]             = EmptyTable( L );
  mt[5][1]          = EmptyTable( L );
  mt[5][1]["hello"] = "payload";
  mt[5][1][2]       = 7.7;

  static_assert( Gettable<maybe<int>> );

  SECTION( "from nil" ) {
    auto mb = mt[5][1]["xxx"].as<maybe<bool>>();
    auto mi = mt[5][1]["xxx"].as<maybe<int>>();
    auto ms = mt[5][1]["xxx"].as<maybe<string>>();
    auto md = mt[5][1]["xxx"].as<maybe<double>>();
    REQUIRE( mb == false );
    REQUIRE( mi == nothing );
    REQUIRE( ms == nothing );
    REQUIRE( md == nothing );
  }

  SECTION( "from string" ) {
    auto mb = mt[5][1]["hello"].as<maybe<bool>>();
    auto mi = mt[5][1]["hello"].as<maybe<int>>();
    auto ms = mt[5][1]["hello"].as<maybe<string>>();
    auto md = mt[5][1]["hello"].as<maybe<double>>();
    REQUIRE( mb == true );
    REQUIRE( mi == nothing );
    REQUIRE( ms == "payload" );
    REQUIRE( md == nothing );
  }

  SECTION( "from double" ) {
    auto mb = mt[5][1][2].as<maybe<bool>>();
    auto mi = mt[5][1][2].as<maybe<int>>();
    auto ms = mt[5][1][2].as<maybe<string>>();
    auto md = mt[5][1][2].as<maybe<double>>();
    REQUIRE( mb == true );
    REQUIRE( mi == nothing );
    REQUIRE( ms == "7.7" );
    REQUIRE( md == 7.7 );
  }
}

LUA_TEST_CASE( "[indexer] cpp from cpp via lua" ) {
  C.openlibs();

  st["go"] = []( int n, string const& s, double d ) -> string {
    return fmt::format( "args: n={}, s='{}', d={}", n, s, d );
  };
  thing th = st["go"];
  REQUIRE( th.is<rfunction>() );

  any a = st["go"]( 3, "hello", 3.6 );
  REQUIRE( a == "args: n=3, s='hello', d=3.6" );

  string s = st["go"].call<string>( 3, "hello", 3.6 );
  REQUIRE( s == "args: n=3, s='hello', d=3.6" );

  th = a;
  REQUIRE( th.is<rstring>() );
  REQUIRE( th == "args: n=3, s='hello', d=3.6" );

  th = st["go"]( 4, "hello", 3.6 );
  REQUIRE( th.is<rstring>() );
  REQUIRE( th == "args: n=4, s='hello', d=3.6" );

  REQUIRE( st["go"]( 3, "hello", 3.7 ) ==
           "args: n=3, s='hello', d=3.7" );
}

LUA_TEST_CASE( "[indexer] cpp->lua->cpp round trip" ) {
  C.openlibs();

  int bad_value = 4;

  st["go"] = [this, bad_value]( int n, string const& s,
                                double d ) -> string {
    if( n == bad_value ) C.error( "n cannot be 4." );
    return fmt::format( "args: n={}, s='{}', d={}", n, s, d );
  };
  thing th = st["go"];
  REQUIRE( th.is<rfunction>() );

  REQUIRE( C.dostring( R"(
    function foo( n, s, d )
      assert( n ~= nil, 'n is nil' )
      assert( s ~= nil, 's is nil' )
      assert( d ~= nil, 'd is nil' )
      return go( n, s, d )
    end
  )" ) == valid );

  // call with no errors.
  REQUIRE( st["foo"]( 3, "hello", 3.6 ) ==
           "args: n=3, s='hello', d=3.6" );
  rstring s = st["go"].call<rstring>( 5, "hello", 3.6 );
  REQUIRE( s == "args: n=5, s='hello', d=3.6" );

  // pcall with no errors.
  REQUIRE( st["foo"].pcall( 3, "hello", 3.6 ) ==
           "args: n=3, s='hello', d=3.6" );

  // pcall with error coming from Lua function.
  // clang-format off
  char const* err =
    "[string \"...\"]:4: s is nil\n"
    "stack traceback:\n"
    "\t[C]: in function 'assert'\n"
    "\t[string \"...\"]:4: in function 'foo'";
  // clang-format on
  REQUIRE( st["foo"].pcall( 3, nil, 3.6 ) ==
           lua_unexpected<any>( err ) );

  // pcall with error coming from C function.
  // clang-format off
  err =
    "[string \"...\"]:6: n cannot be 4.\n"
    "stack traceback:\n"
    "\t[C]: in function 'go'\n"
    "\t[string \"...\"]:6: in function 'foo'";
  // clang-format on
  REQUIRE( st["foo"].pcall( 4, "hello", 3.6 ) ==
           lua_unexpected<any>( err ) );
}

} // namespace
} // namespace lua
