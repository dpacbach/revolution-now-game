/****************************************************************
**co-maybe.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2020-12-31.
*
* Description: Setup for using `maybe` with coroutines.
*
*****************************************************************/
#pragma once

// base
#include "co-compat.hpp"
#include "maybe.hpp"
#include "stack-trace.hpp"

// FIXME: maybes, expecteds, etc. should probably not be used
// with coroutines until it is verified in godbolt that both
// clang and gcc can optimize away all of the coroutine state
// heap allocations. At the time of writing, clang seems to be
// able to do it but not gcc. See https://godbolt.org/z/MKsPPG

namespace base {

template<typename T>
auto operator co_await( maybe<T> const& o ) {
  struct maybe_awaitable {
    maybe<T> const* o_;
    bool await_ready() noexcept { return o_->has_value(); }
    void await_suspend( coro::coroutine_handle<> h ) noexcept {
      // If we are suspending then that means that one of the
      // maybe's is `nothing`, in which case we will return to
      // the caller and never resume the coroutine, so there is
      // never a need to resume.
      h.destroy();
    }
    T await_resume() noexcept { return o_->value(); }
  };
  return maybe_awaitable{ &o };
}

namespace detail {

// Take advantage of the fact that the get_return_object function
// on the promise is allowed to return a type that is implicitly
// convertible to the result. This type is non-copyable and non-
// movable, so should guarantee that the maybe<T> that it holds
// will be a singleton within the coroutine and will not move ad-
// dresses. This is then used to give the promise the address of
// its maybe<T> member so that the promise can fill it in.
// Without this, a co_return would not be able to communicate the
// result since the get_return_object is called at the start of
// the coroutine.
template<typename T, typename PromiseT>
struct maybe_holder {
  maybe<T> o_;
  maybe_holder( PromiseT* p ) { p->o_ = &o_; }
  maybe_holder( maybe_holder&& )      = delete;
  maybe_holder( maybe_holder const& ) = delete;
  operator maybe<T>() const noexcept { return std::move( o_ ); }
};

} // namespace detail

} // namespace base

namespace CORO_NS {

template<typename T>
struct coroutine_traits<::base::maybe<T>> {
  struct promise_type {
    ::base::maybe<T>* o_ = nullptr;

    // Need to use base::suspend_never because it fixes some
    // methods that need to be noexcept (used with the clang+lib-
    // stdc++ combo).
    auto initial_suspend() noexcept {
      return ::base::suspend_never{};
    }

    using holder = ::base::detail::maybe_holder<T, promise_type>;

    holder get_return_object() noexcept {
      return holder{ this };
    }

    void return_value( T const& val ) noexcept {
      assert_bt( o_ != nullptr );
      *o_ = val;
    }

    auto final_suspend() noexcept {
      return ::base::suspend_never{};
    }

    void unhandled_exception() noexcept {}
  };
};

} // namespace CORO_NS