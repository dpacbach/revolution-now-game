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
#include "nation.hpp"
#include "tiles.hpp"
#include "typed-int.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace rn {

TYPED_ID( UnitId )
using OptUnitId = std::optional<UnitId>;
using UnitIdVec = std::vector<UnitId>;

enum class e_unit_type {
  free_colonist,
  caravel
};

// Static information describing classes of units.  There will be
// one of these for each type of unit.
struct UnitDescriptor {
  char const* name;
  e_unit_type type;

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

// should be variant?
struct Cargo {
  bool is_unit; // determines which of the following are relevant.
  UnitId unit_id;
  /* more to come */
};

enum class e_unit_orders {
  none,
  sentry, // includes units on ships
  fortified,
  enroute,
};

// Mutable.  This holds information about a specific instance
// of a unit that is intrinsic to the unit apart from location.
// We don't allow copying (since their should never be two unit
// objects alive with the same ID) but moving is fine.
class Unit {

public:
  static Unit& create( e_nation nation, e_unit_type type );

  Unit( Unit&& ) = default;
  Unit& operator=( Unit&& ) = default;

  UnitId id() const { return id_; }
  UnitDescriptor const& descriptor() const { return *desc_; }
  e_nation nation() const { return nation_; }
  MovementPoints movement_points() const
    { return movement_points_; }

  // Has the unit been fully processed this turn.
  bool finished_turn() const { return finished_turn_; }
  // If the unit has physically moved this turn. This concept is
  // dinstict from whether the unit has been evolved this turn,
  // since not all units need to physically move or take orders
  // each turn (i.e., pioneer building).
  bool moved_this_turn() const { return movement_points_ == 0; }
  // Returns true if the unit's orders are such that the unit may
  // physically move this turn, either by way of player input or
  // automatically, assuming it has movement points.
  bool orders_mean_move_needed() const;
  // Returns true if the unit's orders are such that the unit re-
  // quires player input this turn, assuming that it has some
  // movement points.
  bool orders_mean_input_required() const;
  // Gives up all movement points this turn and marks unit as
  // having moved. This can be called when the player directly
  // issues the "pass" command, or if e.g. a unit waiting for or-
  // ders is added to a colony, or if a unit waiting for orders
  // boards a ship.
  void forfeight_mv_points();
  // Marks unit as not having moved this turn.
  void new_turn();
  // Marks unit as having finished processing this turn.
  void finish_turn();
  // Called to consume movement points as a result of a move.
  void consume_mv_points( MovementPoints points );

private:
  Unit( e_nation nation, e_unit_type type );

  Unit() = delete;
  Unit( Unit const& ) = delete;
  Unit& operator=( Unit const& ) = delete;

  void check_invariants() const;

  // universal, unique, non-repeating, non-changing ID
  UnitId id_;
  // A unit can change type, but we cannot change the type
  // information of a unit descriptor itself.
  UnitDescriptor const* desc_;
  e_unit_orders orders_;
  std::vector<std::optional<Cargo>> cargo_slots_;
  e_nation nation_;
  // Movement points left this turn.
  MovementPoints movement_points_;
  bool finished_turn_;
};

Unit& unit_from_id( UnitId id );

UnitIdVec units_all( std::optional<e_nation> n = std::nullopt );

// Apply a function to all units. The function may mutate the
// units.
void map_units( std::function<void( Unit& )> func );

} // namespace rn

namespace std {
  DEFINE_HASH_FOR_TYPED_INT( ::rn::UnitId )
}
