/****************************************************************
**plane.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-12-30.
*
* Description: Rendering planes.
*
*****************************************************************/
#include "plane.hpp"

// Revolution Now
#include "app-state.hpp"
#include "colony-view.hpp"
#include "console.hpp"
#include "europort-view.hpp"
#include "frame.hpp"
#include "gfx.hpp"
#include "image.hpp"
#include "init.hpp"
#include "land-view.hpp"
#include "logging.hpp"
#include "main-menu.hpp"
#include "menu.hpp"
#include "panel.hpp"
#include "render.hpp"
#include "screen.hpp"
#include "tiles.hpp"
#include "window.hpp"

// base
#include "base/lambda.hpp"

// base-util
#include "base-util/algo.hpp"

// C++ standard library
#include <algorithm>
#include <array>

using namespace std;

namespace rn {

namespace {

constexpr auto kNumPlanes =
    static_cast<size_t>( magic_enum::enum_count<e_plane>() );

// The `values` array should be a constexpr.
vector<e_plane> g_plane_list{
    magic_enum::enum_values<e_plane>().begin(),
    magic_enum::enum_values<e_plane>().end() };
// When a new list of planes is set, it will be set here instead
// of modifying the primary one. This is to guarantee that no one
// can mutate the primary list at the wrong time (i.e., while it
// is being iterated over). Only at the start of each frame will
// the contents of this vector (if present) be moved into the
// primary one above.
maybe<vector<e_plane>> g_plane_list_next;

/****************************************************************
** Plane Textures
*****************************************************************/
// Planes are rendered from 0 --> count.
array<Plane*, kNumPlanes>  planes;
array<Texture, kNumPlanes> textures;

Plane*& plane( e_plane plane ) {
  auto idx = magic_enum::enum_integer( plane );
  CHECK( idx < int( planes.size() ) );
  return planes[idx];
}

Texture& plane_tx( e_plane plane ) {
  auto idx = magic_enum::enum_integer( plane );
  CHECK( idx < int( planes.size() ) );
  return textures[idx];
}

/****************************************************************
** The InactivePlane Plane
*****************************************************************/
struct InactivePlane : public Plane {
  InactivePlane() {}
  bool covers_screen() const override { return false; }
  void draw( Texture& /*unused*/ ) const override {}
};

InactivePlane dummy;

/****************************************************************
** The Omni Plane
*****************************************************************/
// This plane is intended to be:
//
//   1) Always present
//   2) Always invisible
//   3) Always on top
//   4) Catching any global events (such as special key presses)
//
struct OmniPlane : public Plane {
  OmniPlane() = default;
  bool covers_screen() const override { return false; }
  void draw( Texture& tx ) const override {
    tx.fill();
    render_sprite( tx, e_tile::mouse_arrow1,
                   input::current_mouse_position() - 16_w );
  }
  e_input_handled input( input::event_t const& event ) override {
    auto handled = e_input_handled::no;
    switch( event.to_enum() ) {
      case input::e_input_event::quit_event:
        throw exception_exit{};
      case input::e_input_event::win_event: //
        break;
      case input::e_input_event::key_event: {
        auto& key_event = event.get<input::key_event_t>();
        if( key_event.change != input::e_key_change::down )
          break;
        handled = e_input_handled::yes;
        switch( key_event.keycode ) {
          case ::SDLK_F12:
            if( !screenshot() )
              lg.warn( "failed to take screenshot." );
            break;
          case ::SDLK_F11:
            if( is_window_fullscreen() ) {
              toggle_fullscreen();
              restore_window();
            } else {
              toggle_fullscreen();
            }
            break;
          case ::SDLK_q:
            if( key_event.mod.ctrl_down ) throw exception_exit{};
            handled = e_input_handled::no;
            break;
          default: //
            handled = e_input_handled::no;
            break;
        }
        break;
      }
      default: //
        break;
    }
    return handled;
  }
};

OmniPlane g_omni_plane;

Plane* omni_plane() { return &g_omni_plane; }

/****************************************************************
** Dragging Metadata
*****************************************************************/
struct DragState {
  explicit DragState() { reset(); }

  // This is the plan that is currently receiving mouse dragging
  // events (nothing if there is not dragging event happening).
  maybe<e_plane> plane{};
  bool           send_as_motion{ false };
  maybe<Delta>   projection{};

