// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>

#include "formats/hok.hpp"
#include "game/finances.hpp"

// The six status lights in the main-menu header (green = all right, red = attention).

namespace em::ui {

// The header lights, top to bottom: 0 line-up complete (a goalkeeper and five skaters in each of the first
// three lines), 1 no pending interest of another club in one of the manager's players (player byte +0x1E,
// own squad and own players in the transfer pool), 2 no cash shortage while assets exist, 3 no paid recovery
// training open, 4 every advertising slot sold, 5 shares worth at least their cost. true selects the green
// sprite. budget must describe manager (0..3); manager 5 is the national-team view, whose last four lights
// the original explicitly forces green. Reads an exported snapshot only; never changes game state or
// consumes random numbers. Throws FormatError for manager 4 or out of range.
std::array<bool, 6> classic_status(const HokFile& snapshot, const ManagerBudget& budget, int manager = 0);

}  // namespace em::ui
