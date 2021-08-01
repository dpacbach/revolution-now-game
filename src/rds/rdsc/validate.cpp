/****************************************************************
**validate.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2020-11-13.
*
* Description: [FILL ME IN]
*
*****************************************************************/
#include "validate.hpp"

// rdsc
#include "rds-util.hpp"

// base
#include "base/lambda.hpp"

// {fmt}
#include "fmt/format.h"

// C++ standard library.
#include <string_view>
#include <unordered_set>

using namespace std;

namespace rds {

namespace {

struct Validator {
  vector<string> errors_;

  template<typename Arg1, typename... Args>
  void error( string_view fmt_str, Arg1&& arg1,
              Args&&... args ) {
    errors_.push_back( fmt::format( fmt_str,
                                    forward<Arg1>( arg1 ),
                                    forward<Args>( args )... ) );
  }

  void error( string_view msg ) { errors_.emplace_back( msg ); }

  void validate_sumtype( expr::Sumtype const& sumtype ) {
    using F = expr::e_sumtype_feature;
    if( sumtype.features.has_value() ) {
      unordered_set<F> features( sumtype.features->begin(),
                                 sumtype.features->end() );
      bool             has_tmpl = !sumtype.tmpl_params.empty();

      // If the sumtype is templated then we do not support
      // serialization. This wouldn't make sense since there
      // can only be one concrete type to which the variant is
      // serialized to flatbuffers.
      if( has_tmpl && features.contains( F::serializable ) )
        error(
            "The sumtype \"{}\" cannot be both templated and "
            "serializable.",
            sumtype.name );
    }
  }

  void validate_sumtypes( expr::Rds const& rds ) {
    perform_on_sumtypes( rds, LC( validate_sumtype( _ ) ) );
  }
};

} // namespace

vector<string> validate( expr::Rds const& rds ) {
  Validator validator;
  validator.validate_sumtypes( rds );
  return move( validator.errors_ );
}

} // namespace rds