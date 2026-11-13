// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Knock-out competitions: DEB cup rounds, first-division play-offs and relegation.
//
// How the original game plays them:
//   - a cup day plays every drawn pair once, in the cup match mode; matches
//     without a human team and without display use the quick path (knockout_quick), others the
//     minute loop with overtime 61-70 and a shoot-out
//   - play-off days (calendar code 0x40) run in play-off mode: halved goal points
// From the manual (p. 37):
//   - play-offs of the first division: the first eight, best of five, 1-8, 2-7, 3-6, 4-5; the
//     semi-finals pair the winners of 1-8 / 3-6 and 2-7 / 4-5 (a pairing table in EM.EXE), the
//     higher seed has home ice in games 1, 3 and 5 (assumed)
//   - relegation (play_relegation, kept as an alternative): the original plays groups instead -
//     it puts the 11th/12th of a division and the 1st/2nd of the one below into a
//     group of four with a double round robin (game.cpp); the pair model below is the manual reading:
//     11th and 12th of a division against 1st and 2nd of the one below, home and away;
//     the two better teams play in the higher division (pairs 11-2 and 12-1 assumed, goal
//     aggregate, a draw decided by shoot-out after the second leg)
//   - the first cup draw reserves slots for the human clubs among the 32; later rounds redraw
//     the winners with twenty swaps; the lower division gets home ice first.
#include <array>
#include <vector>

#include "game/match.hpp"
#include "game/scorers.hpp"

namespace em {

// One knock-out game and its result.
struct Tie {
    int home = -1, away = -1;  // team indices
    MatchResult result;        // includes overtime and shoot-out goals
    int winner() const { return result.home > result.away ? home : away; }
};

// HumanSide (the human clubs of a match and their squads) is in match.hpp.

// One cup round: plays every pair once, knock-out.
std::vector<Tie> play_cup_round(const std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw,
                                GameRandom& rng, const HumanSide& human = {},
                                const MatchMinuteObserver* observer = nullptr);  // the WM display: the minute games one after another

// The same round with the cup display on: all pairs minute by minute (see competitions.cpp); updates the computer teams' records.
std::vector<Tie> play_cup_round_live(std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw, GameRandom& rng,
                                     const HumanSide& human, BorlandRandom* brng, const MatchMinuteObserver* observer = nullptr,
                                     int* loop_minutes = nullptr,  // the last minute of the round's minute loop (60..70)
                                     bool playoff = false,  // play-off mode (halved goal points)
                                     size_t knockout_pairs = size_t(-1),  // pairs from this index on play without overtime (relegation round)
                                     int* phantom_role = nullptr,  // set to the role of a phantom release (pair-table write), else unchanged
                                     int level = 0,  // the match level (WM games: 1, see quick_competition_match)
                                     bool ended_breaks = false);  // WM: a managed match over at 60 takes the overtime breaks too

// Draw of a later cup round as the original does it: twenty random swaps of two slots each (a swap may pick the
// same slot twice), then home ice for the lower division.
// places (optional): the clubs in place order after the swaps, before the home rights.
std::vector<HokPairing> draw_cup_round(const std::vector<int>& winners, BorlandRandom& rng, std::vector<int>* places = nullptr);

// First round as the original draws it: 32 entrants from clubs 0..35; every human club is guaranteed a slot.
// The human clubs (at most four, team < 53, no duplicates; otherwise FormatError) first get random free slots,
// then the remaining slots are filled with random clubs 0..35 that are neither human nor already placed.
std::vector<HokPairing> draw_initial_cup(const std::vector<int>& human_teams, BorlandRandom& rng, std::vector<int>* places = nullptr);

// The live draw ("Aber klar"): the n places are uncovered in the order
// rand(0, n - 1), a place already open is drawn again. Returns the places in uncovering order.
std::vector<int> cup_reveal_order(int places, BorlandRandom& rng);

// Pairs a list of clubs two by two and gives the first home game to the lower division.
std::vector<HokPairing> cup_home_rights(const std::vector<int>& pool);

// Team strength as the draws use it: skill 1, skill 2 and form of both kinds, / 6.
int team_strength(const HokTeam& team);

// European cup groups of a new season. Eight European clubs
// (team indices 64-149) are drawn with Borland rand; a club of strength below 70 is kept only
// when rand(0, 3) == 0, a club drawn twice is drawn again. Then each German entrant (the German
// champion, and the European cup winner when that is a German club) takes a random
// slot rand(0, 7) that still holds a European club. Slots 0-3 form group 1, 4-7 group 2.
std::array<std::array<int, 4>, 2> draw_euro_groups(const std::vector<HokTeam>& teams, const std::vector<int>& german,
                                                    BorlandRandom& rng);

// A best-of-five play-off series.
struct Series {
    int higher = -1, lower = -1;  // seeds: higher = better placed
    int wins_higher = 0, wins_lower = 0;
    std::vector<MatchResult> games;  // from the view of the home side of each game
    int winner() const { return wins_higher > wins_lower ? higher : lower; }
};

// One play-off game (minute loop with play-off rules, overtime and shoot-out).
MatchResult playoff_game(const std::vector<HokTeam>& teams, int home, int away, GameRandom& rng,
                         const HumanSide& human = {});

// Best of five (play-off rules: halved goal points, overtime and shoot-out).
Series best_of_five(const std::vector<HokTeam>& teams, int higher, int lower, GameRandom& rng,
                    const HumanSide& human = {});

// The three play-off rounds.
struct PlayoffResult {
    std::vector<Series> quarter, semi;  // quarter: 1-8, 2-7, 3-6, 4-5; semi: winners of 1-8/3-6 and 2-7/4-5
    Series final_series;
    int champion() const { return final_series.winner(); }
};

// Play-offs of the first eight; `order` = team indices sorted by the final table (leader first).
PlayoffResult play_playoffs(const std::vector<HokTeam>& teams, const std::vector<int>& order, GameRandom& rng,
                            const HumanSide& human = {});

// One relegation pairing over two legs (manual model, see above).
struct RelegationTie {
    int upper = -1, lower = -1;  // team of the higher division, team of the lower one
    MatchResult first_leg, second_leg;  // first leg at the lower team, second at the upper team
    int stays_up() const;               // the team that plays in the higher division next season
};

// Relegation between two divisions: 11th/12th of the upper against 1st/2nd of the lower.
std::vector<RelegationTie> play_relegation(const std::vector<HokTeam>& teams, const std::vector<int>& upper_order,
                                           const std::vector<int>& lower_order, GameRandom& rng,
                                           const HumanSide& human = {});

}  // namespace em
