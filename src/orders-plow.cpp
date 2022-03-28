/****************************************************************
**orders-plow.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-03-27.
*
* Description: Carries out orders to plow.
*
*****************************************************************/
#include "orders-plow.hpp"

// Revolution Now
#include "co-wait.hpp"
#include "game-state.hpp"
#include "gs-terrain.hpp"
#include "gs-units.hpp"
#include "logger.hpp"
#include "plow.hpp"
#include "window.hpp"
#include "world-map.hpp"

// refl
#include "refl/to-str.hpp"

using namespace std;

namespace rn {

namespace {

struct PlowHandler : public OrdersHandler {
  PlowHandler( UnitId unit_id_ ) : unit_id( unit_id_ ) {}

  wait<bool> confirm() override {
    UnitsState const& units_state = GameState::units();
    Unit const&       unit = units_state.unit_for( unit_id );
    if( unit.type() == e_unit_type::hardy_colonist ) {
      co_await ui::message_box_basic(
          "This @[H]Hardy Pioneer@[] requires at least 20 tools "
          "to plow." );
      co_return false;
    }
    if( unit.type() != e_unit_type::pioneer &&
        unit.type() != e_unit_type::hardy_pioneer ) {
      co_await ui::message_box_basic(
          "Only @[H]Pioneers@[] and @[H]Hardy Pioneers@[] can "
          "plow." );
      co_return false;
    }
    UnitOwnership_t const& ownership =
        units_state.ownership_of( unit_id );
    if( !ownership.is<UnitOwnership::world>() ) {
      // This can happen if a pioneer is on a ship asking for or-
      // ders and it is given plowing orders.
      co_await ui::message_box_basic(
          "Plowing can only be done while directly on a land "
          "tile." );
      co_return false;
    }
    Coord world_square = units_state.coord_for( unit_id );
    TerrainState const& terrain_state = GameState::terrain();
    CHECK( is_land( terrain_state, world_square ) );
    if( !can_plow( terrain_state, world_square ) ) {
      co_await ui::message_box(
          "@[H]{}@[] tiles cannot be plowed or cleared.",
          square_at( terrain_state, world_square ).terrain );
      co_return false;
    }
    if( has_irrigation( terrain_state, world_square ) ) {
      co_await ui::message_box_basic(
          "There is already irrigation on this square." );
      co_return false;
    }
    co_return true;
  }

  wait<> perform() override {
    lg.info( "plowing." );
    UnitsState& units_state = GameState::units();
    Unit&       unit        = units_state.unit_for( unit_id );
    // The unit of course does not need movement points to plow
    // but we use those to also track if the unit has used up its
    // turn.
    CHECK( !unit.mv_pts_exhausted() );
    // Note that we don't charge the unit any movement points
    // yet. That way the player can change their mind after
    // plowing and move the unit. They are only charged movement
    // points at the start of the next turn when they contribute
    // some progress to plowing.
    unit.plow();
    unit.set_turns_worked( 0 );
    co_return;
  }

  UnitId unit_id;
};

} // namespace

/****************************************************************
** Public API
*****************************************************************/
unique_ptr<OrdersHandler> handle_orders(
    UnitId id, orders::plow const& /*plow*/ ) {
  return make_unique<PlowHandler>( id );
}

} // namespace rn