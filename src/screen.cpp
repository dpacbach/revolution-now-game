/****************************************************************
**screen.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-02-18.
*
* Description: Handles screen resolution and scaling.
*
*****************************************************************/
#include "screen.hpp"

// Revolution Now
#include "config-files.hpp"
#include "errors.hpp"
#include "gfx.hpp"
#include "init.hpp"
#include "logging.hpp"
#include "menu.hpp"
#include "terrain.hpp" // FIXME: remove
#include "tiles.hpp"

// Revolution Now (config)
#include "../config/ucl/rn.inl"

// base
#include "base/lambda.hpp"

// SDL
#include "SDL.h"

// C++ standard library
#include <cmath>

using namespace std;

namespace rn {

::SDL_Window*   g_window   = nullptr;
::SDL_Renderer* g_renderer = nullptr;
Texture         g_texture_viewport;

Scale g_resolution_scale_factor{ 0 };
Scale g_optimal_resolution_scale_factor{ 0 };
Delta g_screen_physical_size{};

namespace {

auto g_pixel_format = ::SDL_PIXELFORMAT_RGBA8888;

constexpr int min_scale_factor = 1;
constexpr int max_scale_factor = 10;

// Cache is invalidated by setting to nothing.
maybe<Delta> main_window_physical_size_cache;

/*
 *::SDL_DisplayMode find_fullscreen_mode() {
 *  ::SDL_DisplayMode dm;
 *  lg.debug( "available display modes:" );
 *  auto num_display_modes = ::SDL_GetNumDisplayModes( 0 );
 *  constexpr int min_x_res{1920};
 *  constexpr int min_y_res{1080};
 *  for( int i = 0; i < num_display_modes; ++i ) {
 *    ::SDL_GetDisplayMode( 0, i, &dm );
 *    if( dm.w % g_tile_width._ == 0 &&
 *        dm.h % g_tile_height._ == 0 ) {
 *      lg.debug( "{}x{}", dm.w, dm.h );
 *      if( dm.w >= min_x_res && dm.h >= min_y_res ) return dm;
 *    }
 *  }
 *  dm.w = dm.h = 0; // means we can't find one.
 *  return dm;
 *}
 */

// Do not use this from outside this module: it refers to the en-
// tire screen, even if the game is in an unmaximized window. In
// the vast majority of cases you want to use
// main_window_logical_size.
Delta screen_logical_size() {
  return g_screen_physical_size / g_resolution_scale_factor;
}

double monitor_diagonal_length( float ddpi, DisplayMode dm ) {
  double length =
      sqrt( pow( dm.size.w._, 2.0 ) + pow( dm.size.h._, 2.0 ) ) /
      ddpi;
  // Round to hearest 1/2 inch.
  return double( lround( length * 2.0 ) ) / 2.0;
}

// Get diagonal DPI of monitor. This seems to basically be the
// same as the horizontal and vertical DPIs.
double monitor_ddpi() {
  float ddpi;
  CHECK( !::SDL_GetDisplayDPI( 0, &ddpi, nullptr, nullptr ) );
  return ddpi;
}

double monitor_inches() {
  return monitor_diagonal_length( monitor_ddpi(),
                                  current_display_mode() );
}

double const& viewer_distance_from_monitor() {
  static double distance = [] {
    // Determined empirically; viewer distance from screen seems
    // to scale linearly with screen size, down to a certain
    // minimum distance.
    constexpr double viewer_distance_multiplier{ 1.25 };
    constexpr double viewer_distance_minimum{ 18 }; // inches
    auto             res =
        std::max( viewer_distance_multiplier * monitor_inches(),
                  viewer_distance_minimum );
    lg.debug( "computed viewer distance from screen: {}in.",
              res );
    return res;
  }();
  return distance;
};

void query_video_stats() {
  float ddpi, hdpi, vdpi;
  ::SDL_GetDisplayDPI( 0, &ddpi, &hdpi, &vdpi );
  lg.debug( "GetDisplayDPI: {{ddpi={}, hdpi={}, vdpi={}}}.",
            ddpi, hdpi, vdpi );

  SDL_DisplayMode dm;

  auto dm_to_str = [&dm] {
    return fmt::format( "{}x{}@{}Hz, pf={}", dm.w, dm.h,
                        dm.refresh_rate,
                        ::SDL_GetPixelFormatName( dm.format ) );
  };
  (void)dm_to_str;

  lg.debug( "default game pixel format: {}",
            ::SDL_GetPixelFormatName( g_pixel_format ) );

  SDL_GetCurrentDisplayMode( 0, &dm );
  lg.debug( "GetCurrentDisplayMode: {}", dm_to_str() );
  if( g_pixel_format != dm.format ) {
    // g_pixel_format =
    //    static_cast<decltype( g_pixel_format )>( dm.format );
    // lg.debug( "correcting game pixel format to {}",
    //               ::SDL_GetPixelFormatName( dm.format ) );
  }

  SDL_GetDesktopDisplayMode( 0, &dm );
  lg.debug( "GetDesktopDisplayMode: {}", dm_to_str() );

  SDL_GetDisplayMode( 0, 0, &dm );
  lg.debug( "GetDisplayMode: {}", dm_to_str() );

  SDL_Rect r;
  SDL_GetDisplayBounds( 0, &r );
  lg.debug( "GetDisplayBounds: {}",
            Rect{ X{ r.x }, Y{ r.y }, W{ r.w }, H{ r.h } } );

  lg.debug( "monitor diagonal length: {}in.", monitor_inches() );
}

struct ScaleInfo {
  int    scale;
  double tile_size_on_screen_surface_inches;
  Delta  resolution;
  double tile_angular_size;
};
NOTHROW_MOVE( ScaleInfo );

ScaleInfo scale_info( int scale_ ) {
  Scale scale{ scale_ };
  Delta resolution = current_display_mode().size / scale;

  // Tile size in inches if it were measured on the surface of
  // the screen.
  double tile_size_screen_surface =
      ( scale * g_tile_scale ).sx._ / monitor_ddpi();

  // Compute the angular size (this is what actually determines
  // how big it looks to the viewer).
  auto theta =
      2.0 * std::atan( ( tile_size_screen_surface / 2.0 ) /
                       viewer_distance_from_monitor() );

  return ScaleInfo{ scale_, tile_size_screen_surface, resolution,
                    theta };
}

// Lower score is better.
double scale_score( ScaleInfo const& info ) {
  return ::abs( info.tile_angular_size -
                config_rn.ideal_tile_angular_size );
}

// This function attempts to find an integer scale factor with
// which to scale the pixel size of the display so that a single
// tile has approximately a given size. The scale factor is an
// integer because we scale both dimensions by the same amount,
// and the scale factor is not a floating point number because we
// don't want any distortion of individual pixels which would
// arise in that situation.
void find_pixel_scale_factor() {
  vector<ScaleInfo> infos;
  for( auto i = min_scale_factor; i < max_scale_factor + 1; ++i )
    infos.push_back( scale_info( i ) );

  auto it = min_element(
      infos.begin(), infos.end(), []( auto& l, auto& r ) {
        return scale_score( l ) < scale_score( r );
      } );

  CHECK( it != infos.end() );
  ScaleInfo const& optimal = *it;

  ///////////////////////////////////////////////////////////////
#if 0
  auto table_row = []( auto possibility, auto resolution,
                       auto tile_size_screen, auto tile_size_1ft,
                       auto score ) {
    lg.debug( "{: ^10}{: ^19}{: ^18}{: ^18}{: ^10}", possibility,
              resolution, tile_size_screen, tile_size_1ft,
              score );
  };

  table_row( "Scale", "Resolution", "Tile-Size-Screen",
             "Tile-Angular-Size", "Score" );
  auto bar = string( 86, '-' );
  lg.debug( bar );
  auto fmt_dbl = []( double d ) {
    return fmt::format( "{:.4}", d );
  };
  for( auto const& info : scale_scores ) {
    string chosen =
        ( info.scale == optimal.scale ) ? "=> " : "   ";
    table_row(
        chosen + to_string( info.scale ), info.resolution,
        fmt_dbl( info.tile_size_on_screen_surface_inches ),
        fmt_dbl( info.tile_angular_size ),
        fmt_dbl( scale_score( info ) ) );
  }
  lg.debug( bar );
#endif
  ///////////////////////////////////////////////////////////////

  g_resolution_scale_factor         = Scale{ optimal.scale };
  g_optimal_resolution_scale_factor = Scale{ optimal.scale };
  g_screen_physical_size =
      optimal.resolution * g_resolution_scale_factor;
  lg.info( "screen physical resolution: {}",
           g_screen_physical_size );
  lg.info( "screen logical  resolution: {}",
           screen_logical_size() );

  // If this is violated then we have non-integer scaling.
  CHECK( g_screen_physical_size % Scale{ optimal.scale } ==
         Delta{} );

  // For informational purposes
  if( current_display_mode().size % Scale{ optimal.scale } !=
      Delta{} )
    lg.warn(
        "Desktop display resolution not commensurate with scale "
        "factor." );
}

void init_screen() {
  query_video_stats();
  find_pixel_scale_factor();

  auto flags = ::SDL_WINDOW_SHOWN | ::SDL_WINDOW_RESIZABLE |
               ::SDL_WINDOW_FULLSCREEN_DESKTOP |
               ::SDL_WINDOW_OPENGL;

  auto dm = current_display_mode().size;

  g_window =
      ::SDL_CreateWindow( config_rn.main_window.title.c_str(), 0,
                          0, dm.w._, dm.h._, flags );
  CHECK( g_window != nullptr, "failed to create window" );
}

void cleanup_screen() {
  if( g_window != nullptr ) SDL_DestroyWindow( g_window );
}

MENU_ITEM_HANDLER(
    scale_up, [] { inc_resolution_scale(); },
    L0( g_resolution_scale_factor !=
        Scale{ max_scale_factor } ) )

MENU_ITEM_HANDLER(
    scale_down, [] { dec_resolution_scale(); },
    L0( g_resolution_scale_factor !=
        Scale{ min_scale_factor } ) )

MENU_ITEM_HANDLER(
    scale_optimal, [] { set_optimal_resolution_scale(); },
    L0( g_resolution_scale_factor !=
        g_optimal_resolution_scale_factor ) )

MENU_ITEM_HANDLER(
    toggle_fullscreen,
    [] {
      auto is_fullscreen = toggle_fullscreen();
      if( !is_fullscreen ) restore_window();
    },
    L0( true ) )

MENU_ITEM_HANDLER(
    restore_window,
    [] {
      if( is_window_fullscreen() ) {
        toggle_fullscreen();
        restore_window();
      } else {
        restore_window();
      }
    },
    L0( true ) )

void on_logical_resolution_changed() {
  // Invalidate cache.
  main_window_physical_size_cache = nothing;

  auto logical_size = main_window_logical_size();
  ::SDL_RenderSetLogicalSize( g_renderer, logical_size.w._,
                              logical_size.h._ );
  lg.debug( "logical resolution changed to {}", logical_size );

  auto physical_size = main_window_physical_size();
  if( physical_size % g_resolution_scale_factor != Delta{} )
    lg.warn(
        "main window physical resolution not commensurate with "
        "scale factor." );
}

void on_renderer_scale_factor_changed() {
  lg.info( "scale factor changed: {}",
           g_resolution_scale_factor );
  on_logical_resolution_changed();
}

void init_renderer() {
  g_renderer = SDL_CreateRenderer(
      g_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |
          SDL_RENDERER_PRESENTVSYNC );

  CHECK( g_renderer, "failed to create renderer" );

  // This will do what is necessary when the scale factor is
  // reset (or, in this case, set for the first time).
  on_renderer_scale_factor_changed();

  // I think in theory we should not need this because we should
  // have already computed a logical_size that allowed for in-
  // teger scaling, but just in case we do the calculations wrong
  // this might help to flag that.
  //::SDL_RenderSetIntegerScale( g_renderer, ::SDL_TRUE );

  ::SDL_SetRenderDrawBlendMode( g_renderer,
                                ::SDL_BLENDMODE_BLEND );

  // Now we calculate the necessary size of the viewport texture.
  // This needs to be large enough to accomodate a zoomed-out
  // view in which the entire world is visible.
  //
  // FIXME: move this out of here or, even better, get rid of it
  // altogether.
  auto delta = world_size * Scale{ 32 };
  lg.debug( "g_texture_viewport proposed size: {}", delta );
  lg.debug( "g_texture_viewport memory usage estimate: {}MB",
            Texture::mem_usage_mb( delta ) );
  g_texture_viewport = create_texture( delta );
}

void cleanup_renderer() {
  if( g_renderer != nullptr )
    ::SDL_DestroyRenderer( g_renderer );
}

} // namespace

REGISTER_INIT_ROUTINE( screen );
REGISTER_INIT_ROUTINE( renderer );

void* main_os_window_handle() { return (void*)g_window; }

void inc_resolution_scale() {
  int scale     = g_resolution_scale_factor.sx._;
  int old_scale = scale;
  scale++;
  scale = clamp( scale, min_scale_factor, max_scale_factor );
  g_resolution_scale_factor.sx = SX{ scale };
  g_resolution_scale_factor.sy = SY{ scale };
  if( old_scale != scale ) on_renderer_scale_factor_changed();
}

void dec_resolution_scale() {
  int scale     = g_resolution_scale_factor.sx._;
  int old_scale = scale;
  scale--;
  scale = clamp( scale, min_scale_factor, max_scale_factor );
  g_resolution_scale_factor.sx = SX{ scale };
  g_resolution_scale_factor.sy = SY{ scale };
  if( old_scale != scale ) on_renderer_scale_factor_changed();
}

void set_optimal_resolution_scale() {
  if( g_resolution_scale_factor !=
      g_optimal_resolution_scale_factor ) {
    g_resolution_scale_factor =
        g_optimal_resolution_scale_factor;
    on_renderer_scale_factor_changed();
  }
}

DisplayMode current_display_mode() {
  SDL_DisplayMode dm;
  SDL_GetCurrentDisplayMode( 0, &dm );
  DisplayMode res{
      { W{ dm.w }, H{ dm.h } }, dm.format, dm.refresh_rate };
  return res;
}

Delta whole_screen_physical_size() {
  return g_screen_physical_size;
}

Delta main_window_logical_size() {
  return main_window_physical_size() / g_resolution_scale_factor;
}

Rect main_window_logical_rect() {
  return main_window_physical_rect() / g_resolution_scale_factor;
}

Delta main_window_physical_size() {
  if( !main_window_physical_size_cache ) {
    CHECK( g_window != nullptr );
    int w{}, h{};
    ::SDL_GetWindowSize( g_window, &w, &h );
    main_window_physical_size_cache = Delta{ W{ w }, H{ h } };
  }
  return *main_window_physical_size_cache;
}

Rect main_window_physical_rect() {
  return Rect::from( Coord{}, main_window_physical_size() );
}

void hide_window() {
  if( g_window ) ::SDL_HideWindow( g_window );
}

// TODO: mac-os, does not seem to be able to detect when the user
// fullscreens a window.
bool is_window_fullscreen() {
  // This bit should always be set even if we're in the "desktop"
  // fullscreen mode.
  return ( ::SDL_GetWindowFlags( g_window ) &
           ::SDL_WINDOW_FULLSCREEN ) != 0;
}

void set_fullscreen( bool fullscreen ) {
  bool already = is_window_fullscreen();
  if( ( fullscreen ^ already ) == 0 ) return;

  // Must only contain one of the following values.
  ::Uint32 flags =
      fullscreen ? ::SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  ::SDL_SetWindowFullscreen( g_window, flags );
}

bool toggle_fullscreen() {
  auto fullscreen = is_window_fullscreen();
  set_fullscreen( !fullscreen );
  return !fullscreen;
}

void restore_window() { ::SDL_RestoreWindow( g_window ); }

void on_main_window_resized() {
  lg.debug( "main window resizing." );
  on_logical_resolution_changed();
}

Delta max_texture_size() {
  ::SDL_RendererInfo info;
  ::SDL_GetRendererInfo( g_renderer, &info );
  return Delta{ W{ info.max_texture_width },
                H{ info.max_texture_height } };
}

} // namespace rn
