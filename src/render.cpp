/****************************************************************
**render.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-31.
*
* Description: Performs all rendering for game.
*
*****************************************************************/
#include "render.hpp"

// Revolution Now
#include "compositor.hpp"
#include "cstate.hpp"
#include "error.hpp"
#include "gfx.hpp"
#include "logger.hpp"
#include "plane.hpp"
#include "screen.hpp"
#include "text.hpp"
#include "ustate.hpp"
#include "views.hpp"
#include "window.hpp"

// base
#include "base/keyval.hpp"

// C++ standard library
#include <vector>

using namespace std;

namespace rn {

namespace {

constexpr Delta nationality_icon_size( 13_h, 13_w );

// Unit only, no nationality icon.
void render_unit_no_icon( Texture& tx, e_unit_type unit_type,
                          Coord pixel_coord ) {
  auto const& desc = unit_attr( unit_type );
  render_sprite( tx, desc.tile, pixel_coord, 0, 0 );
}

void render_colony_flag( Texture& tx, Coord coord,
                         gfx::pixel color ) {
  auto cloth_rect = Rect::from( coord, Delta{ 8_w, 6_h } );
  render_fill_rect( tx, color, cloth_rect );
  render_line( tx, gfx::pixel::wood().shaded( 4 ),
               cloth_rect.upper_right(), Delta{ 0_w, 12_h } );
}

} // namespace

/****************************************************************
** Rendering Building Blocks
*****************************************************************/
Texture render_nationality_icon_impl( e_nation nation, char c ) {
  Delta       delta = nationality_icon_size;
  Rect        rect  = Rect::from( Coord{}, delta );
  Coord       origin{};
  auto const& nation_o = nation_obj( nation );

  auto tx = create_texture( delta );

  auto color  = nation_o.flag_color;
  auto dark1  = color.shaded( 2 );
  auto dark2  = dark1.shaded( 2 );
  auto dark3  = dark2.shaded( 2 );
  auto light1 = color.highlighted( 1 );
  auto light2 = light1.highlighted( 1 );
  auto light3 = light2.highlighted( 1 );

  auto text_color = color.shaded( 7 );

  render_fill_rect( tx, color, rect );

  render_line( tx, light1, origin + 1_w,
               { 0_h, delta.w - 1_w } );
  render_line( tx, light1, origin + ( delta.w - 1_w ),
               { delta.h - 1_h, 0_w } );
  render_line( tx, light2, origin + 4_w,
               { 0_h, delta.w - 4_w } );
  render_line( tx, light2, origin + ( delta.w - 1_w ),
               { delta.h - 4_h, 0_w } );
  render_line( tx, light3, origin + 7_w,
               { 0_h, delta.w - 7_w } );
  render_line( tx, light3, origin + ( delta.w - 1_w ),
               { delta.h - 7_h, 0_w } );

  render_line( tx, dark1, origin + 1_h, { delta.h - 1_h, 0_w } );
  render_line( tx, dark1, origin + ( delta.h - 1_h ),
               { 0_h, delta.w - 1_w } );
  render_line( tx, dark2, origin + 4_h, { delta.h - 4_h, 0_w } );
  render_line( tx, dark2, origin + ( delta.h - 1_h ),
               { 0_h, delta.w - 4_w } );
  render_line( tx, dark3, origin + 7_h, { delta.h - 7_h, 0_w } );
  render_line( tx, dark3, origin + ( delta.h - 1_h ),
               { 0_h, delta.w - 7_w } );

  auto const& char_tx = render_text(
      font::nat_icon(), text_color, string( 1, c ) );

  auto char_tx_size = char_tx.size();
  copy_texture(
      char_tx, tx,
      centered( char_tx_size, rect ) + Delta{ 1_w, 0_h } );

  return tx;
}

struct NatIconRenderDesc {
  e_nation nation;
  char     c;

  auto to_tuple() const { return tuple{ nation, c }; }

  friend bool operator==( NatIconRenderDesc const& lhs,
                          NatIconRenderDesc const& rhs ) {
    return lhs.to_tuple() == rhs.to_tuple();
  }
};
NOTHROW_MOVE( NatIconRenderDesc );

} // namespace rn

