// SPDX-License-Identifier: GPL-3.0-only
#include "game/match.hpp"

#include <algorithm>
#include <cstdlib>

namespace em {

namespace {
// DS:4E99: share of defence / attack lines per strength mode (percent)
constexpr int kModeWeight[3][2] = {{85, 15}, {50, 50}, {25, 75}};
// DS:4948: goal range of the stronger side by strength difference 0..19
constexpr int kGoalRange[20] = {2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4};

int clamp_byte(int v) { return std::clamp(v, 0, 255); }  // ovl12_0c45 (assumed: byte range)

// ovl06_134f: half the time the larger of two rolls, otherwise the second one
int better_of(int a, int b, GameRandom& rng) { return rng.range(0, 1) == 0 ? b : std::max(a, b); }

int offence_bonus(const HokTeam& t, GameRandom& rng) {
    int r = int(rng.range((t.flags & 0x70) >> 4, 7));
    return std::max(0, r - 5);
}
}  // namespace

long GameRandom::range(long lo, long hi) {
    s_ = uint32_t((uint64_t(s_) * 16 + 167) % kModulus);
    return lo + long(int64_t(s_) * (hi - lo + 1) / kModulus);
}

LineValues line_strength(const HokTeam& t, int line, int kind, bool in_play, int man_advantage) {
    LineValues v;
    const int shift = 4 - 4 * line;
    v.skill1 = t.skill1[kind] + shift;
    v.skill2 = t.skill2[kind] + shift;
    v.form = std::max(0, t.form[kind] + shift);
    if (kind == 0 && man_advantage < 0) v.skill2 >>= -man_advantage;
    if (kind != 0 && man_advantage > 0) v.skill2 += v.skill2 >> (3 - man_advantage);
    if (in_play) {
        v.skill2 = v.skill2 * ((t.energy[kind][line] >> 2) + 75) / 100;
        v.skill1 -= (100 - t.effort_current * v.skill1 / 100) / 4;
    }
    v.skill1 = clamp_byte(v.skill1);
    v.skill2 = clamp_byte(v.skill2);
    v.form = clamp_byte(v.form);
    return v;
}

LineValues line_strength(const HokTeam& t, const HumanSquad& hs, int line, int kind, bool in_play, int man_advantage) {
    constexpr int kGoalieWeight[2] = {80, 140};  // DS:4946
    int s1 = 0, s2 = 0, f = 0, count = 0, lo_nibbles = 0, hi_nibbles = 0;
    bool goalie_found = false;
    const auto& squad = *hs.squad;
    const auto& players = *hs.players;
    for (const HokSquadPlayer& p : squad) {
        if (p.role == 0) continue;
        const int position_value = players[size_t(p.player)].position;
        const bool goalie = p.line == 1 && position_value == 0 && kind == 0;
        goalie_found = goalie_found || goalie;
        const bool member = (((p.line - 1) & 0x7F) == line && (p.line >> 7) == kind) || goalie;
        if (!member) continue;
        if (count >= hs.per_line[kind] && in_play) continue;
        // +0x23 is the player energy (90 = fresh, lower after playing); 0 in a savegame from before the
        // first match means "not set yet" and is taken as fresh (assumption, MONAT1 shows 90 for rested
        // players)
        const int energy = p.fitness == 0 ? 90 : p.fitness;
        const int m = (in_play && !goalie) ? (energy >> 1) + 50 : 100;
        hi_nibbles += p.nibbles >> 4;
        lo_nibbles += p.nibbles & 0x0F;
        s1 += p.skill_a;
        int v = p.skill_b * m / 100;
        if (hs.level < 2 && in_play && p.home_line - 1 != line && v > 12) v -= 10;
        if (hs.level < 5 && in_play && !goalie && std::min(2, position_value / 25) - 1 != kind && v > 15) v -= 15;
        if (kind == 0) v = kGoalieWeight[goalie ? 1 : 0] * v / 100;
        s2 += v;
        f += p.form;
        if (!goalie) count++;
    }
    if (hs.per_line[kind] == 4) s2 = kind == 0 ? 0 : s2 + (s2 >> 1);
    s1 /= 3;
    s2 /= 3;
    f /= 3;
    if (kind == 0 && (man_advantage < 0 || !goalie_found)) {
        s2 -= (45 - lo_nibbles) + (-1 - man_advantage) * 16;
        if (!goalie_found) s2 /= 3;
        s2 = std::max(0, s2);
    }
    if (!goalie_found && in_play && kind == 0 && man_advantage < 1) s1 = s2 = 0;  // empty net
    if (kind != 0 && man_advantage > 0 && s2 != 0) s2 += hi_nibbles + (man_advantage - 1) * 16;
    if (in_play) s1 -= (100 - t.effort_current * s1 / 100) / 4;
    return {clamp_byte(s1), clamp_byte(s2), clamp_byte(f)};
}

int playing_strength(const HokTeam& t, const HumanSquad* human, StrengthMode mode, int minute, int man_advantage,
                     int defence_line, int attack_line) {
    int sum = 0;
    for (int kind = 0; kind < 2; kind++) {
        const int line = kind == 0 ? defence_line : attack_line;
        LineValues v = human ? line_strength(t, *human, line, kind, true, man_advantage)
                             : line_strength(t, line, kind, true, man_advantage);
        int tired = std::max(0, v.skill2 - minute * 10 / (v.skill2 + 1));
        int value = std::min(255, v.skill1 + v.form + tired);
        sum += value * kModeWeight[int(mode)][kind] / 100;
    }
    if (sum != 0) sum += (t.bonus - 44) / 2;  // C division truncates towards 0 like IDIV
    return sum;
}

int playing_strength(const HokTeam& t, StrengthMode mode, int minute, int man_advantage, int defence_line,
                     int attack_line) {
    return playing_strength(t, nullptr, mode, minute, man_advantage, defence_line, attack_line);
}

namespace {
// FUN_162c_1e9c up to the point where whole-match and per-minute mode part: goal points of both
// sides for the given minute (whole = the call with minutes 0..60).
// `special`: play-off mode (DS:76B2 == 1) or overtime (minute > 60) - halves the points (+1) and, per
// minute, adds one point each.
MatchResult goal_points(const HokTeam& h, const HokTeam& a, int minute, bool whole, GameRandom& rng, int level,
                        const HumanSquad* hh = nullptr, const HumanSquad* ha = nullptr, bool special = false) {
    const int bonus_h = offence_bonus(h, rng), bonus_a = offence_bonus(a, rng);
    const int sh = playing_strength(h, hh, StrengthMode::Neutral, minute, h.skaters - a.skaters, h.current_line[0], h.current_line[1]);
    const int sa = playing_strength(a, ha, StrengthMode::Neutral, minute, a.skaters - h.skaters, a.current_line[0], a.current_line[1]);
    int d = sh / 3 - sa / 3;
    int away = (level == 0 || d > 18) ? 0 : int(rng.range(1, 2));
    int home = (d < -18) ? 0 : int(rng.range(1, 2));
    if (d < 1) {
        d = -d;
        int m = d < 20 ? kGoalRange[d] : d / 5;
        if (d < 25) home += int(rng.range(1, 3 - level));
        int u1 = int(rng.range(1, m)), u2 = int(rng.range(1, m));
        if (d > 8 && rng.range(0, 3 - level) == 0)
            away += better_of(u1, u2, rng);
        else
            away += u1;
    } else {
        int m = d < 20 ? kGoalRange[d] : 5;
        int u1 = int(rng.range(1, m)), u2 = int(rng.range(1, m));
        if (d > 8 && rng.range(0, 2) == 0) u1 = better_of(u1, u2, rng);
        home += u1;
        if (d < 25) away += int(rng.range(1, 2));
    }
    // two more goals, steered by the offence settings
    if (rng.range(0, 1) == 0 || rng.range(0, bonus_h) == 0)
        home++;
    else
        away++;
    if (rng.range(0, 1) != 0 && rng.range(0, bonus_a) != 0)
        home++;
    else
        away++;
    if (!whole)
        while (home > 10 || away > 10) {
            home--;
            away--;
        }
    home = std::max(0, home);
    away = std::max(0, away);
    if (sh == 0) {
        home = 0;
        if (sa > 20) away = 9;
    }
    if (sa == 0) {
        away = 0;
        if (sh > 20) home = 9;
    }
    if (special || minute > 60) {
        if (sh != 0) home = home / 2 + 1;
        if (sa != 0) away = away / 2 + 1;
    }
    if (h.skaters < a.skaters) home >>= 2;
    if (a.skaters < h.skaters) away >>= 2;
    if (minute < 58 && ((h.flags | a.flags) & 0x08)) home = away = 9;  // bit 3: goalkeeper pulled (ovl06_34f6)
    if (!whole && (special || minute > 60)) {
        home++;
        away++;
    }
    return {home, away};
}

// FUN_162c_23c4 for a computer match: does the chance of side 0 (home) or 1 (away) become a goal?
bool chance_is_goal(const HokTeam& h, const HokTeam& a, int side, int minute, GameRandom& rng, int level,
                    const HumanSquad* hh, const HumanSquad* ha) {
    const int ch = h.skaters - a.skaters, ca = a.skaters - h.skaters;
    const int h_def = playing_strength(h, hh, StrengthMode::Defensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_off = playing_strength(a, ha, StrengthMode::Offensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int h_off = playing_strength(h, hh, StrengthMode::Offensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_def = playing_strength(a, ha, StrengthMode::Defensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int away_edge = (a_off - h_def) / 3, home_edge = (h_off - a_def) / 3;
    const int q = (h_def + a_off + a_def + h_off) < rng.range(550, 850) ? 1 : 0;
    int d;
    if (side == 0) {  // 162c:2632
        if (rng.range(0, 2 * q + 4) <= 1) return false;
        d = home_edge;
    } else {  // FUN_162c_26cf: the away side has the level in its first hurdle
        if (rng.range(0, 3 * level + 2 * q + 1) <= level) return false;
        d = away_edge;
    }
    if (d >= 0) return rng.range(0, (d >> 2) + q + 2) != 0;
    if (rng.range(0, -d / 4 + 1) == 0) return true;
    return rng.range(0, 6 - q) == 0;
}
}  // namespace

MatchResult quick_match(const HokTeam& h, const HokTeam& a, GameRandom& rng, int level, bool playoff) {
    return goal_points(h, a, 0, true, rng, level, nullptr, nullptr, playoff);
}

namespace {
int situation_of(int own, int opp) {
    if (own - 2 > opp) return kHighLead;
    if (own > opp) return kNarrowLead;
    if (own == opp) return kDraw;
    if (opp - 2 > own) return kHighDeficit;
    return kNarrowDeficit;
}

// Energy of a line: computer teams keep it in the team record, a human team in its players
// (squad +0x23); ovl14 writes the players' mean back into the record (FUN_162c_0737).
int line_energy(const HokTeam& t, const std::vector<HokSquadPlayer>* squad, int kind, int line) {
    if (!squad) return t.energy[kind][line];
    int sum = 0, n = 0;
    for (const HokSquadPlayer& p : *squad)
        if (p.role != 0 && p.line != 0 && ((p.line - 1) & 0x7F) == line && (p.line >> 7) == kind) {
            sum += p.fitness == 0 ? 90 : p.fitness;
            n++;
        }
    return n ? sum / n : 90;
}

// FUN_162c_0737: one minute of energy - the lines on the ice lose
// (rand(0, effort) + 75) / 40 + 1, +4 if the shift is at least rand(0, 5) minutes old, +8 if it is
// longer than rand(5, 15); lines on the bench below 90 win 1 (+1 more if rand(0, 4) == 0 and
// skill 1 > rand(0, 100)).
int drain(const HokTeam& t, int shift, GameRandom& rng) {
    int d = int((rng.range(0, t.effort_current) + 75) / 40) + 1;
    if (shift >= rng.range(0, 5)) d += 4;
    if (shift > rng.range(5, 15)) d += 8;
    return d;
}

void energy_minute(HokTeam& t, std::vector<HokSquadPlayer>* squad, GameRandom& rng) {
    for (int kind = 0; kind < 2; kind++) {
        const int on_ice = t.current_line[kind];
        t.shift[kind]++;
        if (!squad) {
            for (int line = 0; line < 4; line++) {
                int& e = t.energy[kind][line];
                if (line == on_ice) {
                    e = std::clamp(e - drain(t, t.shift[kind], rng), 0, 255);
                } else if (e < 90) {
                    int bonus = (rng.range(0, 4) == 0 && t.skill1[kind] > rng.range(0, 100)) ? 1 : 0;
                    e += 1 + bonus;
                }
            }
            continue;
        }
        for (HokSquadPlayer& p : *squad) {  // human branch: the players carry the energy
            if (p.role == 0 || (p.line >> 7) != kind) continue;
            int e = p.fitness == 0 ? 90 : p.fitness;
            if (p.line != 0 && ((p.line - 1) & 0x7F) == on_ice) {
                e = std::clamp(e - drain(t, t.shift[kind], rng), 0, 255);
            } else if (e < 90) {
                int bonus = (rng.range(0, 4) == 0 && p.skill_a > rng.range(0, 100)) ? 1 : 0;
                e += 1 + bonus;
            }
            p.fitness = e;
        }
    }
}

// FUN_162c_0e32 / FUN_162c_113f: line changes by the tactics mode (+0x2C bits 0-2). Modes 1 and 2
// (two lines / automatic): after more than 3 minutes on the ice the freshest line comes on (the
// second freshest if the freshest is already playing); mode 3 follows the opponent's line of the
// other kind unless that is line 3; mode 0 (all computer teams in the saves) never changes.
// The original also compares "energy + 4 < best line" - a line index, so it never fires.
void change_lines(HokTeam& t, const HokTeam& opp, const std::vector<HokSquadPlayer>* squad) {
    const int mode = t.flags & 7;
    for (int kind = 0; kind < 2; kind++) {
        if (kind == 0 && (t.flags & 0x08)) continue;  // goalkeeper pulled
        int next = t.current_line[kind];
        if (mode == 1 || mode == 2 || mode == 4) {
            if (t.shift[kind] > 3) {
                const int lines = mode == 1 ? 2 : 3;
                int best = 0, second = 1;
                for (int l = 1; l < lines; l++) {
                    if (line_energy(t, squad, kind, l) > line_energy(t, squad, kind, best)) {
                        second = best;
                        best = l;
                    } else if (second == best || line_energy(t, squad, kind, l) > line_energy(t, squad, kind, second)) {
                        second = l;
                    }
                }
                next = t.current_line[kind] == best ? second : best;
            }
        } else if (mode == 3) {
            next = opp.current_line[1 - kind];
            if (next == 3) next = 0;
        }
        if (next != t.current_line[kind]) {
            t.current_line[kind] = next;
            t.shift[kind] = 0;
        }
    }
}

// Penalties of one side (team record +0x33: two 3-bit timers, bit 3 = major).
struct PenaltyBox {
    int minutes[2] = {};
    bool major[2] = {};
    int running() const { return (minutes[0] != 0) + (minutes[1] != 0); }
};

// ovl06_0000(team, 1): one minute passes; an expiring penalty frees the player (ovl06_0151).
int tick_penalties(PenaltyBox& box, HokTeam& t) {
    int running = 0;
    for (int s = 0; s < 2; s++) {
        if (box.minutes[s] == 0) continue;
        running++;
        if (--box.minutes[s] == 0) {
            box.major[s] = false;
            t.skaters++;
            running--;
        }
    }
    if (box.minutes[0] == 0 && box.minutes[1] != 0) {  // the later penalty moves up
        std::swap(box.minutes[0], box.minutes[1]);
        std::swap(box.major[0], box.major[1]);
    }
    return running;
}

// ovl06_02d8(team, other, ...): maybe a penalty for `t`; with 1 in 4 the other side gets one too.
void penalty_minute(HokTeam& t, PenaltyBox& box, HokTeam& other, PenaltyBox& other_box, bool may_pair,
                    GameRandom& rng) {
    const int running = tick_penalties(box, t);
    if (running > 1) return;
    if (rng.range(0, 12) != 0 && rng.range(0, 110 - t.effort_current) != 0) return;
    bool major = false;
    if (running == 0 && rng.range(0, 15) == 0) major = true;  // code 13 = 5 minutes + major bit
    t.skaters--;
    box.minutes[running] = major ? 5 : 2;
    box.major[running] = major;
    if (may_pair && rng.range(0, 3) == 0 && running == 0) penalty_minute(other, other_box, t, box, false, rng);
}

// ovl06_0000(team, 0) + ovl06_0151: a goal against ends the first minor penalty.
void goal_against(HokTeam& t, PenaltyBox& box) {
    for (int s = 0; s < 2; s++)
        if (box.minutes[s] != 0 && !box.major[s]) {
            box.minutes[s] = 0;
            t.skaters++;
            if (s == 0 && box.minutes[1] != 0) {
                std::swap(box.minutes[0], box.minutes[1]);
                std::swap(box.major[0], box.major[1]);
            }
            return;
        }
}
}  // namespace

void apply_effort(HokTeam& t, int own, int opp) { apply_effort(t, own, opp, nullptr); }

void apply_effort(HokTeam& t, int own, int opp, const std::vector<HokSquadPlayer>* squad) {
    const int situation = situation_of(own, opp);
    int effort = t.effort_base + t.effort[situation];
    const int energy = (line_energy(t, squad, 0, t.current_line[0]) + line_energy(t, squad, 1, t.current_line[1])) >> 1;
    if (energy < 50) effort += energy - 50;
    t.effort_current = std::clamp(effort, 0, 100);
    t.flags = (t.flags & 0x8F) | (((t.offence_base + t.offence[situation]) << 4) & 0x70);
}

void setup_computer_team(HokTeam& t, const HokTeam& opp) {
    // FUN_162c_02a5: neutral strengths (doubled) decide; tables DS:4912 (effort per situation) and
    // DS:491C (offence per situation)
    static constexpr int kEffortTable[2][kTacticSituations] = {{0, 25, 20, 30, 0}, {20, 20, 15, 20, 0}};
    static constexpr int kOffenceTable[2][kTacticSituations] = {{5, 6, 4, 4, 0}, {4, 5, 6, 7, 4}};
    const int own = playing_strength(t, StrengthMode::Neutral, 0, 0) * 2;
    const int other = playing_strength(opp, StrengthMode::Neutral, 0, 0) * 2;
    const bool stronger = other <= own;
    t.flags = stronger ? (t.flags | 0x80) : (t.flags & 0x7F);
    const bool close = std::abs(own - other) < 25;
    for (int s = 0; s < kTacticSituations; s++) {
        t.effort[s] = kEffortTable[close ? 0 : 1][s];
        t.offence[s] = kOffenceTable[stronger ? 1 : 0][s];
    }
    t.effort_base = t.effort_current = close ? 60 : 40;
    t.offence_base = 0;
    t.flags &= ~0x08;
    t.skaters = 5;
}

// Penalty shoot-out of a drawn knock-out match (ovl06_161d quick path): both sides add rand(0, 2)
// until the totals differ; the original marks the home value with +30.
void shootout(MatchResult& r, GameRandom& rng) {
    int x, y;
    do {
        x = int(rng.range(0, 2));
        y = int(rng.range(0, 2));
    } while (r.home + x == r.away + y);
    r.home += x;
    r.away += y;
    r.shootout = true;
}

MatchResult knockout_quick(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                           GameRandom& rng, int level) {
    // ovl06_161d, quick path: whole-match results (FUN_162c_1e9c), German clubs (team < 64) +6,
    // re-rolled up to min(4, |difference / 3|) times while the favourite does not win
    int sh = playing_strength(home, StrengthMode::Neutral, 0, 0) + (home_german ? 6 : 0);
    int sa = playing_strength(away, StrengthMode::Neutral, 0, 0) + (away_german ? 6 : 0);
    const int rerolls = std::min(4, std::abs(sh / 3 - sa / 3));
    MatchResult r;
    do {
        r = quick_match(home, away, rng, level);
        if (level == 0) {
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 2) != 0)) r.home -= int(rng.range(0, r.home));
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 1) != 0)) r.away += int(rng.range(0, 4));
        }
    } while (rng.range(0, rerolls) != 0 && ((r.home <= r.away && sa < sh) || (r.away <= r.home && sh < sa)));
    if (r.home == r.away) shootout(r, rng);
    return r;
}

