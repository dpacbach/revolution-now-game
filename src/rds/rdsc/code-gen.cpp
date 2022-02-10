/****************************************************************
**code-gen.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2020-11-11.
*
* Description: Code generator for the RDS language.
*
*****************************************************************/
#include "code-gen.hpp"

// rdsc
#include "rds-util.hpp"

// base
#include "base/lambda.hpp"
#include "base/maybe.hpp"
#include "base/meta.hpp"

// {fmt}
#include "fmt/format.h"

// Abseil
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

// c++ standard library
#include <iomanip>
#include <sstream>
#include <stack>

using namespace std;

namespace rds {

namespace {

using ::base::maybe;

// Parameters:
//   - member_var_name
constexpr string_view kSumtypeAlternativeMemberSerial = R"xyz(
    auto s_{member_var_name} = serialize<::rn::serial::fb_serialize_hint_t<
        decltype( std::declval<fb_target_t>().{member_var_name}() )>>(
        builder, {member_var_name}, ::rn::serial::ADL{{}} );
)xyz";

// Parameters:
//   - member_var_name
constexpr string_view kSumtypeAlternativeMemberDeserial = R"xyz(
    HAS_VALUE_OR_RET( deserialize(
        ::rn::serial::detail::to_const_ptr( src.{member_var_name}() ),
        &dst->{member_var_name}, ::rn::serial::ADL{{}} ) );
)xyz";

// Parameters:
//   - sumtype_name
//   - alt_name
//   - members_serialization
//   - members_deserialization
//   - members_s_get:
//       Vertical comma-separated list of "s_<member>.get()"
constexpr string_view kSumtypeAlternativeSerial = R"xyz(
  using fb_target_t = fb::{sumtype_name}::{alt_name};

  rn::serial::FBOffset<fb::{sumtype_name}::{alt_name}> serialize_table(
      rn::serial::FBBuilder& builder ) const {{
    using ::rn::serial::serialize;
    {members_serialization}
    // We must always serialize this table even if it is
    // empty/default-valued because, for variants, its presence
    // indicates that it is the active alternative.
    return fb::{sumtype_name}::Create{alt_name}( builder
        {members_s_get}
    );
  }}

  static ::rn::valid_deserial_t deserialize_table(
      fb::{sumtype_name}::{alt_name} const& src,
      {alt_name}* dst ) {{
    (void)src;
    (void)dst;
    DCHECK( dst );
    using ::rn::serial::deserialize;
    {members_deserialization}
    return ::rn::valid;
  }}

  ::rn::valid_deserial_t check_invariants_safe() const {{
    return ::rn::valid;
  }}
)xyz";

void remove_common_space_prefix( vector<string>* lines ) {
  if( lines->empty() ) return;
  size_t min_spaces = 10000000;
  for( string_view sv : *lines ) {
    size_t first = sv.find_first_not_of( ' ' );
    if( first == string_view::npos ) continue;
    min_spaces = std::min( first, min_spaces );
  }
  for( string& s : *lines ) {
    if( string_view( s ).find_first_not_of( ' ' ) ==
        string_view::npos )
      // Either empty or just spaces.
      continue;
    string new_s( s.begin() + min_spaces, s.end() );
    s = std::move( new_s );
  }
}

template<typename Range, typename Projection, typename Default>
auto max_of( Range&& rng, Projection&& proj, Default value )
    -> invoke_result_t<Projection,
                       typename decay_t<Range>::value_type> {
  if( rng.empty() ) return value;
  auto t = proj( *rng.begin() );
  for( auto&& elem : forward<Range>( rng ) ) {
    auto p = proj( elem );
    if( p > t ) t = p;
  }
  return t;
}

string template_params( vector<expr::TemplateParam> const& tmpls,
                        bool put_typename, bool space = true ) {
  if( tmpls.empty() ) return "";
  vector<string> names;
  string         tp_name = put_typename ? "typename " : "";
  for( expr::TemplateParam const& param : tmpls )
    names.push_back( tp_name + param.param );
  string sep = ",";
  if( space ) sep += ' ';
  return "<"s + absl::StrJoin( names, sep ) + ">";
}

string all_int_tmpl_params( int count ) {
  vector<expr::TemplateParam> params(
      count, expr::TemplateParam{ "int" } );
  return template_params( params, /*put_typename=*/false,
                          /*space=*/true );
}

