// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>

#include "formats/hok.hpp"
#include "game/finances.hpp"

namespace em::ui {

// Native header lights, top to bottom: true selects the green atlas cell.
// budget must describe manager (0..3); manager5 is the national-team view,
// whose last four lights the original explicitly forces green.
// Reads an exported snapshot only; never changes game state or consumes RNG.
std::array<bool, 6> classic_status(const HokFile& snapshot, const ManagerBudget& budget, int manager = 0);

}  // namespace em::ui
