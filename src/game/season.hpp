// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// League matchdays: pairings (schedule.hpp) + match results (match.hpp) booked into the league
// table as the original does.
//
// Booking (table rows, see hok.hpp 0x3BAC): goals for / against, games,
// wins and losses are counted per side (home / away); the form string keeps the last 8 results,
// newest on the right: 's'/'u'/'n' for a home win / draw / loss, 'S'/'U'/'N' away (manual p. 29).
// Order: points (2 per win, 1 per draw), fewer games, goal difference, goals scored; like the
// original, an exchange sort (not a stable sort) starts from the saved ranking permutation, so
// ties are resolved exactly as in the original.
#include <array>
#include <vector>

#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/schedule.hpp"

namespace em {

// The table of one league (12 teams).
struct League {
    int first_team = 0;                       // team index of slot 0 (league * 12)
    std::array<HokTableRow, kHokLeagueSize> rows{};  // indexed by slot (team - first_team)
    // The ranking permutation (slots, leader first) as saved at state 0x8EDC. It is independent of
    // the cached position byte in each row (which can still be stale at season start).
    std::array<int, kHokLeagueSize> ranking = [] {
        std::array<int, kHokLeagueSize> order{};
        for (int i = 0; i < kHokLeagueSize; i++) order[size_t(i)] = i;
        return order;
    }();
};

// Copies the table of one league (0 = first division) out of a savegame. The saved ranking is used
// if it is a valid permutation of the league's slots, otherwise the identity order.
League league_from_save(const HokFile& save, int league);

// Books one result for the slots home / away (0..11).
void book_result(League& league, int home, int away, MatchResult result);
// the same booking on two table rows (form letters lower case at home, goals, games, wins, losses); null rows are skipped
void book_result_rows(HokTableRow* home, HokTableRow* away, MatchResult result);

// Sorts the league (see the order above) and recomputes HokTableRow::position (0 = leader) and
// League::ranking.
void sort_league(League& league);

// The table position after `matchday` (0..44) for the fever curve on the desk: a packed nibble at
// row +0x2D + matchday/2. Matchday 44 deliberately reads the low nibble of the penalty-minutes
// field that follows, as the original does. Throws FormatError outside 0..44.
int fever_curve_position(const HokTableRow& row, int matchday);

// One league matchday to be played on the current day.
struct LeagueDay {
    League* league;
    int matchday;  // 0-based
};
// Several league matchdays of one day, played minute by minute across all matches (the original
// interleaves them: minute 1 of every match, then minute 2, ...); returns the results per entry and
// books them like play_matchday. `humans` gives the managers' squads and statistics, `brng` the
// Borland generator for the reseeding draws after the 20th and 40th minute, `observer` the live
// match display. `played` (optional) receives home/away of every pair as scheduled. The original's
// pair-table error (a known deviation that is reproduced, see the .cpp) can replace teams in the
// Oberliga pairs during the day.
std::vector<std::vector<MatchResult>> play_matchdays(const std::vector<LeagueDay>& days, std::vector<HokTeam>& teams,
                                                     const ScheduleTables& tables, GameRandom& rng, const HumanSide& humans = {},
                                                     BorlandRandom* brng = nullptr,
                                                     const MatchMinuteObserver* observer = nullptr,
                                                     std::vector<std::vector<std::pair<int, int>>>* played = nullptr);

// Plays one league matchday (0-based) and books it; the club `human_team` (team index) plays with
// the line strengths of its squad (`human`), all others as computer teams. Penalty minutes go into
// the rows, the table is sorted and the fever curve entry of the matchday is set.
std::vector<MatchResult> play_matchday(League& league, const std::vector<HokTeam>& teams,
                                       const ScheduleTables& tables, int matchday, GameRandom& rng,
                                       const HumanSquad* human = nullptr, int human_team = -1,
                                       MatchStats* human_stats = nullptr);

}  // namespace em
