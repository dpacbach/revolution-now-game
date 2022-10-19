/****************************************************************
**plow.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-03-27.
*
* Description: Plowing rendering and state changes.
*
*****************************************************************/
#include "plow.hpp"

// Revolution Now
#include "imap-updater.hpp"
#include "logger.hpp"
#include "map-square.hpp"
#include "terrain.hpp"
#include "tiles.hpp"

// config
#include "config/orders.rds.hpp"
#include "config/unit-type.rds.hpp"

// ss
#include "ss/ref.hpp"
#include "ss/terrain.hpp"
#include "ss/units.hpp"

// render
#include "render/painter.hpp"

// refl
#include "refl/to-str.hpp"

// base
#include "base/to-str-ext-std.hpp"

using namespace std;

namespace rn {

namespace {

void irrigate( IMapUpdater& map_updater, Coord tile ) {
  map_updater.modify_map_square(
      tile, []( MapSquare& square ) { irrigate( square ); } );
}

void clear_forest( IMapUpdater& map_updater, Coord tile ) {
  map_updater.modify_map_square( tile, []( MapSquare& square ) {
    clear_forest( square );
  } );
}

int turns_required( e_unit_type unit_type, e_terrain terrain ) {
  UNWRAP_CHECK( for_terrain, config_orders.plow_turns[terrain] );
  switch( unit_type ) {
    case e_unit_type::pioneer: return for_terrain;
    case e_unit_type::hardy_pioneer:
      return std::max( for_terrain / 2, 1 );
    default: break;
  }
  FATAL( "unit type {} cannot plow/clear.", unit_type );
}

// If the player has at least one colony in the vicinity then
// this will pick the closest one and compute how much lumber
// should yield to the colony, though it won't actually add it.
maybe<LumberYield> lumber_yield( SSConst const&, Coord,
                                 e_unit_type ) {
  // TODO
  return nothing;
}

// Applies the yield to the colony and displays a message to the
// player.
void apply_lumber_yield( SS&, LumberYield const& ) {
  // TODO
}

} // namespace

/****************************************************************
** Plowing State
*****************************************************************/
void plow_square( TerrainState const& terrain_state,
                  IMapUpdater& map_updater, Coord tile ) {
  CHECK( terrain_state.is_land( tile ) );
  MapSquare const& square = terrain_state.square_at( tile );
  if( has_forest( square ) ) {
    clear_forest( map_updater, tile );
    return;
  }
  CHECK( !square.irrigation,
         "tile {} already has irrigation and thus cannot be "
         "plowed.",
         tile );
  if( can_irrigate( square ) ) {
    irrigate( map_updater, tile );
    return;
  }
  FATAL( "terrain type {} cannot be plowed: square={}",
         effective_terrain( square ), square );
}

bool can_plow( TerrainState const& terrain_state, Coord tile ) {
  MapSquare const& square = terrain_state.square_at( tile );
  if( square.irrigation ) return false;
  return config_orders.plow_turns[effective_terrain( square )]
      .has_value();
}

bool can_irrigate( MapSquare const& square ) {
  return !square.irrigation &&
         square.overlay != e_land_overlay::forest &&
         config_orders.plow_turns[effective_terrain( square )]
             .has_value();
}

bool can_irrigate( TerrainState const& terrain_state,
                   Coord               tile ) {
  MapSquare const& square = terrain_state.square_at( tile );
  return can_irrigate( square );
}

bool has_irrigation( TerrainState const& terrain_state,
                     Coord               tile ) {
  MapSquare const& square = terrain_state.square_at( tile );
  return has_irrigation( square );
}

bool has_irrigation( MapSquare const& square ) {
  return square.irrigation;
}

/****************************************************************
** Unit State
*****************************************************************/
PlowResult_t perform_plow_work( SS& ss, Player const& player,
                                IMapUpdater& map_updater,
                                Unit&        unit ) {
  Coord location = ss.units.coord_for( unit.id() );
  CHECK( unit.orders() == e_unit_orders::plow );
  CHECK( unit.type() == e_unit_type::pioneer ||
             unit.type() == e_unit_type::hardy_pioneer,
         "unit type {} should not be plowing.", unit.type() );
  CHECK_GT( unit.movement_points(), 0 );
  auto log = [&]( string_view status ) {
    lg.debug( "plow work {} for unit {} with {} tools left.",
              status, debug_string( unit ),
              unit.composition()[e_unit_inventory::tools] );
  };

  // First check if there is already irrigation on this square.
  // This could happen if there are two units plowing on the same
  // square and the other unit finished first. In that case, we
  // will just clear this unit's orders and not charge it any
  // tools.
  if( has_irrigation( ss.terrain, location ) ) {
    log( "cancelled" );
    unit.clear_orders();
    unit.set_turns_worked( 0 );
    return PlowResult::cancelled{};
  }
  // The unit is still plowing.
  int       turns_worked = unit.turns_worked();
  int const plow_turns   = turns_required(
      unit.type(),
      effective_terrain( ss.terrain.square_at( location ) ) );
  CHECK_LE( turns_worked, plow_turns );
  if( turns_worked == plow_turns ) {
    PlowResult_t res = PlowResult::irrigated{};
    if( has_forest( ss.terrain.square_at( location ) ) ) {
      maybe<LumberYield> const yield =
          lumber_yield( ss, location, unit.type() );
      res = PlowResult::cleared_forest{ .yield = yield };
      if( yield.has_value() ) apply_lumber_yield( ss, *yield );
    }
    // We're finished plowing.
    plow_square( ss.terrain, map_updater, location );
    unit.clear_orders();
    unit.set_turns_worked( 0 );
    unit.consume_20_tools( player );
    log( "finished" );
    return res;
  }
  // We need more work.
  log( "ongoing" );
  unit.forfeight_mv_points();
  unit.set_turns_worked( turns_worked + 1 );
  return PlowResult::ongoing{};
}

bool can_plow( Unit const& unit ) {
  return unit.type() == e_unit_type::pioneer ||
         unit.type() == e_unit_type::hardy_pioneer;
}

/****************************************************************
** Rendering
*****************************************************************/
void render_plow_if_present( rr::Painter& painter, Coord where,
                             MapSquare const& square ) {
  if( !has_irrigation( square ) ) return;
  render_sprite( painter, where, e_tile::irrigation );
}

} // namespace rn
