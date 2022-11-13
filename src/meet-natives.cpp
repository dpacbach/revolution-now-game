/****************************************************************
**meet-natives.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-11-09.
*
* Description: Handles the sequence of events that happen when
*              first encountering a native tribe.
*
*****************************************************************/
#include "meet-natives.hpp"

// Revolution Now
#include "co-wait.hpp"
#include "igui.hpp"
#include "logger.hpp"
#include "society.hpp"
#include "ts.hpp"

// config
#include "config/nation.rds.hpp"
#include "config/natives.rds.hpp"

// ss
#include "ss/colonies.hpp"
#include "ss/natives.hpp"
#include "ss/player.rds.hpp"
#include "ss/ref.hpp"
#include "ss/terrain.hpp"
#include "ss/tribe.rds.hpp"

// refl
#include "refl/to-str.hpp"

// base
#include "base/keyval.hpp"

using namespace std;

namespace rn {

namespace {

unordered_set<DwellingId> dwellings_for_tribe( SSConst const& ss,
                                               e_tribe tribe ) {
  vector<DwellingId> list =
      ss.natives.dwellings_for_tribe( tribe );
  unordered_set<DwellingId> set( list.begin(), list.end() );
  return set;
}

MeetTribe check_meet_tribe_single( SSConst const& ss,
                                   Player const&  player,
                                   e_tribe        tribe ) {
  // Compute the land "occupied" by the player that will be
  // awarded to them by this tribe.
  lg.debug( "meeting the {} tribe.", tribe );

  // 1. Compute all land square occupied by the player, meaning
  // the squares containing colonies and the outdoor workers in
  // those colonies.
  unordered_set<Coord>   land_occupied;
  vector<ColonyId> const colonies =
      ss.colonies.for_nation( player.nation );
  for( ColonyId colony_id : colonies ) {
    Colony const& colony = ss.colonies.colony_for( colony_id );
    Coord const   home   = colony.location;
    land_occupied.insert( home );
    for( auto& [direction, outdoor_unit] :
         colony.outdoor_jobs ) {
      if( !outdoor_unit.has_value() ) continue;
      Coord const outdoor = home.moved( direction );
      land_occupied.insert( outdoor );
    }
  }

  // 2. Get all dwellings for this tribe.
  unordered_set<DwellingId> const dwellings =
      dwellings_for_tribe( ss, tribe );

  // 3. For each occupied square, see if it is owned by one of
  // the above dwellings.
  unordered_set<Coord> land_awarded;
  if( !player.fathers.has[e_founding_father::peter_minuit] ) {
    unordered_map<Coord, DwellingId> const& land_owned =
        ss.natives.owned_land_without_minuit();
    for( Coord occupied : land_occupied ) {
      if( !land_owned.contains( occupied ) ) continue;
      UNWRAP_CHECK( dwelling_id,
                    base::lookup( land_owned, occupied ) );
      if( !dwellings.contains( dwelling_id ) ) continue;
      // The square is owned by natives of this tribe, so award
      // it to the player.
      land_awarded.insert( occupied );
    }
  }
  vector<Coord> sorted_land_awarded( land_awarded.begin(),
                                     land_awarded.end() );
  sort( sorted_land_awarded.begin(), sorted_land_awarded.end() );

  int const num_dwellings = dwellings.size();
  return MeetTribe{
      .is_first      = false, // filled out later.
      .tribe         = tribe,
      .num_dwellings = num_dwellings,
      .land_awarded  = std::move( sorted_land_awarded ) };
}

} // namespace

/****************************************************************
** Public API
*****************************************************************/
vector<MeetTribe> check_meet_tribes( SSConst const& ss,
                                     Player const&  player,
                                     Coord          coord ) {
  vector<MeetTribe> res;
  MapSquare const&  square = ss.terrain.square_at( coord );
  if( square.surface == e_surface::water )
    // Cannot make an initial encounter with the natives from a
    // water square.
    return res;
  unordered_set<e_tribe> met;
  for( e_direction d : refl::enum_values<e_direction> ) {
    Coord const moved = coord.moved( d );
    if( !ss.terrain.square_exists( moved ) ) continue;
    maybe<Society_t> const society =
        society_on_square( ss, moved );
    if( !society.has_value() ) continue;
    maybe<Society::native const&> native =
        society->get_if<Society::native>();
    if( !native.has_value() ) continue;
    if( met.contains( native->tribe ) ) continue;
    if( ss.natives.tribe_for( native->tribe )
            .relationship[player.nation]
            .has_value() )
      continue;
    // We're meeting a new tribe.
    met.insert( native->tribe );
    res.push_back(
        check_meet_tribe_single( ss, player, native->tribe ) );
  }

  if( res.empty() ) return res;

  // Determines if this is the very first tribe we're meeting. We
  // do this last in order to avoid running this every time a
  // unit moves to a new square, since it will yield "no" the
  // vast majority of the time.
  for( e_tribe tribe : refl::enum_values<e_tribe> ) {
    if( !ss.natives.tribe_exists( tribe ) ) continue;
    Tribe const& tribe_obj = ss.natives.tribe_for( tribe );
    if( tribe_obj.relationship[player.nation].has_value() )
      // We've already met at least one tribe, so we're done.
      return res;
  }
  CHECK( !res.empty() );
  res[0].is_first = true;
  return res;
}

wait<e_declare_war_on_natives> perform_meet_tribe_ui_sequence(
    TS& ts, Player const& player, MeetTribe const& meet_tribe ) {
  if( meet_tribe.tribe == e_tribe::inca )
    co_await ts.gui.message_box( "(Woodcut: The Inca Nation)" );
  else if( meet_tribe.tribe == e_tribe::aztec )
    co_await ts.gui.message_box( "(Woodcut: The Aztec Nation)" );
  else if( meet_tribe.is_first )
    co_await ts.gui.message_box(
        "(Woodcut: Meeting the Natives)" );

  auto const& tribe_conf =
      config_natives.tribes[meet_tribe.tribe];
  ui::e_confirm accept_peace =
      co_await ts.gui.required_yes_no( YesNoConfig{
          .msg = fmt::format(
              "The @[H]{}@[] tribe is a celebrated nation of "
              "@[H]{} {}@[].  In honor of our glorious future "
              "together we will generously give you all of the "
              "land that your colonies now occupy. Will you "
              "accept our peace treaty and agree to live in "
              "harmony with us?",
              tribe_conf.name_singular, meet_tribe.num_dwellings,
              meet_tribe.num_dwellings > 1
                  ? config_natives
                        .dwelling_types[tribe_conf.dwelling_type]
                        .name_plural
                  : config_natives
                        .dwelling_types[tribe_conf.dwelling_type]
                        .name_singular ),
          .yes_label      = "Yes",
          .no_label       = "No",
          .no_comes_first = false } );
  switch( accept_peace ) {
    case ui::e_confirm::no: {
      co_await ts.gui.message_box(
          "In that case the mighty @[H]{}@[] will drive you "
          "into oblivion. Prepare for WAR!",
          tribe_conf.name_singular );
      co_return e_declare_war_on_natives::yes;
    }
    case ui::e_confirm::yes: break;
  }

  co_await ts.gui.message_box(
      "Let us smoke a peace pipe to celebrate our purpetual "
      "friendship with the @[H]{}@[].",
      config_nation.nations[player.nation].display_name );

  co_await ts.gui.message_box(
      "We hope that you will send us your colonists and "
      "@[H]Wagon Trains@[] to share knowledge and to trade." );

  co_return e_declare_war_on_natives::no;
}

void perform_meet_tribe( SS& ss, Player const& player,
                         MeetTribe const&         meet_tribe,
                         e_declare_war_on_natives declare_war ) {
  Tribe& tribe = ss.natives.tribe_for( meet_tribe.tribe );

  // Create the relationship object.
  CHECK( !tribe.relationship[player.nation].has_value() );
  tribe.relationship[player.nation] = TribeRelationship{
      .at_war = ( declare_war == e_declare_war_on_natives::yes ),
      .tribal_alarm = 0 };

  unordered_map<Coord, DwellingId>& owned_land =
      ss.natives.owned_land_without_minuit();
  // Award player any land they "occupy" that is owned by this
  // tribe. Note that if the player has Peter Minuit then this
  // should be an empty list.
  if( player.fathers.has[e_founding_father::peter_minuit] ) {
    CHECK( meet_tribe.land_awarded.empty() );
  }
  for( Coord to_award : meet_tribe.land_awarded ) {
    CHECK( owned_land.contains( to_award ),
           "square {} was supposed to be owned by the {} tribe "
           "but isn't owned at all.",
           to_award, meet_tribe.tribe );
    auto it = owned_land.find( to_award );
    CHECK( it != owned_land.end() );
    owned_land.erase( it );
  }
}

} // namespace rn