template<typename T>
bool item_has_feature( T const& item, expr::e_feature feature ) {
  return item.features.has_value() &&
         item.features->contains( feature );
}

string trim_trailing_spaces( string s ) {
  string_view sv             = s;
  auto        last_non_space = sv.find_last_not_of( ' ' );
  if( last_non_space != string_view::npos ) {
    auto trim_start = last_non_space + 1;
    sv.remove_suffix( sv.size() - trim_start );
  }
  return string( sv );
}

struct CodeGenerator {
  struct Options {
    int  indent_level = 0;
    bool quotes       = false;

    bool operator==( Options const& ) const = default;
    bool operator!=( Options const& ) const = default;
  };

  ostringstream oss_;
  maybe<string> curr_line_;
  Options       default_options_ = {};

  stack<Options> options_;

  Options& options() {
    if( options_.empty() ) return default_options_;
    return options_.top();
  }

  Options const& options() const {
    if( options_.empty() ) return default_options_;
    return options_.top();
  }

  void push( Options options ) {
    options_.push( move( options ) );
  }

  void pop() {
    assert( !options_.empty() );
    options_.pop();
  }

  struct [[nodiscard]] AutoPopper {
    CodeGenerator* gen_;
    AutoPopper( CodeGenerator& gen ) : gen_( &gen ) {
      assert( gen_ != nullptr );
    }
    AutoPopper( AutoPopper const& ) = delete;
    AutoPopper& operator=( AutoPopper const& ) = delete;
    AutoPopper& operator=( AutoPopper&& ) = delete;
    AutoPopper( AutoPopper&& rhs ) {
      gen_     = rhs.gen_;
      rhs.gen_ = nullptr;
    }
    ~AutoPopper() {
      if( gen_ ) gen_->pop();
    }
    void cancel() { gen_ = nullptr; }
  };

  AutoPopper indent( int levels = 1 ) {
    push( options() );
    options().indent_level += levels;
    return AutoPopper( *this );
  }

  AutoPopper quoted() {
    push( options() );
    options().quotes = true;
    return AutoPopper( *this );
  }

  string result() const {
    assert( !curr_line_.has_value() );
    assert( options_.empty() );
    assert( options() == Options{} );
    return oss_.str();
  }

  // Format string should not contain any new lines in it. This
  // is the only function that should be using oss_.
  template<typename Arg1, typename... Args>
  void line( string_view fmt_str, Arg1&& arg1, Args&&... args ) {
    assert( !curr_line_.has_value() );
    assert( fmt_str.find_first_of( "\n" ) == string_view::npos );
    string indent( options().indent_level * 2, ' ' );
    string to_print = trim_trailing_spaces( fmt::format(
        fmt::runtime( fmt_str ), forward<Arg1>( arg1 ),
        forward<Args>( args )... ) );
    // Only print empty strings if they are to be quoted.
    if( options().quotes )
      oss_ << indent << std::quoted( to_print );
    else if( !to_print.empty() )
      oss_ << indent << to_print;
    oss_ << "\n";
  }

  // Braces {} do NOT have to be escaped for this one.
  void line( string_view l ) { line( "{}", l ); }

  template<typename Arg1, typename... Args>
  void frag( string_view fmt_str, Arg1&& arg1, Args&&... args ) {
    assert( fmt_str.find_first_of( "\n" ) == string_view::npos );
    if( !curr_line_.has_value() ) curr_line_.emplace();
    curr_line_ = absl::StrCat(
        *curr_line_, fmt::format( fmt::runtime( fmt_str ),
                                  std::forward<Arg1>( arg1 ),
                                  forward<Args>( args )... ) );
  }

  // Braces {} do NOT have to be escaped for this one.
  void frag( string_view l ) { frag( "{}", l ); }

  void flush() {
    if( !curr_line_.has_value() ) return;
    string to_write = move( *curr_line_ );
    curr_line_.reset();
    line( to_write );
  }

  void newline() { line( "" ); }

  template<typename... Args>
  void comment( string_view fmt_str, Args&&... args ) {
    frag( "// " );
    frag( "{}", fmt::format( fmt::runtime( fmt_str ),
                             forward<Args>( args )... ) );
    flush();
  }

  void section( string_view section ) {
    const int line_width = 65;
    line( "/{}", string( line_width - 1, '*' ) );
    line( "*{: ^{}}", section, line_width - 2 );
    line( "{}/", string( line_width, '*' ) );
  }

