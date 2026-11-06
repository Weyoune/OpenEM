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
    std::array<bool, kHokLeagueSize> seen{};
    bool valid = true;
    for (size_t place = 0; place < l.ranking.size(); place++) {
        const int slot = save.table_order[size_t(league)][place] - l.first_team;
        if (slot < 0 || slot >= kHokLeagueSize || seen[size_t(slot)]) { valid = false; break; }
        seen[size_t(slot)] = true;
    }
    if (valid)
        for (size_t place = 0; place < l.ranking.size(); place++)
            l.ranking[place] = save.table_order[size_t(league)][place] - l.first_team;
    return l;
}

void book_result(League& league, int home, int away, MatchResult r) {
    HokTableRow& h = league.rows[size_t(home)];
    HokTableRow& a = league.rows[size_t(away)];
    h.goals_for[0] = uint8_t(h.goals_for[0] + r.home);
    h.goals_against[0] = uint8_t(h.goals_against[0] + r.away);
    h.games[0]++;
    a.goals_for[1] = uint8_t(a.goals_for[1] + r.away);
    a.goals_against[1] = uint8_t(a.goals_against[1] + r.home);
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

int fever_curve_position(const HokTableRow& row, int matchday) {
    if (matchday < 0 || matchday > 44) throw FormatError("League: invalid fever-curve matchday");
    if (matchday == 44) return row.penalty_minutes & 15;
    return row.position_history[size_t(matchday)] & 15;
}

void sort_league(League& league) {
    auto order = league.ranking;
    auto key = [&](int s) {
        const HokTableRow& r = league.rows[size_t(s)];
        int gf = r.total(r.goals_for), ga = r.total(r.goals_against);
        return std::array<int, 4>{r.points_plus(), -r.total(r.games), gf - ga, gf};
    };
    // ovl13_3855, normal DS:06B0=0: points, fewer games, goal difference,
    // goals scored. Strict non-adjacent exchanges preserve native tie effects.
    for (size_t i = 0; i + 1 < order.size(); i++)
        for (size_t j = i + 1; j < order.size(); j++) if (key(order[j]) > key(order[i])) std::swap(order[i], order[j]);
    for (int p = 0; p < kHokLeagueSize; p++) league.rows[size_t(order[size_t(p)])].position = p;
    league.ranking = order;
}

std::vector<MatchResult> play_matchday(League& league, const std::vector<HokTeam>& teams,
                                       const ScheduleTables& tables, int matchday, GameRandom& rng,
                                       const HumanSquad* human, int human_team, MatchStats* human_stats) {
    std::vector<MatchResult> results;
    for (const Pairing& p : round_robin_pairings(tables.teams12, matchday)) {
        const HokTeam& home = teams[size_t(league.first_team + p.home)];
        const HokTeam& away = teams[size_t(league.first_team + p.away)];
        const int hi = league.first_team + p.home, ai = league.first_team + p.away;
        MatchStats local;
        MatchStats* st = (hi == human_team || ai == human_team) && human_stats ? human_stats : &local;
        const int before[2] = {st->penalty_minutes[0], st->penalty_minutes[1]};
        MatchResult r = league_match(home, away, rng, 0, hi == human_team ? human : nullptr,
                                     ai == human_team ? human : nullptr, st);
        book_result(league, p.home, p.away, r);
        league.rows[size_t(p.home)].penalty_minutes = uint16_t(league.rows[size_t(p.home)].penalty_minutes +
            st->penalty_minutes[0] - before[0]);
        league.rows[size_t(p.away)].penalty_minutes = uint16_t(league.rows[size_t(p.away)].penalty_minutes +
            st->penalty_minutes[1] - before[1]);
        results.push_back(r);
    }
    sort_league(league);
    if (matchday >= 0 && matchday < 44)  // the fever curve: position after this matchday
        for (HokTableRow& row : league.rows) row.position_history[matchday] = row.position;
    return results;
}


std::vector<std::vector<MatchResult>> play_matchdays(const std::vector<LeagueDay>& days, const std::vector<HokTeam>& teams,
                                                     const ScheduleTables& tables, GameRandom& rng, const HumanSquad* human,
                                                     int human_team, MatchStats* human_stats) {
    // ovl06_2522 plays all matches of the day at once: minute 1 of every match (league by league, pair by pair), then minute 2 ...
    struct Entry {
        size_t day, pair;
        int home, away;
        MatchStats local;
        MatchStats* stats = nullptr;
        int before[2] = {0, 0};
        std::unique_ptr<MatchRun> run;
    };
    std::vector<std::unique_ptr<Entry>> entries;
    std::vector<std::vector<Pairing>> pairings;
    for (size_t d = 0; d < days.size(); d++) {
        pairings.push_back(round_robin_pairings(tables.teams12, days[d].matchday));
        League& league = *days[d].league;
        for (size_t k = 0; k < pairings[d].size(); k++) {
            auto e = std::make_unique<Entry>();
            e->day = d;
            e->pair = k;
            e->home = league.first_team + pairings[d][k].home;
            e->away = league.first_team + pairings[d][k].away;
            e->stats = (e->home == human_team || e->away == human_team) && human_stats ? human_stats : &e->local;
            e->before[0] = e->stats->penalty_minutes[0];
            e->before[1] = e->stats->penalty_minutes[1];
            e->run = std::make_unique<MatchRun>(teams[size_t(e->home)], teams[size_t(e->away)], rng, 0, e->home == human_team ? human : nullptr,
                                                e->away == human_team ? human : nullptr, e->stats);
            entries.push_back(std::move(e));
        }
    }
    for (bool more = true; more;) {
        more = false;
        for (auto& e : entries) more = e->run->step() || more;
    }
    std::vector<std::vector<MatchResult>> results(days.size());
    for (auto& e : entries) {
        League& league = *days[e->day].league;
        const Pairing& p = pairings[e->day][e->pair];
        const MatchResult r = e->run->finish();
        book_result(league, p.home, p.away, r);
        league.rows[size_t(p.home)].penalty_minutes = uint16_t(league.rows[size_t(p.home)].penalty_minutes + e->stats->penalty_minutes[0] - e->before[0]);
        league.rows[size_t(p.away)].penalty_minutes = uint16_t(league.rows[size_t(p.away)].penalty_minutes + e->stats->penalty_minutes[1] - e->before[1]);
        results[e->day].push_back(r);
    }
    for (size_t d = 0; d < days.size(); d++) {
        League& league = *days[d].league;
        sort_league(league);
        if (days[d].matchday >= 0 && days[d].matchday < 44)  // the fever curve: position after this matchday
            for (HokTableRow& row : league.rows) row.position_history[size_t(days[d].matchday)] = row.position;
    }
    return results;
}

}  // namespace em
