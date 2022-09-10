/****************************************************************
**harbor-view-dock.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2022-09-10.
*
* Description: Units on dock UI element within the harbor view.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "harbor-view-entities.hpp"
#include "wait.hpp"

namespace rn {

struct HarborBackdrop;
struct SS;
struct TS;
struct Player;

/****************************************************************
** HarborDockUnits
*****************************************************************/
struct HarborDockUnits : public ui::View, public HarborSubView {
  static PositionedHarborSubView<HarborDockUnits> create(
      SS& ss, TS& ts, Player& player, Rect canvas,
      HarborBackdrop const& backdrop );

  HarborDockUnits( SS& ss, TS& ts, Player& player,
                   Delta size_blocks );

  // Implement ui::Object.
  Delta delta() const override;

  // Implement IDraggableObjectsView.
  maybe<int> entity() const override;

  ui::View&       view() noexcept override;
  ui::View const& view() const noexcept override;

  maybe<DraggableObjectWithBounds> object_here(
      Coord const& where ) const override;

  // Implement ui::Object.
  void draw( rr::Renderer& renderer,
             Coord         coord ) const override;

  // Implement ui::AwaitView.
  virtual wait<> perform_click(
      input::mouse_button_event_t const& ) override;

 private:
  struct UnitWithPosition {
    UnitId id;
    Coord  pixel_coord;
  };

  std::vector<UnitWithPosition> units( Coord origin ) const;

  // The coord is relative to the upper left of this view.
  maybe<UnitWithPosition> unit_at_location( Coord where ) const;

  wait<> click_on_unit( UnitId unit_id );

  Delta size_blocks() const;
  Delta size_pixels() const;

  struct Draggable {
    UnitId unit_id = {};
  };

  maybe<Draggable> dragging_;
  Delta            size_blocks_ = {};
};

} // namespace rn