  void emit_vert_list( vector<string> const& lines,
                       string_view           sep ) {
    int count = lines.size();
    for( string const& l : lines ) {
      if( count-- == 1 ) sep = "";
      line( "{}{}", l, sep );
    }
  }

  void open_ns( string_view ns, string_view leaf = "" ) {
    frag( "namespace {}", ns );
    if( !leaf.empty() ) frag( "::{}", leaf );
    frag( " {" );
    flush();
    newline();
    indent().cancel();
  }

  void close_ns( string_view ns, string_view leaf = "" ) {
    pop();
    frag( "}} // namespace {}", ns );
    if( !leaf.empty() ) frag( "::{}", leaf );
    flush();
  }

  template<typename... Args>
  void emit_code_block( string_view fmt_str, Args&&... args ) {
    string formatted     = fmt::format( fmt::runtime( fmt_str ),
                                        forward<Args>( args )... );
    vector<string> lines = absl::StrSplit( formatted, "\n" );
    remove_common_space_prefix( &lines );
    if( lines.empty() ) return;
    int i = 0;
    // Remove the first line if it's empty.
    if( lines[0].empty() ) i = 1;
    for( ; i < int( lines.size() ); ++i ) line( lines[i] );
  }

  void emit_template_decl(
      vector<expr::TemplateParam> const& tmpls ) {
    if( tmpls.empty() ) return;
    line( "template{}",
          template_params( tmpls, /*put_typename=*/true ) );
  }

  void emit_format_str_for_formatting_alternative(
      expr::Alternative const&           alt,
      vector<expr::TemplateParam> const& tmpls,
      string_view                        sumtype_name ) {
    auto _ = quoted();
    if( tmpls.empty() )
      frag( "{}::{}", sumtype_name, alt.name );
    else
      frag( "{}::{}<{{}}>", sumtype_name, alt.name );
    if( !alt.members.empty() ) frag( "{{" );
    flush();
    if( !alt.members.empty() ) {
      vector<string> fmt_members;
      for( expr::StructMember const& member : alt.members )
        fmt_members.push_back(
            fmt::format( "{}={{}}", member.var ) );
      {
        auto _ = indent();
        emit_vert_list( fmt_members, "," );
      }
      line( "}}" );
    }
  }

  void emit_sumtype_alternative(
      vector<expr::TemplateParam> const& tmpls,
      expr::Alternative const& alt, string_view sumtype_name,
      bool emit_equality, bool emit_serialization ) {
    emit_template_decl( tmpls );
    if( alt.members.empty() && !emit_equality &&
        !emit_serialization ) {
      line( "struct {} {{}};", alt.name );
    } else {
      line( "struct {} {{", alt.name );
      {
        auto cleanup = indent();
        int  max_type_len =
            max_of( alt.members, L( _.type.size() ), 0 );
        for( expr::StructMember const& alt_mem : alt.members )
          line( "{: <{}} {};", alt_mem.type, max_type_len,
                alt_mem.var );
        if( emit_equality ) {
          comment( "{}",
                   "This requires that the types of the member "
                   "variables " );
          comment( "{}", "also support equality." );
          // We need the 'struct' keyword in fron of the
          // alternative name to disambiguate in cases where
          // there is an alternative member with the same name as
          // the alternative.
          line(
              "bool operator==( struct {} const& ) const = "
              "default;",
              alt.name );
          line(
              "bool operator!=( struct {} const& ) const = "
              "default;",
              alt.name );
        }
        if( emit_serialization ) {
          string member_serials;
          string member_deserials;
          string members_s_get;
          for( expr::StructMember const& alt_mem :
               alt.members ) {
            member_serials += fmt::format(
                kSumtypeAlternativeMemberSerial,
                fmt::arg( "member_var_name", alt_mem.var ) );
            member_deserials += fmt::format(
                kSumtypeAlternativeMemberDeserial,
                fmt::arg( "member_var_name", alt_mem.var ) );
            members_s_get +=
                fmt::format( ", s_{}.get()", alt_mem.var );
          }

          emit_code_block(
              kSumtypeAlternativeSerial,
              fmt::arg( "sumtype_name", sumtype_name ),
              fmt::arg( "alt_name", alt.name ),
              fmt::arg( "members_serialization",
                        member_serials ),
              fmt::arg( "members_deserialization",
                        member_deserials ),
              fmt::arg( "members_s_get", members_s_get ) );
        }
      }
      line( "};" );
    }
  }