MatchResult league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level, const HumanSquad* hh,
                         const HumanSquad* ha, MatchStats* stats, const MatchOptions& opts) {
    // Working copies: the set-up (FUN_162c_02a5 for computer teams, 5 skaters for everybody), then
    // per minute chances and goals, energy (0737), line changes (0e32), effort (12da) and
    // penalties (ovl06_02d8, not in minute 60) as in the matchday loop ovl06_2522.
    HokTeam h = home, a = away;
    std::vector<HokSquadPlayer> hsq, asq;
    HumanSquad hh2, ha2;
    if (hh) {
        hsq = *hh->squad;
        hh2 = *hh;
        hh2.squad = &hsq;
    }
    if (ha) {
        asq = *ha->squad;
        ha2 = *ha;
        ha2.squad = &asq;
    }
    const HumanSquad* hp = hh ? &hh2 : nullptr;
    const HumanSquad* ap = ha ? &ha2 : nullptr;
    // FUN_162c_02a5 also sets up a human team whose manager leaves effort and offence on automatic
    // (DS:A6FA / DS:A706 per manager; Kölner EC in MONAT1 carries exactly the automatic values).
    // Manual tactics are not modelled yet, so every team gets the set-up; the line mode stays.
    setup_computer_team(h, a);
    setup_computer_team(a, h);
    h.skaters = a.skaters = 5;
    for (HokTeam* t : {&h, &a})
        for (int kind = 0; kind < 2; kind++) {
            t->current_line[kind] = 0;
            t->shift[kind] = 0;
        }
    PenaltyBox hbox, abox;
    MatchResult score;
    // ovl06_161d: three periods 1-20, 21-40, 41-60; a drawn knock-out match goes on 61-65 and 66-70
    int last = 60;
    for (int minute = 1; minute <= last; minute++) {
        if (minute == 60 && opts.knockout && score.home == score.away) last = 70;
        MatchResult pts = goal_points(h, a, minute, false, rng, level, hp, ap, opts.playoff);
        int events = 0;  // FUN_162c_1e9c: a home chance sets the value to 1, each away chance adds 2
        for (int i = 0; i < pts.home; i++)
            if (rng.range(1, 20) == 1) events = 1;
        for (int i = 0; i < pts.away; i++)
            if (rng.range(1, 20) == 1) events += 2;
        if ((events & 1) && chance_is_goal(h, a, 0, minute, rng, level, hp, ap)) {
            score.home++;
            if (stats && h.skaters > a.skaters) stats->power_play_goals[0]++;
            if (stats && h.skaters < a.skaters) stats->short_handed_goals[0]++;
            goal_against(a, abox);
        }
        if ((events & 2) && chance_is_goal(h, a, 1, minute, rng, level, hp, ap)) {
            score.away++;
            if (stats && a.skaters > h.skaters) stats->power_play_goals[1]++;
            if (stats && a.skaters < h.skaters) stats->short_handed_goals[1]++;
            goal_against(h, hbox);
        }
        energy_minute(h, hp ? &hsq : nullptr, rng);
        energy_minute(a, ap ? &asq : nullptr, rng);
        change_lines(h, a, hp ? &hsq : nullptr);
        change_lines(a, h, ap ? &asq : nullptr);
        apply_effort(h, score.home, score.away, hp ? &hsq : nullptr);
        apply_effort(a, score.away, score.home, ap ? &asq : nullptr);
        if (minute != 60 && minute != 70) {
            const int before_h = hbox.running(), before_a = abox.running();
            penalty_minute(h, hbox, a, abox, true, rng);
            penalty_minute(a, abox, h, hbox, true, rng);
            if (stats) {
                stats->penalties[0] += std::max(0, hbox.running() - before_h);
                stats->penalties[1] += std::max(0, abox.running() - before_a);
            }
        }
        if (stats)
            for (int kind = 0; kind < 2; kind++) stats->line_minutes[kind][h.current_line[kind]]++;
    }
    if (opts.knockout && score.home == score.away) shootout(score, rng);
    return score;
}

}  // namespace em
