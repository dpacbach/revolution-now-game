/****************************************************************
**tiles.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-08-25.
*
* Description: Handles loading and retrieving tiles
*
*****************************************************************/
#include "tiles.hpp"

#include "errors.hpp"
#include "global-constants.hpp"
#include "globals.hpp"
#include "sdl-util.hpp"
#include "util.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;
using namespace std::literals::string_literals;

namespace rn {

namespace {

struct tile_map {
  struct one_tile {
    int index;
    X   x;
    Y   y;
    int tile, rot, flip_x;
  };
  vector<one_tile> tiles;
};

unordered_map<g_tile, sprite, EnumClassHash> sprites;
unordered_map<std::string, tile_map>         tile_maps;

} // namespace

sprite create_sprite_32( Texture const& texture, Y row, X col ) {
  Rect rect{col * g_tile_width._, row * g_tile_height._,
            g_tile_width, g_tile_height};
  return {&texture, to_SDL( rect ), g_tile_width, g_tile_height};
}

void load_sprites() {
  auto& tile_set = load_texture( "assets/art/tiles-all.png" );

  sprites[g_tile::water] =
      create_sprite_32( tile_set, 0_y, 0_x );
  sprites[g_tile::land] = create_sprite_32( tile_set, 0_y, 1_x );
  sprites[g_tile::land_1_side] =
      create_sprite_32( tile_set, 0_y, 2_x );
  sprites[g_tile::land_2_sides] =
      create_sprite_32( tile_set, 0_y, 3_x );
  sprites[g_tile::land_3_sides] =
      create_sprite_32( tile_set, 0_y, 4_x );
  sprites[g_tile::land_4_sides] =
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
      create_sprite_32( tile_set, 0_y, 5_x );
  sprites[g_tile::land_corner] =
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
      create_sprite_32( tile_set, 0_y, 6_x );

  sprites[g_tile::fog] = create_sprite_32( tile_set, 1_y, 0_x );
  sprites[g_tile::fog_1_side] =
      create_sprite_32( tile_set, 1_y, 1_x );
  sprites[g_tile::fog_corner] =
      create_sprite_32( tile_set, 1_y, 2_x );

  sprites[g_tile::terrain_grass] =
      create_sprite_32( tile_set, 2_y, 0_x );

  sprites[g_tile::panel] =
      create_sprite_32( tile_set, 3_y, 0_x );
  sprites[g_tile::panel_edge_left] =
      create_sprite_32( tile_set, 3_y, 1_x );
  sprites[g_tile::panel_slate] =
      create_sprite_32( tile_set, 3_y, 2_x );
  sprites[g_tile::panel_slate_1_side] =
      create_sprite_32( tile_set, 3_y, 3_x );
  sprites[g_tile::panel_slate_2_sides] =
      create_sprite_32( tile_set, 3_y, 4_x );

  sprites[g_tile::free_colonist] =
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
      create_sprite_32( tile_set, 5_y, 0_x );
  sprites[g_tile::caravel] =
      // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
      create_sprite_32( tile_set, 5_y, 1_x );
}

sprite const& lookup_sprite( g_tile tile ) {
  auto where = sprites.find( tile );
  if( where == sprites.end() )
    DIE( "failed to find sprite "s +
         std::to_string( static_cast<int>( tile ) ) );
  return where->second;
}

void render_sprite( g_tile tile, Y pixel_row, X pixel_col,
                    int rot, int flip_x ) {
  auto where = sprites.find( tile );
  if( where == sprites.end() )
    DIE( "failed to find sprite "s +
         std::to_string( static_cast<int>( tile ) ) );
  sprite const& sp = where->second;

  Rect destination;
  destination.x        = pixel_col;
  destination.y        = pixel_row;
  destination.w        = sp.w;
  destination.h        = sp.h;
  auto destination_sdl = to_SDL( destination );

  constexpr double right_angle = 90.0; // degrees

  double angle = rot * right_angle;

  SDL_RendererFlip flip =
      ( flip_x != 0 ) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

  render_texture( *sp.texture, sp.source, destination_sdl, angle,
                  flip );
}

void render_sprite_grid( g_tile tile, Y tile_row, X tile_col,
                         int rot, int flip_x ) {
  render_sprite( tile, tile_row * g_tile_height._,
                 tile_col * g_tile_width._, rot, flip_x );
}

g_tile index_to_tile( int index ) {
  return static_cast<g_tile>( index );
}

void render_tile_map( std::string_view name ) {
  auto where = tile_maps.find( string( name ) );
  if( where == tile_maps.end() )
    DIE( "failed to find tile_map "s + string( name ) );
  auto tm = where->second;
  for( auto const& tile : tm.tiles )
    render_sprite_grid( index_to_tile( tile.tile ), tile.y,
                        tile.x, tile.rot, tile.flip_x );
}

// tile_map load_tile_map( char const* path ) {
//  ifstream in( path );
//  if( !in.good() )
//    DIE( "failed to open file "s + string( path ) );
//
//  tile_map tm;
//
//  string comments;
//  getline( in, comments );
//
//  while( true ) {
//    int index, tile, rot, flip_x;
//    X   x;
//    Y   y;
//    in >> index >> x >> y >> tile >> rot >> flip_x;
//    if( in.eof() || !in.good() ) break;
//    if( tile < 0 ) continue;
//    tm.tiles.push_back( {index, x, y, tile, rot, flip_x} );
//  }
//  return tm;
//}

void load_tile_maps() {
  // tile_maps["panel"] = load_tile_map( "assets/art/panel.tm" );
}

} // namespace rn
