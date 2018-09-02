/****************************************************************
* unit.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-28.
*
* Description: Data structure for units.
*
*****************************************************************/
#include "base-util.hpp"
#include "macros.hpp"
#include "tiles.hpp"
#include "unit.hpp"
#include "world.hpp"

#include <unordered_map>
#include <unordered_set>

using namespace std;

namespace rn {

namespace {

UnitId next_id = 0;

unordered_map<UnitId, Unit> units;

// For units that are on (owned by) the map.
unordered_map<Coord, unordered_set<UnitId>> units_from_coords;
unordered_map<UnitId, Coord> coords_from_unit;

#if 1
namespace explicit_types {
  // These are to make the auto-completer happy since it doesn't
  // want to recognize the fully generic templated one.
  OptCRef<unordered_set<UnitId>> get_val_safe(
        unordered_map<Coord,unordered_set<UnitId>> const& m,
        Coord const& k ) {
      auto found = m.find( k );
      if( found == m.end() )
          return std::nullopt;
      return found->second;
  }

  OptCoord get_val_safe(
        unordered_map<UnitId, Coord> const& m, UnitId k ) {
      auto found = m.find( k );
      if( found == m.end() )
          return std::nullopt;
      return found->second;
  }

  OptRef<Unit> get_val_safe( unordered_map<UnitId,Unit>& m, UnitId k ) {
      auto found = m.find( k );
      if( found == m.end() )
          return std::nullopt;
      return found->second;
  }
}
#endif

unordered_map<g_unit_type, UnitDescriptor, EnumClassHash> unit_desc{
  {g_unit_type::free_colonist, UnitDescriptor{
    /*name=*/"free colonist",
    /*type=*/g_unit_type::free_colonist,
    /*tile=*/g_tile::free_colonist,
    /*boat=*/false,
    /*visibility=*/1,
    /*movement_points=*/1,
    /*can_attack=*/false,
    /*attack_points=*/0,
    /*defense_points=*/1,
    /*unit_cargo_slots=*/0,
    /*cargo_slots_occupied=*/1
  }},
  {g_unit_type::caravel, UnitDescriptor{
    /*name=*/"caravel",
    /*type=*/g_unit_type::caravel,
    /*tile=*/g_tile::caravel,
    /*boat=*/true,
    /*visibility=*/1,
    /*movement_points=*/4,
    /*can_attack=*/false,
    /*attack_points=*/0,
    /*defense_points=*/2,
    /*unit_cargo_slots=*/4,
    /*cargo_slots_occupied=*/-1
  }},
};

Unit& unit_from_id_mutable( UnitId id ) {
  auto res = explicit_types::get_val_safe( units, id );
  ASSERT( res );
  return *res;
}

} // namespace

g_nation player_nationality() {
  return g_nation::dutch;
}

vector<UnitId> units_all( g_nation nation ) {
  vector<UnitId> res; res.reserve( units.size() );
  for( auto const& p : units )
    if( p.second.nation == nation )
      res.push_back( p.first );
  return res;
}

// need to think about what this API should be.
UnitId create_unit_on_map( g_unit_type type, Y y, X x ) {
  auto const& desc = unit_desc[type];
  units[next_id] = Unit{
    next_id,
    &desc,
    g_unit_orders::none,
    false,
    {},
    g_nation::dutch,
    desc.movement_points,
  };
  units.at( next_id ).cargo_slots.resize( desc.unit_cargo_slots );
  units_from_coords[Coord{y,x}].insert( next_id );
  coords_from_unit[next_id] = Coord{y,x};
  return next_id++;
}

Unit const& unit_from_id( UnitId id ) {
  return unit_from_id_mutable( id );
}

UnitIdVec units_from_coord( Y y, X x ) {
  auto opt_set = explicit_types::get_val_safe( units_from_coords, Coord{y,x} );
  if( !opt_set ) return {};
  unordered_set<UnitId> const& set = (*opt_set);
  UnitIdVec res; res.reserve( set.size() );
  for( auto id : set )
    res.push_back( id );
  return res;
}

UnitIdVec units_int_rect( Rect const& rect ) {
  UnitIdVec res;
  for( Y i = rect.y; i < rect.y+rect.h; ++i )
    for( X j = rect.x; j < rect.x+rect.w; ++j )
      for( auto id : units_from_coord( i, j ) )
        res.push_back( id );
  return res;
}

OptCoord coords_for_unit_safe( UnitId id ) {
  return explicit_types::get_val_safe( coords_from_unit, id );
}

Coord coords_for_unit( UnitId id ) {
  auto opt_coord = coords_for_unit_safe( id );
  ASSERT( opt_coord );
  return *opt_coord;
}

// This function will allow the move by default, and so it is
// the burden of the logic in this function to find every possible
// way that the move is *not* allowed and to flag it if that is
// the case.
UnitMoveDesc move_consequences( UnitId id, Coord coords ) {
  Y y = coords.y;
  X x = coords.x;
  MovementPoints cost( 1 );
  if( y-Y(0) >= world_size_tiles_y() ||
      x-X(0) >= world_size_tiles_x() ||
      y < 0 || x < 0 )
    return {{y, x}, false, k_unit_mv_desc::map_edge, cost};
  auto& unit = unit_from_id( id );
  // This function doesn't necessarily have to be responsible for
  // checking this, but it may end up catching some problems.
  ASSERT( !unit.moved_this_turn );
  auto& square = square_at( y, x );

  if( unit.desc->boat && square.land ) {
    return {{y, x}, false, k_unit_mv_desc::land_forbidden, cost};
  }
  if( !unit.desc->boat && !square.land ) {
    return {{y, x}, false, k_unit_mv_desc::water_forbidden, cost};
  }
  return {{y, x}, true, k_unit_mv_desc::none, cost};
}

// Called at the beginning of each turn; marks all units
// as not yet having moved.
void reset_moves() {
  for( auto & [id, unit] : units ) {
    unit.moved_this_turn = false;
    unit.movement_points = unit.desc->movement_points;
  }
}

// Mark unit as having moved.
void forfeight_mv_points( UnitId id ) {
  auto& unit = unit_from_id_mutable( id );
  // This function doesn't necessarily have to be responsible for
  // checking this, but it may end up catching some problems.
  ASSERT( !unit.moved_this_turn );
  unit.moved_this_turn = true;
  unit.movement_points = 0;
}

void move_unit_to( UnitId id, Coord target ) {
  UnitMoveDesc move_desc = move_consequences( id, target );
  // Caller should have checked this.
  ASSERT( move_desc.can_move );

  auto& unit = unit_from_id_mutable( id );
  ASSERT( !unit.moved_this_turn );

  // Remove unit from current square.
  auto opt_current_coords = coords_for_unit_safe( id );
  // Will trigger if the unit trying to be moved is not
  // on the map.  Will eventually have to remove this.
  ASSERT( opt_current_coords );
  auto [curr_y, curr_x] = *opt_current_coords;
  auto& unit_set = units_from_coords[{curr_y,curr_x}];
  auto iter = unit_set.find( id );
  // Will trigger if an internal invariant is broken.
  ASSERT( iter != unit_set.end() );
  unit_set.erase( iter );

  // Add unit to new square.
  units_from_coords[{target.y,target.x}].insert( id );

  // Set unit coords to new value.
  coords_from_unit[id] = {target.y,target.x};

  unit.movement_points -= move_desc.movement_cost;
  ASSERT( unit.movement_points >= 0 );
  if( unit.movement_points == 0 )
    unit.moved_this_turn = true;
}

bool all_units_moved( g_nation nation ) {
  for( auto const& [id, unit] : units )
    if( unit.nation == nation )
      if( !unit.moved_this_turn )
        return false;
  return true;
}

} // namespace rn
