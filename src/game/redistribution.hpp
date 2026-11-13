// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Redistribution of free players: players nobody owns are moved to random computer teams and
// renamed after that team's roster in the player database (KB\23.vga). Called by
// Game::redistribute_players (game/game.cpp).

#include <functional>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// Every free player (owner +0x1A = 6) of the range selected by `mode` (0: national team players, 1: club players)
// whose team is not held by a human manager gets a random team and a new name from that team's roster in KB\23.vga.
// `manager_team(team)` tells whether a human manager holds the team. Returns the number of renamed players.
int redistribute_free_players(HokFile& save, const Database& db, BorlandRandom& rng, int mode,
                              const std::function<bool(int)>& manager_team);

}  // namespace em
