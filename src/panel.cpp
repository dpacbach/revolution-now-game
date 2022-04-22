/****************************************************************
**panel.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-11-01.
*
* Description: The side panel on land view.
*
*****************************************************************/
#include "panel.hpp"

// Revolution Now
#include "co-wait.hpp"
#include "compositor.hpp"
#include "error.hpp"
#include "logger.hpp"
#include "lua.hpp"
#include "menu.hpp"
#include "plane.hpp"
#include "screen.hpp"
#include "views.hpp"

// luapp
#include "luapp/state.hpp"

// base
#include "base/scope-exit.hpp"

using namespace std;

namespace rn {

namespace {

/****************************************************************
** Panel Rendering
*****************************************************************/
struct PanelPlane : public Plane {
  PanelPlane() = default;
  bool covers_screen() const override { return false; }

  static auto rect() {
    UNWRAP_CHECK( res, compositor::section(
                           compositor::e_section::panel ) );
    return res;
  }
  static W panel_width() { return rect().w; }
  static H panel_height() { return rect().h; }

  Delta delta() const {
    return { panel_width(), panel_height() };
  }
  Coord origin() const { return rect().upper_left(); };

  ui::ButtonView& next_turn_button() {
    auto p_view = view->at( 0 );
    return *p_view.view->cast<ui::ButtonView>();
  }

  void initialize( IMapUpdater& ) override {
    vector<ui::OwningPositionedView> view_vec;

    auto button_view =
        make_unique<ui::ButtonView>( "Next Turn", [this] {
          w_promise.set_value( {} );
          // Disable the button as soon as it is clicked.
          this->next_turn_button().enable( /*enabled=*/false );
        } );
    button_view->blink( /*enabled=*/true );

    auto button_size = button_view->delta();
    auto where =
        Coord{} + ( panel_width() / 2 ) - ( button_size.w / 2 );
    where += 16_h;

    ui::OwningPositionedView p_view( std::move( button_view ),
                                     where );
    view_vec.emplace_back( std::move( p_view ) );

    view = make_unique<ui::InvisibleView>(
        delta(), std::move( view_vec ) );

    next_turn_button().enable( false );
  }

  void draw( rr::Renderer& renderer ) const override {
    rr::Painter painter = renderer.painter();
    tile_sprite( painter, e_tile::wood_middle, rect() );
    view->draw( renderer, origin() );
  }

  e_input_handled input( input::event_t const& event ) override {
    // FIXME: we need a window manager in the panel to avoid du-
    // plicating logic between here and the window module.
    if( input::is_mouse_event( event ) ) {
      UNWRAP_CHECK( pos, input::mouse_position( event ) );
      if( pos.is_inside( rect() ) ) {
        auto new_event =
            move_mouse_origin_by( event, origin() - Coord{} );
        (void)view->input( new_event );
        return e_input_handled::yes;
      }
      return e_input_handled::no;
    } else
      return view->input( event ) ? e_input_handled::yes
                                  : e_input_handled::no;
  }

  wait<> user_hits_eot_button() {
    next_turn_button().enable( /*enabled=*/true );
    // Use a scoped setter here so that the button gets disabled
    // if this coroutine gets cancelled.
    SCOPE_EXIT( next_turn_button().enable( /*enabled=*/false ) );
    w_promise = {};
    co_await w_promise.wait();
  }

  unique_ptr<ui::InvisibleView> view;
  wait_promise<>                w_promise;
};

PanelPlane g_panel_plane;

/****************************************************************
** Menu Handlers
*****************************************************************/
MENU_ITEM_HANDLER(
    next_turn,
    [] {
      g_panel_plane.w_promise.set_value_emplace_if_not_set();
    },
    [] { return g_panel_plane.next_turn_button().enabled(); } )

} // namespace

/****************************************************************
** Public API
*****************************************************************/
Plane* panel_plane() { return &g_panel_plane; }

wait<> wait_for_eot_button_click() {
  return g_panel_plane.user_hits_eot_button();
}

/****************************************************************
** Lua bindings
*****************************************************************/
LUA_FN( end_turn, void ) {
  g_panel_plane.w_promise.set_value_emplace_if_not_set();
}

} // namespace rn
