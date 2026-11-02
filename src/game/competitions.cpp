// SPDX-License-Identifier: GPL-3.0-only
#include "game/competitions.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr int kFirstForeignTeam = 64;  // team records 0-63: German clubs and nations

// A knock-out or play-off game: the minute loop when the human club plays, else the quick path
// for cup games and the minute loop with play-off rules for play-off games.
MatchResult ko_game(const std::vector<HokTeam>& teams, int home, int away, GameRandom& rng, const HumanSide& human,
                    bool playoff) {
    const HumanSquad* hh = home == human.team ? human.squad : nullptr;
    const HumanSquad* ha = away == human.team ? human.squad : nullptr;
    if (!playoff && !hh && !ha)
        return knockout_quick(teams[size_t(home)], teams[size_t(away)], home < kFirstForeignTeam,
                              away < kFirstForeignTeam, rng);
    MatchOptions opts;
    opts.knockout = true;
    opts.playoff = playoff;
    return league_match(teams[size_t(home)], teams[size_t(away)], rng, 0, hh, ha, nullptr, opts);
}
}  // namespace

MatchResult playoff_game(const std::vector<HokTeam>& teams, int home, int away, GameRandom& rng,
                         const HumanSide& human) {
    return ko_game(teams, home, away, rng, human, true);
}

std::vector<Tie> play_cup_round(const std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw,
                                GameRandom& rng, const HumanSide& human) {
    std::vector<Tie> out;
    for (const HokPairing& p : draw) {
        Tie t{p.home, p.away, ko_game(teams, p.home, p.away, rng, human, false)};
        out.push_back(t);
    }
    return out;
}

std::vector<HokPairing> draw_cup_round(const std::vector<int>& winners, BorlandRandom& rng) {
    std::vector<int> pool = winners;
    for (size_t i = pool.size(); i > 1; i--) std::swap(pool[i - 1], pool[size_t(rng.range(0, int(i) - 1))]);
    std::vector<HokPairing> draw;
    for (size_t i = 0; i + 1 < pool.size(); i += 2) draw.push_back({pool[i], pool[i + 1]});
    return draw;
}

int team_strength(const HokTeam& team) {
    int sum = 0;
    for (int k = 0; k < 2; k++) sum += team.skill1[k] + team.skill2[k] + team.form[k];
    return sum / 6;
}

std::array<std::array<int, 4>, 2> draw_euro_groups(const std::vector<HokTeam>& teams, const std::vector<int>& german,
                                                    BorlandRandom& rng) {
    constexpr int kLastTeam = 149;  // DS:07CB + (0x39 - DS:07CB + 0x5C)
    std::array<int, 8> slot{};
    for (size_t i = 0; i < slot.size(); i++) {
        for (;;) {
            const int t = rng.range(kFirstForeignTeam, std::min(kLastTeam, int(teams.size()) - 1));
            if (team_strength(teams[size_t(t)]) < 70 && rng.range(0, 3) != 0) continue;
            if (std::find(slot.begin(), slot.begin() + long(i), t) != slot.begin() + long(i)) continue;
            slot[i] = t;
            break;
        }
    }
    for (int t : german) {
        for (;;) {
            const int s = rng.range(0, 7);
            if (slot[size_t(s)] < kFirstForeignTeam) continue;
            slot[size_t(s)] = t;
            break;
        }
    }
    return {{{slot[0], slot[1], slot[2], slot[3]}, {slot[4], slot[5], slot[6], slot[7]}}};
}

Series best_of_five(const std::vector<HokTeam>& teams, int higher, int lower, GameRandom& rng,
                    const HumanSide& human) {
    Series s{higher, lower};
    for (int game = 0; s.wins_higher < 3 && s.wins_lower < 3; game++) {
        const bool higher_home = game % 2 == 0;  // games 1, 3, 5 at the higher seed
        const int home = higher_home ? higher : lower, away = higher_home ? lower : higher;
        MatchResult r = ko_game(teams, home, away, rng, human, true);
        s.games.push_back(r);
        const bool home_won = r.home > r.away;
        if (home_won == higher_home)
            s.wins_higher++;
        else
            s.wins_lower++;
    }
    return s;
}

PlayoffResult play_playoffs(const std::vector<HokTeam>& teams, const std::vector<int>& order, GameRandom& rng,
                            const HumanSide& human) {
    PlayoffResult p;
    for (int i = 0; i < 4; i++) p.quarter.push_back(best_of_five(teams, order[size_t(i)], order[size_t(7 - i)], rng, human));
    // bracket: (1-8 vs 4-5) and (2-7 vs 3-6); the better placed team is the higher seed
    auto seed = [&](int team) { return int(std::find(order.begin(), order.end(), team) - order.begin()); };
    auto pair = [&](int a, int b) {
        return seed(a) < seed(b) ? best_of_five(teams, a, b, rng, human) : best_of_five(teams, b, a, rng, human);
    };
    p.semi.push_back(pair(p.quarter[0].winner(), p.quarter[3].winner()));
    p.semi.push_back(pair(p.quarter[1].winner(), p.quarter[2].winner()));
    p.final_series = pair(p.semi[0].winner(), p.semi[1].winner());
    return p;
}

int RelegationTie::stays_up() const {
    const int upper_goals = first_leg.away + second_leg.home;
    const int lower_goals = first_leg.home + second_leg.away;
    if (upper_goals != lower_goals) return upper_goals > lower_goals ? upper : lower;
    return second_leg.home > second_leg.away ? upper : lower;  // shoot-out after the second leg
}

std::vector<RelegationTie> play_relegation(const std::vector<HokTeam>& teams, const std::vector<int>& upper_order,
                                           const std::vector<int>& lower_order, GameRandom& rng,
                                           const HumanSide& human) {
    std::vector<RelegationTie> out;
    const int pairs[2][2] = {{upper_order[10], lower_order[1]}, {upper_order[11], lower_order[0]}};
    for (const auto& pr : pairs) {
        RelegationTie t;
        t.upper = pr[0];
        t.lower = pr[1];
        const HumanSquad* hu = t.upper == human.team ? human.squad : nullptr;
        const HumanSquad* hl = t.lower == human.team ? human.squad : nullptr;
        t.first_leg = league_match(teams[size_t(t.lower)], teams[size_t(t.upper)], rng, 0, hl, hu);
        t.second_leg = league_match(teams[size_t(t.upper)], teams[size_t(t.lower)], rng, 0, hu, hl);
        const int upper_goals = t.first_leg.away + t.second_leg.home;
        const int lower_goals = t.first_leg.home + t.second_leg.away;
        if (upper_goals == lower_goals) {
            // level on aggregate: shoot-out after the second leg (assumed; same rule as the cup)
            int x, y;
            do {
                x = int(rng.range(0, 2));
                y = int(rng.range(0, 2));
            } while (x == y);
            t.second_leg.home += x;
            t.second_leg.away += y;
            t.second_leg.shootout = true;
        }
        out.push_back(t);
    }
    return out;
}

}  // namespace em
