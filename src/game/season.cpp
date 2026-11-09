// SPDX-License-Identifier: GPL-3.0-only
#include "game/season.hpp"
#include "game/scorers.hpp"
#include <cstdlib>
#include <map>
#include <cstdio>

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

void book_result_rows(HokTableRow* hp, HokTableRow* ap, MatchResult r) {
    HokTableRow dummy_h, dummy_a;
    HokTableRow& h = hp ? *hp : dummy_h;
    HokTableRow& a = ap ? *ap : dummy_a;
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

void book_result(League& league, int home, int away, MatchResult r) {
    book_result_rows(&league.rows[size_t(home)], &league.rows[size_t(away)], r);
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


std::vector<std::vector<MatchResult>> play_matchdays(const std::vector<LeagueDay>& days, std::vector<HokTeam>& teams,
                                                     const ScheduleTables& tables, GameRandom& rng, const HumanSquad* human,
                                                     int human_team, MatchStats* human_stats, BorlandRandom* brng,
                                                     const MatchMinuteObserver* observer,
                                                     std::vector<std::vector<std::pair<int, int>>>* played) {
    // ovl06_2522 plays all matches of the day at once: minute 1 of every match (league by league, pair by pair), then minute 2 ...
    struct Entry {
        size_t day, pair;
        int home, away;
        int scheduled[2] = {0, 0};  // the teams of the pair table before the pair-table error; the table booking uses them (REAL-CODE period 28)
        MatchStats local;
        MatchStats* stats = nullptr;
        int before[2] = {0, 0};
        int frozen[2] = {-1, -1};  // penalty minutes of a side when the pair-table error replaced its team
        HokTeam frozen_team[2];  // the record of a replaced team at the moment of the swap (REAL-CODE period 28)
        bool has_frozen_team[2] = {false, false};
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
            e->scheduled[0] = e->home; e->scheduled[1] = e->away;
            e->stats = (e->home == human_team || e->away == human_team) && human_stats ? human_stats : &e->local;
            e->before[0] = e->stats->penalty_minutes[0];
            e->before[1] = e->stats->penalty_minutes[1];
            e->run = std::make_unique<MatchRun>(teams[size_t(e->home)], teams[size_t(e->away)], rng, 0, e->home == human_team ? human : nullptr,
                                                e->away == human_team ? human : nullptr, e->stats);
            entries.push_back(std::move(e));
        }
    }
    std::vector<LiveMatch> live;  // the match display only observes (no draws, no state changes)
    if (observer) {
        for (auto& e : entries) live.push_back({e->home, e->away, e->run.get()});
        (*observer)(0, live);
    }
    // The pair-table error of the original (DEVIATIONS.md): when a penalty of the human club without a bound player ends,
    // ovr030:0151 writes through the slot -1 record DS:5D8F into the Oberliga row of the pair table (DS:5D7B + 24): the
    // away team of pair 2 loses bit 0, the home team of pair 3 and the away team of pair 4 become team 0. From then on these
    // pairs play with the records of the new teams, which they share with the matches those teams play themselves.
    struct SharedTeam { HokTeam team; PenaltyBox box; };
    std::map<int, SharedTeam> shared;  // team id -> current record (and running penalties) of a team that plays in more than one slot
    auto side_state = [](Entry& e, int side) -> HokTeam& { return side == 0 ? e.run->home_mut() : e.run->away_mut(); };
    auto side_box = [](Entry& e, int side) -> PenaltyBox& { return side == 0 ? e.run->home_box() : e.run->away_box(); };
    auto corrupt_pairs = [&](const Entry* trigger, int role) {
        for (auto& f : entries) {
            if (days[f->day].league->first_team != 24 || f->pair < 2 || f->pair > 4) continue;
            const int side = f->pair == 3 ? 0 : 1;
            int& id = side == 0 ? f->home : f->away;
            const int next = f->pair == 2 ? (id & ~1) : f->pair == 3 ? role : 0;  // +0x0A gets the saved role of the timer
            if (next == id) continue;
            SharedTeam copy;
            bool found = false;
            if (auto it = shared.find(next); it != shared.end()) { copy = it->second; found = true; }
            else
                for (auto& g : entries) {
                    if (g->home == next) { copy = {g->run->home_mut(), g->run->home_box()}; found = true; break; }
                    if (g->away == next) { copy = {g->run->away_mut(), g->run->away_box()}; found = true; break; }
                }
            if (!found) continue;
            shared[next] = copy;
            if (!f->has_frozen_team[side]) { f->frozen_team[side] = side_state(*f, side); f->frozen_team[side].penalty_code = side_box(*f, side).code(); f->has_frozen_team[side] = true; }
            side_state(*f, side) = copy.team;
            // the replaced team keeps the penalty minutes it had so far; the shared record counts on for the new team (REAL-CODE period 28)
            if (f->frozen[side] < 0) f->frozen[side] = side_box(*f, side).minutes_assessed();
            side_box(*f, side) = copy.box;
            id = next;
            if (f->home == f->away) f->run->mark_same_club();
            if (std::getenv("EM_DEBUG_PHANTOM")) std::fprintf(stderr, "phantom: Oberliga pair %zu side %d -> team %d\n", f->pair, side, next);
        }
        (void)trigger;
    };
    auto pull_shared = [&](Entry& e) {
        for (int side = 0; side < 2; side++)
            if (auto it = shared.find(side == 0 ? e.home : e.away); it != shared.end()) { side_state(e, side) = it->second.team; side_box(e, side) = it->second.box; }
    };
    auto push_shared = [&](Entry& e) {
        for (int side = 0; side < 2; side++)
            if (auto it = shared.find(side == 0 ? e.home : e.away); it != shared.end()) it->second = {side_state(e, side), side_box(e, side)};
    };
    int minute_number = 0;
    for (bool more = true; more;) {
        more = false;
        for (auto& e : entries) {
            pull_shared(*e);
            more = e->run->step() || more;
            push_shared(*e);
            if (const int role = e->run->take_phantom_release(); role >= 0) corrupt_pairs(e.get(), role);
        }
        if (observer) (*observer)(minute_number + 1, live);
        // ovr030:2BA..: after the periods the original reseeds its streams (a Borland draw 100..300, then that many coin draws of the game stream)
        minute_number++;
        if (brng && (minute_number == 20 || minute_number == 40)) {
            const int spin = brng->range(100, 300);
            for (int i = 0; i < spin; i++) rng.range(0, 1);
        }
    }
    std::vector<std::vector<MatchResult>> results(days.size());
    for (auto& e : entries) {
        const MatchResult r = e->run->finish();
        // book every side into the league of the team that actually played (the pair-table error can put a team of another
        // league into an Oberliga pair, DEVIATIONS.md)
        auto row_of = [&](int team) -> HokTableRow* {
            for (const LeagueDay& ld : days)
                if (team >= ld.league->first_team && team < ld.league->first_team + kHokLeagueSize) return &ld.league->rows[size_t(team - ld.league->first_team)];
            return nullptr;
        };
        if (played) { if (played->size() < days.size()) played->resize(days.size()); (*played)[e->day].push_back({e->scheduled[0], e->scheduled[1]}); }
        {  // the original plays on the real team records: the match state stays there (the human club is booked by the game)
            auto write_back = [&](HokTeam& t, const HokTeam& m) {
                t.effort_current = m.effort_current;
                t.flags = m.flags;
                t.effort_base = m.effort_base;
                for (int k = 0; k < 2; k++) { t.current_line[k] = m.current_line[k]; t.shift[k] = m.shift[k]; for (int line = 0; line < 4; line++) t.energy[k][line] = m.energy[k][line]; }
                t.skaters = m.skaters; t.penalty_code = m.penalty_code;
                if (&t == &teams[0] && t.penalty_code) { t.penalty_code = 0; t.skaters = std::max(t.skaters, 5); }  // REAL-CODE: only team record 0 ends its running penalties with the game (cause open, index 0 as an empty marker?)
                for (int i = 0; i < kTacticSituations; i++) { t.effort[i] = m.effort[i]; t.offence[i] = m.offence[i]; }
            };
            for (int side = 0; side < 2; side++) {
                // a team replaced by the pair-table error keeps its record as it was at the swap
                if (e->has_frozen_team[side] && e->scheduled[side] != human_team) write_back(teams[size_t(e->scheduled[side])], e->frozen_team[side]);
                const int tid = side == 0 ? e->home : e->away;
                if (tid == human_team) continue;
                if (shared.count(tid)) {  // a record shared by two pairs keeps the running penalties of its box (REAL-CODE period 28: +0x33)
                    HokTeam m = shared.at(tid).team;
                    m.penalty_code = shared.at(tid).box.code();
                    write_back(teams[size_t(tid)], m);
                } else
                    write_back(teams[size_t(tid)], side == 0 ? e->run->home_state() : e->run->away_state());
            }
        }
        book_result_rows(row_of(e->scheduled[0]), row_of(e->scheduled[1]), r);
        if (false && brng) {  // moved to Game::play_league_day (per league, before the scorers)  // ovl13 table booking (sub_5C067, twice): the morale byte +0x17 follows the goal difference, clamped to 47..53
            const int diff = r.home - r.away;
            auto morale = [&](int team, int d) {
                const int step = int(brng->range(0, 2 * std::abs(d)));
                teams[size_t(team)].bonus = std::clamp(teams[size_t(team)].bonus + (d > 0 ? step : -step), 47, 53);
            };
            morale(e->home, diff);
            morale(e->away, -diff);
        }
        const int pm0 = e->frozen[0] >= 0 ? e->frozen[0] : e->stats->penalty_minutes[0] - e->before[0];
        const int pm1 = e->frozen[1] >= 0 ? e->frozen[1] : e->stats->penalty_minutes[1] - e->before[1];
        if (HokTableRow* rh = row_of(e->scheduled[0])) rh->penalty_minutes = uint16_t(rh->penalty_minutes + pm0);
        if (HokTableRow* ra = row_of(e->scheduled[1])) ra->penalty_minutes = uint16_t(ra->penalty_minutes + pm1);
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
