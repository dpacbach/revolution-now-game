/****************************************************************
**cargo.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2018-09-08.
*
* Description: Handles the cargo that a unit may carry.
*
*****************************************************************/
#include "cargo.hpp"

// Revolution Now
#include "errors.hpp"
#include "logging.hpp"
#include "ownership.hpp"
#include "util.hpp"

// base-util
#include "base-util/algo.hpp"
#include "base-util/variant.hpp"

// Range-v3
#include "range/v3/view/group_by.hpp"
#include "range/v3/view/iota.hpp"
#include "range/v3/view/map.hpp"

// C++ standard library
#include <type_traits>

using namespace std;

namespace rn {

using util::holds;

namespace {

constexpr int const k_max_commodity_cargo_per_slot = 100;

} // namespace

string CargoHold::debug_string() const {
  return fmt::format( "{}", FmtJsonStyleList{slots_} );
}

void CargoHold::check_invariants() const {
  // 0. Accurate reporting of number of slots.
  CHECK( slots_total() == int( slots_.size() ) );
  // 1. First slot is not an `overflow`.
  CHECK( !holds<CargoSlot::overflow>( slots_[0] ) );
  // 2. There are no `overflow`s following `empty`s.
  for( int i = 0; i < slots_total() - 1; ++i )
    if( holds<CargoSlot::empty>( slots_[i] ) )
      CHECK( !holds<CargoSlot::overflow>( slots_[i + 1] ) );
  // 3. There are no `overflow`s following `commodity`s.
  for( int i = 0; i < slots_total() - 1; ++i ) {
    if_v( slots_[i], CargoSlot::cargo, cargo ) {
      if( holds<Commodity>( cargo->contents ) )
        CHECK( !holds<CargoSlot::overflow>( slots_[i + 1] ) );
    }
  }
  // 4. Commodities don't exceed max quantity.
  for( auto const& slot : slots_ )
    if_v( slot, CargoSlot::cargo, cargo )
        if_v( cargo->contents, Commodity, commodity )
            CHECK( commodity->quantity <=
                   k_max_commodity_cargo_per_slot );
  // 5. Units with overflow are properly followed by `overflow`.
  for( int i = 0; i < slots_total(); ++i ) {
    auto const& slot = slots_[i];
    if_v( slot, CargoSlot::cargo, cargo ) {
      if_v( cargo->contents, UnitId, unit_id ) {
        auto const& unit = unit_from_id( *unit_id );
        auto        occupies =
            unit.desc().cargo_slots_occupies.value_or( 0 );
        CHECK( occupies > 0 );
        // Check for overflow slots.
        while( occupies > 1 ) {
          --occupies;
          ++i;
          CHECK( i < slots_total() );
          CHECK( holds<CargoSlot::overflow>( slots_[i] ) );
        }
      }
    }
  }
  // 6. Slots occupied matches real contents.
  int occupied = 0;
  for( int i = 0; i < slots_total(); ++i ) {
    auto const& slot = slots_[i];
    switch_( slot ) {
      case_( CargoSlot::empty ) {}
      case_( CargoSlot::overflow ) {}
      case_( CargoSlot::cargo, contents ) {
        switch_( contents ) {
          case_( UnitId ) {
            occupied += unit_from_id( val )
                            .desc()
                            .cargo_slots_occupies.value_or( 0 );
          }
          case_( Commodity ) { occupied++; }
          switch_exhaustive;
        }
      }
      switch_exhaustive;
    }
  }
  CHECK( occupied == slots_occupied() );
}

CargoHold::~CargoHold() {
  if( count_items() != 0 )
    lg.warn( "CargoHold destroyed with {} remaining items.",
             count_items() );
}

int CargoHold::slots_occupied() const {
  return slots_total() - slots_remaining();
}

int CargoHold::slots_remaining() const {
  return util::count_if( slots_,
                         L( holds<CargoSlot::empty>( _ ) ) );
}

int CargoHold::slots_total() const { return slots_.size(); }

int CargoHold::count_items() const {
  return util::count_if( slots_,
                         L( holds<CargoSlot::cargo>( _ ) ) );
}

CargoSlot_t const& CargoHold::operator[]( int idx ) const {
  CHECK( idx >= 0 && idx < int( slots_.size() ) );
  return slots_[idx];
}

Opt<int> CargoHold::find_unit( UnitId id ) const {
  auto is_unit = [id]( CargoSlot_t const& slot ) {
    if_v( slot, CargoSlot::cargo, cargo ) {
      if_v( cargo->contents, UnitId, unit_id ) {
        return id == *unit_id;
      }
    }
    return false;
  };
  return util::find_first_if( slots_, is_unit );
}

// Returns all units in the cargo.
Vec<UnitId> CargoHold::units() const {
  Vec<UnitId> res;
  for( auto unit_id_ref : items_of_type<UnitId>() )
    res.push_back( unit_id_ref.get() );
  return res;
}

// Returns all commodities (and slot indices) in the cargo un-
// less a specific type is specified in which case it will be
// limited to those.
Vec<Pair<Commodity, int>> CargoHold::commodities(
    Opt<e_commodity> type ) {
  Vec<Pair<Commodity, int>> res;

  int idx = 0;
  for( auto comm_ref : items_of_type<Commodity>() ) {
    if( !type || ( comm_ref.get().type == *type ) )
      res.emplace_back( comm_ref.get(), idx );
    idx++;
  }
  return res;
}

void CargoHold::compactify() {
  auto           unit_ids    = units();
  auto           comms_pairs = commodities();
  Vec<Commodity> comms       = comms_pairs | rv::keys;
  // negative to do reverse sort.
  util::sort_by_key(
      unit_ids,
      L( -unit_from_id( _ ).desc().cargo_slots_occupies.value_or(
          0 ) ) );
  util::sort_by_key( comms, L( _.type ) );
  for( UnitId id : unit_ids )
    CHECK( try_add_first_available( id ) );
  auto like_types =
      comms | rv::group_by( L2( _1.type == _2.type ) );
  for( auto group : like_types ) {
    auto           type = group.begin()->type;
    Vec<Commodity> new_comms;
    new_comms.push_back( Commodity{/*type=*/type,
                                   /*quantity=*/0} );
    for( Commodity const& comm : group ) {
      auto delta = k_max_commodity_cargo_per_slot -
                   ( new_comms.back().quantity + comm.quantity );
      if( delta >= 0 )
        new_comms.back().quantity += comm.quantity;
      else {
        new_comms.back().quantity =
            k_max_commodity_cargo_per_slot;
        new_comms.push_back( Commodity{/*type=*/type,
                                       /*quantity=*/-delta} );
      }
    }
    for( auto const& comm : new_comms )
      CHECK( try_add_first_available( comm ) );
  }
  check_invariants();
}

bool CargoHold::fits( Cargo const& cargo, int idx ) const {
  CHECK( idx >= 0 && idx < int( slots_.size() ) );
  return matcher_( cargo ) {
    case_( UnitId ) {
      auto maybe_occupied =
          unit_from_id( val ).desc().cargo_slots_occupies;
      if( !maybe_occupied )
        // Unit cannot be held as cargo.
        result_ false;
      auto occupied = *maybe_occupied;
      // Check that all needed slots are `empty`.
      for( int i = idx; i < idx + occupied; ++i ) {
        if( i >= slots_total() )
          // Not enough slots left.
          result_ false;
        if( !holds<CargoSlot::empty>( slots_[i] ) )
          // Needed slots are not empty.
          result_ false;
      }
      result_ true;
    }
    case_( Commodity ) {
      auto const& proposed = val;
      if( proposed.quantity > k_max_commodity_cargo_per_slot )
        result_ false;
      result_ matcher_( slots_[idx] ) {
        case_( CargoSlot::overflow ) result_ false;
        case_( CargoSlot::empty ) result_ true;
        case_( CargoSlot::cargo ) {
          result_ matcher_( val.contents ) {
            case_( UnitId ) result_ false;
            case_( Commodity ) {
              if( proposed.type != val.type ) //
                result_ false;
              result_( val.quantity + proposed.quantity <=
                       k_max_commodity_cargo_per_slot );
            }
            matcher_exhaustive;
          }
        }
        matcher_exhaustive;
      }
    }
    matcher_exhaustive;
  }
}

Vec<int> CargoHold::find_fit( Cargo const& cargo ) const {
  Vec<int> res;
  res.reserve( slots_.size() );
  for( int idx : rv::ints( 0, slots_total() ) )
    if( fits( cargo, idx ) ) //
      res.push_back( idx );
  return res;
}

bool CargoHold::try_add_first_available( Cargo const& cargo ) {
  for( int idx : rv::ints( 0, slots_total() ) )
    if( try_add( cargo, idx ) ) //
      return true;
  return false;
}

bool CargoHold::try_add( Cargo const& cargo, int idx ) {
  if( !fits( cargo, idx ) ) return false;
  if_v( cargo, UnitId, id ) {
    // Make sure that the unit is not already in this cargo.
    auto units = items_of_type<UnitId>();
    CHECK( util::count_if( units, LC( _.get() == *id ) ) == 0 );
  }
  // From here on we assume it is totally safe in every way to
  // blindly add this cargo into the given slot(s).
  auto was_added = matcher_( cargo ) {
    case_( UnitId ) {
      auto maybe_occupied =
          unit_from_id( val ).desc().cargo_slots_occupies;
      if( !maybe_occupied ) result_ false;
      auto occupied = *maybe_occupied;
      slots_[idx]   = CargoSlot::cargo{/*contents=*/cargo};
      // Now handle overflow.
      while( idx++, occupied-- > 1 )
        slots_[idx] = CargoSlot::overflow{};
      result_ true;
    }
    case_( Commodity ) {
      if( holds<CargoSlot::empty>( slots_[idx] ) )
        slots_[idx] = CargoSlot::cargo{/*contents=*/cargo};
      else {
        GET_CHECK_VARIANT( cargo, slots_[idx],
                           CargoSlot::cargo );
        GET_CHECK_VARIANT( comm, cargo.contents, Commodity );
        CHECK( comm.type == val.type );
        comm.quantity += val.quantity;
      }
      result_ true;
    }
    matcher_exhaustive;
  }
  check_invariants();
  return was_added;
}

void CargoHold::remove( int idx ) {
  CHECK( idx >= 0 && idx < int( slots_.size() ) );
  CHECK( holds<CargoSlot::cargo>( slots_[idx] ) );
  slots_[idx] = CargoSlot::empty{};
  idx++;
  while( idx < int( slots_.size() ) &&
         holds<CargoSlot::overflow>( slots_[idx] ) ) {
    slots_[idx] = CargoSlot::empty{};
    ++idx;
  }
  check_invariants();
}

} // namespace rn
