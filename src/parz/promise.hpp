/****************************************************************
**promise.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-07-30.
*
* Description: Parser coroutine promise type.
*
*****************************************************************/
#pragma once

#include "magic.hpp"

// base
#include "base/co-compat.hpp"
#include "base/error.hpp"
#include "base/expect.hpp"
#include "base/maybe.hpp"

// C++ standard library
#include <string>
#include <string_view>
#include <type_traits>

/****************************************************************
** Parser Promise
*****************************************************************/
namespace parz {

namespace detail {

// We put the return_value and return_void in these two structs
// so that we can decide based on the type of T which one to in-
// clude (we are only allowed to have one in a promise type).
// When T is std::monostate we want the void one, otherwise we
// want the value one.
template<typename Derived, typename T>
struct promise_value_returner {
  void return_value( T const& val ) {
    static_cast<Derived&>( *this ).return_value_( val );
  }
};

template<typename Derived>
struct promise_void_returner {
  void return_void() {
    static_cast<Derived&>( *this ).return_void_();
  }
};

} // namespace detail

template<typename T>
struct promise_type
  : std::conditional_t<
        std::is_same_v<T, std::monostate>,
        detail::promise_void_returner<promise_type<T>>,
        detail::promise_value_returner<promise_type<T>, T>> {
  base::maybe<base::expect<T, error>> o_;
  std::string_view                    in_       = "";
  int                                 consumed_ = 0;
  int                                 farthest_ = 0;

  promise_type() = default;

  std::string_view& buffer() { return in_; }

  // Always suspend initially because the coroutine is unable to
  // do anything immediately because it does not have access to
  // the buffer to be parsed. It only gets that when it is
  // awaited upon.
  auto initial_suspend() const { return base::suspend_always{}; }

  auto final_suspend() const noexcept {
    // Always suspend because the parser object owns the corou-
    // tine and will be the one to destroy it when it goes out of
    // scope.
    return base::suspend_always{};
  }

  // Ensure that this is not copyable. See
  // https://devblogs.microsoft.com/oldnewthing/20210504-00/?p=105176
  // for the arcane reason as to why.
  promise_type( promise_type const& ) = delete;
  void operator=( promise_type const& ) = delete;

  auto get_return_object() { return parser<T>( this ); }

  void unhandled_exception() { SHOULD_NOT_BE_HERE; }

  // Note this ends in an underscore; the real return_value, if
  // present, is provided by a base class so that we can control
  // when it appears.
  void return_value_( T const& val ) {
    DCHECK( !o_ );
    o_.emplace( val );
  }

  // Note this ends in an underscore; the real return_void, if
  // present, is provided by a base class so that we can control
  // when it appears.
  void return_void_() {
    DCHECK( !o_ );
    o_.emplace( T{} );
  }

  auto await_transform( error const& w ) noexcept {
    o_ = w;
    return base::suspend_always{};
  }

  template<typename U>
  struct awaitable {
    parser<U>     parser_;
    promise_type* promise_;

    // Promise pointer so that template type can be inferred.
    awaitable( promise_type* p, parser<U> parser )
      : parser_( std::move( parser ) ), promise_( p ) {}

    bool await_ready() noexcept {
      // At this point parser should be at its initial suspend
      // point, waiting to be run.
      DCHECK( !parser_.finished() );
      // Now we need to give the parser's promise object the
      // buffer to be parsed.
      parser_.resume( promise_->buffer() );
      // parser should be at its final suspend point now, waiting
      // to be destroyed by the parser object that owns it.
      DCHECK( parser_.finished() );
      promise_->farthest_ =
          std::max( promise_->farthest_,
                    promise_->consumed_ + parser_.farthest() );
      return parser_.is_good();
    }

    void await_suspend( coro::coroutine_handle<> ) noexcept {
      // A parse failed.
      promise_->o_.emplace( std::move( parser_.error() ) );
    }

    U await_resume() {
      DCHECK( parser_.is_good() );
      int chars_consumed =
          promise_->buffer().size() - parser_.buffer().size();
      DCHECK( chars_consumed >= 0 );
      DCHECK( promise_->buffer().size() >=
              size_t( chars_consumed ) );
      // We only consume chars upon success.
      promise_->buffer().remove_prefix( chars_consumed );
      promise_->consumed_ += chars_consumed;
      return parser_.get();
    }
  };

  template<typename U>
  auto await_transform( parser<U> p ) noexcept {
    return awaitable<U>( this, std::move( p ) );
  }

  // This parser is allowed to fail.
  template<typename U>
  auto await_transform( Try<U> t ) noexcept {
    // Slight modification to awaitable to allow it to fail.
    struct tryable_awaitable : awaitable<U> {
      using Base = awaitable<U>;
      using Base::Base;
      using Base::parser_;
      using Base::promise_;

      bool await_ready() noexcept {
        Base::await_ready();
        // This is a parser that is allowed to fail, so always
        // return true;
        return true;
      }

      base::expect<U, error> await_resume() {
        if( parser_.is_error() ) return parser_.error();
        return Base::await_resume();
      }
    };
    return tryable_awaitable( this, std::move( t.p ) );
  }

  auto await_transform( next_char ) noexcept {
    struct next_char_awaitable {
      promise_type* p_;

      bool await_ready() noexcept {
        bool ready = ( p_->buffer().size() > 0 );
        if( !ready ) p_->o_.emplace( error( "EOF" ) );
        return ready;
      }

      void await_suspend( coro::coroutine_handle<> ) noexcept {
        // ran out of characters in input buffer.
      }

      char await_resume() noexcept {
        CHECK_GE( int( p_->buffer().size() ), 1 );
        char res = p_->buffer()[0];
        p_->buffer().remove_prefix( 1 );
        p_->consumed_++;
        p_->farthest_ = p_->consumed_;
        return res;
      }
    };
    return next_char_awaitable{ this };
  }
};

} // namespace parz
