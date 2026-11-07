// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Goal scorers and assists of a human manager's team (ovl14_2349 with ovl06_3191).
//
// Random numbers: the Borland RTL rand() (1000:06df: seed = seed * 22695477 + 1, value =
// (seed >> 16) & 0x7FFF) through 199f:0847(lo, hi) = lo + rand() % (hi - lo + 1).
//
// Picking a player, ovl06_3191(manager, kind 0 = scorer / 1 = assist, goal): up to 250 tries of
//   slot = rand(0, squad size - 1); skip goalkeepers (position value 0) and players without a
//   role (1..11); with manual line control (ovl14_0d01) only the line on the ice
//   weight = skill2 * 3 / (5 - 3 * kind) + skill1 / 5 + form / 5   (for a non-goal scorer pick:
//            rand(120, 170) instead)
//          + natural position (0..2, FUN_1238_0000) * (72 - 17 * kind)
//          + goals this season in the league (squad byte +0x03) * 46
//          + 700 for forwards (block bit 0x80)
//   accepted if rand(0, 3000) < weight; after 250 misses the first skater of the squad.
// Booking (ovl14_2349): scorer = pick(0, goal), assist = pick(1, 1) until it differs; on a goal
// the assist gets +1 in the player record (+0x1F, u16), the scorer +1 league goal in the player
// record (+0x1B, league only, DS:76AF = 0) and in the squad record (+0x22 + 2 * competition,
// +0x03 + competition).
#include <cstdint>
#include <vector>

#include "formats/hok.hpp"

namespace em {

// When set, every BorlandRandom::range call appends (lo, hi) (whole-program differential test).
extern std::vector<std::pair<long, long>>* g_borland_log;
extern std::vector<const char*>* g_borland_scopes;  // parallel to g_borland_log: the DrawScope active at each draw
extern const char* g_draw_scope;
struct DrawScope {  // names the code section that draws (differential tests)
    const char* prev;
    explicit DrawScope(const char* name) : prev(g_draw_scope) { g_draw_scope = name; }
    ~DrawScope() { g_draw_scope = prev; }
};

class BorlandRandom {
public:
    explicit BorlandRandom(uint32_t seed = 1) : seed_(seed) {}
    int rand();                      // 0..32767
    int range(int lo, int hi);       // 199f:0847, inclusive
    uint32_t state() const { return seed_; }

private:
    uint32_t seed_;
};

// ovl06_3191: index into `squad` of the chosen player.
int pick_player(const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int kind,
                bool goal, BorlandRandom& rng, const HokTeam* on_ice = nullptr);

struct GoalCredit {
    int scorer = -1, assist = -1;  // indices into the squad
};

// ovl14_2349 for a goal of the human team: picks scorer and assist and books the goal into the
// squad (competition 0 league/WM/play-offs, 1 DEB cup, 2 EuroCup).
// A shoot-out still chooses both players (the native assist remains), but does not
// increment normal goal fields. The copy of the player records is left to the caller.
GoalCredit credit_goal(std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players,
                       BorlandRandom& rng, int competition = 0, bool shootout = false, bool goal = true,
                       const HokTeam* on_ice = nullptr);


// Scorers of a computer club, 1000:3228 (segment 1238:0ea8), called by 1238:114f for both teams
// of every league pairing (mode 0) and, with mode m > 0, of the pairs at DS:5CFB + 32 m (m = 1 the
// European cup slots DS:5D1B; the DEB cup is not counted: MONAT1 players have 9 games after 9
// league matchdays and a cup round). Own goals, goals against. Nothing happens for a club with
// a manager or when the club scored no goal (then not even the games are counted). Otherwise:
//   - every player of the club (player +0x1D) gets a game (+0x1C); a skater joins the shooters
//     when position value + rand(5, 25) > rand(10, 99) (at most 50)
//   - with fewer than two shooters no goal is credited; else for every goal, k = min(n, 10)
//     (DS:4831[n] - 1, one less above 2): with rand(0, 4) == 0 the goal stays without scorer;
//     else, while rand(0, k) != 0, a random shooter gets the goal (+0x1B, at most 255) and the
//     next draw the assist (+0x1F, another shooter); a rand(0, k) == 0 ends the goal unless
//     k > 0 and n > rand(0, 1) + 2
// In league games 1000:2fb6 runs first (also with no own goal), for clubs below 64: the first goalkeeper
// of the club (up to three are collected) gets the goals against (+0x1B, while it stays <= 255)
// and 60 minutes (+0x1F u16); if there are two and rand(0, 8) == 0 the first gets rand(0, 100) %
// of the goals and 60 * rand(2, 100) / 100 minutes, the second the rest.
void credit_club_scorers(std::vector<HokPlayer>& players, Bytes& state, int team, int goals, int goals_against,
                         bool league, BorlandRandom& rng);

}  // namespace em