  void emit_enum_for_sumtype(
      vector<expr::Alternative> const& alternatives ) {
    assert( !alternatives.empty() );
    line( "enum class e {" );
    {
      auto _ = indent();
      for( expr::Alternative const& alternative : alternatives )
        line( "{},", alternative.name );
    }
    line( "};" );
  }

  void emit_variant_to_enum_specialization(
      string_view ns, expr::Sumtype const& sumtype ) {
    if( sumtype.alternatives.empty() ) return;
    string full_sumtype_name =
        fmt::format( "{}::{}_t{}", ns, sumtype.name,
                     template_params( sumtype.tmpl_params,
                                      /*put_typename=*/false ) );
    newline();
    comment(
        "This gives us the enum to use in a switch "
        "statement." );
    if( sumtype.tmpl_params.empty() )
      line( "template<>" );
    else
      emit_template_decl( sumtype.tmpl_params );
    line( "struct base::variant_to_enum<{}> {{",
          full_sumtype_name );
    {
      auto _ = indent();
      line( "using type = {}::{}::e;", ns, sumtype.name );
    }
    line( "};" );
  }

  void emit( string_view ns, expr::Enum const& e ) {
    section( "Enum: "s + e.name );
    open_ns( ns );
    line( "enum class {} {{", e.name );
    {
      auto _ = indent();
      emit_vert_list( e.values, "," );
    }
    line( "};" );
    newline();
    close_ns( ns );
    // Emit the reflection traits.
    newline();
    open_ns( "refl" );
    comment( "Reflection info for enum {}.", e.name );
    line( "template<>" );
    line( "struct traits<{}::{}> {{", ns, e.name );
    {
      auto _ = indent();
      line( "using type = {}::{};", ns, e.name );
      newline();
      line(
          "static constexpr type_kind kind        = "
          "type_kind::enum_kind;" );
      line( "static constexpr std::string_view ns   = \"{}\";",
            ns );
      line( "static constexpr std::string_view name = \"{}\";",
            e.name );
      newline();
      frag(
          "static constexpr std::array<std::string_view, {}> "
          "value_names{{",
          e.values.size() );
      if( e.values.empty() ) {
        frag( "};" );
        flush();
      } else {
        flush();
        {
          auto _ = indent();
          for( string const& s : e.values ) line( "\"{}\",", s );
        }
        line( "};" );
      }
    }
    line( "};" );
    newline();
    close_ns( "refl" );
  }

  void emit_reflection_for_struct(
      string_view                        ns,
      vector<expr::TemplateParam> const& tmpl_params,
      string const&                      name,
      vector<expr::StructMember> const&  members ) {
    comment( "Reflection info for struct {}.", name );
    string tmpl_brackets =
        tmpl_params.empty()
            ? "<>"
            : template_params( tmpl_params,
                               /*put_typename=*/false );
    string tmpl_brackets_typename =
        tmpl_params.empty()
            ? "<>"
            : template_params( tmpl_params,
                               /*put_typename=*/true );
    line( "template{}", tmpl_brackets_typename );
    string name_w_tmpl =
        fmt::format( "{}{}", name,
                     template_params( tmpl_params,
                                      /*put_typename=*/false ) );
    string full_name_w_tmpl =
        fmt::format( "{}::{}", ns, name_w_tmpl );
    line( "struct traits<{}> {{", full_name_w_tmpl );
    {
      auto _ = indent();
      line( "using type = {};", full_name_w_tmpl );
      newline();
      line(
          "static constexpr type_kind kind        = "
          "type_kind::struct_kind;" );
      line( "static constexpr std::string_view ns   = \"{}\";",
            ns );
      line( "static constexpr std::string_view name = \"{}\";",
            name );
      newline();
      line( "using template_types = std::tuple{};",
            tmpl_brackets );
      newline();
      frag( "static constexpr std::tuple fields{" );
      if( members.empty() ) {
        frag( "};" );
        flush();
      } else {
        flush();
        {
          auto _ = indent();
          for( expr::StructMember const& sm : members )
            line( "refl::StructField{{ \"{}\", &{}::{} }},",
                  sm.var, full_name_w_tmpl, sm.var );
        }
        line( "};" );
      }
    }
    line( "};" );
  }

