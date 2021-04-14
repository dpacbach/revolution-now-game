/****************************************************************
**frame.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-02-13.
*
* Description: Frames and frame rate.
*
*****************************************************************/
#include "frame.hpp"

// Revolution Now
#include "co-registry.hpp"
#include "compositor.hpp" // FIXME: temporary
#include "config-files.hpp"
#include "input.hpp"
#include "macros.hpp"
#include "moving-avg.hpp"
#include "plane.hpp"
#include "render.hpp" // FIXME
#include "screen.hpp"
#include "time.hpp"
#include "variant.hpp"

// Revolution Now (config)
#include "../config/ucl/rn.inl"

// base
#include "base/function-ref.hpp"
#include "base/lambda.hpp"
#include "base/scope-exit.hpp"
#include "base/variant.hpp"

// C++ standard library
#include <thread>

using namespace std;

namespace rn {

namespace {

MovingAverage frame_rate( chrono::seconds( 3 ) );

EventCountMap g_event_counts;

struct FrameSubscriptionTick {
  bool       done = false; // for one-time notifications
  FrameCount interval{ 1 };
  uint64_t   last_message{ 0 };
  FrameSubscriptionFunc func;
};
NOTHROW_MOVE( FrameSubscriptionTick );

struct FrameSubscriptionTime {
  bool done = false; // for one-time notifications
  chrono::microseconds  interval;
  Time_t                last_message{};
  FrameSubscriptionFunc func;
};
NOTHROW_MOVE( FrameSubscriptionTime );

using FrameSubscription =
    base::variant<FrameSubscriptionTick, FrameSubscriptionTime>;
NOTHROW_MOVE( FrameSubscription );

vector<FrameSubscription>& subscriptions() {
  static vector<FrameSubscription> subs;
  return subs;
}

vector<FrameSubscription>& subscriptions_oneoff() {
  static vector<FrameSubscription> subs;
  return subs;
}

void notify_subscribers() {
  auto try_notify = []( FrameSubscription& sub ) {
    overload_visit(
        sub,
        []( FrameSubscriptionTick& tick_sub ) {
          auto& [done, interval, last_message, func] = tick_sub;
          auto total = total_frame_count();
          if( total - last_message >= interval ) {
            last_message = total;
            func();
            done = true;
          }
        },
        []( FrameSubscriptionTime& time_sub ) {
          auto& [done, interval, last_message, func] = time_sub;
          auto now = Clock_t::now();
          if( now - last_message >= interval ) {
            last_message = now;
            func();
            done = true;
          }
        } );
  };
  for( auto& sub : subscriptions() ) try_notify( sub );
  for( auto& sub : subscriptions_oneoff() ) try_notify( sub );
  erase_if( subscriptions_oneoff(), []( FrameSubscription& fs ) {
    return std::visit( L( _.done ), fs );
  } );
}

using InputReceivedFunc = function_ref<void()>;
using FrameLoopBodyFunc =
    function_ref<void( InputReceivedFunc )>;

void frame_loop_scheduler( waitable<> const& what,
                           FrameLoopBodyFunc body ) {
  static bool guard = false;
  CHECK( !guard, "cannot re-enter frame_loop function." );
  guard = true;
  SCOPE_EXIT( guard = false );

  using namespace chrono;

  auto normal_frame_length =
      1000000us / config_rn.target_frame_rate;
  auto slow_frame_length = 1000000us / 5;

  static auto time_of_last_input = Clock_t::now();

  while( !what ) {
    // If we go more than the configured time without any user
    // input then slow down the frame rate to save battery.
    auto frame_length = ( Clock_t::now() - time_of_last_input >
                          config_rn.power.time_till_slow_fps )
                            ? slow_frame_length
                            : normal_frame_length;

    auto start = system_clock::now();
    frame_rate.tick();
    auto on_input = [] { time_of_last_input = Clock_t::now(); };
    // ----------------------------------------------------------
    body( on_input );
    // ----------------------------------------------------------
    auto delta = system_clock::now() - start;
    if( delta < frame_length )
      this_thread::sleep_for( frame_length - delta );
  }
}

// Called once per frame.
void frame_loop_body( InputReceivedFunc input_received ) {
  // ----------------------------------------------------------
  // 1. Notify

  // This invokes (synchronous/blocking) callbacks to any sub-
  // scribers that want to be notified at regular tick or time
  // intervals.
  notify_subscribers();
  run_all_coroutines();

  // Keep the state of the moving averages up to date even when
  // there are no ticks happening on them. Specifically, if there
  // are no ticks happening, then this will slowly cause the av-
  // erage to drop.
  for( auto& p : g_event_counts ) p.second.update();

  // ----------------------------------------------------------
  // 1. Get Input.
  input::pump_event_queue();

  auto is_win_resize = []( auto const& e ) {
    if_get( e, input::win_event_t, val ) {
      return val.type == input::e_win_event_type::resized;
    }
    return false;
  };

  auto& q = input::event_queue();
  while( q.size() > 0 ) {
    input_received();
    UNWRAP_CHECK( event, q.front() );
    if( is_win_resize( event ) ) on_main_window_resized();
    (void)send_input_to_planes( event );
    q.pop();
    run_all_coroutines();
  }

  // ----------------------------------------------------------
  // 2. Update State.
  advance_plane_state();
  run_all_coroutines();

  // ----------------------------------------------------------
  // 3. Draw.
  draw_all_planes();
  ::SDL_RenderPresent( g_renderer );
};

void deinit_frame() {
  subscriptions().clear();
  subscriptions_oneoff().clear();
}

} // namespace

void subscribe_to_frame_tick( FrameSubscriptionFunc func,
                              FrameCount n, bool repeating ) {
  ( repeating ? subscriptions : subscriptions_oneoff )()
      .push_back( FrameSubscriptionTick{
          .done         = false,
          .interval     = n,
          .last_message = total_frame_count(),
          .func         = func } );
}

void subscribe_to_frame_tick( FrameSubscriptionFunc func,
                              chrono::microseconds  n,
                              bool                  repeating ) {
  ( repeating ? subscriptions : subscriptions_oneoff )()
      .push_back(
          FrameSubscriptionTime{ .done         = false,
                                 .interval     = n,
                                 .last_message = Clock_t::now(),
                                 .func         = func } );
}

waitable<> wait_n_frames( FrameCount n ) {
  if( n == 0_frames ) return make_waitable<>();
  waitable_promise<> p;
  auto after_ticks = [p]() mutable { p.set_value_emplace(); };
  subscribe_to_frame_tick( after_ticks, n, /*repeating=*/false );
  return p.waitable();
}

waitable<chrono::microseconds> wait_for_duration(
    chrono::microseconds us ) {
  if( us == chrono::microseconds{ 0 } )
    return waitable<chrono::microseconds>( 0 );
  waitable_promise<chrono::microseconds> p;
  auto                                   now = Clock_t::now();
  auto after_time = [p, then = now]() mutable {
    p.set_value( duration_cast<chrono::microseconds>(
        Clock_t::now() - then ) );
  };
  subscribe_to_frame_tick( after_time, us, /*repeating=*/false );
  return p.waitable();
}

EventCountMap& event_counts() { return g_event_counts; }

uint64_t total_frame_count() { return frame_rate.total_ticks(); }
double   avg_frame_rate() { return frame_rate.average(); }

void frame_loop( waitable<> const& what ) {
  frame_loop_scheduler( what, frame_loop_body );
  deinit_frame();
}

} // namespace rn
