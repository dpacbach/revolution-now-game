/****************************************************************
* Music
*****************************************************************/
#ifndef MUSIC_INL
#define MUSIC_INL

#include "../../src/conductor.hpp"

namespace rn {

// This is to avoid commas in macro arguments.
using SpecialMusicEventMap =
    absl::flat_hash_map<e_special_music_event, std::string>;

CFG( music,
  FLD( fs::path, midi_folder )
  FLD( fs::path, ogg_folder )

  FLD( e_music_player, first_choice_music_player )
  FLD( e_music_player, second_choice_music_player )

  FLD( Seconds, threshold_previous_tune_secs )

  FLD( std::vector<Tune>, tunes )

  FLD( SpecialMusicEventMap, special_event_tunes )

  FLD( bool, autoplay )
  FLD( double, initial_volume )
)

}

#endif
