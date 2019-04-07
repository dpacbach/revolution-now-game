/****************************************************************
**views.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-03-16.
*
* Description: Views for populating windows in the UI.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "color.hpp"
#include "sdl-util.hpp"
#include "ui.hpp"

namespace rn::ui {

// NOTE: this header should only be directly included by
// window.cpp.

/****************************************************************
** Fundamental Views
*****************************************************************/
class View : public Object {};

// This is a View coupled with a coordinate representing the po-
// sition of the upper-left corner of the view. Note that the co-
// ordinate is in the coordinate system of the parent view or
// window (whose position in turn will not be know by this
// struct).
struct PositionedView {
  ObserverPtr<View> const view;
  Coord const             coord;

  Rect rect() const { return view->rect( coord ); }
};
struct PositionedViewConst {
  ObserverCPtr<View> const view;
  Coord const              coord;

  Rect rect() const { return view->rect( coord ); }
};

// Same as above, but owns the view.  The
class OwningPositionedView {
public:
  OwningPositionedView( std::unique_ptr<View> view,
                        Coord const&          coord )
    : view_( std::move( view ) ), coord_( coord ) {}

  ObserverCPtr<View> view() const {
    return ObserverCPtr<View>( view_.get() );
  }
  ObserverPtr<View> view() {
    return ObserverPtr<View>( view_.get() );
  }

  UPtr<View>& mutable_view() { return view_; }

  Coord const& coord() const { return coord_; }
  Coord&       coord() { return coord_; }

private:
  std::unique_ptr<View> view_;
  Coord                 coord_;
};

class CompositeView : public View {
public:
  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta delta() const override;

  bool on_key( input::key_event_t const& event ) override;
  bool on_wheel(
      input::mouse_wheel_event_t const& event ) override;
  bool on_mouse_move(
      input::mouse_move_event_t const& event ) override;
  bool on_mouse_button(
      input::mouse_button_event_t const& event ) override;

  // Implement ui::Object
  void children_under_coord( Coord      where,
                             ObjectSet& objects ) override;

  virtual int count() const = 0;

  virtual UPtr<View>& mutable_at( int idx )   = 0;
  virtual Coord       pos_of( int idx ) const = 0;

  virtual PositionedViewConst at( int idx ) const;
  virtual PositionedView      at( int idx );

  // This has to be implemented if the view holds any state that
  // must be recomputed if the child views are changed in either
  // number of geometry. For example, this may be called if the
  // child views change size.
  virtual void notify_children_updated() = 0;

  struct iter {
    CompositeView* cview;
    int            idx;
    auto           operator*() { return cview->at( idx ); }
    void           operator++() { ++idx; }
    bool operator!=( iter const& rhs ) { return rhs.idx != idx; }
  };
  struct citer {
    CompositeView const* cview;
    int                  idx;
    auto                 operator*() { return cview->at( idx ); }
    void                 operator++() { ++idx; }
    bool                 operator!=( citer const& rhs ) {
      return rhs.idx != idx;
    }
  };

  iter  begin() { return iter{this, 0}; }
  iter  end() { return iter{this, count()}; }
  citer begin() const { return citer{this, 0}; }
  citer end() const { return citer{this, count()}; }

private:
  bool dispatch_mouse_event( input::event_t const& event );
};

class VectorView : public CompositeView {
public:
  VectorView() {}

  VectorView( std::vector<OwningPositionedView> views )
    : views_( std::move( views ) ) {}

  // Implement CompositeView
  Coord pos_of( int idx ) const override;
  // Implement CompositeView
  UPtr<View>& mutable_at( int idx ) override;
  // Implement CompositeView
  int count() const override { return int( views_.size() ); }

  void push_back( OwningPositionedView view ) {
    views_.push_back( std::move( view ) );
  }

  OwningPositionedView const& operator[]( int idx ) const {
    CHECK( idx >= 0 && idx < int( views_.size() ) );
    return views_[idx];
  }
  OwningPositionedView& operator[]( int idx ) {
    CHECK( idx >= 0 && idx < int( views_.size() ) );
    return views_[idx];
  }

private:
  std::vector<OwningPositionedView> views_;
};

/****************************************************************
** Simple Views
*****************************************************************/
class SolidRectView : public View {
public:
  SolidRectView( Color color ) : color_( color ), delta_{} {}

  SolidRectView( Color color, Delta delta )
    : color_( color ), delta_( delta ) {}

  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta delta() const override { return delta_; }

  void set_delta( Delta const& delta ) { delta_ = delta; }

  // Implement ui::Object
  void children_under_coord( Coord, ObjectSet& ) override {}

protected:
  Color color_;
  Delta delta_;
};

class OneLineStringView : public View {
public:
  OneLineStringView( std::string msg, Color color, bool shadow );

  // Implement Object
  void draw( Texture const& tx, Coord coord ) const override;
  // Implement Object
  Delta delta() const override { return tx_.size(); }

  std::string const& msg() const { return msg_; }

