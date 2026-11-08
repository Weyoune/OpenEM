// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Knock-out competitions: DEB cup rounds, first-division play-offs and relegation.
//
// From EM.EXE (see docs/logic/SPIELSIMULATION.md):
//   - a cup day plays every drawn pair once (ovl06_090c -> ovl06_161d with DS:76B2 = 6); matches
//     without a human team and without display use the quick path (knockout_quick), others the
//     minute loop with overtime 61-70 and a shoot-out
//   - play-off days (calendar code 0x40, ovl05_2404) run with DS:76B2 = 1: halved goal points
// From the manual (p. 37), not yet traced in the code:
//   - play-offs of the first division: the first eight, best of five, 1-8, 2-7, 3-6, 4-5; the
//     semi-finals pair the winners of 1-8 / 3-6 and 2-7 / 4-5 (DS:0144), the higher seed
//     has home ice in games 1, 3 and 5 (assumed)
//   - relegation (play_relegation, kept as an alternative): the original plays groups instead -
//     ovl05_0ef7(1) puts the 11th/12th of a division and the 1st/2nd of the one below into a
//     group of four with a double round robin (game.cpp); the pair model below is the manual reading:
//     11th and 12th of a division against 1st and 2nd of the one below, home and away;
//     the two better teams play in the higher division (pairs 11-2 and 12-1 assumed, goal
//     aggregate, a draw decided by shoot-out after the second leg)
//   - ovl05_4c7b reserves human clubs in the 32-slot cup draw; ovl05_536d redraws
//     winners with twenty swaps; ovl05_52bf gives the lower division home ice first.
#include <array>
#include <vector>

#include "game/match.hpp"
#include "game/scorers.hpp"

namespace em {

struct Tie {
    int home = -1, away = -1;  // team indices
    MatchResult result;
    int winner() const { return result.home > result.away ? home : away; }
};

// The human manager's club (or -1) and its squad.
struct HumanSide {
    int team = -1;
    const HumanSquad* squad = nullptr;
    MatchStats* stats = nullptr;
};

// One cup round: plays every pair once, knock-out.
std::vector<Tie> play_cup_round(const std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw,
                                GameRandom& rng, const HumanSide& human = {});

// The same round with the cup display on: all pairs minute by minute (see competitions.cpp); updates the computer teams' records.
std::vector<Tie> play_cup_round_live(std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw, GameRandom& rng,
                                     const HumanSide& human, BorlandRandom* brng);

// Native redraw: twenty random swaps, then home ice for the lower division.
std::vector<HokPairing> draw_cup_round(const std::vector<int>& winners, BorlandRandom& rng);

// Native first round: 32 entrants from clubs 0..35; every human club is guaranteed a slot.
std::vector<HokPairing> draw_initial_cup(const std::vector<int>& human_teams, BorlandRandom& rng);

// ovl05_52bf: pairs a list of clubs two by two and gives the first home game to the lower division.
std::vector<HokPairing> cup_home_rights(const std::vector<int>& pool);

// Team strength as the draws use it (ovl13_0026): skill 1, skill 2 and form of both kinds, / 6.
int team_strength(const HokTeam& team);

// European cup groups of a new season (ovl05_4c7b(1), then ovl05_0ef7(2)). Eight European clubs
// (team indices 64-149) are drawn with Borland rand; a club of strength below 70 is kept only
// when rand(0, 3) == 0, a club drawn twice is drawn again. Then each German entrant (the list of
// ovl05_51a5: champion DS:D0E6, the European cup winner DS:0388 when German) takes a random
// slot rand(0, 7) that still holds a European club. Slots 0-3 form group 1, 4-7 group 2.
std::array<std::array<int, 4>, 2> draw_euro_groups(const std::vector<HokTeam>& teams, const std::vector<int>& german,
                                                    BorlandRandom& rng);

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

struct PlayoffResult {
    std::vector<Series> quarter, semi;
    Series final_series;
    int champion() const { return final_series.winner(); }
};

// Play-offs of the first eight; `order` = team indices sorted by the final table (leader first).
PlayoffResult play_playoffs(const std::vector<HokTeam>& teams, const std::vector<int>& order, GameRandom& rng,
                            const HumanSide& human = {});

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
