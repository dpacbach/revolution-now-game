/****************************************************************
**turn.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-31.
*
* Description: Main loop that processes a turn.
*
*****************************************************************/
#include "turn.hpp"

// Revolution Now
#include "co-combinator.hpp"
#include "colony-mgr.hpp"
#include "colony-view.hpp"
#include "cstate.hpp"
#include "fb.hpp"
#include "flat-deque.hpp"
#include "flat-queue.hpp"
#include "frame.hpp"
#include "land-view.hpp"
#include "logging.hpp"
#include "orders.hpp"
#include "panel.hpp" // FIXME
#include "sg-macros.hpp"
#include "sound.hpp"
#include "unit.hpp"
#include "ustate.hpp"
#include "viewport.hpp"
#include "waitable-coro.hpp"
#include "window.hpp"

// base
#include "base/lambda.hpp"

// Flatbuffers
#include "fb/sg-turn_generated.h"

// base-util
#include "base-util/algo.hpp"

// C++ standard library
#include <algorithm>

using namespace std;

namespace rn {

DECLARE_SAVEGAME_SERIALIZERS( Turn );

namespace {

/****************************************************************
** Coroutine turn state.
*****************************************************************/
struct NationState {
  NationState() = default;
  NationState( e_nation nat ) : NationState() { nation = nat; }
  valid_deserial_t check_invariants_safe() { return valid; }

  bool operator==( NationState const& ) const = default;

  // clang-format off
  SERIALIZABLE_TABLE_MEMBERS( fb, NationState,
  ( e_nation,           nation       ),
  ( bool,               started      ),
  ( bool,               did_colonies ),
  ( bool,               did_units    ),
  ( flat_deque<UnitId>, units        ));
  // clang-format on
};

struct TurnState {
  TurnState() = default;

  void new_turn() {
    started   = false;
    need_eot  = true;
    nation    = nothing;
    remainder = {
        e_nation::english, //
        e_nation::french,  //
        e_nation::dutch,   //
        e_nation::spanish  //
    };
  }

  bool operator==( TurnState const& ) const = default;
  valid_deserial_t check_invariants_safe() { return valid; }

  // clang-format off
  SERIALIZABLE_TABLE_MEMBERS( fb, TurnState,
  ( bool,                 started   ),
  ( bool,                 need_eot  ),
  ( maybe<NationState>,   nation    ),
  ( flat_queue<e_nation>, remainder ));
  // clang-format on
};

/****************************************************************
** Save-Game State
*****************************************************************/
struct SAVEGAME_STRUCT( Turn ) {
  // Fields that are actually serialized.