  void reset() {
    plane          = nothing;
    send_as_motion = false;
    projection     = nothing;
  }
};
NOTHROW_MOVE( DragState );

DragState g_drag_state;

/****************************************************************
** Plane Ranges
*****************************************************************/
vector<pair<e_plane, Plane*>> relevant_planes() {
  static auto covers_screen = L( _.second->covers_screen() );
  vector<pair<e_plane, Plane*>> res;
  res.reserve( kNumPlanes );
  for( e_plane p : g_plane_list )
    res.push_back( { p, plane( p ) } );
  reverse( res.begin(), res.end() );
  auto it = find_if( res.begin(), res.end(), covers_screen );
  DCHECK( it != res.end(),
          "plane list {} does not contain a plane that covers "
          "the screen.",
          FmtJsonStyleList{ g_plane_list } );
  ++it;
  res.erase( it, res.end() );
  return res;
}

vector<pair<e_plane, Plane*>> planes_to_draw() {
  auto v = relevant_planes();
  reverse( v.begin(), v.end() );
  return v;
}

/****************************************************************
** Initialization
*****************************************************************/
void init_planes() {
  // By default, all planes are dummies, unless we provide an
  // object below.
  planes.fill( &dummy );

  plane( e_plane::main_menu ) = main_menu_plane();
  plane( e_plane::land_view ) = land_view_plane();
  plane( e_plane::panel )     = panel_plane();
  plane( e_plane::image )     = image_plane();
  plane( e_plane::colony )    = colony_plane();
  plane( e_plane::europe )    = europe_plane();
  plane( e_plane::window )    = window_plane();
  plane( e_plane::menu )      = menu_plane();
  plane( e_plane::console )   = console_plane();
  plane( e_plane::omni )      = omni_plane();

  // No plane must be null, they must all point to a valid Plane
  // object even if it is the dummy above.
  for( auto p : planes ) { CHECK( p ); }

  // Call any custom initialization routines.
  for( auto p : planes ) { p->initialize(); }

  // Initialize the textures. These are intended to be large
  // enough to cover the entire screen at a scale factor of unity
  // if necessary (i.e., when in fullscreen mode with smallest
  // scale) but will also work when the main window is restored
  // (only part of them will be used). Their coordinates are mea-
  // sured in logical coordinates (which means, typically, that
  // they will be smaller than the full screen resolution).
  for( auto& tx : textures ) {
    tx = create_screen_physical_sized_texture();
    clear_texture_transparent( tx );
  }
}

void cleanup_planes() {
  // This actually just destroys the textures, since the planes
  // will be held by value as global variables elsewhere.
  for( auto& tx : textures ) tx.free();
}

REGISTER_INIT_ROUTINE( planes );

input::mouse_drag_event_t project_drag_event(
    input::mouse_drag_event_t const& drag_event,
    Delta const&                     along ) {
  // Takes the vector defined by (end-start) and projects it
  // along the `along` vector, returning a new `end`.
  auto project = []( Coord const& start, Coord const& end,
                     Delta const& along ) {
    return start + ( end - start ).projected_along( along );
  };

  auto res = drag_event;
  res.prev = project( /*start=*/drag_event.state.origin,
                      /*end=*/drag_event.prev,
                      /*along=*/along );
  res.pos  = project( /*start=*/drag_event.state.origin,
                     /*end=*/drag_event.pos,
                     /*along=*/along );
  return res;
}

bool input_catchall( input::event_t const& event ) {
  bool handled = false;
  switch( event.to_enum() ) {
    case input::e_input_event::quit_event:
      throw exception_exit{};
    case input::e_input_event::key_event: {
      auto& key_event = event.get<input::key_event_t>();
      if( key_event.change != input::e_key_change::down ) break;
      switch( key_event.keycode ) {
        case ::SDLK_ESCAPE: //
          handled = true;
          back_to_main_menu();
          break;
        default: //
          break;
      }
      break;
    }
    default: //
      break;
  }
  return handled;
}

} // namespace

/****************************************************************
** Plane Default Implementations
*****************************************************************/
void Plane::initialize() {}

Plane& Plane::get( e_plane p ) { return *plane( p ); }

Plane::e_input_handled Plane::input(
    input::event_t const& /*unused*/ ) {
  return e_input_handled::no;
}

void Plane::advance_state() {}

Plane::DragInfo Plane::can_drag(
    input::e_mouse_button /*unused*/, Coord /*unused*/ ) {
  return e_accept_drag::no;
}

void Plane::on_drag( input::mod_keys const& /*unused*/,
                     input::e_mouse_button /*unused*/,
                     Coord /*unused*/, Coord /*unused*/,
                     Coord /*unused*/ ) {}

void Plane::on_drag_finished( input::mod_keys const& /*unused*/,
                              input::e_mouse_button /*unused*/,
                              Coord /*unused*/,
                              Coord /*unused*/ ) {}

maybe<Plane::MenuClickHandler> Plane::menu_click_handler(
    e_menu_item /*unused*/ ) const {
  return nothing;
}

/****************************************************************
** External API
*****************************************************************/
void set_plane_list( vector<e_plane> const& arr ) {
  vector<e_plane>        res;
  unordered_set<e_plane> set;
  res.reserve( magic_enum::enum_count<e_plane>() );
  for( auto plane : arr ) {
    CHECK( plane != e_plane::omni );
    CHECK( !set.contains( plane ),
           "plane {} appears twice in list.", plane );
    set.insert( plane );
    res.push_back( plane );
  }
  res.push_back( e_plane::omni );
  // Only assign to the global variable as a last step, that way
  // if we encounter an error we still have a usable plane list.
  lg.debug( "setting next plane list: {}",
            FmtJsonStyleList{ res } );
  g_plane_list_next.emplace( std::move( res ) );
}

bool is_plane_enabled( e_plane plane ) {
  return std::find( g_plane_list.begin(), g_plane_list.end(),
                    plane ) != g_plane_list.end();
}

void draw_all_planes( Texture& tx ) {
  clear_texture_black( tx );

  // This will find the last plane that will render (opaquely)
  // over every pixel. If one is found then we will not render
  // any planes before it. This is technically not necessary, but
  // saves rendering work by avoiding to render things that would
  // go unseen anyway.
  for( auto [e, ptr] : planes_to_draw() ) {
    plane_tx( e ).set_render_target();
    ptr->draw( plane_tx( e ) );
    copy_texture( plane_tx( e ), tx );
  }
}

void advance_plane_state() {
  if( g_plane_list_next.has_value() ) {
    g_plane_list      = std::move( *g_plane_list_next );
    g_plane_list_next = nothing;
  }
  for( auto [e, ptr] : relevant_planes() ) ptr->advance_state();
}

Plane::e_input_handled send_input_to_planes(
    input::event_t const& event ) {
  using namespace input;
  if( auto* drag_event =
          std::get_if<input::mouse_drag_event_t>( &event ) ) {
    if( g_drag_state.plane ) {
      auto& plane = Plane::get( *g_drag_state.plane );
      // Drag should already be in progress.
      CHECK( drag_event->state.phase != e_drag_phase::begin );
      if( g_drag_state.send_as_motion ) {
        // The plane wants to be sent this drag event as regular
        // mouse motion / click events, so we need to convert the
        // drag events to those events and send them.
        auto motion = input::drag_event_to_mouse_motion_event(
            *drag_event );
        // There is already a drag in progress, so send this one
        // to the same plane that accepted the initial drag
        // event. Motion first, then button, since if the button
        // event exists then it's a mouse-up, should should be
        // sent after the motion. The mouse motion event might
        // not exist if this is a drag end event consisting just
        // of a released button.
        (void)plane.input( motion );
        // If the drag is finished then send out that event.
        if( drag_event->state.phase == e_drag_phase::end ) {
          // lg.debug( "finished `{}` drag motion event",
          //          *g_drag_state.plane );
          auto maybe_button =
              input::drag_event_to_mouse_button_event(
                  *drag_event );
          CHECK( maybe_button.has_value() );
          (void)plane.input( *maybe_button );
          g_drag_state.reset();
        }
      } else {
        // The plane wishes to be sent these drag events as drag
        // events. There is already a drag in progress, so send
        // this one to the same plane that accepted the initial
        // drag event.
        //
        // First project the coordinates if it was requested.
        input::mouse_drag_event_t prj_drag_event =
            ( g_drag_state.projection.has_value() )
                ? project_drag_event( *drag_event,
                                      *g_drag_state.projection )
                : *drag_event;
        plane.on_drag( prj_drag_event.mod, prj_drag_event.button,
                       prj_drag_event.state.origin,
                       prj_drag_event.prev, //
                       prj_drag_event.pos );
        // Sanity check that we copied properly.
        DCHECK( prj_drag_event.state.phase ==
                drag_event->state.phase );
        // If the drag is finished then send out that event.
        if( prj_drag_event.state.phase == e_drag_phase::end ) {
          // lg.debug( "finished `{}` drag event",
          //          *g_drag_state.plane );
          plane.on_drag_finished(
              prj_drag_event.mod, prj_drag_event.button,
              prj_drag_event.state.origin, prj_drag_event.pos );
          g_drag_state.reset();
        }
      }
      // Here it is assumed/required that the plane handle it
      // because the plane has already accepted this drag.
      return Plane::e_input_handled::yes;
    }
    // No drag plane registered to accept the event, so lets
    // send out the event but only if it's a `begin` event.
    if( drag_event->state.phase == e_drag_phase::begin ) {
      for( auto [e, plane] : relevant_planes() ) {
        // Note here we use the origin position of the mouse drag
        // as opposed to the current mouse position because that
        // is what is relevant for determining whether the plane
        // can handle the drag event or not (at this point, even
        // though we are in a `begin` event, the current mouse
        // position may already have moved a bit from the orig-
        // in).
        auto drag_desc = plane->can_drag(
            drag_event->button, drag_event->state.origin );
        switch( drag_desc.accept ) {
          // If the plane doesn't want to handle it then move
          // on to ask the next one.
          case Plane::e_accept_drag::no: continue;
          case Plane::e_accept_drag::motion: {
            // In this case the plane says that it wants to re-
            // ceive the events, but just as normal mouse
            // move/click events.
            g_drag_state.reset();
            g_drag_state.plane          = e;
            g_drag_state.send_as_motion = true;
            // lg.debug( "plane `{}` can drag as motion.", e );
            auto motion =
                input::drag_event_to_mouse_motion_event(
                    *drag_event );
            auto maybe_button =
                input::drag_event_to_mouse_button_event(
                    *drag_event );
            // Drag events in the e_drag_phase::begin phase
            // should not contain any button events because the
            // initial mouse-down will always be sent as a normal
            // click event.
            CHECK( !maybe_button.has_value() );
            (void)plane->input( motion );
            // All events within a drag are assumed handled.
            return Plane::e_input_handled::yes;
          }
          case Plane::e_accept_drag::swallow:
            // In this case the plane says that it doesn't want
            // to handle it AND it doesn't want anyone else to
            // handle it.
            // lg.debug( "plane `{}` swallowed drag.", e );
            return Plane::e_input_handled::yes;
          case Plane::e_accept_drag::yes:
            // Wants to handle it.
            g_drag_state.reset();
            g_drag_state.plane          = e;
            g_drag_state.send_as_motion = false;
            g_drag_state.projection     = drag_desc.projection;
            // lg.debug( "plane `{}` can drag", e );
            // Now we must send it an on_drag because this mouse
            // event that we're dealing with serves both to tell
            // us about a new drag even but also may have a mouse
            // delta in it that needs to be processed.
            //
            // First project the coordinates if it was requested.
            input::mouse_drag_event_t prj_drag_event =
                ( g_drag_state.projection.has_value() )
                    ? project_drag_event(
                          *drag_event, *g_drag_state.projection )
                    : *drag_event;
            // Sanity check that we copied properly.
            DCHECK( prj_drag_event.state.phase ==
                    drag_event->state.phase );
            plane->on_drag( prj_drag_event.mod,
                            prj_drag_event.button,
                            prj_drag_event.state.origin,
                            prj_drag_event.prev, //
                            prj_drag_event.pos );
            return Plane::e_input_handled::yes;
        }
      }
    }
    // If no one handled it then that's it.
    return Plane::e_input_handled::no;
  }

  // Just a normal event, so send it out using the usual proto-
  // col.
  for( auto p : relevant_planes() ) {
    switch( p.second->input( event ) ) {
      case Plane::e_input_handled::yes:
        return Plane::e_input_handled::yes;
      case Plane::e_input_handled::no: //
        break;
    }
  }

  if( input_catchall( event ) )
    return Plane::e_input_handled::yes;

  return Plane::e_input_handled::no;
}

/****************************************************************
** Plane Menu Handling
*****************************************************************/
namespace {

bool is_menu_item_enabled_( e_menu_item item ) {
  for( auto p : relevant_planes() ) {
    if( p.second->menu_click_handler( item ).has_value() )
      return true;
  }
  return false;
}

auto is_menu_item_enabled =
    per_frame_memoize( is_menu_item_enabled_ );

void on_menu_item_clicked( e_menu_item item ) {
  for( auto p : relevant_planes() ) {
    if( auto maybe_handler =
            p.second->menu_click_handler( item );
        maybe_handler.has_value() ) {
      maybe_handler.value()();
      return;
    }
  }
  SHOULD_NOT_BE_HERE;
}

#define MENU_ITEM_HANDLER_PLANE( item )                        \
  MENU_ITEM_HANDLER(                                           \
      item, [] { on_menu_item_clicked( e_menu_item::item ); }, \
      [] {                                                     \
        return is_menu_item_enabled( e_menu_item::item );      \
      } )

MENU_ITEM_HANDLER_PLANE( about );
MENU_ITEM_HANDLER_PLANE( economics_advisor );
MENU_ITEM_HANDLER_PLANE( european_advisor );
MENU_ITEM_HANDLER_PLANE( fortify );
MENU_ITEM_HANDLER_PLANE( founding_father_help );
MENU_ITEM_HANDLER_PLANE( military_advisor );
MENU_ITEM_HANDLER_PLANE( restore_zoom );
MENU_ITEM_HANDLER_PLANE( retire );
MENU_ITEM_HANDLER_PLANE( revolution );
MENU_ITEM_HANDLER_PLANE( sentry );
MENU_ITEM_HANDLER_PLANE( terrain_help );
MENU_ITEM_HANDLER_PLANE( units_help );
MENU_ITEM_HANDLER_PLANE( zoom_in );
MENU_ITEM_HANDLER_PLANE( zoom_out );

} // namespace

} // namespace rn
