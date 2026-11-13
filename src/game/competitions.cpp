// SPDX-License-Identifier: GPL-3.0-only
// Cup rounds and draws, play-offs, the manual's relegation model and the European cup group draw
// (see competitions.hpp).
#include "game/competitions.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr int kFirstForeignTeam = 64;  // team records 0-63: German clubs and nations

// The lower division hosts the first cup game. Division classes as the game ranks them: first division and
// national teams 1, second division 2, everything else 4 (higher number = lower class).
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
                    bool playoff, const MatchMinuteObserver* observer = nullptr) {
    const HumanSquad* hh = human.squad_of(home);
    const HumanSquad* ha = human.squad_of(away);
    if (!hh && !ha && playoff) {  // a play-off game without the human club: match set-up (5 skaters, energies) and a quick result, as in the original
        HokTeam h = teams[size_t(home)], a = teams[size_t(away)];
        prepare_match_energy(h, nullptr); prepare_match_energy(a, nullptr);
        setup_computer_team(h, a); setup_computer_team(a, h);
        return knockout_quick(h, a, home < kFirstForeignTeam, away < kFirstForeignTeam, rng, 0, playoff);
    }
    if (!hh && !ha)
        return knockout_quick(teams[size_t(home)], teams[size_t(away)], home < kFirstForeignTeam,
                              away < kFirstForeignTeam, rng, 0, playoff);
    MatchOptions opts;
    opts.knockout = true;
    opts.playoff = playoff;
    return observed_league_match(teams[size_t(home)], teams[size_t(away)], rng, 0, hh, ha, human.match_stats(home, away), opts,
                                 observer, home, away);
}
}  // namespace

MatchResult playoff_game(const std::vector<HokTeam>& teams, int home, int away, GameRandom& rng,
                         const HumanSide& human) {
    return ko_game(teams, home, away, rng, human, true);
}

std::vector<Tie> play_cup_round(const std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw,
                                GameRandom& rng, const HumanSide& human, const MatchMinuteObserver* observer) {
    std::vector<Tie> out;
    for (const HokPairing& p : draw) {
        Tie t{p.home, p.away, ko_game(teams, p.home, p.away, rng, human, false, observer)};
        out.push_back(t);
    }
    return out;
}

std::vector<HokPairing> cup_home_rights(const std::vector<int>& pool) { return cup_pairs(pool); }

std::vector<HokPairing> draw_cup_round(const std::vector<int>& winners, BorlandRandom& rng, std::vector<int>* places) {
    std::vector<int> pool = winners;
    if (pool.empty()) return {};
    // twenty swaps, including draws that select the same slot twice.
    for (int swap = 0; swap < 20; swap++) {
        const size_t a = size_t(rng.range(1, int(pool.size())) - 1);
        const size_t b = size_t(rng.range(1, int(pool.size())) - 1);
        std::swap(pool[a], pool[b]);
    }
    if (places) *places = pool;
    return cup_pairs(pool);
}

std::vector<int> cup_reveal_order(int places, BorlandRandom& rng) {
    std::vector<int> order;
    std::vector<bool> open(size_t(std::max(places, 0)), false);
    for (int k = 0; k < places; k++) {
        int place;
        do { place = rng.range(0, places - 1); } while (open[size_t(place)]);
        open[size_t(place)] = true;
        order.push_back(place);
    }
    return order;
}

std::vector<HokPairing> draw_initial_cup(const std::vector<int>& human_teams, BorlandRandom& rng, std::vector<int>* places) {
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
    // human clubs have reserved slots; draw the remaining clubs
    // from 0..35 with rejection, including humans whose reserved slot is later.
    for (size_t slot = 0; slot < pool.size(); slot++) {
        if (pool[slot] >= 0) continue;
        int team;
        do { team = rng.range(0, 35); }
        while (std::find(humans.begin(), humans.end(), team) != humans.end() ||
               std::find(pool.begin(), pool.begin() + long(slot), team) != pool.begin() + long(slot));
        pool[slot] = team;
    }
    if (places) *places = pool;
    return cup_pairs(pool);
}

int team_strength(const HokTeam& team) {
    int sum = 0;
    for (int k = 0; k < 2; k++) sum += team.skill1[k] + team.skill2[k] + team.form[k];
    return sum / 6;
}