  // clang-format off
  SAVEGAME_MEMBERS( Turn,
  ( TurnState, turn ));
  // clang-format on

public:
  // Fields that are derived from the serialized fields.

private:
  SAVEGAME_FRIENDS( Turn );
  SAVEGAME_SYNC() {
    // Sync all fields that are derived from serialized fields
    // and then validate (check invariants).

    return valid;
  }
  // Called after all modules are deserialized.
  SAVEGAME_VALIDATE() { return valid; }
};
SAVEGAME_IMPL( Turn );

/****************************************************************
** Top-level per-turn workflows.
*****************************************************************/
// Returns true if the unit needs to ask the user for input.
bool advance_unit( UnitId id ) {
  auto& unit = unit_from_id( id );
  // - if it is it in `goto` mode focus on it and advance it
  //
  // - if it is a ship on the high seas then advance it if it has
  //   arrived in the old world then jump to the old world screen
  //   (maybe ask user whether they want to ignore), which has
  //   its own game loop (see old-world loop).
  //
  // - if it is in the old world then ignore it, or possibly re-
  //   mind the user it is there.
  //
  // - if it is performing an action, such as building a road,
  //   advance the state. If it finishes then mark it as active
  //   so that it will wait for orders in the next step.
  //
  // - if it is in an indian village then advance it, and mark it
  //   active if it is finished.
  //
  // - if unit is waiting for orders then focus on it, make it
  //   blink, and wait for orders.

  if( !is_unit_on_map_indirect( id ) ) {
    // TODO.
    unit.finish_turn();
    return false;
  }

  if( !unit.orders_mean_input_required() ||
      unit.mv_pts_exhausted() ) {
    unit.finish_turn();
    return false;
  }

  // Unit needs to ask for orders.
  return true;
}

waitable<> process_eot_player_inputs() {
  while( true ) {
    LandViewPlayerInput_t response =
        co_await landview_eot_get_next_input();
    switch( response.to_enum() ) {
      using namespace LandViewPlayerInput;
      case e::colony: {
        co_await show_colony_view( response.get<colony>().id );
        break;
      }
      // We have some orders for the current unit.
      case e::clear_orders: {
        auto& val = response.get<clear_orders>();
        // Move some units to the back of the queue.
        unit_from_id( val.unit ).clear_orders();
        break;
      }
      default: break;
    }
  }
}

waitable<> end_of_turn() {
  return co::any( process_eot_player_inputs(), // never ends
                  wait_for_eot_button_click() );
}

waitable<> next_player_input( UnitId              id,
                              flat_deque<UnitId>* q ) {
  LandViewPlayerInput_t response;
  if( auto maybe_orders = pop_unit_orders( id ) ) {
    response = LandViewPlayerInput::give_orders{
        .orders = *maybe_orders };
  } else {
    lg.debug( "asking orders for: {}", debug_string( id ) );
    SG().turn.need_eot = false;

    response = co_await landview_get_next_input( id );
  }
  switch( response.to_enum() ) {
    using namespace LandViewPlayerInput;
    case e::colony: {
      co_await show_colony_view( response.get<colony>().id );
      break;
    }
    // We have some orders for the current unit.
    case e::give_orders: {
      auto& orders = response.get<give_orders>().orders;
      if( orders.holds<orders::wait>() ) {
        q->push_back( id );
        CHECK( q->front() == id );
        q->pop_front();
        break;
      }
      if( orders.holds<orders::forfeight>() ) {
        unit_from_id( id ).forfeight_mv_points();
        break;
      }

      unique_ptr<OrdersHandler> handler =
          orders_handler( id, orders );
      CHECK( handler );
      auto run_result = co_await handler->run();

      // If we suspended at some point during the above process
      // (apart from animations), then that probably means that
      // the user was presented with a prompt, in which case it
      // seems like a good idea to clear the input buffers for an
      // intuitive user experience.
      if( run_result.suspended ) {
        lg.debug( "clearing land-view input buffers." );
        landview_reset_input_buffers();
      }
      if( !run_result.order_was_run ) break;
      // !! The unit may no longer exist at this point, e.g. if
      // they were disbanded or if they lost a battle to the na-
      // tives.

      for( auto id : handler->units_to_prioritize() ) {
        q->push_front( id );
        unit_from_id( id ).unfinish_turn();
      }
      break;
    }
    case e::clear_orders: {
      auto& val = response.get<clear_orders>();
      // Move some units to the back of the queue.
      unit_from_id( val.unit ).clear_orders();
      unit_from_id( val.unit ).unfinish_turn();
      // In addition to clearing orders, we also need to add this
      // unit onto the back of the queue in case e.g. a unit that
      // is sentry'd has already been removed from the queue
      // (without asking for orders) and later in the same turn
      // had its orders cleared by the player (but not priori-
      // tized), this will allow it to ask for orders this turn.
      q->push_back( val.unit );
      break;
    }
    case e::prioritize: {
      auto& val = response.get<prioritize>();
      // Move some units to the front of the queue.
      auto prioritize = val.units;
      erase_if( prioritize,
                L( unit_from_id( _ ).mv_pts_exhausted() ) );
      auto orig_size = val.units.size();
      auto curr_size = prioritize.size();
      CHECK( curr_size <= orig_size );
      if( curr_size == 0 )
        co_await ui::message_box(
            "The selected unit(s) have already moved this "
            "turn." );
      else if( curr_size < orig_size )
        co_await ui::message_box(
            "Some of the selected units have already moved this "
            "turn." );
      for( UnitId id_to_add : prioritize ) {
        q->push_front( id_to_add );
        unit_from_id( id_to_add ).unfinish_turn();
      }
      break;
    }
  }
}

waitable<> units_turn() {
  CHECK( SG().turn.nation );
  auto& st = *SG().turn.nation;
  auto& q  = st.units;

  // Initialize.
  if( q.empty() ) {
    auto units = units_all( st.nation );
    util::sort_by_key( units, []( auto id ) { return id._; } );
    // Why do we need this?
    erase_if( units, L( unit_from_id( _ ).finished_turn() ) );
    for( UnitId id : units ) q.push_back( id );
  }

  while( !q.empty() ) {
    lg.trace( "q: {}", q.to_string( 3 ) );
    UnitId id = *q.front();
    // We need this check because units can be added into the
    // queue in this loop by user input. Also, the very first
    // check that we must do needs to be to check if the unit
    // still exists, which it might not if e.g. it was disbanded.
    if( !unit_exists( id ) ||
        unit_from_id( id ).finished_turn() ) {
      q.pop_front();
      continue;
    }

    bool should_ask = advance_unit( id );
    if( !should_ask ) {
      q.pop_front();
      continue;
    }

    // We have a unit that needs to ask the user for orders. This
    // will open things up to player input not only to the unit
    // that needs orders, but to all units on the map, and will
    // update the queue with any changes that result. This func-
    // tion will return on any player input (e.g. clearing the
    // orders of another unit) and so we might need to circle
    // back to this line a few times in this while loop until we
    // get the order for the unit in question (unless the player
    // activates another unit).
    co_await next_player_input( id, &q );
    // !! The unit may no longer exist at this point, e.g. if
    // they were disbanded or if they lost a battle to the na-
    // tives.
  }
}

waitable<> colonies_turn() {
  CHECK( SG().turn.nation );
  auto& st = *SG().turn.nation;
  lg.info( "processing colonies for the {}.", st.nation );
  flat_queue<ColonyId> colonies = colonies_all( st.nation );
  while( !colonies.empty() ) {
    ColonyId colony_id = *colonies.front();
    colonies.pop();
    co_await evolve_colony_one_turn( colony_id );
  }
}

waitable<> nation_turn() {
  CHECK( SG().turn.nation );
  auto& st = *SG().turn.nation;

  // Starting.
  if( !st.started ) {
    print_bar( '-', fmt::format( "[ {} ]", st.nation ) );
    st.started = true;
  }

  // Colonies.
  if( !st.did_colonies ) {
    co_await colonies_turn();
    st.did_colonies = true;
  }

  if( !st.did_units ) {
    co_await units_turn();
    st.did_units = true;
  }
  CHECK( st.units.empty() );
}

waitable<> next_turn_impl() {
  landview_start_new_turn();
  auto& st = SG().turn;

  // Starting.
  if( !st.started ) {
    print_bar( '=', "[ Starting Turn ]" );
    map_units( []( Unit& unit ) { unit.new_turn(); } );
    st.new_turn();
    st.started = true;
  }

  // Body.
  if( st.nation.has_value() ) {
    co_await nation_turn();
    st.nation.reset();
  }

  while( !st.remainder.empty() ) {
    st.nation = NationState( *st.remainder.front() );
    st.remainder.pop();
    co_await nation_turn();
    st.nation.reset();
  }

  // Ending.
  if( st.need_eot ) co_await end_of_turn();

  st.new_turn();
}

} // namespace

/****************************************************************
** Turn State Advancement
*****************************************************************/
waitable<> next_turn() { return next_turn_impl(); }

} // namespace rn
