// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// League matchdays: pairings (schedule.hpp) + quick results (match.hpp) booked into the league
// table like ovl13_2035 does.
//
// Booking (ovl13_2035, table rows at DS:825E, see hok.hpp 0x3BAC): goals for / against, games,
// wins and losses are counted per side (home / away); the form string keeps the last 8 results,
// newest on the right: 's'/'u'/'n' for a home win / draw / loss, 'S'/'U'/'N' away (manual p. 29).
// Order: points (2 per win, 1 per draw), fewer games, goal difference, goals scored;
// native non-adjacent exchange sort starts from the saved ranking permutation.
#include <array>
#include <vector>

#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/schedule.hpp"

namespace em {

struct League {
    int first_team = 0;                       // team index of slot 0 (league * 12)
    std::array<HokTableRow, kHokLeagueSize> rows{};
    // Native DS:5CCB permutation is independent of the cached position byte in
    // each row (which can still be stale at season start).
    std::array<int, kHokLeagueSize> ranking = [] {
        std::array<int, kHokLeagueSize> order{};
        for (int i = 0; i < kHokLeagueSize; i++) order[size_t(i)] = i;
        return order;
    }();
};

// Copies the table of one league (0 = first division) out of a savegame.
League league_from_save(const HokFile& save, int league);

// Books one result for the slots home / away (0..11).
void book_result(League& league, int home, int away, MatchResult result);

// Recomputes HokTableRow::position (0 = leader).
void sort_league(League& league);

// ovl10_0a9d: packed nibble at row +0x2D + matchday/2. Day 44
// deliberately reaches the following native penalty-minutes byte.
int fever_curve_position(const HokTableRow& row, int matchday);

// Plays one league matchday (0-based) and books it; the club `human_team` (team index) plays with
// the line strengths of its squad (`human`), all others as computer teams.
std::vector<MatchResult> play_matchday(League& league, const std::vector<HokTeam>& teams,
                                       const ScheduleTables& tables, int matchday, GameRandom& rng,
                                       const HumanSquad* human = nullptr, int human_team = -1,
                                       MatchStats* human_stats = nullptr);

}  // namespace em
