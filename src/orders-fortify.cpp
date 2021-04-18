/****************************************************************
**orders-fortify.cpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2021-04-16.
*
* Description: Carries out orders to fortify or sentry a unit.
*
*****************************************************************/
#include "orders-fortify.hpp"

// Revolution Now
#include "ustate.hpp"
#include "waitable-coro.hpp"
#include "window.hpp"

using namespace std;

namespace rn {

namespace {

struct FortifyHandler : public OrdersHandler {
  FortifyHandler( UnitId unit_id_ ) : unit_id( unit_id_ ) {}

  waitable<bool> confirm() override {
    if( unit_from_id( unit_id ).desc().ship ) {
      co_await ui::message_box( "Ships cannot be fortified." );
      co_return false;
    }
    if( is_unit_onboard( unit_id ) ) {
      co_await ui::message_box(
          "Cannot fortify while on a ship." );
      co_return false;
    }
    co_return true;
  }

  waitable<> perform() override {
    Unit& unit = unit_from_id( unit_id );
    unit.forfeight_mv_points();
    unit.fortify();
    co_return;
  }

  UnitId unit_id;
};

struct SentryHandler : public OrdersHandler {
  SentryHandler( UnitId unit_id_ ) : unit_id( unit_id_ ) {}

  waitable<bool> confirm() override { co_return true; }

  waitable<> perform() override {
    unit_from_id( unit_id ).sentry();
    co_return;
  }

  UnitId unit_id;
};

} // namespace

/****************************************************************
** Public API
*****************************************************************/
std::unique_ptr<OrdersHandler> handle_orders(
    UnitId id, orders::fortify const& fortify ) {
  return make_unique<FortifyHandler>( id );
}

std::unique_ptr<OrdersHandler> handle_orders(
    UnitId id, orders::sentry const& sentry ) {
  return make_unique<SentryHandler>( id );
}

} // namespace rn