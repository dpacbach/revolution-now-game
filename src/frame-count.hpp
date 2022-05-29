/****************************************************************
**frame-count.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-04-04.
*
* Description: Defines a strong int representing a frame count.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "typed-int.hpp"
#include "wait.hpp"

// Strongly-typed integer for representing frame counts.
TYPED_INT( FrameCount, frames );

namespace rn {

// This allows co_await'ing directly on a frame count.
//
//   co_await 5_frames.
//
wait<> co_await_transform( FrameCount count );

// The returned wait becomes ready after `n` frames have passed.
//
// Note: instead of co_await'ing this directly, you can do:
//
//   co_await 5_frames.
//
wait<> wait_n_frames( FrameCount n );

} // namespace rn
