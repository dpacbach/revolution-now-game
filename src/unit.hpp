/****************************************************************
* unit.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-28.
*
* Description: Data structure for units.
*
*****************************************************************/
#pragma once

#include "base-util.hpp"
#include "mv-points.hpp"
#include "tiles.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace rn {

enum class g_unit_type {
  free_colonist,
  caravel
};

// Static information describing classes of units.  There will be
// one of these for each type of unit.
struct UnitDescriptor {
  char const* name;
  g_unit_type type;

  // Rendering
  g_tile tile;

  // Movement
  bool boat;
  int visibility;
  MovementPoints movement_points;

  // Combat
  bool can_attack;
  int attack_points;
  int defense_points;

  // Cargo
  int unit_cargo_slots;
  int cargo_slots_occupied;
};

using UnitId = int;

struct Cargo {
  bool is_unit; // determines which of the following are relevant.
  UnitId unit_id;
  /* more to come */
};

enum class g_unit_orders {
  none,
  sentry, // includes units on ships
  fortified,
  enroute,
};

enum class g_nation {
  dutch
};

// Game is designed so that only one of these can be true
// for a given unit moving to a given square.  Also, these
// are independent of where the unit is coming from, i.e.,
// they are only a function of the target square of the
// move.
enum class k_unit_mv_desc {
  none,
  map_edge,
  land_forbidden,
  water_forbidden,
  insufficient_movement_points,
/*land_fall,
  board_ship,
  board_ship_full
  high_seas,
  dock,
  attack_nation,
  attack_tribe,
  attack_privateer,
  enter_village_live,
  enter_village_scout,
  trade_with_nation,
  trade_with_village,
  enter_ruins
 */
};

// Describes what would happen if a unit were to move to a
// given square.
struct UnitMoveDesc {
  // The target square of move being described.
  Coord coords;
  // Is it flat out impossible
  bool can_move;
  // Description of what would happen if the move were carried
  // out.  This will also be set even if can_move == false.
  k_unit_mv_desc desc;
  // Cost in movement points that would be incurred; this is
  // a positive number.
  MovementPoints movement_cost;
};

// Mutable.  This holds information about a specific instance
// of a unit that is intrinsic to the unit apart from location.
// We don't allow copying (since their should never be two unit
// objects alive with the same ID) but moving is fine.
struct Unit {
  // universal, unique, non-repeating, non-changing ID
  UnitId id;
  // A unit can change type, but we cannot change the type
  // information of a unit descriptor itself.
  UnitDescriptor const* desc;
  g_unit_orders orders;
  bool moved_this_turn() const { return movement_points == 0; }
  std::vector<std::optional<Cargo>> cargo_slots;
  g_nation nation;
  // Movement points left this turn.
  MovementPoints movement_points;
};

using UnitIdVec = std::vector<UnitId>;

// Not safe, probably temporary.
UnitId create_unit_on_map( g_unit_type type, Y y, X x );

Unit const& unit_from_id( UnitId id );

UnitIdVec units_from_coord( Y y, X x );
UnitIdVec units_int_rect( Rect const& rect );
OptCoord coords_for_unit_safe( UnitId id );
Coord coords_for_unit( UnitId id );

// Called at the beginning of each turn; marks all units
// as not yet having moved.
void reset_moves();
// Gives up all movement points this turn and marks unit as
// having moved. This can be called when the player directly is-
// sues the "pass" command, or if e.g. a unit waiting for orders
// is added to a colony, or if a unit waiting for orders boards a
// ship.
void forfeight_mv_points( UnitId id );
// Returns true if the unit's orders are among the set
// of possible orders that require the unit to make a
// move assuming it has movement points.
bool unit_orders_mean_move_needed( UnitId id );
// Returns true if the unit's orders are among the set
// of possible orders that require the player to give
// input to move the unit, assuming that it has some
// movement points.
bool unit_orders_mean_input_required( UnitId id );
std::vector<UnitId> units_to_move( g_nation nation );

std::vector<UnitId> units_all( g_nation nation );

g_nation player_nationality();

UnitMoveDesc move_consequences( UnitId id, Coord coords );
void move_unit_to( UnitId, Coord target );

} // namespace rn
