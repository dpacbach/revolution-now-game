/****************************************************************
**menu.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-01-27.
*
* Description: Menu Bar
*
*****************************************************************/
#include "menu.hpp"

// Revolution Now
#include "color.hpp"
#include "compositor.hpp"
#include "config-files.hpp"
#include "errors.hpp"
#include "frame.hpp"
#include "gfx.hpp"
#include "init.hpp"
#include "logging.hpp"
#include "macros.hpp"
#include "plane.hpp"
#include "screen.hpp"
#include "text.hpp"
#include "tiles.hpp"
#include "variant.hpp"

// Rnl
#include "rnl/menu.hpp"

// Revolution Now (config)
#include "../config/ucl/palette.inl"
#include "../config/ucl/ui.inl"

// base
#include "base/lambda.hpp"

// base-util
#include "base-util/algo.hpp"

// magic enum
#include "magic_enum.hpp"

// C++ standard library
#include <chrono>

using namespace std;

namespace rn {

namespace {

/****************************************************************
** Main Data Structures
*****************************************************************/
struct Menu {
  string name;
  bool   right_side;
  char   shortcut;
};
NOTHROW_MOVE( Menu );

unordered_map<e_menu, Menu> g_menus{
    { e_menu::game, { "Game", false, 'G' } },
    { e_menu::view, { "View", false, 'V' } },
    { e_menu::orders, { "Orders", false, 'O' } },
    { e_menu::colony, { "Colony", false, 'C' } },
    { e_menu::europort, { "Europe", false, 'E' } },
    { e_menu::advisors, { "Advisors", false, 'A' } },
    { e_menu::music, { "Music", false, 'M' } },
    { e_menu::window, { "Window", false, 'W' } },
    { e_menu::debug, { "Debug", true, 'D' } },
    { e_menu::pedia, { "Revolopedia", true, 'R' } } };

} // namespace

struct MenuCallbacks {
  function<void( void )> on_click;
  function<bool( void )> enabled;
};
NOTHROW_MOVE( MenuCallbacks );

} // namespace rn

// Rnl
#include "rnl/menu-impl.hpp"

