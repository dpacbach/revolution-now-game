/****************************************************************
**movement.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-09-03.
*
* Description: Physical movement of units.
*
*****************************************************************/
#include "movement.hpp"

// Revolution Now
#include "errors.hpp"
#include "id.hpp"
#include "orders.hpp"
#include "ownership.hpp"
#include "util.hpp"
#include "window.hpp"
#include "world.hpp"

// base-util
#include "base-util/variant.hpp"

using namespace std;

namespace rn {

namespace {} // namespace

bool ProposedMoveAnalysisResult::allowed() const {
  return util::holds<e_unit_mv_good>( desc );
}

// Called at the beginning of each turn; marks all units
// as not yet having moved.
void reset_moves() {
  map_units( []( Unit& unit ) { unit.new_turn(); } );
}

// This function will allow the move by default, and so it is the
// burden of the logic in this function to find every possible
// way that the move is *not* allowed (among the situations that
// this function is concerned about) and to flag it if that is
// the case.
ProposedMoveAnalysisResult analyze_proposed_move_impl(
    UnitId id, e_direction d ) {
  auto src_coord = coords_for_unit( id );
  auto coords    = src_coord.moved( d );

  Y y = coords.y;
  X x = coords.x;

  auto& unit = unit_from_id( id );
  CHECK( !unit.moved_this_turn() );

  MovementPoints cost( 1 );

  ProposedMoveAnalysisResult result{
      /*id=*/id,
      /*unit_would_move=*/true,
      /*move_src=*/src_coord,
      /*move_target=*/coords,
      /*desc=*/e_unit_mv_good::map_to_map,
      /*movement_cost=*/cost,
      /*target_unit=*/{},
      /*to_prioritize=*/{}};

  if( !coords.is_inside( world_rect() ) ) {
    result.desc = e_unit_mv_error::map_edge;
    return result;
  }
  auto& square = square_at( y, x );

  if( unit.desc().boat && square.land ) {
    std::vector<UnitId> to_offload;
    for( auto cargo_id : unit.cargo().items_of_type<UnitId>() ) {
      auto const& cargo_unit = unit_from_id( cargo_id );
      if( !cargo_unit.moved_this_turn() )
        to_offload.push_back( cargo_id );
    }
    if( !to_offload.empty() ) {
      // We have at least one unit in the cargo that is able to
      // make landfall. So we will indicate that the unit is
      // allowed to make this move, but we change the target
      // square to where the unit currently is since it will
      // not physically move.
      result.desc            = e_unit_mv_good::land_fall;
      result.unit_would_move = false;
      result.move_target     = coords;
      result.movement_cost   = 0;
      result.to_prioritize   = to_offload;
      return result;
    }
    result.desc = e_unit_mv_error::land_forbidden;
    return result;
  }

  if( !unit.desc().boat && !square.land ) {
    auto const& ships = units_from_coord( y, x );
    if( ships.empty() ) {
      result.desc = e_unit_mv_error::water_forbidden;
      return result;
    }
    // We have at least on ship, so iterate through and find the
    // first one (if any) that the unit can board.
    for( auto ship_id : ships ) {
      auto const& ship_unit = unit_from_id( ship_id );
      CHECK( ship_unit.desc().boat );
      auto& cargo = ship_unit.cargo();
      if( cargo.fits( id ) ) {
        result.desc          = e_unit_mv_good::board_ship;
        result.target_unit   = ship_id;
        result.to_prioritize = {ship_id};
        return result;
      }
    }
    result.desc = e_unit_mv_error::board_ship_full;
    return result;
  }

  // `holder` will be a valid value if the unit is cargo of an-
  // other unit; the holder's id in that case will be *holder.
  auto holder = is_unit_onboard( unit.id() );
  if( !unit.desc().boat && square.land && holder ) {
    // We have a unit onboard a ship moving onto land.
    result.desc = e_unit_mv_good::offboard_ship;
    return result;
  }

  result.desc = e_unit_mv_good::map_to_map;
  return result;
}

// This is the entry point; calls the implementation then checks
// invariants.
ProposedMoveAnalysisResult analyze_proposed_move(
    UnitId id, e_direction d ) {
  auto res = analyze_proposed_move_impl( id, d );
  // Now check invariants.
  CHECK( res.id == id );
  CHECK( res.move_src != res.move_target );
  CHECK( find( res.to_prioritize.begin(),
               res.to_prioritize.end(),
               id ) == res.to_prioritize.end() );
  CHECK( res.move_src == coords_for_unit( id ) );
  CHECK( res.move_src.is_adjacent_to( res.move_target ) );
  CHECK( res.target_unit != id );
  return res;
}

void move_unit( ProposedMoveAnalysisResult const& analysis ) {
  auto  id   = analysis.id;
  auto& unit = unit_from_id( id );
  CHECK( !unit.moved_this_turn() );

  CHECK( unit.orders() == Unit::e_orders::none );

  // Caller should have checked this.
  CHECK( analysis.allowed() );

  e_unit_mv_good outcome = get<e_unit_mv_good>( analysis.desc );

  // This will throw if the unit has no coords, but I think it
  // should always at this point if we're moving it.
  auto old_coord = coords_for_unit( id );

  switch( outcome ) {
    case e_unit_mv_good::map_to_map:
      // If it's a ship then sentry all its units before it
      // moves.
      if( unit.desc().boat ) {
        for( UnitId id : unit.cargo().items_of_type<UnitId>() ) {
          auto& cargo_unit = unit_from_id( id );
          cargo_unit.sentry();
        }
      }
      ownership_change_to_map( id, analysis.move_target );
      unit.consume_mv_points( analysis.movement_cost );
      break;
    case e_unit_mv_good::board_ship:
      CHECK( analysis.target_unit.has_value() );
      ownership_change_to_cargo( *analysis.target_unit, id );
      unit.forfeight_mv_points();
      unit.sentry();
      break;
    case e_unit_mv_good::offboard_ship:
      ownership_change_to_map( id, analysis.move_target );
      unit.forfeight_mv_points();
      CHECK( unit.orders() == Unit::e_orders::none );
      break;
    case e_unit_mv_good::land_fall:
      // Just activate all the units on the ship that have not
      // completed their turns. Note that the ship's movement
      // points are not consumed.
      for( auto cargo_id :
           unit.cargo().items_of_type<UnitId>() ) {
        auto& cargo_unit = unit_from_id( cargo_id );
        if( !cargo_unit.moved_this_turn() ) {
          cargo_unit.clear_orders();
          // In case the unit has already been processed in the
          // turn loop and was passed over due to sentry status
          // onboard (and therefore marked as having finished its
          // turn) while still having movement points left. Since
          // the unit is now being re-prioritized to the begin-
          // ning of the turn loop, we need to mark it's turn as
          // unfinished again this turn so that it will take or-
          // ders otherwise it will just be passed over again
          // this turn.
          cargo_unit.unfinish_turn();
          auto direction =
              old_coord.direction_to( analysis.move_target );
          CHECK( direction.has_value() );
          PlayerUnitOrders orders = orders::move{*direction};
          push_unit_orders( cargo_id, orders );
        }
      }
      break;
  }

  // Now do a sanity check.
  auto new_coord = coords_for_unit( id );
  CHECK( analysis.unit_would_move ==
         ( new_coord == analysis.move_target ) );
}

bool confirm_explain_move(
    ProposedMoveAnalysisResult const& analysis ) {
  if( !analysis.allowed() ) return false;
  // If we're here then that means that the move is physically
  // allowed assuming there are enough movement points.  Check
  // for that now.
  auto& unit = unit_from_id( analysis.id );
  if( unit.movement_points() < analysis.movement_cost )
    return false;
  // The above should have checked that the variant holds the
  // e_unit_mv_good type for us.
  auto& kind = val_or_die<e_unit_mv_good>( analysis.desc );

  switch( kind ) {
    case e_unit_mv_good::land_fall: {
      auto answer =
          ui::yes_no( "Would you like to make landfall?" );
      return ( answer == ui::e_confirm::yes );
    }
    case e_unit_mv_good::map_to_map:
    case e_unit_mv_good::board_ship:
    case e_unit_mv_good::offboard_ship:
      // Nothing to ask here, just allow the move.
      return true;
  }
  SHOULD_NOT_BE_HERE;
  return false;
}

} // namespace rn
