/****************************************************************
**immigration.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-05-28.
*
* Description: All things immigration.
*
*****************************************************************/
#include "immigration.hpp"

// Revolution Now
#include "co-wait.hpp"
#include "gs-settings.hpp"
#include "gs-units.hpp"
#include "harbor-units.hpp"
#include "igui.hpp"
#include "logger.hpp"
#include "lua.hpp"
#include "player.hpp"
#include "rand-enum.hpp"
#include "utype.hpp"

// config
#include "config/immigration.rds.hpp"
#include "config/nation.rds.hpp"

// Rds
#include "old-world-state.rds.hpp"

// luapp
#include "luapp/state.hpp"

// refl
#include "refl/to-str.hpp"

using namespace std;

namespace rn {

namespace {

using WeightsMap = refl::enum_map<e_unit_type, double>;

WeightsMap immigrant_weights_for_level( int level ) {
  WeightsMap const& scaling =
      config_immigration.difficulty_factor_per_level;
  // Any unit types that are not in the config will have default
  // values of 0.0 in the below enum_map which is what we want,
  // since that will prevent them from being selected.
  WeightsMap weights = config_immigration.base_weights;
  for( e_unit_type type : refl::enum_values<e_unit_type> )
    weights[type] *= pow( scaling[type], double( level ) );
  return weights;
}

struct UnitCounts {
  int total_units   = 0;
  int units_on_dock = 0;
};

UnitCounts unit_counts( UnitsState const& units_state,
                        e_nation          nation ) {
  UnitCounts counts;
  for( auto const& [id, state] : units_state.all() ) {
    if( state.unit.nation() != nation ) continue;
    ++counts.total_units;
    if( !state.unit.desc().ship &&
        state.ownership.holds<UnitOwnership::harbor>() )
      ++counts.units_on_dock;
  }
  return counts;
}

} // namespace

wait<int> ask_player_to_choose_immigrant(
    IGui& gui, ImmigrationState const& immigration,
    string msg ) {
  array<e_unit_type, 3> const& pool =
      immigration.immigrants_pool;
  vector<ChoiceConfigOption> options{
      { .key = "0", .display_name = unit_attr( pool[0] ).name },
      { .key = "1", .display_name = unit_attr( pool[1] ).name },
      { .key = "2", .display_name = unit_attr( pool[2] ).name },
  };
  ChoiceConfig config{
      .msg           = std::move( msg ),
      .options       = options,
      .key_on_escape = nothing,
  };

  std::string res = co_await gui.choice( config );
  if( res == "0" ) co_return 0;
  if( res == "1" ) co_return 1;
  if( res == "2" ) co_return 2;
  FATAL(
      "unexpected selection result: {} (should be '0', '1', or "
      "'2')",
      res );
}

e_unit_type take_immigrant_from_pool(
    ImmigrationState& immigration, int n,
    e_unit_type replacement ) {
  CHECK_GE( n, 0 );
  CHECK_LE( n, 2 );
  DCHECK( immigration.immigrants_pool.size() >= 3 );
  e_unit_type taken = immigration.immigrants_pool[n];
  immigration.immigrants_pool[n] = replacement;
  return taken;
}

e_unit_type pick_next_unit_for_pool(
    Player const& player, SettingsState const& settings ) {
  WeightsMap weights =
      immigrant_weights_for_level( settings.difficulty );

  // Having William Brewster prevents criminals and servants from
  // showing up on the docks.
  bool has_brewster =
      player.fathers.has[e_founding_father::william_brewster];
  if( has_brewster ) {
    weights[e_unit_type::petty_criminal]     = 0.0;
    weights[e_unit_type::indentured_servant] = 0.0;
  }

  return rng::pick_from_weighted_enum_values( weights );
}

CrossesCalculation compute_crosses(
    UnitsState const& units_state, e_nation nation ) {
  // First compute the crosses bonus from the dock.
  //
  // The original game gives an extra two crosses per turn when
  // the dock is empty, and subtracts two crosses per turn for
  // each unit on the dock. That means that if no units are on
  // the dock we get +2, but if 1 unit is on the dock we get -2
  // (because we both lose the empty-dock bonus and incur the
  // penalty of one unit on the dock).
  auto const [total_units, units_on_dock] =
      unit_counts( units_state, nation );
  int const dock_crosses_bonus =
      ( units_on_dock == 0 ) ? 2 : ( -units_on_dock * 2 );
  DCHECK( dock_crosses_bonus != 0 );

  // Next compute the total number of crosses needed for the next
  // immigration. The formula given in the Colonization 1
  // strategy guide is this:
  //
  //   8 + 2*(units in colonies + units (people?) in new world)
  //
  // But this does not seem to be right.  This page:
  //
  //   civilization.fandom.com/wiki/Colonization_tips
  //
  // gives the correct formula, which is:
  //
  //   8 + 2*(units + units-on-dock)
  //
  // where 'units' are all owned units of any kind, including the
  // ones on the dock, and units-on-dock are the units on the
  // dock (and so they are counted twice here). Note that "dock"
  // here refers only to the non-ship units on the dock. As de-
  // scribed in the above link, the Col 1 debug view will not in-
  // clude the units-on-dock when it prints the number of crosses
  // needed on the Religion Advisor page, but it still includes
  // them in the calculation.
  int const default_crosses_needed =
      8 + 2 * ( total_units + units_on_dock );

  // This is what will incorporate the English's special ability
  // to more quickly attract immigrants.
  int const crosses_needed = std::lround(
      default_crosses_needed * config_nation.abilities[nation]
                                   .crosses_needed_multiplier );

  return CrossesCalculation{
      .dock_crosses_bonus = dock_crosses_bonus,
      .crosses_needed     = crosses_needed };
}

void add_player_crosses( Player& player,
                         int     total_colonies_cross_production,
                         int     dock_crosses_bonus ) {
  // This bit of logic is important: the total colonies' produc-
  // tion must be added to the dock bonus before adding them to
  // the player's total so that we can make sure that the differ-
  // ential is not negative. That could happen e.g. when the
  // player has only one colony producing a single cross but has
  // one or more units waiting on the dock.
  int const delta =
      total_colonies_cross_production + dock_crosses_bonus;
  if( delta < 0 ) return;
  lg.debug( "{} crosses increased by {}.", player.nation,
            delta );
  player.crosses += delta;
}

wait<maybe<UnitId>> check_for_new_immigrant(
    IGui& gui, UnitsState& units_state, Player& player,
    SettingsState const& settings, int crosses_needed ) {
  CHECK_GE( crosses_needed, 0 );
  if( player.crosses < crosses_needed ) co_return nothing;
  player.crosses -= crosses_needed;
  DCHECK( player.crosses >= 0 );
  int immigrant_idx = {};
  rng::between( 0, 2, rng::e_interval::closed );
  if( player.fathers.has[e_founding_father::william_brewster] ) {
    string msg =
        "Word of religious freedom has spread! New immigrants "
        "are ready to join us in the New World.  Which of the "
        "following shall we choose?";
    immigrant_idx = co_await ask_player_to_choose_immigrant(
        gui, player.old_world.immigration, msg );
    CHECK_GE( immigrant_idx, 0 );
    CHECK_LE( immigrant_idx, 2 );
  } else {
    immigrant_idx =
        rng::between( 0, 2, rng::e_interval::closed );
    string msg = fmt::format(
        "Word of religious freedom has spread! A new immigrant "
        "(@[H]{}@[]) has arrived on the docks.",
        unit_attr( player.old_world.immigration
                       .immigrants_pool[immigrant_idx] )
            .name );
    co_await gui.message_box( msg );
  }
  e_unit_type replacement =
      pick_next_unit_for_pool( player, settings );
  e_unit_type type = take_immigrant_from_pool(
      player.old_world.immigration, immigrant_idx, replacement );
  co_return create_unit_in_harbor( units_state, player.nation,
                                   type );
}

/****************************************************************
** Lua Bindings
*****************************************************************/
namespace {

LUA_FN( pick_next_unit_for_pool, e_unit_type,
        Player const& player, SettingsState const& settings ) {
  return pick_next_unit_for_pool( player, settings );
};

} // namespace

} // namespace rn