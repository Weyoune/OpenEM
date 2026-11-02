// SPDX-License-Identifier: GPL-3.0-only
#include "game/season.hpp"

#include <algorithm>
#include <numeric>

namespace em {

namespace {
void push_form(HokTableRow& row, char c) {
    std::string f = row.form;
    if (f.size() < 8) f.insert(0, 8 - f.size(), '-');
    row.form = f.substr(1) + c;
}
}  // namespace

League league_from_save(const HokFile& save, int league) {
    League l;
    l.first_team = league * kHokLeagueSize;
    for (int s = 0; s < kHokLeagueSize; s++) l.rows[size_t(s)] = save.table[size_t(l.first_team + s)];
    return l;
}

void book_result(League& league, int home, int away, MatchResult r) {
    HokTableRow& h = league.rows[size_t(home)];
    HokTableRow& a = league.rows[size_t(away)];
    h.goals_for[0] += r.home;
    h.goals_against[0] += r.away;
    h.games[0]++;
    a.goals_for[1] += r.away;
    a.goals_against[1] += r.home;
    a.games[1]++;
    if (r.home > r.away) {
        h.wins[0]++;
        a.losses[1]++;
        push_form(h, 's');
        push_form(a, 'N');
    } else if (r.home < r.away) {
        h.losses[0]++;
        a.wins[1]++;
        push_form(h, 'n');
        push_form(a, 'S');
    } else {
        push_form(h, 'u');
        push_form(a, 'U');
    }
}

void sort_league(League& league) {
    std::array<int, kHokLeagueSize> order{};
    std::iota(order.begin(), order.end(), 0);
    auto key = [&](int s) {
        const HokTableRow& r = league.rows[size_t(s)];
        int gf = r.total(r.goals_for), ga = r.total(r.goals_against);
        return std::array<int, 3>{r.points_plus(), gf - ga, gf};
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return key(a) > key(b); });
    for (int p = 0; p < kHokLeagueSize; p++) league.rows[size_t(order[size_t(p)])].position = p;
}

std::vector<MatchResult> play_matchday(League& league, const std::vector<HokTeam>& teams,
                                       const ScheduleTables& tables, int matchday, GameRandom& rng,
                                       const HumanSquad* human, int human_team, MatchStats* human_stats) {
    std::vector<MatchResult> results;
    for (const Pairing& p : round_robin_pairings(tables.teams12, matchday)) {
        const HokTeam& home = teams[size_t(league.first_team + p.home)];
        const HokTeam& away = teams[size_t(league.first_team + p.away)];
        const int hi = league.first_team + p.home, ai = league.first_team + p.away;
        MatchStats* st = (hi == human_team || ai == human_team) ? human_stats : nullptr;
        MatchResult r = league_match(home, away, rng, 0, hi == human_team ? human : nullptr,
                                     ai == human_team ? human : nullptr, st);
        book_result(league, p.home, p.away, r);
        results.push_back(r);
    }
    sort_league(league);
    if (matchday >= 0 && matchday < 44)  // the fever curve: position after this matchday
        for (HokTableRow& row : league.rows) row.position_history[matchday] = row.position;
    return results;
}

}  // namespace em