std::array<std::array<int, 4>, 2> draw_euro_groups(const std::vector<HokTeam>& teams, const std::vector<int>& german,
                                                    BorlandRandom& rng) {
    constexpr int kLastTeam = 149;  // last European club (team records 64..149)
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
    // semi-final pairing table: (1-8 vs 3-6) and (2-7 vs 4-5); the better placed team is the higher seed
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
        const HumanSquad* hu = human.squad_of(t.upper);
        const HumanSquad* hl = human.squad_of(t.lower);
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

// A cup round with the cup display switched on (Einstellungen / DEB-Pokal): the original plays all pairs at once, minute by
// minute, exactly like a league day, with the knock-out rules (overtime, shoot-out); the random-stream spin after the first
// and second period and the watched list after a home game of the human club are the same as on a league day.
// Afterwards the match state (effort, lines, shifts, energies, skaters, penalties, tactics) of the computer teams is
// written back into `teams`.
std::vector<Tie> play_cup_round_live(std::vector<HokTeam>& teams, const std::vector<HokPairing>& draw, GameRandom& rng,
                                     const HumanSide& human, BorlandRandom* brng, const MatchMinuteObserver* observer,
                                     int* loop_minutes, bool playoff, size_t knockout_pairs, int* phantom_role, int level, bool ended_breaks) {
    struct Entry {
        int home, away;
        std::unique_ptr<MatchRun> run;
    };
    std::vector<Entry> entries;
    MatchOptions opts;
    opts.knockout = true;
    opts.playoff = playoff;
    for (const HokPairing& p : draw) {
        const HumanSquad* hh = human.squad_of(p.home);
        const HumanSquad* ha = human.squad_of(p.away);
        MatchOptions o = opts;
        o.knockout = entries.size() < knockout_pairs;  // the relegation games of a play-off day run in the same loop, without overtime
        entries.push_back({p.home, p.away, std::make_unique<MatchRun>(teams[size_t(p.home)], teams[size_t(p.away)], rng, level, hh, ha,
                                                                         human.match_stats(p.home, p.away), o)});
    }
    // the display of the round (switch DEB-Pokal, or a manager's club plays) and whether any match has a board, as in play_matchdays
    bool display = g_display_flags[10] != 0, board = false;
    for (auto& e : entries) { display = display || human.is_human(e.home) || human.is_human(e.away); board = board || e.run->has_board(); }
    for (auto& e : entries) e.run->set_messages(display && board);
    std::vector<LiveMatch> live;  // the match display only observes (no draws, no state changes)
    if (observer) {
        for (auto& e : entries) live.push_back({e.home, e.away, e.run.get()});
        (*observer)(0, live);
    }
    int minute_number = 0;
    for (bool more = true; more;) {
        more = false;
        bool minute_left = false;
        for (auto& e : entries) minute_left = minute_left || e.run->minutes_left();
        if (std::getenv("EM_DEBUG_LIVEMIN")) {  // the minute start with the draw counter and every human side's record
            std::fprintf(stderr, "livemin %d at %zu\n", minute_number + 1, (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
            for (auto& e : entries)
                for (int side = 0; side < 2; side++) {
                    if (!human.is_human(side == 0 ? e.home : e.away)) continue;
                    const HokTeam& t = side == 0 ? e.run->home_state() : e.run->away_state();
                    std::fprintf(stderr, "  %s energy %d %d %d %d / %d %d %d %d lines %d/%d shift %d/%d\n", t.name.c_str(), t.energy[0][0], t.energy[0][1],
                                 t.energy[0][2], t.energy[0][3], t.energy[1][0], t.energy[1][1], t.energy[1][2], t.energy[1][3],
                                 t.current_line[0], t.current_line[1], t.shift[0], t.shift[1]);
                }
        }
        if (minute_left)  // the reporter draw at the start of the minute
            if (const int r = reporter_minute_draw(rng, display); r >= 0) for (auto& e : entries) e.run->set_reporter(r);
        if (minute_number == 60 || minute_number == 65)  // the overtime breaks reach every pair of the round, as in the original
            for (auto& e : entries) e.run->overtime_break(ended_breaks);
        for (auto& e : entries) {
            more = e.run->step() || more;
            // a manager penalty without a bound player ending here writes through the slot -1 record into the pair table
            // as in a league match (a quirk of the original that OpenEM reproduces); only the stored table is affected
            if (const int role = e.run->take_phantom_release(); role >= 0 && phantom_role) *phantom_role = role;
        }
        if (observer) (*observer)(minute_number + 1, live);
        minute_number++;
        if (brng && (minute_number == 20 || minute_number == 40)) {
            const int spin = brng->range(100, 300);
            for (int i = 0; i < spin; i++) rng.range(0, 1);
        }
    }
    // WM: a round decided in the overtime before minute 66 still gets the break of minute 66, as in the original (its
    // effect on the energies is still visible at the start of the next WM)
    if (ended_breaks && minute_number > 60 && minute_number <= 65 && !std::getenv("EM_OLD_WMOTBREAK"))
        for (auto& e : entries) e.run->overtime_break(true);    if (loop_minutes) *loop_minutes = minute_number;
    std::vector<Tie> out;
    for (auto& e : entries) {
        Tie t{e.home, e.away, e.run->finish()};
        for (int side = 0; side < 2; side++) {
            if (human.is_human(side == 0 ? e.home : e.away)) continue;
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
            // as in the league (season.cpp): only team record 0 ends its running penalties with the game (a quirk of the
            // original: team 0 with a penalty running at the end of a cup game is left with 5 skaters and +0x33 = 0)
            if (&team == &teams[0] && team.penalty_code && !std::getenv("EM_OLD_CUPTEAM0")) { team.penalty_code = 0; team.skaters = std::max(team.skaters, 5); }
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