  void emit( string_view ns, expr::Struct const& strukt ) {
    section( "Struct: "s + strukt.name );
    open_ns( ns );
    emit_template_decl( strukt.tmpl_params );
    bool comparable =
        item_has_feature( strukt, expr::e_feature::equality );
    bool has_members = !strukt.members.empty();
    if( !has_members && !comparable ) {
      line( "struct {} {{}};", strukt.name );
    } else {
      line( "struct {} {{", strukt.name );
      int max_type_len =
          max_of( strukt.members, L( _.type.size() ), 0 );
      int max_var_len =
          max_of( strukt.members, L( _.var.size() ), 0 );
      {
        auto _ = indent();
        for( expr::StructMember const& member : strukt.members )
          line( "{: <{}} {: <{}} = {{}};", member.type,
                max_type_len, member.var, max_var_len );
        if( comparable ) {
          if( has_members ) newline();
          line( "bool operator==( {} const& ) const = default;",
                strukt.name );
        }
        if( item_has_feature( strukt,
                              expr::e_feature::validation ) ) {
          newline();
          comment(
              "Validates invariants among members.  Must be "
              "manually" );
          comment( "defined in some translation unit." );
          line(
              "base::valid_or<std::string> validate() const;" );
        }
      }
      line( "};" );
    }
    newline();
    close_ns( ns );
    // Emit the reflection traits.
    newline();
    open_ns( "refl" );
    emit_reflection_for_struct( ns, strukt.tmpl_params,
                                strukt.name, strukt.members );
    newline();
    close_ns( "refl" );
  }

  void emit( string_view ns, expr::Sumtype const& sumtype ) {
    section( "Sum Type: "s + sumtype.name );
    open_ns( ns );
    if( !sumtype.alternatives.empty() ) {
      open_ns( sumtype.name );
      for( expr::Alternative const& alt :
           sumtype.alternatives ) {
        bool emit_equality = item_has_feature(
            sumtype, expr::e_feature::equality );
        bool emit_serialization = item_has_feature(
            sumtype, expr::e_feature::serializable );
        emit_sumtype_alternative( sumtype.tmpl_params, alt,
                                  sumtype.name, emit_equality,
                                  emit_serialization );
        newline();
      }
      emit_enum_for_sumtype( sumtype.alternatives );
      newline();
      close_ns( sumtype.name );
      newline();
    }
    emit_template_decl( sumtype.tmpl_params );
    if( sumtype.alternatives.empty() ) {
      line( "using {}_t = std::monostate;", sumtype.name );
    } else {
      line( "using {}_t = base::variant<", sumtype.name );
      vector<string> variants;
      for( expr::Alternative const& alt : sumtype.alternatives )
        variants.push_back( absl::StrCat(
            "  ", sumtype.name, "::", alt.name,
            template_params( sumtype.tmpl_params,
                             /*put_typename=*/false ) ) );
      emit_vert_list( variants, "," );
      line( ">;" );
      // Ensure that the variant is nothrow move'able since this
      // makes code more efficient that uses it.
      line( "NOTHROW_MOVE( {}_t{} );", sumtype.name,
            all_int_tmpl_params( sumtype.tmpl_params.size() ) );
    }
    newline();
    close_ns( ns );
    // Global namespace.
    emit_variant_to_enum_specialization( ns, sumtype );
    // Emit the reflection traits.
    if( !sumtype.alternatives.empty() ) {
      newline();
      comment( "Reflection traits for alternatives." );
      open_ns( "refl" );
      for( expr::Alternative const& alt :
           sumtype.alternatives ) {
        string sumtype_ns =
            fmt::format( "{}::{}", ns, sumtype.name );
        emit_reflection_for_struct( sumtype_ns,
                                    sumtype.tmpl_params,
                                    alt.name, alt.members );
        newline();
      }
      close_ns( "refl" );
    }
  }

  void emit_item( expr::Item const& item ) {
    string cpp_ns =
        absl::StrReplaceAll( item.ns, { { ".", "::" } } );
    auto visitor = [&]( auto const& v ) { emit( cpp_ns, v ); };
    for( expr::Construct const& construct : item.constructs ) {
      newline();
      visit( visitor, construct );
    }
  }

  void emit_preamble() {
    line( "#pragma once" );
    newline();
  }

