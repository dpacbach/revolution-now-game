/****************************************************************
**game-ui-views.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-12-13.
*
* Description: Contains high-level game-specific UI Views.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "id.hpp"
#include "macros.hpp"
#include "unit.hpp"
#include "views.hpp"

// Rds
#include "rds/game-ui-views.hpp"

// C++ standard library
#include <memory>
#include <unordered_map>
#include <vector>

namespace rn::ui {

/****************************************************************
** UnitActivationView
*****************************************************************/
class UnitActivationView final : public CompositeSingleView {
 public:
  using map_t = std::unordered_map<UnitId, UnitActivationInfo>;

  // Preferred way to create.
  static std::unique_ptr<UnitActivationView> Create(
      std::vector<UnitId> const& ids_, bool allow_activation );

  UnitActivationView( bool allow_activation );

  // Implement CompositeView
  void notify_children_updated() override {}

  map_t& info_map() { return info_map_; }

 private:
  void on_click_unit( UnitId id );

  bool  allow_activation_{};
  map_t info_map_;
};

} // namespace rn::ui
