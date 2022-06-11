/****************************************************************
**game.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-05-11.
*
* Description: Overall game flow of an individual game.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "wait.hpp"

namespace rn {

struct IGui;
struct Planes;

// Run through the entire sequence of starting a new game and
// playing it.
wait<> run_new_game( Planes& planes );

// Run through the sequence of asking the user which game to load
// and then loading it and playing it.
wait<> run_existing_game( Planes& planes );

} // namespace rn
