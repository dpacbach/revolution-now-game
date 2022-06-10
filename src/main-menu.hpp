/****************************************************************
**main-menu.hpp
*
* Project: Revolution Now
*
* Created by dsicilia on 2019-10-25.
*
* Description: Main application menu.
*
*****************************************************************/
#pragma once

#include "core-config.hpp"

// Revolution Now
#include "wait.hpp"

// Rds
#include "main-menu.rds.hpp"
#include "plane-stack.rds.hpp"

// C++ standard library
#include <memory>

namespace rn {

struct IGui;
struct MenuPlane;
struct Planes;
struct WindowPlane;

/****************************************************************
** MainMenuPlane
*****************************************************************/
struct MainMenuPlane {
  MainMenuPlane( Planes& planes, e_plane_stack where,
                 WindowPlane& window_plane, IGui& gui );
  ~MainMenuPlane() noexcept;

  wait<> run();

 private:
  Planes&             planes_;
  e_plane_stack const where_;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rn
