// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/match.hpp"

namespace em {
struct TeamRefreshCounts { int defence=2,attack=3; };
//162C:146C(manager,0), including34F6's current-line count cache.
// No automatic selection, fatigue booking or random draws.
TeamRefreshCounts refresh_menu_team(HokFile&,int manager);
} // namespace em
