/****************************************************************
**colony.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-12-15.
*
* Description: Data structure representing a Colony.
*
*****************************************************************/
#include "colony.hpp"

// Revolution Now
#include "errors.hpp"
#include "lua.hpp"

using namespace std;

namespace rn {

namespace {} // namespace

/****************************************************************
** Public API
*****************************************************************/

string debug_string( Colony const& ) { NOT_IMPLEMENTED; }

} // namespace rn

/****************************************************************
** Lua Bindings
*****************************************************************/
namespace {

LUA_STARTUP( sol::state& st ) {
  using C = ::rn::Colony;

  sol::usertype<C> c =
      st.new_usertype<C>( "Colony", sol::no_constructor );

  // Getters.
  c["id"]        = &C::id;
  c["nation"]    = &C::nation;
  c["name"]      = &C::name;
  c["location"]  = &C::location;
  c["sentiment"] = &C::sentiment;
};

} // namespace
