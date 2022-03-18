/****************************************************************
**plane.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-12-30.
*
* Description: Rendering planes.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "input.hpp"
#include "menu.hpp"

// render
#include "render/renderer.hpp"

// Rds
#include "plane.rds.hpp"

// base
#include "base/function-ref.hpp"

// C++ standard library
#include <array>

namespace rn {

struct Plane {
  NO_COPY_NO_MOVE( Plane );

  Plane()          = default;
  virtual ~Plane() = default;

  static Plane& get( e_plane plane );

  // Will be called on all planes (whether enabled or not) before
  // any other methods are called on it. Default does nothing.
  void virtual initialize();

  // Will rendering this plane cover all pixels?  If so, then
  // planes under it will not be rendered.
  bool virtual covers_screen() const = 0;

  void virtual draw( rr::Renderer& renderer ) const = 0;

  // Called once per frame.
  virtual void advance_state();

  // yes:     Will not be given to any other planes.
  // no:      Will try the next plane.
  enum class e_input_handled { yes, no };

  // Accept input; returns true/false depending on whether the
  // input was handled or not.  If it was handled (true) then
  // this input will not be given to any further planes.
  ND e_input_handled virtual input(
      input::event_t const& event );

  // This encodes the result of asking a plane if it can handle a
  // drag event:
  //
  // yes: The plane it will be given all the events for that
  // drag.
  //
  // no: The drag will be offered to another plane.
  //
  // swallow: This will cause the drag not to be sent to any
  // planes at all. This is useful if a plane doesn't want to
  // handle a drag but it would not be appropriate for the drag
  // to be given to other planes.
  //
  // motion: The drag event will be sent to the plane but as
  // monormal use motion events and/or mouse button events.
  enum class e_accept_drag {
    yes,         // send using dedicated plane drag API methods.
    no,          // don't send them; try the next plane down.
    yes_but_raw, // send drag events as "raw" input_t.
    motion,      // send drag events as normal motion events.
    swallow      // don't send them to me or to anyone else.
  };

  // This is to determine if a plane is willing to accept a drag
  // event (and also serves as a notification that a drag event
  // has started). If it returns `yes` then it will immediately
  // receive the initial on_drag() event and then continue to re-
  // ceive all the drag events until the current drag ends.
  ND e_accept_drag virtual can_drag(
      input::e_mouse_button button, Coord origin );

  // For drag events from [first, last). This will only be called
  // if the can_drag returned true at the start of the drag
  // action.
  void virtual on_drag( input::mod_keys const& mod,
                        input::e_mouse_button  button,
                        Coord origin, Coord prev,
                        Coord current );

  void virtual on_drag_finished( input::mod_keys const& mod,
                                 input::e_mouse_button  button,
                                 Coord origin, Coord end );

  // This handler function does not take the e_menu_item as a pa-
  // rameter to force the planes to supply a unique handler func-
  // tion for each item that it implements. Otherwise a plane
  // might be tempted to supply a "catch-all" handler; that would
  // be error-prone in that it may end up receiving a request to
  // handle an item that it does not actually handle, which would
  // then require a check failure, which we want to avoid.
  using MenuClickHandler = base::function_ref<void()>;

  // Asks the plane if it can handler a particular menu item. If
  // it returns nothing that means "no." Otherwise it means
  // "yes," and it must return reference to a handler function
  // which will be called when them item is clicked assuming that
  // the menu item is enabled and if there are no higher planes
  // that also handle it. Default implementation returns nothing.
  //
  // IMPORTANT: Being that this is returning a function_ref, it
  // is important that function returned outlive the function
  // call. So e.g. returning a non-static lambda with captures
  // would probably not be good.
  virtual maybe<MenuClickHandler> menu_click_handler(
      e_menu_item item ) const;
};

// This should NOT be called directly, only by plane-ctrl. In-
// stead call set_plane_config.
//
// Last in the list becomes the top of the stack, and any planes
// that are not in this list are disabled. The omni plane should
// not be in this list, as it will always be enabled as the
// front-most plane.
void set_plane_list( std::vector<e_plane> const& planes );

bool is_plane_enabled( e_plane plane );

void draw_all_planes( rr::Renderer& renderer );

// This will call the advance_state method on each plane to up-
// date any state that it has. It will only be called on frames
// that are enabled and visible.
void advance_plane_state();

void reinitialize_planes();

// Returns true if one of the planes handled the input, false
// otherwise. At most one plane will handle the input.
ND Plane::e_input_handled send_input_to_planes(
    input::event_t const& event );

} // namespace rn