namespace std {

template<>
struct hash<::rn::NatIconRenderDesc> {
  auto operator()(
      ::rn::NatIconRenderDesc const& o ) const noexcept {
    uint32_t i = 0;
    i += o.c;
    i <<= 8;
    i += static_cast<uint32_t>( o.nation );
    return hash<uint32_t>{}( i );
  }
};

} // namespace std

namespace rn {

unordered_map<NatIconRenderDesc, Texture> nat_icon_cache;

Texture const& render_nationality_icon( e_nation nation,
                                        char     c ) {
  auto do_render = [&] {
    return render_nationality_icon_impl( nation, c );
  };

  NatIconRenderDesc desc{ nation, c };

  if( auto maybe_cached = base::lookup( nat_icon_cache, desc );
      maybe_cached.has_value() )
    return *maybe_cached;

  nat_icon_cache.emplace( desc, do_render() );
  return nat_icon_cache[desc];
}

void render_nationality_icon( Texture&                  dest,
                              UnitTypeAttributes const& desc,
                              e_nation                  nation,
                              e_unit_orders             orders,
                              Coord pixel_coord ) {
  // Now we will advance the pixel_coord to put the icon at the
  // location specified in the unit descriptor.
  auto position = desc.nat_icon_position;
  switch( position ) {
    case e_direction::nw: break;
    case e_direction::ne:
      pixel_coord +=
          ( ( 1_w * g_tile_width ) - nationality_icon_size.w );
      break;
    case e_direction::se:
      pixel_coord += ( ( Delta{ 1_w, 1_h } * g_tile_scale ) -
                       nationality_icon_size );
      break;
    case e_direction::sw:
      pixel_coord +=
          ( ( 1_h * g_tile_height ) - nationality_icon_size.h );
      break;
      // By default we keep it in the northwest corner.
    default: break;
  };

  char c{ '-' }; // gcc seems to want us to initialize this
  switch( orders ) {
    case e_unit_orders::none: c = '-'; break;
    case e_unit_orders::sentry: c = 'S'; break;
    case e_unit_orders::fortified: c = 'F'; break;
  };
  auto const& nat_icon = render_nationality_icon( nation, c );
  copy_texture( nat_icon, dest, pixel_coord );
}

void render_nationality_icon( Texture& dest, e_unit_type type,
                              e_nation      nation,
                              e_unit_orders orders,
                              Coord         pixel_coord ) {
  render_nationality_icon( dest, unit_attr( type ), nation,
                           orders, pixel_coord );
}

void render_nationality_icon( Texture& dest, UnitId id,
                              Coord pixel_coord ) {
  auto const& unit = unit_from_id( id );
  render_nationality_icon( dest, unit.desc(), unit.nation(),
                           unit.orders(), pixel_coord );
}

void render_unit( Texture& tx, UnitId id, Coord pixel_coord,
                  bool with_icon ) {
  auto const& unit = unit_from_id( id );
  if( with_icon ) {
    // Should the icon be in front of the unit or in back.
    if( !unit.desc().nat_icon_front ) {
      render_nationality_icon( tx, id, pixel_coord );
      render_unit_no_icon( tx, unit.desc().type, pixel_coord );
    } else {
      render_unit_no_icon( tx, unit.desc().type, pixel_coord );
      render_nationality_icon( tx, id, pixel_coord );
    }
  } else {
    render_unit_no_icon( tx, unit.desc().type, pixel_coord );
  }
}

void render_unit( Texture& tx, e_unit_type unit_type,
                  Coord pixel_coord ) {
  render_unit_no_icon( tx, unit_type, pixel_coord );
}

void render_colony( Texture& tx, ColonyId id,
                    Coord pixel_coord ) {
  auto const& colony = colony_from_id( id );
  auto        tile   = colony_from_id( id ).buildings().contains(
                           e_colony_building::stockade )
                           ? e_tile::colony_stockade
                           : e_tile::colony_basic;
  render_sprite( tx, tile, pixel_coord, 0, 0 );
  auto const& nation = nation_obj( colony.nation() );
  render_colony_flag( tx, pixel_coord + Delta{ 8_w, 8_h },
                      nation.flag_color );
}

} // namespace rn
