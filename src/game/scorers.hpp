// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Goal scorers and assists: the Borland random number generator used throughout the game, the
// scorer/assist choice for a human manager's team, and the statistics of computer clubs.
//
// Random numbers: the Borland C runtime rand() (seed = seed * 22695477 + 1, value =
// (seed >> 16) & 0x7FFF) through the game's helper rand(lo, hi) = lo + rand() % (hi - lo + 1).
//
// Picking a player (kind 0 = scorer / 1 = assist): up to 250 tries of
//   slot = rand(0, squad size - 1); skip goalkeepers (position value 0) and players without a
//   role (1..11); when the line on the ice is tracked, only players of that line
//   weight = skill2 * 3 / (5 - 3 * kind) + skill1 / 5 + form / 5   (for a non-goal scorer pick:
//            rand(120, 170) instead)
//          + natural position (position value / 25, at most 2) * (72 - 17 * kind)
//          + goals this season in the league (squad byte +0x03) * 46
//          + 700 for forwards (block bit 0x80)
//   accepted if rand(0, 3000) < weight; after 250 misses the first skater of the squad.
// Booking: scorer = pick(0, goal), assist = pick(1, 1) until it differs; on a goal
// the assist gets +1 in the player record (+0x1F, u16), the scorer +1 league goal in the player
// record (+0x1B, league only) and in the squad record (+0x22 + 2 * competition,
// +0x03 + competition).
#include <cstdint>
#include <vector>

#include "formats/hok.hpp"

namespace em {

// Test/debug hook: when set, every BorlandRandom::range call appends (lo, hi), so the sequence of
// draws can be checked in tests.
extern std::vector<std::pair<long, long>>* g_borland_log;
extern std::vector<const char*>* g_borland_scopes;  // parallel to g_borland_log: the DrawScope active at each draw
extern const char* g_draw_scope;                    // name of the current DrawScope ("" outside any)
// RAII label for the code section that draws random numbers (used by tests and debug traces).
struct DrawScope {
    const char* prev;
    explicit DrawScope(const char* name) : prev(g_draw_scope) { g_draw_scope = name; }
    ~DrawScope() { g_draw_scope = prev; }
};

// The Borland C runtime linear congruential generator, as used by the original game.
class BorlandRandom {
public:
    explicit BorlandRandom(uint32_t seed = 1) : seed_(seed) {}
    int rand();                      // 0..32767
    int range(int lo, int hi);       // the game's rand(lo, hi), inclusive; swapped bounds are allowed
    uint32_t state() const { return seed_; }  // the raw 32-bit seed

private:
    uint32_t seed_;
};

// Picks a scorer (kind 0) or assist (kind 1) as described above; `goal` false selects the
// rand(120, 170) weight for a scorer pick, `on_ice` (if given) restricts the choice to the lines
// on the ice. Returns the index into `squad` of the chosen player, or -1 if there is none.
int pick_player(const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int kind,
                bool goal, BorlandRandom& rng, const HokTeam* on_ice = nullptr);

// Result of credit_goal.
struct GoalCredit {
    int scorer = -1, assist = -1;  // indices into the squad, -1 = none
};

// A goal (or, with `goal` false, a chance) of the human team: picks scorer and assist and books the
// goal into the squad (competition 0 league/WM/play-offs, 1 DEB cup, 2 EuroCup), capped at 255.
// Squads with fewer than 4 players get no credit. A shoot-out still chooses both players (the
// assist pick consumes its draws as in the original), but does not increment the normal goal
// fields. The copy of the player records is left to the caller.
GoalCredit credit_goal(std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players,
                       BorlandRandom& rng, int competition = 0, bool shootout = false, bool goal = true,
                       const HokTeam* on_ice = nullptr);


// Scorers of a computer club, run for both teams of every league pairing and of the European cup
// pairings; DEB cup games are not counted. Arguments: own goals, goals against. Clubs with a
// manager are not booked here (this function does not check that itself; the caller must skip
// them). Nothing happens when the club scored no goal (then not even the games are counted,
// apart from the goalkeeper booking below). Otherwise:
//   - every player of the club (player +0x1D) gets a game (+0x1C); a skater joins the shooters
//     when position value + rand(5, 25) > rand(10, 99) (at most 50)
//   - with fewer than two shooters no goal is credited; else for every goal, k = min(n, 10),
//     one less above 2: with rand(0, 4) == 0 the goal stays without scorer;
//     else, while rand(0, k) != 0, a random shooter gets the goal (+0x1B, at most 255) and the
//     next draw the assist (+0x1F, another shooter); a rand(0, k) == 0 ends the goal unless
//     k > 0 and n > rand(0, 1) + 2
// In league games the goalkeepers are booked first (also with no own goal), for clubs below 64: the first goalkeeper
// of the club (up to three are collected) gets the goals against (+0x1B, while it stays <= 255)
// and 60 minutes (+0x1F u16); if there are two and rand(0, 8) == 0 the first gets rand(0, 100) %
// of the goals and 60 * rand(2, 100) / 100 minutes, the second the rest.
void credit_club_scorers(std::vector<HokPlayer>& players, Bytes& state, int team, int goals, int goals_against,
                         bool league, BorlandRandom& rng);

}  // namespace em
