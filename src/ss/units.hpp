/****************************************************************
**units.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-02-13.
*
* Description: Unit-related save-game state.
*
*****************************************************************/
#pragma once

// Rd
#include "ss/units.rds.hpp"

// Revolution Now
#include "ss/colony-id.hpp"
#include "ss/colony.hpp"
#include "ss/unit-id.hpp"

// gfx
#include "gfx/coord.hpp"

// luapp
#include "luapp/ext-userdata.hpp"

// C++ standard library
#include <unordered_map>
#include <unordered_set>

namespace rn {

struct SS;
struct TS;

struct UnitsState {
  UnitsState();
  bool operator==( UnitsState const& ) const = default;

  // Implement refl::WrapsReflected.
  UnitsState( wrapped::UnitsState&& o );
  wrapped::UnitsState const&        refl() const { return o_; }
  static constexpr std::string_view refl_ns   = "rn";
  static constexpr std::string_view refl_name = "UnitsState";

  // API. Here we mainly just have the functions that are re-
  // quired to access the state in this class and maintain in-
  // variants. Any more complicated game logic that gets layered
  // on top of these should go elsewhere.

  UnitId last_unit_id() const;

  std::unordered_map<UnitId, UnitState> const& all() const;

  // This one we allow non-const access to publicly. The unit
  // must exist.
  Unit const& unit_for( UnitId id ) const;
  Unit&       unit_for( UnitId id );

  // Unit must exist.
  UnitState const&       state_of( UnitId id ) const;
  UnitOwnership_t const& ownership_of( UnitId id ) const;

  maybe<Coord> maybe_coord_for( UnitId id ) const;
  Coord        coord_for( UnitId id ) const;

  maybe<UnitId> maybe_holder_of( UnitId id ) const;
  UnitId        holder_of( UnitId id ) const;

  // We allow non-const access to the harbor view state because
  // changing it will not affect the invariants of this class.
  maybe<UnitHarborViewState&> maybe_harbor_view_state_of(
      UnitId id );
  maybe<UnitHarborViewState const&> maybe_harbor_view_state_of(
      UnitId id ) const;

  UnitHarborViewState& harbor_view_state_of( UnitId id );

  std::unordered_set<UnitId> const& from_coord(
      Coord const& c ) const;

  // Note this returns only units that are working in the colony,
  // not units that are on the map at the location of the colony.
  std::unordered_set<UnitId> const& from_colony(
      Colony const& colony ) const;

  // The id of this unit must be zero (i.e., you can't select the
  // ID); a new ID will be generated for this unit and returned.
  [[nodiscard]] UnitId add_unit( Unit&& unit );

  // Should not be holding any references to the unit after this.
  void destroy_unit( UnitId id );

  // This should probably only be used in unit tests. Returns
  // false if the unit currently exists; returns true if the unit
  // existed but has since been deleted; check-fails if the id
  // doesn't correspond to a unit that ever existed. Note that
  // this does not use the deleted-units cache, it uses the
  // next_unit_id, and so should be ok to call across saves. That
  // said, normal game code probably shouldn't need to call this.
  bool exists( UnitId id ) const;

 private:
  // Changes a unit's ownership from whatever it is (map or oth-
  // erwise) to the map at the given coordinate. It will always
  // move the unit to the target square without question
  // (checking only that the unit exists).
  //
  // NOTE: This is a low-level function; it does not do any
  // checking, and should not be called directly. E.g., this
  // function will happily move a land unit into water.
  // Furthermore, it will not carry out any of the other actions
  // that need to be done when a unit moves onto a new square. In
  // practice, it should only be called by the higher level
  // function in in the on-map module.
  void change_to_map( UnitId id, Coord target );

  // This is the function that calls the above.
  friend void unit_to_map_square_non_interactive(
      SS& ss, TS& ts, UnitId id, Coord world_square );

 private:
  // This is private because it should only be called via the
  // higher level method (below) that can also update the
  // colonies state.
  void change_to_colony( UnitId id, ColonyId col_id );

  friend void move_unit_to_colony( UnitsState& units_state,
                                   Colony&     colony,
                                   UnitId      unit_id,
                                   ColonyJob_t const& job );

 public:
  // Will start at the starting slot and rotate right trying to
  // find a place where the unit can fit.
  void change_to_cargo_somewhere( UnitId new_holder, UnitId held,
                                  int starting_slot = 0 );

  void change_to_cargo( UnitId new_holder, UnitId held,
                        int slot );

  void change_to_harbor_view( UnitId              id,
                              UnitHarborViewState info );

  // ------ Non-invariant Preserving ------
  // This will erase any ownership that is had over the given
  // unit and mark it as free. The unit must soon be assigned a
  // new owernership in order to uphold invariants. This function
  // should rarely be called.
  void disown_unit( UnitId id );

 private:
  [[nodiscard]] UnitId next_unit_id();

  UnitState&       state_of( UnitId id );
  UnitOwnership_t& ownership_of( UnitId id );

  valid_or<std::string> validate() const;
  void                  validate_or_die() const;

  // ----- Serializable state.
  wrapped::UnitsState o_;

  // ----- Non-serializable (transient) state.

  // Holds deleted units for debugging purposes (they will never
  // be resurrected and their IDs will never be reused). Holding
  // the IDs here is technically redundant, but this is on pur-
  // pose in the hope that it might catch a bug.
  std::unordered_set<UnitId> deleted_;

  // For units that are on (owned by) the world (map).
  std::unordered_map<Coord, std::unordered_set<UnitId>>
      units_from_coords_;

  // For units that are held in a colony.
  std::unordered_map<ColonyId, std::unordered_set<UnitId>>
      worker_units_from_colony_;
};

} // namespace rn

/****************************************************************
** Lua
*****************************************************************/
namespace lua {
LUA_USERDATA_TRAITS( ::rn::UnitsState, owned_by_cpp ){};
}
