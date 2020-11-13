/****************************************************************
**midiseq.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-04-20.
*
* Description: Realtime MIDI Sequencer.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "enum.hpp"

// Rnl
#include "rnl/midiseq.hpp"

// C++ standard library.
#include <string>

namespace rn::midiseq {

// State held by (and updated by) the midi thread.
enum class e_( midiseq_state,
               playing, //
               paused,  //
               stopped, //
               failed,  //
               off      //
);

bool midiseq_enabled();
// We can get state, but not set it. To change the state of the
// midi player you must send it commands.
e_midiseq_state state();

// If playing a tune, returns [0,1.0] progress.
Opt<double> progress();

bool is_processing_commands();

Opt<Duration_t> can_play_tune( fs::path const& path );

void send_command( command_t cmd );

// Testing.
void test();

} // namespace rn::midiseq
