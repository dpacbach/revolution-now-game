--[[ ------------------------------------------------------------
|
| global.lua
|
| Project: Revolution Now
|
| Created by dsicilia on 2021-04-09.
|
| Description: Globals.
|
--]] ------------------------------------------------------------
-- Global function
function grid() terrain.toggle_grid() end

-- This module does not export anything since its purpose is to
-- create globals.
package_exports = {}