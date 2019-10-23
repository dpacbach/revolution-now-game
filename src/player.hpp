/****************************************************************
**player.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-08-29.
*
* Description: Data structure representing a human or AI player.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "aliases.hpp"
#include "fb.hpp"
#include "nation.hpp"
#include "sg-macros.hpp"

// Flatbuffers
#include "fb/player_generated.h"

// base-util
#include "base-util/non-copyable.hpp"

// Abseil
#include "absl/types/span.h"

namespace rn {

DECLARE_SAVEGAME_SERIALIZERS( Player );

class Player : public util::movable_only {
public:
  Player() = default; // for serialization framework.
  Player( e_nation nation, bool is_human, int money );
  e_nation nation() const { return nation_; }
  int      money() const { return money_; }
  bool     is_human() const { return human_; }

  expect<> check_invariants_safe() const {
    return xp_success_t{};
  }

private:
  // clang-format off
  SERIALIZABLE_TABLE_MEMBERS( fb, Player,
  ( e_nation, nation_ ),
  ( bool,     human_ ),
  ( int,      money_ ));
  // clang-format on
};

Player& player_for_nation( e_nation nation );

void set_players( Vec<e_nation> const& nations );

} // namespace rn