namespace rn {
namespace {

unordered_map<e_menu_item, MenuItem::menu_clickable*>
                                           g_menu_items;
unordered_map<e_menu_item, e_menu>         g_item_to_menu;
unordered_map<e_menu, vector<e_menu_item>> g_items_from_menu;

#define ITEM( item, name )      \
  MenuItem::menu_clickable {    \
    e_menu_item::item, name, {} \
  }

#define DIVIDER \
  MenuItem::menu_divider {}

/****************************************************************
** The Menus
*****************************************************************/
unordered_map<e_menu, vector<MenuItem_t>> g_menu_def{
    { e_menu::game,
      {
          ITEM( about, "About this Game" ),    //
          /***********/ DIVIDER, /***********/ //
          ITEM( revolution, "REVOLUTION!" ),   //
          /***********/ DIVIDER, /***********/ //
          ITEM( retire, "Retire" ),            //
          ITEM( exit, "Exit to \"DOS\"" )      //
      } },
    { e_menu::view,
      {
          ITEM( zoom_in, "Zoom In" ),          //
          ITEM( zoom_out, "Zoom Out" ),        //
          /***********/ DIVIDER, /***********/ //
          ITEM( restore_zoom, "Zoom Default" ) //
      } },
    { e_menu::orders,
      {
          ITEM( sentry, "Sentry" ),  //
          ITEM( fortify, "Fortify" ) //
      } },
    { e_menu::colony,
      {
          ITEM( colony_view_close, "Close" ) //
      } },
    { e_menu::europort,
      {
          ITEM( europort_view, "View" ),  //
          ITEM( europort_close, "Close" ) //
      } },
    { e_menu::advisors,
      {
          ITEM( military_advisor, "Military Advisor" ),   //
          ITEM( economics_advisor, "Economics Advisor" ), //
          ITEM( european_advisor, "Europian Advisor" )    //
      } },
    { e_menu::music,
      {
          ITEM( music_play, "Play" ),                     //
          ITEM( music_stop, "Stop" ),                     //
          ITEM( music_pause, "Pause" ),                   //
          ITEM( music_resume, "Resume" ),                 //
          /***********/ DIVIDER, /***********/            //
          ITEM( music_next, "Next Tune" ),                //
          ITEM( music_prev, "Previous Tune" ),            //
          /***********/ DIVIDER, /***********/            //
          ITEM( music_vol_up, "Volume Up" ),              //
          ITEM( music_vol_down, "Volume Down" ),          //
          /***********/ DIVIDER, /***********/            //
          ITEM( music_set_player, "Set Music Player..." ) //
      } },
    { e_menu::window,
      {
          ITEM( toggle_fullscreen, "Toggle Fullscreen" ), //
          ITEM( restore_window, "Restore" ),              //
          /***********/ DIVIDER, /***********/            //
          ITEM( scale_up, "Scale Up" ),                   //
          ITEM( scale_down, "Scale Down" ),               //
          ITEM( scale_optimal, "Scale Optimal" )          //
      } },
    { e_menu::debug,
      {
          ITEM( toggle_console, "Toggle Console" ) //
      } },
    { e_menu::pedia,
      {
          ITEM( units_help, "Units" ),                     //
          ITEM( terrain_help, "Terrain" ),                 //
          ITEM( founding_father_help, "Founding Fathers" ) //
      } } };

/****************************************************************
** Menu State
*****************************************************************/
namespace click_anim {

// TODO: make an animation framework that can manage the states
// of an animation along with durations in Frames.

// For the MacOS single-blink-and-fade style.
auto constexpr half_period      = Frames{ 6 };
auto constexpr post_off_time    = Frames{ 0 };
auto constexpr num_half_periods = int( 2 );
bool constexpr start_on         = false;
auto constexpr fade_time        = Frames{ 22 };

// For example, the click could be like this:
//
//   click_anim_start_on=true, num_half_periods=4
//   ----------------------------------------------------
//   |  on  |  off |  on  |  off | off-time | fade-time |
//   ----------------------------------------------------
//
// where each box is a "half period."

auto period = half_period * 2;
auto total_duration =
    half_period * num_half_periods + post_off_time + fade_time;
} // namespace click_anim

MenuState_t g_menu_state{ MenuState::menus_closed{} };

void log_menu_state() {
  lg.trace( "g_menu_state: {}", g_menu_state );
}

/****************************************************************
** Querying State
*****************************************************************/
bool is_menu_open( e_menu menu ) {
  switch( g_menu_state.to_enum() ) {
    case MenuState::e::menus_hidden: {
      return false;
    }
    case MenuState::e::menus_closed: {
      return false;
    }
    case MenuState::e::item_click: {
      auto& click = g_menu_state.get<MenuState::item_click>();
      CHECK( g_item_to_menu.contains( click.item ) );
      return g_item_to_menu[click.item] == menu;
    }
    case MenuState::e::menu_open: {
      auto& o = g_menu_state.get<MenuState::menu_open>();
      return o.menu == menu;
    }
  }
}

maybe<e_menu> opened_menu() {
  if_get( g_menu_state, MenuState::menu_open, val ) {
    return val.menu;
  }
  return {};
}

bool is_menu_item_enabled_( e_menu_item item ) {
  CHECK( g_menu_items.contains( item ) );
  return g_menu_items[item]->callbacks.enabled();
}

auto is_menu_item_enabled =
    per_frame_memoize( is_menu_item_enabled_ );

bool is_menu_visible_( e_menu menu ) {
  auto& items = g_items_from_menu[menu];
  return any_of( items.begin(), items.end(),
                 L( is_menu_item_enabled( _ ) ) );
}

auto is_menu_visible = per_frame_memoize( is_menu_visible_ );

auto visible_menus_() {
  auto& values = magic_enum::enum_values<e_menu>();

  vector<e_menu> res;
  res.reserve( values.size() );
  for( e_menu v : values )
    if( is_menu_visible( v ) ) //
      res.push_back( v );
  return res;
}

auto visible_menus = per_frame_memoize( visible_menus_ );

bool have_some_visible_menus() {
  auto& values = magic_enum::enum_values<e_menu>();
  return any_of( values.begin(), values.end(),
                 L( is_menu_visible( _ ) ) );
}

maybe<e_menu> first_visible_menu() {
  maybe<e_menu> res;
  auto&         menus = visible_menus();
  if( !menus.empty() ) res = menus[0];
  return res;
}

/****************************************************************
** Colors
*****************************************************************/
auto const& menu_theme_color1 = Color::banana();
auto const& menu_theme_color2 = Color::wood();

namespace color::item::foreground {
auto disabled() {
  auto color = config_palette.grey.n88;
  color.a    = 200;
  return color;
}
} // namespace color::item::foreground

namespace color::menu::foreground {
auto const& disabled = config_palette.grey.nA4;
} // namespace color::menu::foreground

/****************************************************************
** Cached Textures
*****************************************************************/
struct ItemTextures {
  Texture normal;
  Texture highlighted;
  Texture disabled;
  // This is the max width of the above textures, just for the
  // sake of having it precomputed.
  W width{ 0 };
};
NOTHROW_MOVE( ItemTextures );

unordered_map<e_menu_item, ItemTextures> g_menu_item_rendered;

struct MenuTextures {
  // Each menu may have a different width depending on the sizes
  // of the elements inside of it, so we will render different
  // sized dividers for each menu. All dividers in a given menu
  // will be the same size though.
  Texture      divider;
  Texture      item_background_highlight;
  ItemTextures name;
  Texture      menu_body;
  Texture      menu_body_shadow;
  Texture      menu_background_highlight;
  Texture      menu_background_hover;
  W            header_width{ 0 };
};
NOTHROW_MOVE( MenuTextures );

unordered_map<e_menu, MenuTextures> g_menu_rendered;

// Must be called after all textures are rendered. It will it-
// erate through all the rendered text textures (both menu
// headers and menu items) and will compute the maximum height of
// all of them. This height will then be used as the height for
// all text elements in the menus for simplicity.
//
// This value is cached because a) it requires querying textures
// and b) its value is not expected to ever change.
H const& max_text_height() {
  static H max_height = [] {
    H res{ 0 };
    for( auto menu : magic_enum::enum_values<e_menu>() ) {
      CHECK( g_menu_rendered.contains( menu ) );
      auto const& textures = g_menu_rendered[menu];
      res = std::max( res, textures.name.normal.size().h );
      for( auto item : g_items_from_menu[menu] ) {
        CHECK( g_menu_item_rendered.contains( item ) );
        auto const& textures = g_menu_item_rendered[item];
        // These are probably all the same size, but just in case
        // they aren't. The 2_sx is because we have padding on
        // each side of the item.
        res = std::max( res, textures.normal.size().h );
        res = std::max( res, textures.highlighted.size().h );
        res = std::max( res, textures.disabled.size().h );
      }
    }
    CHECK( res > 0_h );
    lg.debug( "max menu text height: {}", res );
    return res;
  }();
  return max_height;
}

/****************************************************************
** Geometry
*****************************************************************/
// The long, thin rectangle around the menu bar. This does not
// include the space that would be occupied by open menu bodies.
Rect menu_bar_rect() {
  ASSIGN_CHECK_OPT( res, compositor::section(
                             compositor::e_section::menu_bar ) );
  return res;
}

H menu_bar_height() { return menu_bar_rect().h; }

Delta menu_header_delta( e_menu menu ) {
  CHECK( g_menu_rendered.contains( menu ) );
  return Delta{ g_menu_rendered[menu].header_width,
                max_text_height() };
}

// These cannot be precalculated because menus might be hidden.
X menu_header_x_pos_( e_menu target ) {
  CHECK( g_menus.contains( target ) );
  CHECK( is_menu_visible( target ) );
  auto const& desc = g_menus[target];
  W           width_delta{ 0 };
  auto        vm = visible_menus();
  erase_if( vm, LC( g_menus[_].right_side != desc.right_side ) );
  auto target_it = find( vm.begin(), vm.end(), target );
  if( desc.right_side )
    vm.erase( vm.begin(), target_it );
  else
    vm.erase( target_it, vm.end() );
  for( e_menu item : vm )
    width_delta +=
        menu_header_delta( item ).w + config_ui.menus.spacing;
  if( !vm.empty() ) width_delta -= config_ui.menus.spacing;
  width_delta += config_ui.menus.first_menu_start;
  CHECK( width_delta >= 0_w );
  return 0_x + ( !desc.right_side
                     ? width_delta
                     : menu_bar_rect().w - width_delta );
}

auto menu_header_x_pos = per_frame_memoize( menu_header_x_pos_ );

// Rectangle around a menu header.
Rect menu_header_rect( e_menu menu ) {
  CHECK( g_menu_rendered.contains( menu ) );
  auto y_offset =
      0_y + ( ( menu_bar_height() - max_text_height() ) / 2_sy );
  return Rect::from(
      Coord{ y_offset, menu_header_x_pos( menu ) },
      menu_header_delta( menu ) );
}

// This is the width of the menu body not including the borders,
// which themselves occupy part of a tile.
W menu_body_width_inner( e_menu menu ) {
  W res{ 0 };
  for( auto const& item : g_items_from_menu[menu] ) {
    CHECK( g_menu_item_rendered.contains( item ) );
    res = std::max( res, g_menu_item_rendered[item].width );
  }
  // At this point, res holds the width of the largest rendered
  // text texture in this menu.  Now add padding on each side:
  res += config_ui.menus.padding * 2_sx;
  res = clamp( res, config_ui.menus.body_min_width, 1000000_w );
  // round up to nearest multiple of 8, since that is the menu
  // tile width.
  if( res % 8_sx != 0_w ) res += ( 8_w - ( res % 8_sx ) );
  // Sanity check
  CHECK( res > 0 && res < 2000 );
  return res;
}

H divider_height() { return max_text_height() / 2; }

// This is the width of the menu body not including the borders,
// which themselves occupy part of a tile.
H menu_body_height_inner( e_menu menu ) {
  H h{ 0 };
  CHECK( g_menu_def.contains( menu ) );
  for( auto const& item : g_menu_def[menu] ) {
    overload_visit(
        item,
        [&]( MenuItem::menu_divider const& ) {
          h += divider_height();
        },
        [&]( MenuItem::menu_clickable const& ) {
          h += max_text_height();
        } );
  }
  // round up to nearest multiple of 8, since that is the menu
  // tile width.
  if( h % 8_sy != 0_h ) h += ( 8_h - ( h % 8_sy ) );
  return h;
}

Delta menu_body_delta_inner( e_menu menu ) {
  return { menu_body_width_inner( menu ),
           menu_body_height_inner( menu ) };
}

Delta menu_body_delta( e_menu menu ) {
  return Delta{ 8_w, 8_h } + Delta{ 8_w, 8_h } +
         menu_body_delta_inner( menu );
}

Delta menu_item_delta( e_menu menu ) {
  return Delta{ menu_body_width_inner( menu ),
                max_text_height() };
}

Delta divider_delta( e_menu menu ) {
  return Delta{ divider_height(),
                menu_body_width_inner( menu ) };
}

Rect menu_body_rect_inner( e_menu menu ) {
  CHECK( g_menus.contains( menu ) );
  if( g_menus[menu].right_side ) {
    Coord pos{ Y{ 0 } + menu_bar_height() + 8_h,
               menu_header_x_pos( menu ) +
                   menu_header_delta( menu ).w -
                   menu_body_delta_inner( menu ).w };
    return Rect::from( pos, menu_body_delta_inner( menu ) );
  } else {
    Coord pos{ Y{ 0 } + menu_bar_height() + 8_h,
               menu_header_x_pos( menu ) };
    return Rect::from( pos, menu_body_delta_inner( menu ) );
  }
}

Rect menu_body_rect( e_menu menu ) {
  CHECK( g_menus.contains( menu ) );
  if( g_menus[menu].right_side ) {
    Coord pos{ Y{ 0 } + menu_bar_height(),
               menu_header_x_pos( menu ) +
                   menu_header_delta( menu ).w + 8_w -
                   menu_body_delta( menu ).w };
    return Rect::from( pos, menu_body_delta( menu ) );
  } else {
    Coord pos{ Y{ 0 } + menu_bar_height(),
               menu_header_x_pos( menu ) - 8_w };
    return Rect::from( pos, menu_body_delta( menu ) );
  }
}

// This includes (roughly) the space overwhich an open menu
// occupies pixels (i.e., is not transparent). This is used to
// decide if the user has clicked on or off of an open menu.
Rect menu_body_clickable_area( e_menu menu ) {
  auto res = menu_body_rect( menu );
  res.x += 8_w / 2_sx;
  res.w -= 8_w / 2_sx * 2_sx;
  res.h -= 8_h / 2_sy;
  return res;
}

// `h` is the vertical position from the top of the menu body.
maybe<e_menu_item> cursor_to_item( e_menu menu, H h ) {
  CHECK( g_menu_def.contains( menu ) );
  H pos{ 0 };
  for( auto const& item : g_menu_def[menu] ) {
    overload_visit(
        item,
        [&]( MenuItem::menu_divider const& ) {
          pos += divider_height();
        },
        [&]( MenuItem::menu_clickable const& ) {
          pos += max_text_height();
        } );
    if( pos > h ) {
      switch( item.to_enum() ) {
        case MenuItem::e::menu_divider: //
          return nothing;
        case MenuItem::e::menu_clickable: {
          auto& val = item.get<MenuItem::menu_clickable>();
          return val.item;
        }
      }
    }
  }
  return {};
}

// `cursor` is the screen position of the mouse cursor.
maybe<e_menu_item> cursor_to_item( e_menu menu, Coord cursor ) {
  if( !cursor.is_inside( menu_body_rect_inner( menu ) ) )
    return {};
  return cursor_to_item(
      menu, cursor.y - menu_body_rect_inner( menu ).top_edge() );
}

/****************************************************************
** Rendering Implmementation
*****************************************************************/
// For either a menu header or item.
ItemTextures render_menu_element( string_view const text,
                                  maybe<char>       shortcut,
                                  Color inactive_color,
                                  Color active_color,
                                  Color disabled_color ) {
  Texture inactive, active;
  if( shortcut.has_value() ) {
    auto shortcut_pos =
        text.find_first_of( toupper( *shortcut ) );
    if( shortcut_pos == string_view::npos )
      shortcut_pos = text.find_first_of( tolower( *shortcut ) );
    CHECK( shortcut_pos != string_view::npos );
    string_view first_part( text.begin(), shortcut_pos );
    char        middle_part = text[shortcut_pos];
    string_view last_part( text.begin() + shortcut_pos + 1,
                           text.size() - shortcut_pos - 1 );
    auto mk_text = string( first_part ) + "@[H]" + middle_part +
                   "@[]" + string( last_part );

    TextMarkupInfo inactive_info{
        /*normal=*/inactive_color,
        /*highlight=*/inactive_color.highlighted( 4 ) };
    TextMarkupInfo active_info{
        /*normal=*/active_color,
        /*highlight=*/active_color.highlighted( 3 ) };

    inactive = clone_texture( render_text_markup(
        config_ui.menus.font, inactive_info, mk_text ) );
    active   = clone_texture( render_text_markup(
        config_ui.menus.font, active_info, mk_text ) );
  } else {
    inactive = clone_texture( render_text(
        config_ui.menus.font, inactive_color, text ) );
    active   = clone_texture( render_text( config_ui.menus.font,
                                         active_color, text ) );
  }
  auto disabled = clone_texture( render_text(
      config_ui.menus.font, disabled_color, text ) );
  // Need to do this first before moving.
  auto width = std::max( { inactive.size().w, active.size().w,
                           disabled.size().w } );
  auto res =
      ItemTextures{ std::move( inactive ), std::move( active ),
                    std::move( disabled ), width };
  // Sanity check
  CHECK( res.width > 0 && res.width < menu_bar_rect().w );
  return res;
}

// For either a menu header or item.
ItemTextures render_menu_item_element( string const& text,
                                       maybe<char> /*unused*/ ) {
  return render_menu_element(
      text, nothing, //
      menu_theme_color1, menu_theme_color2,
      color::item::foreground::disabled() );
}

// For either a menu header or item.
ItemTextures render_menu_header_element( string const& text,
                                         maybe<char> shortcut ) {
  return render_menu_element(
      text, shortcut, menu_theme_color1, menu_theme_color2,
      color::menu::foreground::disabled );
}

Texture render_divider( e_menu menu ) {
  Delta   delta      = divider_delta( menu );
  Texture res        = create_texture_transparent( delta );
  Color   color_fore = menu_theme_color2.shaded( 3 );
  render_line( res, color_fore, Coord{} + delta.h / 2 + 2_w,
               { delta.w - 5_w, 0_h } );
  return res;
}

Texture create_menu_body_texture( e_menu menu ) {
  return create_texture_transparent( menu_body_delta( menu ) );
}

Texture render_item_background( e_menu menu, bool active ) {
  CHECK( active );
  auto res = create_texture( menu_item_delta( menu ) );
  clear_texture_transparent( res );
  render_sprite( res, e_tile::menu_item_sel_back, Coord{}, 0,
                 0 );
  auto sprite_width =
      lookup_sprite( e_tile::menu_item_sel_back ).size().w;
  render_sprite( res, e_tile::menu_item_sel_back,
                 Coord{} + sprite_width, 0, 0 );
  return res;
}

Texture render_menu_header_background( e_menu menu, bool active,
                                       bool hover ) {
  CHECK( active || hover );
  // FIXME
  CHECK( !( active && hover ) );
  auto res = create_texture( menu_header_delta( menu ) );
  clear_texture_transparent( res );
  if( active )
    render_sprite( res, e_tile::menu_item_sel_back, Coord{}, 0,
                   0 );
  else
    render_sprite( res, e_tile::menu_hdr_sel_back, Coord{}, 0,
                   0 );
  return res;
}

Texture& render_open_menu( e_menu             menu,
                           maybe<e_menu_item> subject,
                           bool               clicking ) {
  CHECK( g_menu_rendered.contains( menu ) );
  if( subject.has_value() ) {
    CHECK( g_item_to_menu[*subject] == menu );
  }
  auto& textures = g_menu_rendered[menu];
  auto& dst      = textures.menu_body;
  Coord pos{ 8_x, 8_y };

  clear_texture_transparent( dst );

  render_rect_of_sprites_with_border(
      dst,                              //
      Coord{},                          //
      dst.size() / Scale{ 8_sx, 8_sy }, //
      e_tile::menu_body,                //
      e_tile::menu_top,                 //
      e_tile::menu_bottom,              //
      e_tile::menu_left,                //
      e_tile::menu_right,               //
      e_tile::menu_top_left,            //
      e_tile::menu_top_right,           //
      e_tile::menu_bottom_left,         //
      e_tile::menu_bottom_right         //
  );

  for( auto const& item : g_menu_def[menu] ) {
    overload_visit(
        item,
        [&]( MenuItem::menu_divider ) {
          copy_texture( textures.divider, dst, pos );
          pos += divider_height();
        },
        [&]( MenuItem::menu_clickable const& clickable ) {
          auto const& rendered =
              g_menu_item_rendered[clickable.item];
          Texture const* foreground{ nullptr };
          Texture const* background{ nullptr };
          if( clicking && clickable.item == subject ) {
            /**********************************************
            ** Click Animation
            ***********************************************/
            using namespace std::chrono;
            using namespace std::literals::chrono_literals;
            auto now = system_clock::now();
            CHECK(
                holds<MenuState::item_click>( g_menu_state ) );
            auto start =
                std::get<MenuState::item_click>( g_menu_state )
                    .start;
            auto elapsed = now - start;
            using namespace click_anim;
            if( elapsed >= total_duration - fade_time ) {
              // We're in the fade, in which case we should be
              // highlighted, because it seems to make a better
              // UX when the selected item is highlighted as
              // the fading happens.
              foreground = &rendered.highlighted;
              background = &textures.item_background_highlight;
            } else if( elapsed >= total_duration - fade_time -
                                      post_off_time ) {
              // We're in a period between the blinking and
              // fading when the highlight is off, although
              // this period may have zero length.
              foreground = &rendered.normal;

            } else if( ( elapsed % period > half_period ) ^
                       start_on ) {
              // Blink on
              foreground = &rendered.highlighted;
              background = &textures.item_background_highlight;
            } else {
              // Blink off
              foreground = &rendered.normal;
            }
          } else {
            foreground = !is_menu_item_enabled( clickable.item )
                             ? &rendered.disabled
                         : ( clickable.item == subject )
                             ? &rendered.highlighted
                             : &rendered.normal;
            if( clickable.item == subject )
              background = &textures.item_background_highlight;
          }
          if( background ) copy_texture( *background, dst, pos );
          CHECK( foreground );
          copy_texture( *foreground, dst,
                        pos + config_ui.menus.padding - 1_h );
          pos += max_text_height();
        } );
  }
  return dst;
}

void render_menu_bar( Texture& tx ) {
  // Render the "wood" panel. Start from the left edge of the
  // panel so that we get a continuous wood texture between the
  // two. Also, put the y position such that the menu bar gets
  // the bottom portion of the texture, again so that it will be
  // continuous with that panel.
  auto panel_upper_left =
      compositor::section( compositor::e_section::panel )
          .value_or( Rect{} )
          .upper_left();
  auto        bar_rect   = menu_bar_rect();
  auto const& wood       = lookup_sprite( e_tile::wood_middle );
  Coord       start      = panel_upper_left - wood.size().h;
  auto        wood_width = wood.size().w;
  for( Coord c = start; c.x >= 0_x - wood_width;
       c -= wood_width )
    render_sprite( tx, e_tile::wood_middle, c, 0, 0 );
  for( Coord c = start; c.x < bar_rect.right_edge();
       c += wood_width )
    render_sprite( tx, e_tile::wood_middle, c, 0, 0 );

  for( auto menu : visible_menus() ) {
    CHECK( g_menu_rendered.contains( menu ),
           "g_menu_rendered.size(): {}",
           g_menu_rendered.size() );
    auto const& textures = g_menu_rendered[menu];
    using Txs = maybe<pair<Texture const*, Texture const*>>;
    // Given `menu`, this matcher visits the global menu state
    // and returns a foreground/background texture pair for that
    // menu. Use a struct to visit so that we can get recursive
    // visitation.
    struct {
      // These would be the "lambda captures".
      e_menu const&       menu;
      MenuTextures const& textures;

      Txs operator()( MenuState::menus_hidden ) const {
        return Txs{};
      }
      Txs operator()( MenuState::menus_closed closed ) const {
        if( menu == closed.hover )
          return Txs{ pair{ &textures.name.normal,
                            &textures.menu_background_hover } };
        return Txs{ pair{ &textures.name.normal, nullptr } };
      }
      Txs operator()( MenuState::item_click const& ic ) const {
        // Just forward this to the MenuState::menu_open.
        CHECK( g_item_to_menu.contains( ic.item ) );
        return ( *this )( MenuState::menu_open{
            g_item_to_menu[ic.item], /*hover=*/{} } );
      }
      Txs operator()( MenuState::menu_open const& o ) const {
        if( o.menu == menu ) {
          return Txs{
              pair{ &textures.name.highlighted,
                    &textures.menu_background_highlight } };
        } else
          return ( *this )( MenuState::menus_closed{} );
      }
    } matcher{ menu, textures };
    if( auto p = std::visit( matcher, g_menu_state );
        p.has_value() ) {
      auto rect = menu_header_rect( menu );
      if( ( *p ).second )
        copy_texture( *( ( *p ).second ), tx,
                      rect.upper_left() );
      CHECK( ( *p ).first );
      copy_texture(
          *( ( *p ).first ), tx,
          rect.upper_left() + config_ui.menus.padding - 1_h );
    }
  }
}

/****************************************************************
** Input Implementation
*****************************************************************/
maybe<MouseOver_t> click_target( Coord screen_coord ) {
  using res_t = maybe<MouseOver_t>;
  // menu. Use a struct to visit so that we can get recursive
  // visitation.
  struct {
    // These would be the "lambda captures".
    Coord const& screen_coord;

    res_t operator()( MenuState::menus_hidden ) const {
      return res_t{};
    }
    res_t operator()( MenuState::menus_closed ) const {
      for( auto menu : visible_menus() )
        if( screen_coord.is_inside( menu_header_rect( menu ) ) )
          return res_t{ MouseOver::header{ menu } };
      if( screen_coord.is_inside( menu_bar_rect() ) )
        return res_t{ MouseOver::bar{} };
      return res_t( nothing );
    }
    res_t operator()( MenuState::item_click const& ic ) const {
      // Just forward this to the MenuState::menu_open.
      CHECK( g_item_to_menu.contains( ic.item ) );
      return ( *this )( MenuState::menu_open{
          g_item_to_menu[ic.item], /*hover=*/{} } );
    }
    res_t operator()( MenuState::menu_open const& o ) const {
      auto closed = ( *this )( MenuState::menus_closed{} );
      if( closed ) return res_t{ closed };
      if( !screen_coord.is_inside(
              menu_body_clickable_area( o.menu ) ) )
        return res_t( nothing );
      // The cursor is over a non-transparent part of the open
      // menu.
      if( !screen_coord.is_inside(
              menu_body_rect_inner( o.menu ) ) )
        return res_t{ MouseOver::border{ o.menu } };
      // The cursor is over the inner menu body, so at this
      // point its either over an item or a divider.
      auto maybe_item = cursor_to_item( o.menu, screen_coord );
      if( !maybe_item.has_value() )
        return res_t{ MouseOver::divider{ o.menu } };
      // Finally, we are over an item.
      return res_t{ MouseOver::item{ *maybe_item } };
    }
  } matcher{ screen_coord };
  return std::visit( matcher, g_menu_state );
}

/****************************************************************
** Top-level Render Method
*****************************************************************/
void render_menus( Texture& tx ) {
  render_menu_bar( tx );
  // maybe render open menu.
  overload_visit(
      g_menu_state, []( MenuState::menus_hidden ) {},
      [&]( MenuState::menus_closed ) {},
      [&]( MenuState::item_click const& ic ) {
        auto  menu   = g_item_to_menu[ic.item];
        auto& shadow = g_menu_rendered[menu].menu_body_shadow;
        // Just forward this to the MenuState::menu_open.
        CHECK( g_item_to_menu.contains( ic.item ) );
        auto& open_tx =
            render_open_menu( menu, ic.item, /*clicking=*/true );
        Coord   pos   = menu_body_rect( menu ).upper_left();
        uint8_t alpha = 255;
        auto    now   = chrono::system_clock::now();
        auto    start_fade =
            ic.start + ( click_anim::total_duration -
                         click_anim::fade_time );
        auto end = ic.start + click_anim::total_duration;
        if( now >= start_fade ) {
          if( now < end ) {
            double percent_completion =
                double( chrono::duration_cast<
                            std::chrono::milliseconds>(
                            now - start_fade )
                            .count() ) /
                double( chrono::duration_cast<
                            std::chrono::milliseconds>(
                            click_anim::fade_time )
                            .count() );
            CHECK( percent_completion >= 0.0 &&
                       percent_completion <= 1.0,
                   "percent_completion: {}",
                   percent_completion );
            uint8_t alpha_shadow =
                64 - uint8_t( 64.0 * percent_completion );
            copy_texture_alpha( shadow, tx,
                                pos + Delta{ 5_w, 5_h },
                                alpha_shadow );
            alpha = 255 - uint8_t( 255.0 * percent_completion );
            copy_texture_alpha( open_tx, tx, pos, alpha );
          } else if( now >= end ) {
            alpha = 0;
            copy_texture_alpha( open_tx, tx, pos, alpha );
          }
        } else {
          copy_texture_alpha( shadow, tx,
                              pos + Delta{ 5_w, 5_h }, 64 );
          copy_texture( open_tx, tx, pos );
        }
      },
      [&]( MenuState::menu_open const& o ) {
        Coord pos = menu_body_rect( o.menu ).upper_left();

        auto& shadow = g_menu_rendered[o.menu].menu_body_shadow;
        copy_texture_alpha( shadow, tx, pos + Delta{ 5_w, 5_h },
                            64 );
        auto const& open_tx = render_open_menu(
            o.menu, o.hover, /*clicking=*/false );
        pos = menu_body_rect( o.menu ).upper_left();
        copy_texture( open_tx, tx, pos );
      } );
}

/****************************************************************
** Registration
*****************************************************************/
auto& on_click_handlers() {
  static unordered_map<e_menu_item, std::function<void( void )>>
      m;
  return m;
}

auto& is_enabled_handlers() {
  static unordered_map<e_menu_item, std::function<bool( void )>>
      m;
  return m;
}

/****************************************************************
** Initialization
*****************************************************************/
void init_menus() {
  // Check that all menus have descriptors.
  for( auto menu : magic_enum::enum_values<e_menu>() ) {
    CHECK( g_menus.contains( menu ) );
    CHECK( g_menu_def.contains( menu ) );
  }

  // Populate the e_menu_item maps and verify no duplicates.
  for( auto& [menu, items] : g_menu_def ) {
    for( auto& item_desc : items ) {
      if( holds<MenuItem::menu_divider>( item_desc ) ) continue;
      CHECK( holds<MenuItem::menu_clickable>( item_desc ) );
      auto& clickable =
          get<MenuItem::menu_clickable>( item_desc );
      g_items_from_menu[menu].push_back( clickable.item );
      CHECK( !g_menu_items.contains( clickable.item ) );
      g_menu_items[clickable.item] = &clickable;
      CHECK( !g_item_to_menu.contains( clickable.item ) );
      g_item_to_menu[clickable.item] = menu;
    }
  }

  // Check that g_items_from_menu is populated.
  for( auto menu : magic_enum::enum_values<e_menu>() ) {
    CHECK( g_items_from_menu.contains( menu ) );
  }

  // Check that all menus have at least one item.
  for( auto menu : magic_enum::enum_values<e_menu>() ) {
    CHECK( g_items_from_menu[menu].size() > 0 );
  }

  // Check that all menus have unique shortcut keys and that the
  // menu header name contains the shortcut key (in either
  // uppercase or lowercase.
  unordered_set<char> keys;
  for( auto menu : magic_enum::enum_values<e_menu>() ) {
    char key = tolower( g_menus[menu].shortcut );
    CHECK( !keys.contains( key ),
           "multiple menus have `{}` as a shortcut key", key );
    keys.insert( key );
    string_view name = g_menus[menu].name;
    CHECK( name.find( tolower( key ) ) != string_view::npos ||
               name.find( toupper( key ) ) != string_view::npos,
           "menu `{}` does not contain shortcut key `{}`", name,
           key );
  }

  // Check that all e_menu_items are in a menu.
  for( auto item : magic_enum::enum_values<e_menu_item>() ) {
    CHECK( g_menu_items.contains( item ) );
    CHECK( g_menu_items[item] != nullptr );
    CHECK( g_item_to_menu.contains( item ) );
  }

  // Populate Callbacks
  for( auto const& [item, func] : on_click_handlers() ) {
    CHECK( !g_menu_items[item]->callbacks.on_click );
    g_menu_items[item]->callbacks.on_click = func;
  }
  for( auto const& [item, func] : is_enabled_handlers() ) {
    CHECK( !g_menu_items[item]->callbacks.enabled );
    g_menu_items[item]->callbacks.enabled = func;
  }

  // Check that all e_menu_items have registered handlers.
  for( auto item : magic_enum::enum_values<e_menu_item>() ) {
    auto const& desc = *g_menu_items[item];
    CHECK( item == desc.item );
    CHECK( desc.name.size() > 0 );
    CHECK( desc.callbacks.on_click && desc.callbacks.enabled,
           "the menu item `{}` does not have callback handlers "
           "registered",
           item );
  }
}

void cleanup_menus() {
  // This should free all the textures representing the menu
  // items.
  g_menu_rendered.clear();
  g_menu_item_rendered.clear();
}

REGISTER_INIT_ROUTINE( menus );

/****************************************************************
** Handlers (temporary)
*****************************************************************/
function<void( void )> empty_handler = [] {};
function<bool( void )> enabled_true  = [] { return true; };
function<bool( void )> enabled_false = [] { return false; };
function<bool( void )> quit_handler  = [] {
  throw exception_exit{};
  return false;
};

MENU_ITEM_HANDLER( exit, quit_handler, enabled_true );

/****************************************************************
** The Menu Plane
*****************************************************************/
struct MenuPlane : public Plane {
  MenuPlane() = default;
  bool covers_screen() const override { return false; }
  void initialize() override {
    // In this method we do things that initialize global state
    // (not necessarily the menu plane) but we do them here be-
    // cause the actions are complex enough that we need the rest
    // of the game to be initialized first to be on the safe/ro-
    // bust side (though theoretically this could probably be
    // done in the init_menu method).

    // Render Menu and Menu-item names. These have to be done
    // first because other things need to be calculated from the
    // sizes of the rendered text.
    for( auto menu_item :
         magic_enum::enum_values<e_menu_item>() )
      g_menu_item_rendered[menu_item] = render_menu_item_element(
          g_menu_items[menu_item]->name, nothing );
    for( auto menu : magic_enum::enum_values<e_menu>() ) {
      g_menu_rendered[menu]      = {};
      g_menu_rendered[menu].name = render_menu_header_element(
          g_menus[menu].name, g_menus[menu].shortcut );
    }

    for( auto menu : magic_enum::enum_values<e_menu>() ) {
      // The order in which these are done matters,
      // unfortunately, because some of the functions below rely
      // on results from the previous ones.
      g_menu_rendered[menu].header_width =
          g_menu_rendered[menu].name.normal.size().w +
          config_ui.menus.padding * 2_sx;
      g_menu_rendered[menu].item_background_highlight =
          render_item_background( menu, /*hightlight=*/true );
      g_menu_rendered[menu].menu_body =
          create_menu_body_texture( menu );
      g_menu_rendered[menu].menu_background_highlight =
          render_menu_header_background( menu,
                                         /*highlight=*/true,
                                         /*hover=*/false );
      g_menu_rendered[menu].menu_background_hover =
          render_menu_header_background( menu,
                                         /*highlight=*/false,
                                         /*hover=*/true );

      g_menu_rendered[menu].divider = render_divider( menu );

      // This will render to the g_menu_rendered[menu].menu_body
      // texture an open menu with nothing selected. This is
      // necessary so that we can then convert it to a shadow. We
      // only need to render the shadow once, because all that
      // matters about it is the shape, which will not change
      // even as the open menu body changes (i.e., items are
      // highlighted).
      auto const& to_be_shadowed =
          render_open_menu( menu, nothing, false );
      g_menu_rendered[menu].menu_body_shadow =
          create_shadow_texture( to_be_shadowed );
    }
  }
  void advance_state() override {
    if_get( g_menu_state, MenuState::item_click, val ) {
      auto item  = val.item;
      auto start = val.start;
      if( chrono::system_clock::now() - start >=
          click_anim::total_duration ) {
        // FIXME
        // Must set this before calling on_click since the
        // on_click function itself might enter into a frame loop
        // that calls this on_frame_end function, then we would
        // end up calling on_click multiple times.
        g_menu_state = MenuState::menus_closed{ /*hover=*/{} };
        g_menu_items[item]->callbacks.on_click();
      }
    }
  }
  Plane::DragInfo can_drag( input::e_mouse_button button,
                            Coord origin ) override {
    if( !click_target( origin ).has_value() )
      return Plane::e_accept_drag::no;
    // Convert drags to mouse motion events.
    return ( button == input::e_mouse_button::l )
               ? Plane::e_accept_drag::motion
               : Plane::e_accept_drag::swallow;
  }
  void draw( Texture& tx ) const override {
    clear_texture_transparent( tx );
    render_menus( tx );
  }
  e_input_handled input( input::event_t const& event ) override {
    return overload_visit(
        event,
        []( input::unknown_event_t ) {
          return e_input_handled::no;
        },
        []( input::quit_event_t ) {
          return e_input_handled::no;
        },
        []( input::win_event_t ) { return e_input_handled::no; },
        [&]( input::key_event_t const& key_event ) {
          if( holds<MenuState::item_click>( g_menu_state ) )
            // If we are in the middle of a click process then
            // let it finish before handling anymore keys.
            return e_input_handled::no;
          auto menu = opened_menu();
          // `is_alt` decides whether the pressed key is an alt,
          // not whether it is being held down while another key
          // is pressed ( the latter would be
          // keyevent.mod.alt_down).
          bool is_alt = ( key_event.keycode == ::SDLK_LALT ||
                          key_event.keycode == ::SDLK_RALT );
          if( !menu.has_value() &&
              key_event.change == input::e_key_change::down &&
              is_alt ) {
            // Menus are closed and the user has pressed an alt
            // key, so add highlighting to the first menu header
            // that is visible (if any).
            auto maybe_first_menu = first_visible_menu();
            if( maybe_first_menu.has_value() ) {
              g_menu_state = MenuState::menus_closed{
                  /*hover=*/*maybe_first_menu };
              return e_input_handled::yes;
            }
          }
          if( !menu.has_value() &&
              key_event.change == input::e_key_change::up &&
              is_alt ) {
            // Menus are closed and the user is releasing an alt
            // key, so remove any highlighting from the menu
            // headers.
            g_menu_state =
                MenuState::menus_closed{ /*hover=*/{} };
            return e_input_handled::yes;
          }
          if( menu.has_value() &&
              key_event.change == input::e_key_change::down &&
              is_alt ) {
            // Menus are open and the user is pressing an alt
            // key, so close menus.
            g_menu_state =
                MenuState::menus_closed{ /*hover=*/{} };
            return e_input_handled::yes;
          }
          // Check for an alt-shortcut key to open a menu.
          if( key_event.change == input::e_key_change::down &&
              key_event.mod.alt_down ) {
            for( auto menu : visible_menus() ) {
              auto const& menu_desc = g_menus[menu];
              if( key_event.keycode ==
                  tolower( menu_desc.shortcut ) ) {
                if( !is_menu_open( menu ) )
                  g_menu_state =
                      MenuState::menu_open{ menu, /*hover=*/{} };
                else
                  g_menu_state =
                      MenuState::menus_closed{ /*hover=*/menu };
                log_menu_state();
                return e_input_handled::yes;
              }
            }
          }
          if( !menu ) return e_input_handled::no;
          if( key_event.change == input::e_key_change::down ) {
            switch( key_event.keycode ) {
              case ::SDLK_RETURN:
              case ::SDLK_KP_5:
              case ::SDLK_KP_ENTER:
                if_get( g_menu_state, MenuState::menu_open,
                        val ) {
                  if( val.hover.has_value() ) {
                    click_menu_item( val.hover.value() );
                    log_menu_state();
                  }
                }
                return e_input_handled::yes;
              case ::SDLK_ESCAPE:
                g_menu_state = MenuState::menus_closed{ {} };
                log_menu_state();
                return e_input_handled::yes;
              case ::SDLK_KP_4:
              case ::SDLK_LEFT: {
                CHECK( have_some_visible_menus() );
                do {
                  menu = util::find_previous_and_cycle(
                      magic_enum::enum_values<e_menu>(), *menu );
                } while( !is_menu_visible( *menu ) );
                CHECK( menu );
                g_menu_state =
                    MenuState::menu_open{ *menu, /*hover=*/{} };
                log_menu_state();
                return e_input_handled::yes;
              }
              case ::SDLK_KP_6:
              case ::SDLK_RIGHT: {
                CHECK( have_some_visible_menus() );
                do {
                  menu = util::find_subsequent_and_cycle(
                      magic_enum::enum_values<e_menu>(), *menu );
                } while( !is_menu_visible( *menu ) );
                CHECK( menu );
                g_menu_state =
                    MenuState::menu_open{ *menu, /*hover=*/{} };
                log_menu_state();
                return e_input_handled::yes;
              }
              case ::SDLK_KP_2:
              case ::SDLK_DOWN: {
                auto state = std::get<MenuState::menu_open>(
                    g_menu_state );
                if( !state.hover )
                  state.hover = g_items_from_menu[*menu].back();
                auto start = state.hover;
                do {
                  state.hover = util::find_subsequent_and_cycle(
                      g_items_from_menu[*menu], *state.hover );
                  if( state.hover == start )
                    // This is just a safe guard in case somehow
                    // there are no menu items enabled at this
                    // point, even though that should never
                    // happen (it would cause an infinite loop).
                    break;
                } while( !is_menu_item_enabled( *state.hover ) );
                g_menu_state = state;
                log_menu_state();
                return e_input_handled::yes;
              }
              case ::SDLK_KP_8:
              case ::SDLK_UP: {
                auto state = std::get<MenuState::menu_open>(
                    g_menu_state );
                if( !state.hover )
                  state.hover = g_items_from_menu[*menu].front();
                auto start = state.hover;
                do {
                  state.hover = util::find_previous_and_cycle(
                      g_items_from_menu[*menu], *state.hover );
                  if( state.hover == start )
                    // This is just a safe guard in case somehow
                    // there are no menu items enabled at this
                    // point, even though that should never
                    // happen (it would cause an infinite loop).
                    break;
                } while( !is_menu_item_enabled( *state.hover ) );
                g_menu_state = state;
                log_menu_state();
                return e_input_handled::yes;
              }
              default: break;
            }
          }
          return e_input_handled::yes;
        },
        []( input::mouse_wheel_event_t ) {
          return e_input_handled::no;
        },
        []( input::mouse_move_event_t mv_event ) {
          // Remove menu-hover by default and enable it again
          // below if the mouse if over a menu and menus are
          // closed.
          if( holds<MenuState::menus_closed>( g_menu_state ) )
            g_menu_state = MenuState::menus_closed{};
          auto over_what = click_target( mv_event.pos );
          if( !over_what.has_value() )
            return e_input_handled::no;
          struct {
            e_input_handled operator()( MouseOver::bar ) {
              return e_input_handled::yes;
            }
            e_input_handled operator()(
                MouseOver::divider desc ) {
              CHECK(
                  holds<MenuState::menu_open>( g_menu_state ) ||
                  holds<MenuState::item_click>( g_menu_state ) );
              if( holds<MenuState::menu_open>( g_menu_state ) ) {
                g_menu_state = MenuState::menu_open{
                    desc.menu, /*hover=*/{} };
              }
              return e_input_handled::yes;
            }
            e_input_handled operator()(
                MouseOver::header header ) {
              if( holds<MenuState::menu_open>( g_menu_state ) )
                g_menu_state = MenuState::menu_open{
                    header.menu, /*hover=*/{} };
              if( holds<MenuState::menus_closed>(
                      g_menu_state ) )
                g_menu_state = MenuState::menus_closed{
                    /*hover=*/header.menu };
              return e_input_handled::yes;
            }
            e_input_handled operator()( MouseOver::item item ) {
              CHECK(
                  holds<MenuState::menu_open>( g_menu_state ) ||
                  holds<MenuState::item_click>( g_menu_state ) );
              if( holds<MenuState::menu_open>( g_menu_state ) ) {
                auto& o = std::get<MenuState::menu_open>(
                    g_menu_state );
                CHECK( o.menu == g_item_to_menu[item.item] );
                o.hover = {};
                if( is_menu_item_enabled( item.item ) )
                  o.hover = item.item;
              }
              return e_input_handled::yes;
            }
            e_input_handled operator()(
                MouseOver::border border ) {
              // Delegate to the divider handler for now.
              return ( *this )(
                  MouseOver::divider{ border.menu } );
            }
          } matcher;
          return std::visit( matcher, *over_what );
        },
        [&]( input::mouse_button_event_t b_event ) {
          auto over_what = click_target( b_event.pos );
          if( !over_what.has_value() ) {
            if( holds<MenuState::menu_open>( g_menu_state ) ) {
              g_menu_state = MenuState::menus_closed{ {} };
              log_menu_state();
              return e_input_handled::yes; // no click through
            }
            return e_input_handled::no;
          }
          if( b_event.buttons ==
              input::e_mouse_button_event::left_down ) {
            return overload_visit(
                *over_what,
                []( MouseOver::bar ) {
                  g_menu_state = MenuState::menus_closed{ {} };
                  log_menu_state();
                  return e_input_handled::yes;
                },
                []( MouseOver::border ) {
                  return e_input_handled::yes;
                },
                []( MouseOver::divider ) {
                  return e_input_handled::yes;
                },
                []( MouseOver::header header ) {
                  if( !is_menu_open( header.menu ) )
                    g_menu_state = MenuState::menu_open{
                        header.menu, /*hover=*/{} };
                  else
                    g_menu_state = MenuState::menus_closed{
                        /*hover=*/header.menu };
                  log_menu_state();
                  return e_input_handled::yes;
                },
                []( MouseOver::item ) {
                  return e_input_handled::yes;
                } );
          }
          if( b_event.buttons ==
              input::e_mouse_button_event::left_up ) {
            return overload_visit(
                *over_what,
                []( MouseOver::bar ) {
                  g_menu_state = MenuState::menus_closed{ {} };
                  log_menu_state();
                  return e_input_handled::yes;
                },
                []( MouseOver::border ) {
                  return e_input_handled::yes;
                },
                []( MouseOver::divider ) {
                  return e_input_handled::yes;
                },
                []( MouseOver::header ) {
                  return e_input_handled::yes;
                },
                [&]( MouseOver::item item ) {
                  click_menu_item( item.item );
                  return e_input_handled::yes;
                } );
          }
          // `true` here because the mouse is over some menu ui
          // element (and that in turn is because
          // over_what.has_value() == true).
          return e_input_handled::yes;
        },
        []( input::mouse_drag_event_t ) {
          // The framework does not send us mouse drag events
          // directly; instead it uses the api methods on the
          // Plane class.
          SHOULD_NOT_BE_HERE;
          return e_input_handled::no;
        } );
  }

private:
  void click_menu_item( e_menu_item item ) {
    switch( g_menu_state.to_enum() ) {
      case MenuState::e::item_click: {
        // Already clicking, so do nothing. This can happen if a
        // menu item is clicked after it is already in the click
        // animation.
        break;
      }
      case MenuState::e::menus_closed: {
        SHOULD_NOT_BE_HERE;
        break;
      }
      case MenuState::e::menus_hidden: {
        SHOULD_NOT_BE_HERE;
        break;
      }
      case MenuState::e::menu_open: {
        // This check is important even if the code in this
        // module is structured in such a way that this function
        // is only called when the menu item has responded as
        // enabled. This is because it is possible that a menu
        // item might be disabled in the small amount of time
        // (within a frame) that the item is checked for
        // enablement and when this click is called. That's also
        // why we don't call the (memoized) is_menu_item_enabled.
        if( !g_menu_items[item]->callbacks.enabled() ) break;
        lg.info( "selected menu item `{}`", item );
        g_menu_state = MenuState::item_click{
            item, chrono::system_clock::now() };
        log_menu_state();
        break;
      }
    };
  }
};

MenuPlane g_menu_plane;

} // namespace

/****************************************************************
** Public API
*****************************************************************/
void register_menu_item_handler(
    e_menu_item                        item,
    std::function<void( void )> const& on_click,
    std::function<bool( void )> const& is_enabled ) {
  CHECK( on_click );
  CHECK( is_enabled );
  auto& on_clicks   = on_click_handlers();
  auto& is_enableds = is_enabled_handlers();
  CHECK( !on_clicks.contains( item ) );
  CHECK( !is_enableds.contains( item ) );
  on_clicks[item]   = on_click;
  is_enableds[item] = is_enabled;
}

H menu_height() { return menu_bar_height(); }

Plane* menu_plane() { return &g_menu_plane; }

} // namespace rn