  // Implement ui::Object
  void children_under_coord( Coord, ObjectSet& ) override {}

protected:
  std::string msg_;
  Texture     tx_;
};

class ButtonBaseView : public View {
public:
  ButtonBaseView( std::string label );
  ButtonBaseView( std::string label, Delta size_in_blocks );

  // Implement Object
  void draw( Texture const& tx,
             Coord          coord ) const override final;
  // Implement Object
  Delta delta() const override final { return pressed_.size(); }

  // Implement ui::Object
  void children_under_coord( Coord, ObjectSet& ) override {}

protected:
  enum class button_state { down, up, hover, disabled };

  void set_state( button_state state ) { state_ = state; }
  button_state state() const { return state_; }

private:
  void render( std::string const& label, Delta size_in_blocks );

  button_state state_{button_state::up};

  Texture pressed_{};
  Texture hover_{};
  Texture unpressed_{};
  Texture disabled_{};
};

/****************************************************************
** Derived Views
*****************************************************************/
// Should not be used directly; will generally be inserted
// automatically by the auto-pad mechanism.
class PaddingView : public CompositeView {
public:
  PaddingView( std::unique_ptr<View> view, bool l, bool r,
               bool u, bool d );

  // Implement Object
  Delta delta() const override;

  // Implement CompositeView
  Coord pos_of( int idx ) const override;
  // Implement CompositeView
  UPtr<View>& mutable_at( int idx ) override;
  // Implement CompositeView
  int count() const override { return 1; }

  // Implement CompositeView
  void notify_children_updated() override {}

private:
  bool l_{false}, r_{false}, u_{false}, d_{false};
  std::unique_ptr<View> view_;
};

class ButtonView : public ButtonBaseView {
public:
  using OnClickFunc = std::function<void( void )>;
  ButtonView( std::string label, OnClickFunc on_click );
  ButtonView( std::string label, Delta size_in_blocks,
              OnClickFunc on_click );

  bool on_mouse_move(
      input::mouse_move_event_t const& event ) override;
  bool on_mouse_button(
      input::mouse_button_event_t const& event ) override;
  void on_mouse_leave() override;

private:
  OnClickFunc on_click_;
};

class OkCancelView : public CompositeView {
public:
  OkCancelView( ButtonView::OnClickFunc on_ok,
                ButtonView::OnClickFunc on_cancel );

  // Implement CompositeView
  Coord pos_of( int idx ) const override;
  // Implement CompositeView
  UPtr<View>& mutable_at( int idx ) override;
  // Implement CompositeView
  int count() const override { return 2; }
  // Implement CompositeView
  void notify_children_updated() override {}

private:
  UPtr<View> ok_;
  UPtr<View> cancel_;
};

// VerticalArrayView: a view that wraps a list of views and dis-
// plays them vertically. On creation, one can specify how to
// justify the views, either left, right, or center. This ques-
// tion of justification arises because the views in the array
// will generally have different widths.
class VerticalArrayView : public VectorView {
public:
  enum class align { left, right, center };
  VerticalArrayView( std::vector<std::unique_ptr<View>> views,
                     align                              how );

  // Implement CompositeView
  void notify_children_updated() override;

  void recompute_child_positions();

private:
  align alignment_;
};

enum class e_( ok_cancel, ok, cancel );

class OkCancelAdapterView : public VerticalArrayView {
public:
  using OnClickFunc = std::function<void( e_ok_cancel )>;
  OkCancelAdapterView( UPtr<View> view, OnClickFunc on_click );
};

enum class e_option_active { inactive, active };

class OptionSelectItemView : public CompositeView {
public:
  OptionSelectItemView( std::string msg );

  // Implement CompositeView
  Coord pos_of( int idx ) const override;
  // Implement CompositeView
  UPtr<View>& mutable_at( int idx ) override;
  // Implement CompositeView
  int count() const override { return 2; }
  // Implement CompositeView
  void notify_children_updated() override {}

  void set_active( e_option_active active ) { active_ = active; }

  std::string const& line() const {
    return foreground_active_->cast<OneLineStringView>()->msg();
  }

  void grow_to( W w );

private:
  e_option_active active_;
  UPtr<View>      background_active_;
  UPtr<View>      background_inactive_;
  UPtr<View>      foreground_active_;
  UPtr<View>      foreground_inactive_;
};

class OptionSelectView : public VectorView {
public:
  OptionSelectView( Vec<Str> const& options,
                    int             initial_selection );

  // Implement CompositeView
  void notify_children_updated() override {}

  bool on_key( input::key_event_t const& event ) override;

  std::string const& get_selected() const;
  bool               confirmed() const { return has_confirmed; }

  void grow_to( W w );

private:
  ObserverPtr<OptionSelectItemView>  get_view( int item );
  ObserverCPtr<OptionSelectItemView> get_view( int item ) const;
  void                               set_selected( int item );

  int  selected_;
  bool has_confirmed;
};

} // namespace rn::ui