  void emit_imports( vector<string> const& imports ) {
    if( imports.empty() ) return;
    section( "Imports" );
    for( string const& import : imports )
      line( "#include \"rds/{}.hpp\"", import );
    newline();
  }

  bool rds_has_sumtype_feature(
      expr::Rds const& rds, expr::e_feature target_feature ) {
    for( expr::Item const& item : rds.items ) {
      for( expr::Construct const& construct : item.constructs ) {
        bool has_feature = visit(
            mp::overload{ [&]( expr::Sumtype const& sumtype ) {
                           return item_has_feature(
                               sumtype, target_feature );
                         },
                          []( auto const& ) { return false; } },
            construct );
        if( has_feature ) return true;
      }
    }
    return false;
  }

  bool rds_has_struct( expr::Rds const& rds ) {
    for( expr::Item const& item : rds.items ) {
      for( expr::Construct const& construct : item.constructs ) {
        bool has_struct = visit(
            mp::overload{
                [&]( expr::Struct const& ) { return true; },
                []( auto const& ) { return false; } },
            construct );
        if( has_struct ) return true;
      }
    }
    return false;
  }

  bool rds_has_sumtype( expr::Rds const& rds ) {
    for( expr::Item const& item : rds.items ) {
      for( expr::Construct const& construct : item.constructs ) {
        bool has_sumtype = visit(
            mp::overload{
                [&]( expr::Sumtype const& ) { return true; },
                []( auto const& ) { return false; } },
            construct );
        if( has_sumtype ) return true;
      }
    }
    return false;
  }

  bool rds_has_enum( expr::Rds const& rds ) {
    for( expr::Item const& item : rds.items ) {
      for( expr::Construct const& construct : item.constructs ) {
        bool has_enum =
            visit( mp::overload{
                       [&]( expr::Enum const& ) { return true; },
                       []( auto const& ) { return false; } },
                   construct );
        if( has_enum ) return true;
      }
    }
    return false;
  }

  bool rds_needs_serial_header( expr::Rds const& rds ) {
    return rds_has_sumtype_feature(
        rds, expr::e_feature::serializable );
  }

  void emit_includes( expr::Rds const& rds ) {
    section( "Includes" );
    if( !rds.includes.empty() ) {
      comment( "Includes specified in rds file." );
      for( string const& include : rds.includes )
        line( "#include {}", include );
      newline();
    }

    comment( "Revolution Now" );
    line( "#include \"core-config.hpp\"" );
    if( rds_has_sumtype( rds ) )
      line( "#include \"rds/helper/sumtype-helper.hpp\"" );
    if( rds_needs_serial_header( rds ) ) {
      line( "#include \"error.hpp\"" );
      line( "#include \"fb.hpp\"" );
    }
    if( rds_has_enum( rds ) ) line( "#include \"maybe.hpp\"" );
    line( "" );
    comment( "refl" );
    line( "#include \"refl/ext.hpp\"" );
    line( "" );
    if( rds_has_sumtype( rds ) ) {
      comment( "base" );
      line( "#include \"base/variant.hpp\"" );
    }
    line( "" );
    comment( "base-util" );
    line( "#include \"base-util/mp.hpp\"" );
    line( "" );
    comment( "C++ standard library" );
    if( rds_has_enum( rds ) ) line( "#include <array>" );
    line( "#include <string_view>" );
    if( rds_has_struct( rds ) ) line( "#include <tuple>" );
    newline();
  }

  void emit_metadata( expr::Rds const& rds ) {
    section( "Global Vars" );
    string stem_to_var = absl::StrReplaceAll(
        rds.meta.module_name, { { "-", "_" } } );
    open_ns( "rn" );
    comment(
        "This will be the naem of this header, not the file "
        "that it" );
    comment( "is include in." );
    line(
        "inline constexpr std::string_view rds_{}_genfile = "
        "__FILE__;",
        stem_to_var );
    newline();
    close_ns( "rn" );
  }

  void emit_rds( expr::Rds const& rds ) {
    emit_preamble();
    emit_imports( rds.imports );
    emit_includes( rds );
    emit_metadata( rds );

    for( expr::Item const& item : rds.items ) emit_item( item );
  }
};

} // namespace

maybe<string> generate_code( expr::Rds const& rds ) {
  CodeGenerator gen;
  gen.emit_rds( rds );
  return gen.result();
}

} // namespace rds
