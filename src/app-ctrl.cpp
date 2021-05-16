/****************************************************************
**app-ctrl.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-10-25.
*
* Description: Handles the top-level game state machines.
*
*****************************************************************/
#include "app-ctrl.hpp"

// Revolution Now
#include "co-combinator.hpp"
#include "conductor.hpp"
#include "game.hpp"
#include "logging.hpp"
#include "lua.hpp"
#include "main-menu.hpp"
#include "plane-ctrl.hpp"
#include "turn.hpp"
#include "window.hpp"

// base
#include "base/scope-exit.hpp"

using namespace std;

namespace rn {

namespace {

struct quit_app_interrupt : exception {};

/****************************************************************
** Main Menu.
*****************************************************************/
waitable<> main_menu_item_selected( e_main_menu_item item ) {
  switch( item ) {
    case e_main_menu_item::new_: //
      co_await run_new_game();
      break;
    case e_main_menu_item::load:
      co_await run_existing_game();
      break;
    case e_main_menu_item::quit: //
      throw quit_app_interrupt{};
    case e_main_menu_item::settings_graphics:
      co_await ui::message_box( "No graphics settings yet." );
      break;
    case e_main_menu_item::settings_sound:
      co_await ui::message_box( "No sound settings yet." );
      break;
  }
}

waitable<> main_menu() {
  conductor::play_request(
      conductor::e_request::fife_drum_happy,
      conductor::e_request_probability::always );
  co::stream<e_main_menu_item>& selections =
      main_menu_input_stream();
  while( true ) {
    clear_plane_stack();
    push_plane_config( e_plane_config::main_menu );
    e_main_menu_item item = co_await selections.next();
    try {
      co_await main_menu_item_selected( item );
    } catch( game_load_interrupt const& ) {
      selections.send( e_main_menu_item::load );
    }
  }
}

} // namespace

/****************************************************************
** Top-Level Application Flow.
*****************************************************************/
waitable<> revolution_now() {
  return co::erase( co::try_<quit_app_interrupt>( main_menu ) );
}

} // namespace rn