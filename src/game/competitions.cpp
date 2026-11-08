// SPDX-License-Identifier: GPL-3.0-only
#include "game/competitions.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr int kFirstForeignTeam = 64;  // team records 0-63: German clubs and nations

// ovl05_52bf / ovl05_5aa5: the lower division hosts the first cup game.
std::vector<HokPairing> cup_pairs(const std::vector<int>& pool) {
    auto division = [](int team) { return team < 12 || team > 52 ? 1 : team < 24 ? 2 : 4; };
    std::vector<HokPairing> draw;
    for (size_t i = 0; i + 1 < pool.size(); i += 2) {
        int home = pool[i], away = pool[i + 1];
        if (division(away) > division(home)) std::swap(home, away);
        draw.push_back({home, away});
    }
    return draw;
}

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
    return league_match(teams[size_t(home)], teams[size_t(away)], rng, 0, hh, ha,
                        hh || ha ? human.stats : nullptr, opts);
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

std::vector<HokPairing> cup_home_rights(const std::vector<int>& pool) { return cup_pairs(pool); }

std::vector<HokPairing> draw_cup_round(const std::vector<int>& winners, BorlandRandom& rng) {
    std::vector<int> pool = winners;
    if (pool.empty()) return {};
    // ovl05_536d: twenty swaps, including draws that select the same slot twice.
    for (int swap = 0; swap < 20; swap++) {
        const size_t a = size_t(rng.range(1, int(pool.size())) - 1);
        const size_t b = size_t(rng.range(1, int(pool.size())) - 1);
        std::swap(pool[a], pool[b]);
    }
    return cup_pairs(pool);
}

std::vector<HokPairing> draw_initial_cup(const std::vector<int>& human_teams, BorlandRandom& rng) {
    if (human_teams.size() > 4) throw FormatError("cup: at most four human managers");
    std::vector<int> pool(32, -1), humans;
    for (int team : human_teams) {
        if (team < 0 || team >= 53 || std::find(humans.begin(), humans.end(), team) != humans.end())
            throw FormatError("cup: invalid or duplicate human club");
        humans.push_back(team);
        int slot;
        do { slot = rng.range(0, 31); } while (pool[size_t(slot)] >= 0);
        pool[size_t(slot)] = team;
    }
    // ovl05_4c7b(0): human clubs have reserved slots; draw the remaining clubs
    // from 0..35 with rejection, including humans whose reserved slot is later.
    for (size_t slot = 0; slot < pool.size(); slot++) {
        if (pool[slot] >= 0) continue;
        int team;
        do { team = rng.range(0, 35); }
        while (std::find(humans.begin(), humans.end(), team) != humans.end() ||
               std::find(pool.begin(), pool.begin() + long(slot), team) != pool.begin() + long(slot));
        pool[slot] = team;
    }
    return cup_pairs(pool);
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
    // DS:0144: (1-8 vs 3-6) and (2-7 vs 4-5); the better placed team is the higher seed
    auto seed = [&](int team) { return int(std::find(order.begin(), order.end(), team) - order.begin()); };
    auto pair = [&](int a, int b) {
        return seed(a) < seed(b) ? best_of_five(teams, a, b, rng, human) : best_of_five(teams, b, a, rng, human);
    };
    p.semi.push_back(pair(p.quarter[0].winner(), p.quarter[2].winner()));
    p.semi.push_back(pair(p.quarter[1].winner(), p.quarter[3].winner()));
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
            t.second_leg.decision_marker = 30;
            t.second_leg.shootout_goals[0] = x;
            t.second_leg.shootout_goals[1] = y;
        }
        out.push_back(t);
    }
    return out;
}

}  // namespace em

namespace em {

// A cup round with the cup display switched on (Einstellungen / DEB-Pokal): ovl06_2522 plays all pairs at once, minute by minute,
// exactly like a league day, with the knock-out rules (overtime, shoot-out); the stream reseeding after the periods and the
// watched list after a home game of the human club are the same (REAL-CODE whole-program test).
std::vector<Tie> play_cup_round_live(std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw, GameRandom& rng,
                                     const HumanSide& human, BorlandRandom* brng) {
    struct Entry {
        int home, away;
        std::unique_ptr<MatchRun> run;
    };
    std::vector<Entry> entries;
    MatchOptions opts;
    opts.knockout = true;
    for (const HokPairing& p : draw) {
        const HumanSquad* hh = p.home == human.team ? human.squad : nullptr;
        const HumanSquad* ha = p.away == human.team ? human.squad : nullptr;
        entries.push_back({p.home, p.away, std::make_unique<MatchRun>(teams[size_t(p.home)], teams[size_t(p.away)], rng, 0, hh, ha,
                                                                         hh || ha ? human.stats : nullptr, opts)});
    }
    int minute_number = 0;
    for (bool more = true; more;) {
        more = false;
        for (auto& e : entries) more = e.run->step() || more;
        minute_number++;
        if (minute_number == 20 && std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.home == human.team; }))
            for (auto& e : entries) e.run->set_watched(true);
        if (brng && (minute_number == 20 || minute_number == 40)) {
            const int spin = brng->range(100, 300);
            for (int i = 0; i < spin; i++) rng.range(0, 1);
        }
    }
    std::vector<Tie> out;
    for (auto& e : entries) {
        Tie t{e.home, e.away, e.run->finish()};
        for (int side = 0; side < 2; side++) {
            if ((side == 0 ? e.home : e.away) == human.team) continue;
            HokTeam& team = teams[size_t(side == 0 ? e.home : e.away)];
            const HokTeam& m = side == 0 ? e.run->home_state() : e.run->away_state();
            team.effort_current = m.effort_current;
            team.flags = m.flags;
            team.effort_base = m.effort_base;
            for (int k = 0; k < 2; k++) {
                team.current_line[k] = m.current_line[k];
                team.shift[k] = m.shift[k];
                for (int line = 0; line < 4; line++) team.energy[k][line] = m.energy[k][line];
            }
            team.skaters = m.skaters;
            team.penalty_code = m.penalty_code;
            for (int i = 0; i < kTacticSituations; i++) {
                team.effort[i] = m.effort[i];
                team.offence[i] = m.offence[i];
            }
        }
        out.push_back(t);
    }
    return out;
}

}  // namespace em
