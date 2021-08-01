/****************************************************************
**combinator.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-07-30.
*
* Description: Parser combinators.
*
*****************************************************************/
#pragma once

// parz
#include "ext.hpp"
#include "parser.hpp"
#include "promise.hpp"

// base
#include "base/meta.hpp"

// C++ standard library
#include <type_traits>
#include <vector>

/****************************************************************
** Macros
*****************************************************************/
#define PZ_LAMBDA( name, ... ) \
  name( [&] { return __VA_ARGS__; } )

#define repeated_L( ... ) PZ_LAMBDA( repeated, __VA_ARGS__ )
#define some_L( ... ) PZ_LAMBDA( some, __VA_ARGS__ )

namespace parz {

// When a parser is run in a repitition, this will look at the
// value type of the parser and decide what container is best to
// return the results in.
template<typename T>
using repeated_result_container_t =
    std::conditional_t<std::is_same_v<T, char>, std::string,
                       std::vector<T>>;

/****************************************************************
** Character Classes
*****************************************************************/
// Consumes one space char or fails.
parser<> space();

// Consumes zero or more spaces.
parser<> spaces();

// Consumes a char that must be c, otherwise it fails.
parser<> chr( char c );

// Consumes any char, fails at eof.
parser<char> any_chr();

// Consumes one char if it is one of the ones in sv.
parser<char> one_of( std::string_view sv );
parser<char> not_of( std::string_view sv );

// Consumes one identifier char or fails.
parser<char> identifier_char();
parser<char> leading_identifier_char();

// Consumes one digit [0-9] char or fails.
parser<char> digit();

// Returns a parser that will always yield the given char. Would
// be nice to make this inline, clang triggers UB.
parser<char> ret( char c );

/****************************************************************
** Strings
*****************************************************************/
// Attempts to consume the exact string, and fails otherwise.
parser<> str( std::string_view sv );

parser<std::string> identifier();

// Parses "..." or '...' and returns the stuff inside, which
// cannot contain newlines.
parser<std::string> double_quoted_string();
parser<std::string> single_quoted_string();
// Allows either double or single quotes.
parser<std::string> quoted_string();

/****************************************************************
** Miscellaneous
*****************************************************************/
// Succeeds if the input stream is finished.
parser<> eof();

/****************************************************************
** pred
*****************************************************************/
// Parses a single character for which the predicate returns
// true, fails otherwise.
struct Pred {
  // Need to take Func by value so that it stays around when this
  // function suspends.
  template<typename Func>
  parser<char> operator()( Func f ) const {
    char next = co_await next_char{};
    if( !f( next ) ) co_await fail();
    co_return next;
  }
};

inline constexpr Pred pred{};

/****************************************************************
** repeated
*****************************************************************/
// Parses zero or more of the given parser.
struct Repeated {
  // This is a struct instead of a function to work around a
  // clang issue where it doesn't like coroutine function tem-
  // plates.  Take args by value for lifetime reasons.
  template<typename Func, typename... Args>
  auto operator()( Func f, Args... args ) const -> parser<
      repeated_result_container_t<typename std::invoke_result_t<
          Func, Args...>::value_type>> {
    using res_t =
        typename std::invoke_result_t<Func, Args...>::value_type;
    repeated_result_container_t<res_t> res;
    while( true ) {
      auto m = co_await Try{ f( std::move( args )... ) };
      if( !m ) break;
      res.push_back( std::move( *m ) );
    }
    co_return res;
  }
};

inline constexpr Repeated repeated{};

/****************************************************************
** repeat_parse
*****************************************************************/
// Parses zero or more of the given type.
template<typename T>
struct RepeatedParse {
  auto operator()() const -> parser<std::vector<T>> {
    return repeated( []() -> parser<T> { return parse<T>(); } );
  }
};

template<typename T>
inline constexpr RepeatedParse<T> repeated_parse{};

/****************************************************************
** some
*****************************************************************/
// Parses one or more of the given parser.
struct Some {
  // This is a struct instead of a function to work around a
  // clang issue where it doesn't like coroutine function tem-
  // plates.
  template<typename Func, typename... Args>
  auto operator()( Func f, Args... args ) const -> parser<
      repeated_result_container_t<typename std::invoke_result_t<
          Func, Args...>::value_type>> {
    using res_t =
        typename std::invoke_result_t<Func, Args...>::value_type;
    repeated_result_container_t<res_t> res = co_await repeated(
        std::move( f ), std::move( args )... );
    if( res.empty() ) co_await fail();
    co_return res;
  }
};

inline constexpr Some some{};

/****************************************************************
** seq
*****************************************************************/
// Runs multiple parsers in sequence, and only succeeds if all of
// them succeed. Returns all results in a tuple.
struct Seq {
  template<typename... Parsers>
  parser<std::tuple<typename Parsers::value_type...>> operator()(
      Parsers... ps ) const {
    co_return std::tuple<typename Parsers::value_type...>{
        co_await std::move( ps )... };
  }
};

inline constexpr Seq seq{};

/****************************************************************
** construct
*****************************************************************/
// Calls the constructor of the given type with the results of
// the parsers as arguments (which must all succeed).
//
// NOTE: the parsers are guaranteed to be run in the order they
// appear in the parameter list, and that is one of the benefits
// of using this helper.
template<typename T>
struct Construct {
  template<size_t... Idx, typename... Parsers>
  parser<T> run( std::index_sequence<Idx...>,
                 Parsers... ps ) const {
    // We can't simply do the following:
    //
    //   co_return T( co_await std::move( ps )... );
    //
    // because then we'd be depending on the order of evaluation
    // of function arguments. So we have to use a fold expression
    // with the comma operator, which apparently guarantees eval-
    // uation order, see:
    //
    //   https://stackoverflow.com/questions/46056268/
    //                order-of-evaluation-for-fold-expressions
    //
    auto ps_tpl = std::tuple{ std::move( ps )... };
    std::tuple<typename Parsers::value_type...> res_tpl;

    auto run_parser =
        [&]<size_t I>(
            std::integral_constant<size_t, I> ) -> parser<> {
      std::get<I>( res_tpl ) =
          co_await std::move( std::get<I>( ps_tpl ) );
    };
    // Here is the fold expression whose order of evaluation is
    // supposed to be well-defined because we are using the comma
    // operator.
    ( co_await run_parser(
          std::integral_constant<size_t, Idx>{} ),
      ... );

    auto applier = [&]( auto&&... args ) {
      return T( FWD( args )... );
    };
    co_return std::apply( applier, std::move( res_tpl ) );
  }

