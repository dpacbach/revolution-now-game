/****************************************************************
**geo-types.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-12-08.
*
* Description: Holds types related to world geography.
*
*****************************************************************/
#include "geo-types.hpp"

// Revolution Now
#include "errors.hpp"
#include "fmt-helper.hpp"

// c++ standard library
#include <algorithm>
#include <utility>

using namespace std;

namespace rn {

namespace {} // namespace

Rect Rect::from( Coord const& _1, Coord const& _2 ) {
  Rect res;
  res.x = std::min( _1.x, _2.x );
  res.y = std::min( _1.y, _2.y );
  res.w = std::max( _1.x, _2.x ) - std::min( _1.x, _2.x );
  res.h = std::max( _1.y, _2.y ) - std::min( _1.y, _2.y );
  return res;
}

Rect Rect::from( Coord const& coord, Delta const& delta ) {
  return {coord.x, coord.y, delta.w, delta.h};
}

// New coord equal to this one unit of edge trimmed off
// on all sides.  (width,height) ==> (width-2,height-2)
Rect Rect::edges_removed() const {
  Rect rect( *this );

  // We always advance location unless length is zero.
  if( w >= 1_w ) ++rect.x;
  if( h >= 1_h ) ++rect.y;

  rect.w -= 2;
  rect.h -= 2;
  if( rect.w < 0 ) rect.w = 0;
  if( rect.h < 0 ) rect.h = 0;

  return rect;
}

Rect Rect::uni0n( Rect const& rhs ) const {
  // NOTE: be careful here with returning references; we should
  // only be using auto const& when the function will not return
  // a reference to a temporary.
  auto const& new_x1 = std::min( x, rhs.x );
  auto const& new_y1 = std::min( y, rhs.y );
  auto /*!!*/ new_x2 = std::max( x + w, rhs.x + rhs.w );
  auto /*!!*/ new_y2 = std::max( y + h, rhs.y + rhs.h );
  return {new_x1, new_y1, ( new_x2 - new_x1 ),
          ( new_y2 - new_y1 )};
}

template<>
X const& Rect::coordinate<X>() const {
  return x;
}
template<>
Y const& Rect::coordinate<Y>() const {
  return y;
}
template<>
W const& Rect::length<X>() const {
  return w;
}
template<>
H const& Rect::length<Y>() const {
  return h;
}

template<>
X const& Coord::coordinate<X>() const {
  return x;
}
template<>
Y const& Coord::coordinate<Y>() const {
  return y;
}

void Coord::clip( Rect const& rect ) {
  if( y < rect.y ) y = rect.y;
  if( y > rect.y + rect.h ) y = rect.y + rect.h;
  if( x < rect.x ) x = rect.x;
  if( x > rect.x + rect.w ) x = rect.x + rect.w;
}

Coord Coord::moved( e_direction d ) const {
  // clang-format off
  switch( d ) {
    case +e_direction::nw: return {y-1,x-1}; break;
    case +e_direction::n:  return {y-1,x  }; break;
    case +e_direction::ne: return {y-1,x+1}; break;
    case +e_direction::w:  return {y,  x-1}; break;
    case +e_direction::c:  return {y,  x  }; break;
    case +e_direction::e:  return {y,  x+1}; break;
    case +e_direction::sw: return {y+1,x-1}; break;
    case +e_direction::s:  return {y+1,x  }; break;
    case +e_direction::se: return {y+1,x+1}; break;
  };
  // clang-format on
  SHOULD_NOT_BE_HERE;
  return {y, x}; // to silence warning; will not get here.
}

Opt<e_direction> Coord::direction_to( Coord dest ) const {
  for( auto d : values<e_direction> )
    if( moved( d ) == dest ) return d;
  return {};
}

bool Coord::is_adjacent_to( Coord other ) const {
  auto direction = direction_to( other );
  if( direction.has_value() &&
      direction.value() != +e_direction::c )
    return true;
  return false;
}

bool Coord::is_inside( Rect const& rect ) const {
  return ( x >= rect.x ) && ( y >= rect.y ) &&
         ( x < rect.x + rect.w ) && ( y < rect.y + rect.h );
}

Delta Delta::trimmed_by_one() const {
  auto res = *this;
  if( res.w > 0 )
    res.w -= 1_w;
  else if( res.w < 0 )
    res.w += 1_w;
  if( res.h > 0 )
    res.h -= 1_h;
  else if( res.h < 0 )
    res.h += 1_h;
  return res;
}

Delta Delta::uni0n( Delta const& rhs ) const {
  return {std::max( w, rhs.w ), std::max( h, rhs.h )};
}

Coord centered( Delta const& delta, Rect const& rect ) {
  return {rect.y + rect.h / 2 - delta.h / 2,
          rect.x + rect.w / 2 - delta.w / 2};
}

Delta max( Delta const& lhs, Delta const& rhs ) {
  return {std::max( lhs.w, rhs.w ), std::max( lhs.h, rhs.h )};
}

Coord operator+( Coord const& coord, Delta const& delta ) {
  return {coord.y + delta.h, coord.x + delta.w};
}

Delta operator-( Delta const& lhs, Delta const& rhs ) {
  return {lhs.w - rhs.w, lhs.h - rhs.h};
}

Coord operator+( Delta const& delta, Coord const& coord ) {
  return {coord.y + delta.h, coord.x + delta.w};
}

Coord operator-( Coord const& coord, Delta const& delta ) {
  return {coord.y - delta.h, coord.x - delta.w};
}

ND Coord operator+( Coord const& coord, W w ) {
  return {coord.y, coord.x + w};
}

ND Coord operator+( Coord const& coord, H h ) {
  return {coord.y + h, coord.x};
}

ND Coord operator-( Coord const& coord, W w ) {
  return {coord.y, coord.x - w};
}

ND Coord operator-( Coord const& coord, H h ) {
  return {coord.y - h, coord.x};
}

void operator+=( Coord& coord, W w ) { coord.x += w; }
void operator+=( Coord& coord, H h ) { coord.y += h; }
void operator-=( Coord& coord, W w ) { coord.x -= w; }
void operator-=( Coord& coord, H h ) { coord.y -= h; }

Delta operator-( Coord const& lhs, Coord const& rhs ) {
  return {lhs.x - rhs.x, lhs.y - rhs.y};
}

ND Coord operator*( Coord const& coord, Scale const& scale ) {
  Coord res = coord;
  res *= scale;
  return res;
}

ND Delta operator*( Delta const& delta, Scale const& scale ) {
  Delta res = delta;
  res *= scale;
  return res;
}

ND Coord operator*( Scale const& scale, Coord const& coord ) {
  Coord res = coord;
  res *= scale;
  return res;
}

ND Delta operator*( Scale const& scale, Delta const& delta ) {
  Delta res = delta;
  res *= scale;
  return res;
}

} // namespace rn
