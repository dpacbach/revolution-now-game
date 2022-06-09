/****************************************************************
**orders-fortify.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-04-16.
*
* Description: Carries out orders to fortify or sentry a unit.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "orders.hpp"

namespace rn {

struct IMapUpdater;
struct LandViewPlane;
struct SettingsState;
struct UnitsState;
struct ColoniesState;
struct TerrainState;
struct Player;
struct Planes;

std::unique_ptr<OrdersHandler> handle_orders(
    UnitId id, orders::fortify const& fortify, IMapUpdater*,
    IGui& gui, Player& player, TerrainState const& terrain_state,
    UnitsState& units_state, ColoniesState& colonies_state,
    SettingsState const& settings,
    LandViewPlane& land_view_plane, Planes& planes );

std::unique_ptr<OrdersHandler> handle_orders(
    UnitId id, orders::sentry const& sentry, IMapUpdater*,
    IGui& gui, Player& player, TerrainState const& terrain_state,
    UnitsState& units_state, ColoniesState& colonies_state,
    SettingsState const& settings,
    LandViewPlane& land_view_plane, Planes& planes );

} // namespace rn