  template<typename... Parsers>
  parser<T> operator()( Parsers... ps ) const {
    return run( std::index_sequence_for<Parsers...>(),
                std::move( ps )... );
  }
};

template<typename T>
inline constexpr Construct<T> construct{};

/****************************************************************
** seq_last
*****************************************************************/
// Runs multiple parsers in sequence, and only succeeds if all of
// them succeed. Returns last result.
struct SeqLast {
  template<typename... Parsers>
  parser<
      mp::last_t<mp::type_list<typename Parsers::value_type...>>>
  operator()( Parsers... ps ) const {
    using ret_t = mp::last_t<
        mp::type_list<typename Parsers::value_type...>>;
    if constexpr( std::is_same_v<ret_t, std::monostate> )
      ( co_await std::move( ps ), ... );
    else
      co_return( co_await std::move( ps ), ... );
  }
};

inline constexpr SeqLast seq_last{};

/****************************************************************
** seq_first
*****************************************************************/
// Runs multiple parsers in sequence, and only succeeds if all of
// them succeed. Returns first result.
struct SeqFirst {
  template<typename Parser, typename... Parsers>
  parser<typename Parser::value_type> operator()(
      Parser fst, Parsers... ps ) const {
    auto res = co_await std::move( fst );
    ( co_await std::move( ps ), ... );
    co_return res;
  }
};

inline constexpr SeqFirst seq_first{};

/****************************************************************
** exhaust
*****************************************************************/
// Runs the given parser and then checks that the input buffers
// has been exhausted (if not, it fails). Returns the result from
// the parser.
struct Exhaust {
  template<typename T>
  parser<T> operator()( parser<T> p ) const {
    T res = co_await std::move( p );
    co_await eof();
    co_return res;
  }
};

inline constexpr Exhaust exhaust{};

/****************************************************************
** unwrap
*****************************************************************/
// This does not do any parsing, it just takes a defererenceable
// object and tries to unwrap it, and if it can't, then it will
// fail the parser.
struct Unwrap {
  template<typename T>
  parser<std::remove_cvref_t<decltype( *std::declval<T>() )>>
  operator()( T&& o ) const {
    if( !o ) fail();
    co_return *FWD( o );
  }
};

inline constexpr Unwrap unwrap{};

/****************************************************************
** Bracketed
*****************************************************************/
// Runs the parser p between characters l and r.
struct Bracketed {
  template<typename T>
  parser<T> operator()( char l, parser<T> p, char r ) const {
    co_await chr( l );
    T res = co_await std::move( p );
    co_await chr( r );
    co_return res;
  }
};

inline constexpr Bracketed bracketed{};

/****************************************************************
** First
*****************************************************************/
// Runs the parsers in sequence until the first one succeeds,
// then returns its result (all of the parsers must return the
// same result type). If none of them succeed then the parser
// fails.
struct First {
  template<typename P, typename... Ps>
  parser<typename P::value_type> operator()( P fst,
                                             Ps... rest ) const {
    using res_t = typename P::value_type;
    base::maybe<res_t> res;

    auto one = [&]<typename T>( parser<T> p ) -> parser<> {
      if( res.has_value() ) co_return;
      auto exp = co_await Try{ std::move( p ) };
      if( !exp ) co_return;
      res.emplace( std::move( *exp ) );
    };
    co_await one( std::move( fst ) );
    ( co_await one( std::move( rest ) ), ... );

    if( !res ) co_await fail{};
    co_return std::move( *res );
  }
};

inline constexpr First first{};

/****************************************************************
** Haskell-like sequencing operator
*****************************************************************/
template<typename T, typename U>
parser<U> operator>>( parser<T> l, parser<U> r ) {
  return seq_last( std::move( l ), std::move( r ) );
}

template<typename T, typename U>
parser<U> operator|( parser<T> l, parser<U> r ) {
  return first( std::move( l ), std::move( r ) );
}

} // namespace parz
