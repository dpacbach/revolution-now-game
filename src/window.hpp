/****************************************************************
**window.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-09-30.
*
* Description: Handles windowing system for user interaction.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "input.hpp"
#include "sdl-util.hpp"

// c++ standard library
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace rn::input {
struct event_t;
}

namespace rn::ui {

class Object {
public:
  Object()          = default;
  virtual ~Object() = default;

  Object( Object const& ) = delete;
  Object( Object&& )      = delete;

  Object& operator=( Object const& ) = delete;
  Object& operator=( Object&& ) = delete;

  virtual void draw( Texture const& tx, Coord coord ) const = 0;
  ND virtual Delta size() const                             = 0;
  // Given a position, returns a bounding rect.  We need to be
  // given a position here because Objects don't know their
  // position, only their size.
  ND virtual Rect rect( Coord position ) const {
    return Rect::from( position, size() );
  }
  ND virtual bool accept_input(
      input::event_t const& /*unused*/ ) {
    return false;
  }
};

/****************************************************************
** Views
*****************************************************************/
class View : public Object {};

struct PositionedView {
  Rect bounds() const {
    return Rect::from( coord, coord + view->size() );
  };
  std::unique_ptr<View> view;
  Coord                 coord;
};

class CompositeView : public View {
public:
  // Pass views by value.
  explicit CompositeView( std::vector<PositionedView> views )
    : views_( std::move( views ) ) {}

  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta size() const override;

protected:
  std::vector<PositionedView> views_;
};

class SolidRectView : public View {
public:
  SolidRectView( Color color, Delta size )
    : color_( color ), size_( std::move( size ) ) {}

  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta size() const override;

protected:
  Color color_;
  Delta size_;
};

class OneLineStringView : public View {
public:
  OneLineStringView( std::string msg, W size );

  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta size() const override;

protected:
  Texture tx;
};

/****************************************************************
** Window
*****************************************************************/
enum class e_window_state { running, closed };

class Window : public Object {
public:
private:
};

/****************************************************************
** WindowManager
*****************************************************************/
using RenderFunc = std::function<void( void )>;
using WinPtr     = std::unique_ptr<Window>;

enum class e_wm_input_result { unhandled, handled, quit };

class WindowManager {
public:
  void draw_layout( Texture const& tx ) const;

  void run( RenderFunc const& render_fn );

  ND e_wm_input_result accept_input( SDL_Event const& event );

  void add_window( std::string title, std::unique_ptr<View> view,
                   Coord position );

private:
  struct window {
    window( std::string title_, std::unique_ptr<View> view_,
            Coord position_ )
      : window_state( e_window_state::running ),
        title( std::move( title_ ) ),
        view( std::move( view_ ) ),
        title_bar(),
        position( position_ ) {
      title_bar = std::make_unique<OneLineStringView>(
          title, view->size().w );
    }

    void  draw( Texture const& tx ) const;
    Delta size() const;
    Rect  rect() const;
    Coord inside_border() const;

    e_window_state                     window_state;
    std::string                        title;
    std::unique_ptr<View>              view;
    std::unique_ptr<OneLineStringView> title_bar;
    Coord                              position;
  };

  // Gets the window with focus, throws if no windows.
  window& focused();

  std::vector<window> windows_;
};

void test_window();

void message_box( std::string_view  msg,
                  RenderFunc const& render_bg );

} // namespace rn::ui
