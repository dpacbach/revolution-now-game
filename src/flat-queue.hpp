/****************************************************************
**flat-queue.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-09-04.
*
* Description: Single Ended Queue in Contiguous Memory.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "aliases.hpp"
#include "errors.hpp"
#include "fb.hpp"
#include "fmt-helper.hpp"

namespace rn {

// If the front of the flat-queue gets this many elements beyond
// the beginning of the storage block then there will be a resize
// and/or reallocation to normalize the internal storage so that
// it doesn't grow indefinitely.
inline constexpr int k_flat_queue_reallocation_size_default =
    1000;

/****************************************************************
** Single-Ended Flat Queue
*****************************************************************/
// Queue with elements stored in contiguous memory, but lacking
// in pointer stability.
//
// Lacks pointer stability due to occasional reallocations. The
// "reallocation size" represents the position in the vector such
// that if the front element is located beyond it then the vector
// will be reallocated (normalized) to bring the head element
// to position zero. This prevents the vector from growing
// indefinitely.
template<typename T>
class flat_queue {
public:
  flat_queue( int reallocation_size =
                  k_flat_queue_reallocation_size_default )
    : queue_{},
      front_{ 0 },
      reallocation_size_( reallocation_size ) {
    DCHECK( reallocation_size_ >= 10,
            "reallocation_size should be >= 10 to good "
            "performance (this is just a heuristic)." );
    check_invariants();
  }

  flat_queue( flat_queue<T> const& ) = default;
  flat_queue( flat_queue<T>&& )      = default;

  flat_queue<T>& operator=( flat_queue<T>&& rhs ) noexcept {
    flat_queue<T> moved( std::move( rhs ) );
    moved.swap( *this );
    return *this;
  }

  void swap( flat_queue<T>& rhs ) noexcept {
    std::swap( queue_, rhs.queue_ );
    std::swap( front_, rhs.front_ );
    std::swap( reallocation_size_, rhs.reallocation_size_ );
  }

  friend void swap( flat_queue<T>& lhs,
                    flat_queue<T>& rhs ) noexcept {
    lhs.swap( rhs );
  }

  int  size() const { return int( queue_.size() ) - front_; }
  bool empty() const { return size() == 0; }

  // !! Ref returned is not stable.
  maybe<T const&> front() const {
    if( front_ != int( queue_.size() ) ) return queue_[front_];
    return nothing;
  }

  // !! Ref returned is not stable.
  maybe<T&> front() {
    if( front_ != int( queue_.size() ) ) {
      maybe<T&> res = queue_[front_];
      check_invariants();
      return res;
    }
    check_invariants();
    return nothing;
  }

  void push( T const& item ) {
    queue_.push_back( item );
    check_invariants();
  }

  template<typename... Args>
  void push_emplace( Args&&... args ) {
    queue_.emplace_back( std::forward<Args>( args )... );
    check_invariants();
  }

  void pop() {
    DCHECK( front_ != int( queue_.size() ) );
    if( front_ == int( queue_.size() ) ) return;
    queue_[front_++].~T();
    if( front_ == reallocation_size_ ) {
      queue_.erase( queue_.begin(),
                    queue_.begin() + reallocation_size_ );
      queue_.shrink_to_fit();
      front_ = 0;
    }
    check_invariants();
  }

  bool operator==( flat_queue<T> const& rhs ) const {
    if( size() != rhs.size() ) return false;
    if( size() == 0 ) return true;
    int front_lhs = front_;
    int front_rhs = rhs.front_;
    DCHECK( int( queue_.size() ) - front_lhs ==
            int( rhs.queue_.size() ) - front_rhs );
    for( int i = 0; i < int( size() ); ++i )
      if( queue_[front_lhs++] != rhs.queue_[front_rhs++] )
        return false;
    return true;
  }

  bool operator!=( flat_queue<T> const& rhs ) const {
    return !( ( *this ) == rhs );
  }

  std::string to_string(
      int max_elems = std::numeric_limits<int>::max() ) const {
    Str res  = "[front:";
    int back = front_ + std::min( max_elems, size() );
    for( int i = front_; i < back; ++i ) {
      res += fmt::format( "{}", queue_[i] );
      if( i != back - 1 ) res += ',';
    }
    if( max_elems < size() ) res += "...";
    res += ']';
    return res;
  }

  // {fmt} formatter.
  friend struct fmt::formatter<flat_queue<T>>;

private:
  void check_invariants() {
    CHECK( front_ >= 0 );
    CHECK( front_ <= int( queue_.size() ) );
    CHECK( front_ < reallocation_size_ );
  }

  Vec<T> queue_;
  int    front_; // no iterator; would be invalidated.
  int    reallocation_size_;
  NOTHROW_MOVE( T );
};

namespace serial {

template<typename Hint, typename T>
auto serialize( FBBuilder& builder, ::rn::flat_queue<T> const& m,
                serial::ADL ) {
  std::vector<T> data;
  data.reserve( size_t( m.size() ) );
  auto m_copy = m;
  while( m_copy.front() ) {
    data.emplace_back( *m_copy.front() );
    m_copy.pop();
  }
  return serialize<Hint>( builder, data, serial::ADL{} );
}

template<typename SrcT, typename T>
expect<> deserialize( SrcT const* src, ::rn::flat_queue<T>* m,
                      serial::ADL ) {
  if( src == nullptr ) {
    // `dst` should be in its default-constructed state, which is
    // an empty queue.
    return xp_success_t{};
  }
  std::vector<T> data;
  XP_OR_RETURN_( deserialize( src, &data, serial::ADL{} ) );
  for( auto& e : data ) m->push_emplace( std::move( e ) );
  return xp_success_t{};
}

} // namespace serial

} // namespace rn

namespace fmt {
// {fmt} formatter.
template<typename T>
struct formatter<::rn::flat_queue<T>> : formatter_base {
  template<typename FormatContext>
  auto format( ::rn::flat_queue<T> const& o,
               FormatContext&             ctx ) {
    std::string res = "[front:";
    for( int i = o.front_; i < int( o.queue_.size() ); ++i ) {
      res += fmt::format( "{}", o.queue_[i] );
      if( i != int( o.queue_.size() ) - 1 ) res += ',';
    }
    res += ']';
    return formatter_base::format( res, ctx );
  }
};
} // namespace fmt
