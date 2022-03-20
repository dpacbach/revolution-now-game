/****************************************************************
**tiles.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-25.
*
* Description: Handles loading and retrieving tiles
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "coord.hpp"
#include "tile-enum.hpp"
#include "util.hpp"

// render
#include "render/painter.hpp"

// C++ standard library
#include <string_view>

namespace rn {

/****************************************************************
** Global Constants
*****************************************************************/
namespace detail {
constexpr int tile_pixel_size{ 32 };
}

inline constexpr SX    g_tile_width{ detail::tile_pixel_size };
inline constexpr SY    g_tile_height{ detail::tile_pixel_size };
inline constexpr Scale g_tile_scale{ detail::tile_pixel_size };

inline constexpr Delta g_tile_delta =
    Delta{ W{ 1 } * g_tile_width, H{ 1 } * g_tile_height };

/****************************************************************
** Querying Tiles
*****************************************************************/
Delta sprite_size( e_tile tile );

gfx::size depixelation_offset( e_tile from_tile,
                               e_tile to_tile );

/****************************************************************
** Rendering Tiles
*****************************************************************/
void render_sprite( rr::Painter& painter, e_tile tile,
                    Coord pixel_coord );

// This one allows stretching the tile.
void render_sprite( rr::Painter& painter, Rect where,
                    e_tile tile );

// This will render a rectangular subsection of the sprite. The
// `source` rect has its origin relative to the upper left corner
// of the sprite in the atlas. Any part of the source rect that
// lies outside of the sprite will be clipped.
void render_sprite_section( rr::Painter& painter, e_tile tile,
                            Coord pixel_coord, Rect source );

// Here the word "tile" is used to mean "repeat the sprite in a
// tiled pattern within the rectangle".
void tile_sprite( rr::Painter& painter, e_tile tile,
                  Rect const& rect );

// This function will render a rectangle with border, but
// where the rectangle and border are comprised of tiles,
// not pixels.  All given tiles must have the same dimensions.
void render_rect_of_sprites_with_border(
    rr::Painter& painter,     // where to draw it
    Coord        dest_origin, // pixel coord of upper left
    Delta        size_tiles,  // tile coords, including border
    e_tile       middle,      //
    e_tile       top,         //
    e_tile       bottom,      //
    e_tile       left,        //
    e_tile       right,       //
    e_tile       top_left,    //
    e_tile       top_right,   //
    e_tile       bottom_left, //
    e_tile       bottom_right //
);

} // namespace rn
