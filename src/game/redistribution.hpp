// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <functional>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// ovl16_0480(mode): every free player (owner +0x1A = 6) of a range whose team is a computer club gets a random team and a
// new name from that team's roster in KB/23.vga (see game.cpp, Game::redistribute_players). `manager_team(team)` tells
// whether a human manager holds the team. Returns the number of renamed players.
int redistribute_free_players(HokFile& save, const Database& db, BorlandRandom& rng, int mode,
                              const std::function<bool(int)>& manager_team);

}  // namespace em
