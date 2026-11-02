// SPDX-License-Identifier: GPL-3.0-only
#include "game/scorers.hpp"

#include <algorithm>

namespace em {

int BorlandRandom::rand() {
    seed_ = seed_ * 22695477u + 1u;
    return int((seed_ >> 16) & 0x7FFF);
}

int BorlandRandom::range(int lo, int hi) {
    if (hi < lo) std::swap(lo, hi);
    return lo + rand() % (hi + 1 - lo);
}

int pick_player(const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int kind,
                bool goal, BorlandRandom& rng) {
    const int n = int(squad.size());
    if (n == 0) return -1;
    for (int attempt = 0; attempt < 250; attempt++) {
        const int slot = rng.range(0, n - 1);
        const HokSquadPlayer& p = squad[size_t(slot)];
        const int position_value = players[size_t(p.player)].position;
        if (position_value == 0 || p.role == 0 || p.role >= 12) continue;
        int weight = (kind == 0 && !goal) ? rng.range(120, 170)
                                          : p.skill_b * 3 / (5 - 3 * kind) + p.skill_a / 5 + p.form / 5;
        weight += std::min(2, position_value / 25) * (72 - 17 * kind);
        weight += p.league_goals_byte * 46;
        if (p.line & 0x80) weight += 700;
        if (rng.range(0, 3000) < weight) return slot;
    }
    for (int slot = 0; slot < n - 1; slot++)
        if (players[size_t(squad[size_t(slot)].player)].position != 0) return slot;
    return 0;
}

GoalCredit credit_goal(std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players,
                       BorlandRandom& rng) {
    GoalCredit c;
    if (squad.size() < 4) return c;  // ovl14_2349 needs more than 3 players
    c.scorer = pick_player(squad, players, 0, true, rng);
    do c.assist = pick_player(squad, players, 1, true, rng);
    while (c.assist == c.scorer);
    HokSquadPlayer& s = squad[size_t(c.scorer)];
    s.league_goals++;
    s.league_goals_byte++;
    return c;
}


namespace {
uint8_t* raw_of(Bytes& state, const HokPlayer& pl) { return state.data() + (pl.raw - state.data()); }
void add_u16(uint8_t* p, int v) {
    const int x = (p[0] | p[1] << 8) + v;
    p[0] = uint8_t(x);
    p[1] = uint8_t(x >> 8);
}

// 1000:2fb6: goals against and minutes of the goalkeepers.
void credit_goalkeepers(std::vector<HokPlayer>& players, Bytes& state, int team, int goals_against, BorlandRandom& rng) {
    if (team > 63) return;
    std::vector<HokPlayer*> keepers;
    for (HokPlayer& pl : players)
        if (pl.position == 0 && pl.team == team && keepers.size() < 3) keepers.push_back(&pl);
    if (keepers.empty()) return;
    int share = 100;
    if (keepers.size() > 1 && rng.range(0, 8) == 0) share = rng.range(0, 100);
    const int first = goals_against * share / 100;
    if (keepers[0]->league_goals + first <= 255) keepers[0]->league_goals += first;
    if (keepers.size() > 1 && keepers[1]->league_goals + goals_against - first <= 255)
        keepers[1]->league_goals += goals_against - first;
    if (share != 100) share = rng.range(2, 100);
    const int minutes = 60 * share / 100;
    add_u16(raw_of(state, *keepers[0]) + 0x1F, minutes);
    if (keepers.size() > 1) add_u16(raw_of(state, *keepers[1]) + 0x1F, 60 - minutes);
}
}  // namespace

void credit_club_scorers(std::vector<HokPlayer>& players, Bytes& state, int team, int goals, int goals_against,
                         bool league, BorlandRandom& rng) {
    if (league) credit_goalkeepers(players, state, team, goals_against, rng);
    if (goals <= 0) return;
    std::vector<HokPlayer*> shooters;
    for (HokPlayer& pl : players) {
        if (pl.team != team) continue;
        pl.league_games = std::min(pl.league_games + 1, 255);
        if (pl.position == 0) continue;
        const int a = pl.position + rng.range(5, 25);
        if (a > rng.range(10, 99) && shooters.size() < 50) shooters.push_back(&pl);
    }
    const int n = int(shooters.size());
    if (n <= 1) return;
    int k = n > 9 ? 10 : n;  // DS:4831[n] - 1
    if (k > 2) k--;
    for (int g = 0; g < goals; g++) {
        if (rng.range(0, 4) == 0) continue;
        int scorer = -1;
        for (;;) {
            if (rng.range(0, k) != 0) {
                int c = 0;
                for (int tries = 0;; tries++) {
                    c = rng.range(0, n - 1);
                    if (tries >= 250 || scorer < 0 || c != scorer) break;
                }
                HokPlayer& q = *shooters[size_t(c)];
                if (scorer < 0) {
                    if (q.league_goals < 255) q.league_goals++;
                } else {
                    add_u16(raw_of(state, q) + 0x1F, 1);
                }
                if (scorer < 0) {
                    scorer = c;
                    continue;
                }
                break;
            }
            if (k == 0 || n <= rng.range(0, 1) + 2) break;
        }
    }
}

}  // namespace em
