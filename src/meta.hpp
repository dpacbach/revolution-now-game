/****************************************************************
**meta.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-10-06.
*
* Description: Metaprogramming utilities.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// C++ standard library
#include <type_traits>

namespace mp {

template<int N>
struct disambiguate;

template<typename...>
struct type_list;

/****************************************************************
** Callable Traits
*****************************************************************/
namespace detail {

template<typename...>
struct callable_traits_impl;

// Function.
template<typename R, typename... Arg>
struct callable_traits_impl<R( Arg... )> {
  using arg_types = type_list<Arg...>;
  using ret_type  = R;
};

// Pointer to member-function.
template<typename T, typename R, typename... Arg>
struct callable_traits_impl<R ( T::* )( Arg... ) const>
  : public callable_traits_impl<R( Arg... )> {};

// Function pointer.
template<typename R, typename... Arg>
struct callable_traits_impl<R ( * )( Arg... )>
  : public callable_traits_impl<R( Arg... )> {};

// Function reference.
template<typename R, typename... Arg>
struct callable_traits_impl<R ( & )( Arg... )>
  : public callable_traits_impl<R( Arg... )> {};

} // namespace detail

template<typename T, typename Enable = void>
struct callable_traits;

// Function.
template<typename R, typename... Arg>
struct callable_traits<R( Arg... )>
  : public detail::callable_traits_impl<R( Arg... )> {};

// Pointer.
template<typename O>
struct callable_traits<
    O, typename std::enable_if_t<
           std::is_pointer_v<std::remove_cvref_t<O>>>>
  : public detail::callable_traits_impl<O> {};

// Function reference.
template<typename R, typename... Arg>
struct callable_traits<R ( & )( Arg... )>
  : public detail::callable_traits_impl<R( Arg... )> {};

// Pointer to member function.
template<typename R, typename C, typename... Arg>
struct callable_traits<R ( C::* )( Arg... )>
  : public detail::callable_traits_impl<R( Arg... )> {};

// Object.
template<typename O>
struct callable_traits<
    O, typename std::enable_if_t<
           !std::is_function_v<std::remove_cvref_t<O>> &&
           !std::is_pointer_v<std::remove_cvref_t<O>> &&
           !std::is_member_function_pointer_v<
               std::remove_cvref_t<O>>>>
  : public detail::callable_traits_impl<
        decltype( &O::operator() )> {};

template<typename F>
using callable_ret_type_t =
    typename callable_traits<F>::ret_type;

template<typename F>
using callable_arg_types_t =
    typename callable_traits<F>::arg_types;

/****************************************************************
** head
*****************************************************************/
template<typename...>
struct head;

template<typename Arg1, typename... Args>
struct head<type_list<Arg1, Args...>> {
  using type = Arg1;
};

template<typename List>
using head_t = typename head<List>::type;

/****************************************************************
** type_list_size
*****************************************************************/
template<typename...>
struct type_list_size;

template<typename... Args>
struct type_list_size<type_list<Args...>> {
  static constexpr std::size_t size = sizeof...( Args );
};

template<typename List>
inline constexpr auto type_list_size_v =
    type_list_size<List>::size;

/****************************************************************
** and
*****************************************************************/
#if 0 // remove duplicate code from base-util and enable this.
template<bool...>
constexpr const bool and_v = false;

template<bool Bool>
constexpr const bool and_v<Bool> = Bool;

template<bool First, bool... Bools>
constexpr bool and_v<First, Bools...> = First&& and_v<Bools...>;
#endif

/****************************************************************
** any
*****************************************************************/
template<bool...>
constexpr const bool any_v = false;

template<bool Bool>
constexpr const bool any_v<Bool> = Bool;

template<bool First, bool... Bools>
constexpr bool any_v<First, Bools...> = First || any_v<Bools...>;

/****************************************************************
** reference_wrapper
*****************************************************************/
template<typename>
inline constexpr bool is_reference_wrapper_v = false;

template<typename T>
inline constexpr bool
    is_reference_wrapper_v<::std::reference_wrapper<T>> = true;

/****************************************************************
** is_map_like
*****************************************************************/
template<typename, typename = void>
struct has_mapped_type_member : std::false_type {};

template<class T>
struct has_mapped_type_member<
    T, std::void_t<typename T::mapped_type>> : std::true_type {};

template<typename T>
constexpr bool has_mapped_type_member_v =
    has_mapped_type_member<T>::value;

template<typename, typename = void>
struct has_key_type_member : std::false_type {};

template<class T>
struct has_key_type_member<T, std::void_t<typename T::key_type>>
  : std::true_type {};

template<typename T>
constexpr bool has_key_type_member_v =
    has_key_type_member<T>::value;

template<typename T>
constexpr bool is_map_like = has_key_type_member_v<T>&& //
               has_mapped_type_member_v<T>;

/****************************************************************
** has_reserve_method
*****************************************************************/
template<typename, typename = void>
inline constexpr bool has_reserve_method = false;

template<class T>
inline constexpr bool has_reserve_method<
    T, std::void_t<decltype( std::declval<T>().reserve( 0 ) )>> =
    true;

} // namespace mp
