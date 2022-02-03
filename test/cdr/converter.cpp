/****************************************************************
**converter.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-01-29.
*
* Description: Unit tests for the src/cdr/converter.* module.
*
*****************************************************************/
#include "test/testing.hpp"

// Under test.
#include "src/cdr/converter.hpp"

// cdr
#include "src/cdr/ext-builtin.hpp"
#include "src/cdr/ext-std.hpp"

// base
#include "src/base/to-str-ext-std.hpp"

// Must be last.
#include "test/catch-common.hpp"

namespace cdr {
namespace {

using namespace std;

converter conv;

TEST_CASE( "[cdr/converter] unordered_map" ) {
  using M  = unordered_map<string, int>;
  value  v = list{ table{ { "key", "one" }, { "val", 1 } },
                  table{ { "key", "two" }, { "val", "2" } } };
  string expected =
      "message: failed to convert value of type string to int.\n"
      "frame trace (most recent frame last):\n"
      "---------------------------------------------------\n"
      "std::unordered_map<std::string, int, "
      "std::hash<std::string>, s...\n"
      " \\-index 1\n"
      "    \\-std::pair<std::string const, int>\n"
      "       \\-value for key 'val'\n"
      "          \\-int\n"
      "---------------------------------------------------";
  REQUIRE( run_conversion_from_canonical<M>( v ) ==
           conv.err( expected ) );
}

} // namespace
} // namespace cdr