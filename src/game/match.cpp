// SPDX-License-Identifier: GPL-3.0-only
#include "game/match.hpp"
#include <cstdio>
#include <cstdlib>
#include "game/penalties.hpp"
#include "game/scorers.hpp"

#include <algorithm>
#include <cstdlib>

namespace em {

namespace {
// DS:4E99: share of defence / attack lines per strength mode (percent)
constexpr int kModeWeight[3][2] = {{85, 15}, {50, 50}, {25, 75}};
// DS:4948: goal range of the stronger side by strength difference 0..19
constexpr int kGoalRange[20] = {2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4};

int clamp_line(int v) { return std::clamp(v, 0, 100); }  // 162c:1bce..1c1b, explicit 0/100 bounds

// ovl06_134f: half the time the larger of two rolls, otherwise the second one
int better_of(int a, int b, GameRandom& rng) {
    const long coin = rng.borland ? rng.borland->range(0, 1) : rng.range(0, 1);
    return coin == 0 ? b : std::max(a, b); }

int offence_bonus(const HokTeam& t, GameRandom& rng) {
    int r = int(rng.range((t.flags & 0x70) >> 4, 7));
    return std::max(0, r - 5);
}
}  // namespace

std::vector<std::pair<long, long>>* g_game_log = nullptr;
std::vector<const char*>* g_game_scopes = nullptr;
std::vector<uint32_t>* g_game_states = nullptr;

long GameRandom::range(long lo, long hi) {
    if (g_game_states) g_game_states->push_back(s_);  // state before the draw (differential tests)
    if (g_game_log) { g_game_log->push_back({lo, hi}); if (g_game_scopes) g_game_scopes->push_back(g_draw_scope); }
    s_ = uint32_t((uint64_t(s_) * 16 + 167) % kModulus);
    // 32-bit signed product that wraps like the original (REAL-CODE test): negative results appear when
    // state * (hi - lo + 1) exceeds 2^31, i.e. for ranges above roughly 540 at high states.
    const int32_t product = int32_t(uint32_t(s_) * uint32_t(int32_t(hi - lo + 1)));
    return lo + long(product / int32_t(kModulus));
}

void configure_human_tactics(HumanSquad& human, const HokFile& save, int manager) {
    if (manager < 0 || manager >= kHokManagers) throw FormatError("HOK: invalid tactics manager");
    human.effort_control = save.state.at(0x01FF + size_t(manager));
    human.offence_control = save.state.at(0x01F3 + size_t(manager));
    human.line_control = save.state.at(0x01F9 + size_t(manager));
    for (size_t kind = 0; kind < 2; kind++)
        for (size_t line = 0; line < 4; line++)
            human.line_quotas[kind][line] = save.state.at(0x025B + size_t(manager) * 8 + kind * 4 + line);
}

void refresh_line_energy(HokTeam& team, const HumanSquad& human) {
    int remaining[2][4], sum[2][4];
    for (int kind = 0; kind < 2; kind++)
        for (int line = 0; line < 4; line++) {
            remaining[kind][line] = human.per_line[kind];
            sum[kind][line] = human.per_line[kind] < 2 ? 0 : (3 - human.per_line[kind]) * 90;
        }
    const auto& squad = *human.squad;
    const auto end = squad.begin() + std::min(size_t(29), squad.size());
    const size_t occupied = size_t(std::count_if(squad.begin(), end, [](const auto& p) { return p.player >= 0; }));
    for (size_t slot = 0; slot < occupied; slot++) {
        const auto& p = squad[slot];
        const int line = (p.line & 0x7F) - 1, kind = (p.line & 0x80) != 0;
        if (p.player < 0 || p.role == 0 || line < 0 || line >= 4 ||
            human.players->at(size_t(p.player)).position == 0 || remaining[kind][line] == 0) continue;
        remaining[kind][line]--;
        sum[kind][line] += p.fitness;
    }
    for (int kind = 0; kind < 2; kind++)
        for (int line = 0; line < 4; line++) team.energy[kind][line] = uint8_t(sum[kind][line] / 3);
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
    v.skill1 = clamp_line(v.skill1);
    v.skill2 = clamp_line(v.skill2);
    v.form = clamp_line(v.form);
    return v;
}

LineValues line_strength(const HokTeam& t, const HumanSquad& hs, int line, int kind, bool in_play, int man_advantage, int minute) {
    constexpr int kGoalieWeight[2] = {80, 140};  // DS:4946
    int s1 = 0, s2 = 0, f = 0, count = 0, lo_nibbles = 0, hi_nibbles = 0;
    bool goalie_found = false, goalie_present = false;
    const auto& squad = *hs.squad;
    const auto& players = *hs.players;
    for (const HokSquadPlayer& p : squad) {
        if (p.role == 0) continue;
        const int position_value = players[size_t(p.player)].position;
        goalie_present = goalie_present || (p.line == 1 && position_value == 0);
        const bool goalie = p.line == 1 && position_value == 0 && kind == 0;
        goalie_found = goalie_found || goalie;
        const bool member = (((p.line - 1) & 0x7F) == line && (p.line >> 7) == kind) || goalie;
        if (!member) continue;
        if (count >= hs.per_line[kind] && in_play) continue;
        // +0x1B is energy, prepared once before the game. Zero during play is exhausted.
        const int energy = p.fitness;
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
    //162C:1A8B and1AA9 both read the defence cacheCC37, including
    //the attack bonus. Four attackers alone do not select this rule.
    if (hs.per_line[0] == 4) s2 = kind == 0 ? 0 : s2 + (s2 >> 1);
    s1 /= 3;
    s2 /= 3;
    f /= 3;
    if (kind == 0 && (man_advantage < 0 || !goalie_found)) {
        s2 -= (45 - lo_nibbles) + (-1 - man_advantage) * 16;
        if (!goalie_found) s2 /= 3;
        s2 = std::max(0, s2);
    }
    // 162C:1B41..1B83: the flag DS:DE16+manager is only rewritten by defence calls (kind 0); an attack call reads the one left by
    // the preceding defence call. Empty net: everything 0 (attack: only before minute 58); form also before minute 58.
    if (!goalie_present && in_play && man_advantage < 1 && (kind == 0 || minute < 58)) {
        s1 = s2 = 0;
        if (minute < 58) f = 0;
    }
    if (kind != 0 && man_advantage > 0 && s2 != 0) s2 += hi_nibbles + (man_advantage - 1) * 16;
    if (in_play) s1 -= (100 - t.effort_current * s1 / 100) / 4;
    return {clamp_line(s1), clamp_line(s2), clamp_line(f)};
}

int playing_strength(const HokTeam& t, const HumanSquad* human, StrengthMode mode, int minute, int man_advantage,
                     int defence_line, int attack_line, int* kinds) {
    int sum = 0;
    for (int kind = 0; kind < 2; kind++) {
        const int line = kind == 0 ? defence_line : attack_line;
        LineValues v = human ? line_strength(t, *human, line, kind, true, man_advantage, minute)
                             : line_strength(t, line, kind, true, man_advantage);
        int tired = std::max(0, v.skill2 - minute * 10 / (v.skill2 + 1));
        int value = std::min(255, v.skill1 + v.form + tired);
        if (kinds) kinds[kind] = value;
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
static int g_goal_kinds[2][2];  // per-kind values of the last goal_points call (the original caches them in DS:E06E)
int strength_from_kinds(const HokTeam& t, const int k[2], StrengthMode mode = StrengthMode::Neutral) {
    int sum = 0;
    for (int kind = 0; kind < 2; kind++) sum += k[kind] * kModeWeight[int(mode)][kind] / 100;
    if (sum != 0) sum += (t.bonus - 44) / 2;
    return sum;
}

MatchResult goal_points(const HokTeam& h, const HokTeam& a, int minute, bool whole, GameRandom& rng, int level,
                        const HumanSquad* hh = nullptr, const HumanSquad* ha = nullptr, bool special = false, int total = 60) {
    const int bonus_h = offence_bonus(h, rng), bonus_a = offence_bonus(a, rng);
    const int sh = playing_strength(h, hh, StrengthMode::Neutral, minute, h.skaters - a.skaters, h.current_line[0], h.current_line[1], g_goal_kinds[0]);
    const int sa = playing_strength(a, ha, StrengthMode::Neutral, minute, a.skaters - h.skaters, a.current_line[0], a.current_line[1], g_goal_kinds[1]);
    if (!whole && std::getenv("EM_DEBUG_GPALL")) {
        static int counter = 0;
        std::fprintf(stderr, "gpall %d %d %d %d lines %d/%d %d/%d sk %d/%d eff %d/%d fl %d/%d\n", minute, counter++ % 18, sh, sa, h.current_line[0], h.current_line[1], a.current_line[0], a.current_line[1], h.skaters, a.skaters, h.effort_current, a.effort_current, h.flags, a.flags);
    }
    if (std::getenv("EM_DEBUG_GP2")) std::fprintf(stderr, "gp2 min %d flags %d/%d shift %d,%d/%d,%d\n", minute, h.flags, a.flags, h.shift[0], h.shift[1], a.shift[0], a.shift[1]);
    if (std::getenv("EM_DEBUG_GP")) std::fprintf(stderr, "G=%zu goal_points min %d: sh %d sa %d lines %d/%d %d/%d skaters %d/%d human %d/%d eff %d/%d en %d %d %d %d %d %d %d %d\n", g_game_log ? g_game_log->size() : size_t(0), minute, sh, sa, h.current_line[0], h.current_line[1], a.current_line[0], a.current_line[1], h.skaters, a.skaters, hh != nullptr, ha != nullptr, h.effort_current, a.effort_current, h.energy[0][0], h.energy[0][1], h.energy[0][2], h.energy[0][3], h.energy[1][0], h.energy[1][1], h.energy[1][2], h.energy[1][3]);
    if (hh && minute == (std::getenv("EM_DEBUG_MIN") ? std::atoi(std::getenv("EM_DEBUG_MIN")) : 2) && std::getenv("EM_DEBUG_GP")) {
        std::fprintf(stderr, "globals: level %d per %d/%d minute %d\n", hh->level, hh->per_line[0], hh->per_line[1], minute);
        std::fprintf(stderr, "team11: flags %d effort %d lines %d/%d energy %d %d %d %d %d %d %d %d skill1 %d/%d skill2 %d/%d form %d/%d bonus %d\n", h.flags, h.effort_current,
                     h.current_line[0], h.current_line[1], h.energy[0][0], h.energy[0][1], h.energy[0][2], h.energy[0][3], h.energy[1][0], h.energy[1][1], h.energy[1][2], h.energy[1][3],
                     h.skill1[0], h.skill1[1], h.skill2[0], h.skill2[1], h.form[0], h.form[1], h.bonus);
    }
    if (hh && minute == (std::getenv("EM_DEBUG_MIN") ? std::atoi(std::getenv("EM_DEBUG_MIN")) : 2) && std::getenv("EM_DEBUG_GP"))
        for (size_t s = 0; s < hh->squad->size() && s < 26; s++) {
            const auto& p = (*hh->squad)[s];
            std::fprintf(stderr, "squad %2zu: %3d %3d %3d %3d %3d %3d %3d %3d %3d\n", s, p.home_line, p.role, p.player + 1, p.skill_a, p.skill_b, p.form, p.line, p.nibbles, p.fitness);
        }
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
    if (special || minute > 60) {  // play-off mode (2a9e:76B2 = DS:D0C2 == 1) or overtime
        if (sh != 0) home = home / 2 + 1;
        if (sa != 0) away = away / 2 + 1;
    }
    if (h.skaters < a.skaters) home >>= 2;
    if (a.skaters < h.skaters) away >>= 2;
    if (minute < 58 && ((h.flags | a.flags) & 0x08)) home = away = 9;  // bit 3: goalkeeper pulled (ovl06_34f6)
    if (!whole && (special || total > 60)) {  // 162c:22B8: byte_32092 == 1 (play-off) or the total length argument above 60 (the overtime loop of the cup, REAL-CODE)
        home++;
        away++;
    }
    return {home, away};
}

// FUN_162c_23c4 for a computer match: does the chance of side 0 (home) or 1 (away) become a goal?
bool chance_is_goal(const HokTeam& h, const HokTeam& a, int side, int minute, GameRandom& rng, int level,
                    const HumanSquad* hh, const HumanSquad* ha, bool* shot = nullptr, const int* kinds_h = nullptr,
                    const int* kinds_a = nullptr) {
    if (shot) *shot = false;
    const int ch = h.skaters - a.skaters, ca = a.skaters - h.skaters;
    // ovl06_1371 without recomputation (23c4): the per-kind values come from the goal-point step of the same minute (DS:E06E)
    const int h_def = kinds_h ? strength_from_kinds(h, kinds_h, StrengthMode::Defensive) : playing_strength(h, hh, StrengthMode::Defensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_off = kinds_a ? strength_from_kinds(a, kinds_a, StrengthMode::Offensive) : playing_strength(a, ha, StrengthMode::Offensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int h_off = kinds_h ? strength_from_kinds(h, kinds_h, StrengthMode::Offensive) : playing_strength(h, hh, StrengthMode::Offensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_def = kinds_a ? strength_from_kinds(a, kinds_a, StrengthMode::Defensive) : playing_strength(a, ha, StrengthMode::Defensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int away_edge = (a_off - h_def) / 3, home_edge = (h_off - a_def) / 3;
    const int q = (h_def + a_off + a_def + h_off) < rng.range(550, 850) ? 1 : 0;
    int d;
    if (side == 0) {  // 162c:2632
        if (rng.range(0, 2 * q + 4) <= 1) return false;
        if (shot) *shot = true;
        d = home_edge;
    } else {  // FUN_162c_26cf: the away side has the level in its first hurdle
        if (rng.range(0, 3 * level + 2 * q + 1) <= level) return false;
        if (shot) *shot = true;
        d = away_edge;
    }
    if (d >= 0) return rng.range(0, (d >> 2) + q + 2) != 0;
    if (rng.range(0, -d / 4 + 1) == 0) return true;
    return rng.range(0, 6 - q) == 0;
}
}  // namespace

// FUN_162c_1e9c(home, away, minute, 60) for one minute: the goal points give chances (1 in 20 per point);
// a home chance sets the event value to 1, each away chance adds 2.
int minute_events(const HokTeam& h, const HokTeam& a, int minute, GameRandom& rng, int level, const HumanSquad* hh,
                  const HumanSquad* ha, bool special, int total) {
    const MatchResult pts = goal_points(h, a, minute, false, rng, level, hh, ha, special, total);
    int events = 0;
    for (int i = 0; i < pts.home; i++)
        if (rng.range(1, 20) == 1) events = 1;
    for (int i = 0; i < pts.away; i++)
        if (rng.range(1, 20) == 1) events += 2;
    return events;
}

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

// Both branches read the cache that 0737 refreshes after updating fitness.
int line_energy(const HokTeam& t, const std::vector<HokSquadPlayer>*, int kind, int line) {
    return t.energy[kind][line];
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
    if (!squad) {
        for (int kind = 0; kind < 2; kind++) {
            const int on_ice = t.current_line[kind];
            t.shift[kind] = uint8_t(t.shift[kind] + 1);
            for (int line = 0; line < 4; line++) {
                int& e = t.energy[kind][line];
                if (line == on_ice) {
                    e = std::clamp(e - drain(t, t.shift[kind], rng), 1, 99);
                } else if (e < 90) {
                    int bonus = (rng.range(0, 4) == 0 && t.skill1[kind] > rng.range(0, 100)) ? 1 : 0;
                    e += 1 + bonus;
                }
            }
        }
        return;
    }
    // Native walks the packed occupied prefix once, in squad-slot order. Each
    // kind's shift advances only when its first on-ice player is encountered.
    bool advanced[2] = {};
    const auto end = squad->begin() + std::min(size_t(29), squad->size());
    const size_t occupied = size_t(std::count_if(squad->begin(), end, [](const auto& p) { return p.player >= 0; }));
    for (size_t slot = 0; slot < occupied; slot++) {
        HokSquadPlayer& p = (*squad)[slot];
        if (p.player < 0 || p.role == 0) continue;
        const int kind = (p.line >> 7) & 1;
        int e = p.fitness;
        if (p.line != 0 && ((p.line - 1) & 0x7F) == t.current_line[kind]) {
            if (!advanced[kind]) {
                t.shift[kind] = uint8_t(t.shift[kind] + 1);
                advanced[kind] = true;
            }
            if (rng.range(0, 4 + (p.skill_b < p.skill_a)) == 0) {
                p.raw_exhaustion = uint8_t(p.raw_exhaustion + 1);
                p.exhaustion = p.raw_exhaustion * 2 / 3;
            }
            e = std::clamp(e - drain(t, t.shift[kind], rng), 1, 99);
        } else if (e < 90) {
            int bonus = (rng.range(0, 4) == 0 && p.skill_a > rng.range(0, 100)) ? 1 : 0;
            e += 1 + bonus;
        }
        p.fitness = e;
    }
}

// FUN_162c_0e32 / FUN_162c_113f: line changes by the tactics mode (+0x2C bits 0-2). Modes 1 and 2
// (two lines / automatic): after more than 3 minutes on the ice the freshest line comes on (the
// second freshest if the freshest is already playing); mode 3 follows the opponent's line of the
// other kind unless that is line 3; mode 0 (all computer teams in the saves) never changes.
// The original also compares "energy + 4 < best line" - a line index, so it never fires.
bool has_fourth_line(const HumanSquad* human, int kind) {
    if (!human) return false;
    const auto& squad = *human->squad;
    const auto end = squad.begin() + std::min(size_t(29), squad.size());
    const size_t occupied = size_t(std::count_if(squad.begin(), end, [](const auto& p) { return p.player >= 0; }));
    for (size_t slot = 0; slot < occupied; slot++)
        if (squad[slot].player >= 0 && squad[slot].role != 0 && squad[slot].line == 4 + kind * 128) return true;
    return false;
}

void change_lines(HokTeam& t, const HokTeam& opp, const HumanSquad* human, LineRotation& rotation) {
    if (human && human->line_control == 5) return;  // native manual/live control
    if (human && human->line_control != 0) t.flags = (t.flags & 0xF8) | human->line_control;
    const int mode = t.flags & 7;
    const auto* squad = human ? human->squad : nullptr;
    for (int kind = 0; kind < 2; kind++) {
        if (kind == 0 && (t.flags & 0x08)) continue;
        const int current = t.current_line[kind];
        int next = current;
        const int lines = mode == 1 ? 2 : 3 + int(has_fourth_line(human, kind));
        // best/runner-up in one pass (REAL-CODE test of FUN_162c_0e32): a new best pushes the old best down only if it
        // beats the old runner-up strictly; ties keep the earlier runner-up
        int best = 0, second = -1;
        for (int line = 1; line < lines; line++) {
            const int e = line_energy(t, squad, kind, line);
            if (e > line_energy(t, squad, kind, best)) {
                if (second < 0 || line_energy(t, squad, kind, best) > line_energy(t, squad, kind, second)) second = best;
                best = line;
            } else if (second < 0 || e > line_energy(t, squad, kind, second)) {
                second = line;
            }
        }
        if (mode == 1 || mode == 2) {
            if (line_energy(t, squad, kind, current) + 4 < best || t.shift[kind] > 3)
                next = current == best ? second : best;
        } else if (mode == 3) {
            next = opp.current_line[1 - kind];
            if (next == 3 || line_energy(t, squad, kind, next) + 8 < best) next = best;
        } else if (mode == 4 && human) {
            uint8_t& used = rotation.used[size_t(kind)][size_t(current)];
            used = uint8_t(used + 1);
            if (used > human->line_quotas[size_t(kind)][size_t(current)]) {
                used = 0;
                for (int tries = 0; tries <= 250; tries++) {
                    if (++next >= lines) next = 0;
                    if (human->line_quotas[size_t(kind)][size_t(next)] != 0) break;
                }
            }
        }
        if (next != current) t.shift[kind] = 0;
        t.current_line[kind] = next;
    }
}
// FUN_162c_000b (called by the match minute on odd minutes): tactics mode (+0x2C bits 0-2) of both sides from the score,
// the number of skaters and the neutral strengths, and the goalkeeper pull of computer teams in the last minutes.
// The mode is written for computer teams and for managers whose line change setting is automatic.
void update_tactics_mode(HokTeam& a, HokTeam& b, const HumanSquad* ha, const HumanSquad* hb, int score_a, int score_b, int minute,
                        const int* kinds_a, const int* kinds_b) {
    const int skaters_a = a.skaters, skaters_b = b.skaters;
    int mode_a = 2, mode_b = 2;
    if (minute > 40 && score_a > score_b && score_a + 3 > score_b) { mode_a = 2; mode_b = 1; }
    else if (skaters_a < skaters_b) { mode_a = 2; mode_b = 1; }
    bool decided = false;
    if (minute > 40 && score_a < score_b && score_a < score_b + 3) { mode_a = 1; mode_b = 2; decided = true; }
    else if (skaters_a > skaters_b) { mode_a = 1; mode_b = 2; decided = true; }
    if (!decided) {
        const int pa = ((kinds_a ? strength_from_kinds(a, kinds_a) : playing_strength(a, ha, StrengthMode::Neutral, minute, skaters_a - skaters_b, a.current_line[0], a.current_line[1])) * 2) & 0xFF;
        const int pb = ((kinds_b ? strength_from_kinds(b, kinds_b) : playing_strength(b, hb, StrengthMode::Neutral, minute, skaters_b - skaters_a, b.current_line[0], b.current_line[1])) * 2) & 0xFF;
        if (std::getenv("EM_DEBUG_MODE")) std::fprintf(stderr, "mode min %d pa %d pb %d sk %d/%d score %d:%d lines A %d/%d B %d/%d\n", minute, pa, pb, skaters_a, skaters_b, score_a, score_b, a.current_line[0], a.current_line[1], b.current_line[0], b.current_line[1]);
        if (pa < pb && pa + 20 < pb) { mode_a = 3; mode_b = 2; }
        if (pb < pa && pb + 15 < pa) { mode_a = 2; mode_b = 3; }
    }
    auto finish_side = [&](HokTeam& t, const HumanSquad* h, int mode, int own_skaters, int own_score, int other_score) {
        const bool automatic = !h || h->line_control == 0;
        if (automatic) t.flags = uint8_t((t.flags & 0xF8) | mode);
        if (h) return;  // the goalkeeper pull is a computer decision
        if (minute == 58 || minute == 59 || minute == 68 || minute == 69) {
            if (own_skaters == 5 && own_score < other_score && own_score + 1 == other_score) {
                t.flags |= 0x08;
                t.skaters = 6;
                return;
            }
        }
        if (t.flags & 0x08) {
            t.flags &= 0xF7;
            t.skaters--;
        }
    };
    finish_side(a, ha, mode_a, skaters_a, score_a, score_b);
    finish_side(b, hb, mode_b, skaters_b, score_b, score_a);
}

// ovr030:34F6 (sub_46386), called by the match minute for a team with a manager: the players on the ice per category
// (goalies counted on any line, skaters only on the current line of their kind) set DS:CC35+ (per line counts) and the
// skaters; running penalties take attackers away (total limit 6, or 5 without a goalie and fewer than six skaters).
void recount_on_ice(HokTeam& t, const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int penalties,
                    int per_line[2]) {
    const auto end = squad.begin() + std::min(size_t(29), squad.size());
    const size_t occupied = size_t(std::count_if(squad.begin(), end, [](const auto& p) { return p.player >= 0; }));
    if (std::count_if(squad.begin(), end, [](const auto& p) { return p.player >= 0 && p.role != 0; }) < 9) return;  // synthetic or depleted squads keep their counts
    int goalies = 0, defenders = 0, attackers = 0;
    for (size_t slot = 0; slot < occupied; slot++) {
        const auto& p = squad[slot];
        if (p.player < 0 || p.role == 0) continue;
        const bool goalie = players[size_t(p.player)].position == 0;
        const int kind = (p.line >> 7) & 1, line = (p.line & 0x7F);
        if (goalie) {
            if (kind == 0 && line != 0) goalies++;
        } else if (kind == 0 && line - 1 == t.current_line[0]) {
            defenders++;
        } else if (kind == 1 && line - 1 == t.current_line[1]) {
            attackers++;
        }
    }
    if (goalies == 0) t.current_line[0] = 0;
    int limit = 6;
    if (goalies == 0 && defenders + attackers < 6) limit = 5;
    const int total = defenders + attackers + goalies + penalties;
    if (total > limit) attackers = 3 - (total - limit);
    per_line[0] = defenders;
    per_line[1] = attackers;
    // ovr030:35F4..: the recount overwrites the skater count of the record (defenders + attackers) and sets/clears the pulled-goalie bit (six skaters)
    t.skaters = defenders + attackers;
    if (defenders + attackers == 6) t.flags |= 0x08; else t.flags &= uint8_t(~0x08);
}

struct PenaltySide {
    HokTeam& team;
    PenaltyBox& box;
    std::vector<HokSquadPlayer>* squad;
    const HumanSquad* human;
    BorlandRandom& selection_rng;
    MatchStats* stats;
    int side;
};

// ovl06_02d8: the paired call also ticks its side, as in the original.
bool penalty_minute(PenaltySide& side, PenaltySide& other, bool may_pair, GameRandom& rng, bool tick = true) {
    const int running = tick ? side.box.tick(side.team, side.squad) : side.box.running();
    if (running > 1) return false;
    const int effort = side.human ? std::clamp(side.team.effort_current + side.human->penalty_effort_adjustment,
                                               0, 100) : side.team.effort_current;
    if (std::getenv("EM_DEBUG_PEN")) std::fprintf(stderr, "pen effort %d (current %d) squad %d at draw %zu skaters %d\n", effort, side.team.effort_current, side.squad != nullptr, g_game_log ? g_game_log->size() : size_t(0), side.team.skaters);
    if (rng.range(0, 12) != 0 && rng.range(0, 110 - effort) != 0) return false;
    if (std::getenv("EM_DEBUG_PK"))
        std::fprintf(stderr, "pen hit at %zu human %d/%d skaters %d/%d running %d\n", (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0),
                     side.human != nullptr, other.human != nullptr, side.team.skaters, other.team.skaters, running);
    if (side.squad && std::count_if(side.squad->begin(), side.squad->begin() + std::min(size_t(29), side.squad->size()), [](const auto& p) {
            return p.player >= 0 && p.role != 0 && p.line != 0;
        }) <= 7) return false;  // ovl12_0d27(manager, 1): protect a depleted human lineup
    const bool major = running == 0 && rng.range(0, 15) == 0;
    const bool pair = may_pair && rng.range(0, 3) == 0 && running == 0;
    // The original books opportunities before attempting to select the player.
    // with the goalkeeper pulled (six skaters) the penalty brings him back: 4 skaters remain (REAL-CODE period 9)
    const int token = side.stats ? side.stats->record(side.side, side.team.skaters == 6 ? 4 : side.team.skaters - 1,
        other.team.skaters, side.human != nullptr, other.human != nullptr) : -1;
    int slot = -1;
    bool selection_failed = false;
    if (side.human) {
        slot = pick_penalized_player(side.team, *side.squad, *side.human->players, side.selection_rng, true);
        selection_failed = slot < 0;
        if (slot >= 0 && side.human->players->at(size_t(side.squad->at(size_t(slot)).player)).position == 0)
            slot = -1;  // the team penalty remains, but goalkeepers receive no personal counter
    }
    side.box.start(side.team, major, slot, side.squad);
    if (selection_failed) return false;  // native sentinel aborts the paired call after the team penalty
    if (pair && !penalty_minute(other, side, false, rng, true) && side.stats) side.stats->rollback(token);
    return true;
}
}  // namespace

void apply_effort(HokTeam& t, int own, int opp) { apply_effort(t, own, opp, nullptr); }

void apply_effort(HokTeam& t, int own, int opp, const std::vector<HokSquadPlayer>* squad,
                  int effort_control, int offence_control) {
    const int situation = situation_of(own, opp);
    int effort = t.effort_base + t.effort[situation];
    const int energy = (line_energy(t, squad, 0, t.current_line[0]) + line_energy(t, squad, 1, t.current_line[1])) >> 1;
    if (energy < 50) effort += energy - 50;
    if (effort_control != 2) t.effort_current = std::clamp(effort, 0, 100);
    if (offence_control < 2)
        t.flags = (t.flags & 0x8F) | (((t.offence_base + t.offence[situation]) << 4) & 0x70);
}

void setup_computer_team(HokTeam& t, const HokTeam& opp, const HumanSquad* human, const HumanSquad* /*opponent_human*/) {
    // FUN_162c_02a5: neutral strengths (doubled) decide; tables DS:4912 (effort per situation) and
    // DS:491C (offence per situation)
    static constexpr int kEffortTable[2][kTacticSituations] = {{0, 25, 20, 30, 0}, {20, 20, 15, 20, 0}};
    static constexpr int kOffenceTable[2][kTacticSituations] = {{5, 6, 4, 4, 0}, {4, 5, 6, 7, 4}};
    // ovl06_1371 takes the lines on the ice from the team records (REAL-CODE test)
    // At the time of the setup the team is not yet in the match pair list DS:0233, so 162C:16bf takes its computer branch (team record)
    // even for the human team (REAL-CODE: original strength of the own team 191 = C++ 382 doubled).
    const int own = playing_strength(t, nullptr, StrengthMode::Neutral, 0, 0, t.current_line[0], t.current_line[1]) * 2;
    const int other = playing_strength(opp, nullptr, StrengthMode::Neutral, 0, 0, opp.current_line[0], opp.current_line[1]) * 2;
    const bool stronger = other <= own;
    if ((human || std::getenv("EM_DEBUG_ALL")) && std::getenv("EM_DEBUG_SETUP")) std::fprintf(stderr, "setup %s: own %d other %d (doubled playing strength), effort before %d/%d, close=%d\n", human ? "human" : "cpu", own, other, t.effort_current, opp.effort_current, std::abs(own - other) < 25);
    t.flags = stronger ? (t.flags | 0x80) : (t.flags & 0x7F);
    const bool close = std::abs(own - other) < 25;
    const bool automatic_effort = !human || human->effort_control == 0;
    const bool automatic_offence = !human || human->offence_control == 0;
    for (int s = 0; s < kTacticSituations; s++) {
        if (automatic_effort) t.effort[s] = kEffortTable[close ? 0 : 1][s];
        if (automatic_offence) t.offence[s] = kOffenceTable[stronger ? 1 : 0][s];
    }
    if (automatic_effort) t.effort_base = t.effort_current = close ? 60 : 40;
    if (automatic_offence) t.offence_base = 0;
    t.flags &= ~0x08;
    t.skaters = 5;
}

// Penalty decisions: ovl06_161d quick path and ovl06_3adb after a full minute game.
void shootout(MatchResult& r, GameRandom& rng, bool minute_game = false, bool human = false) {
    int x = 0, y = 0;
    // the original draws the shoot-out with Borland rand (REAL-CODE: (2,5) pairs of the cup overtime)
    auto draw = [&](int lo, int hi) { return rng.borland ? rng.borland->range(lo, hi) : int(rng.range(lo, hi)); };
    if (minute_game && human) {
        // ovl06_3adb: five complete pairs, then complete sudden-death pairs.
        // A shot succeeds when Borland rand(0,10) > 6. The simulation still uses
        // its supplied GameRandom stream; native RNG restoration remains separate.
        const int first = draw(0, 1);
        int goals[2] = {};
        for (int pair = 0; pair < 5 || goals[0] == goals[1]; pair++) {
            goals[first] += draw(0, 10) > 6;
            goals[1 - first] += draw(0, 10) > 6;
        }
        x = goals[0]; y = goals[1];
    } else {
        // Full CPU display: a coin (0,1), then rand(2,5) pairs until the totals differ (REAL-CODE cup overtime). Quick path: rand(0,2).
        if (minute_game) draw(0, 1);
        do {
            x = draw(minute_game ? 2 : 0, minute_game ? 5 : 2);
            y = draw(minute_game ? 2 : 0, minute_game ? 5 : 2);
        } while (r.home + x == r.away + y);
    }
    r.home += x;
    r.away += y;
    r.shootout_goals[0] = x;
    r.shootout_goals[1] = y;
    r.shootout = true;
    r.decision_marker += 30;
}

MatchResult knockout_quick(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                           GameRandom& rng, int level) {
    return quick_competition_match(home, away, home_german, away_german, rng, level, true);
}

MatchResult quick_competition_match(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                                    GameRandom& rng, int level, bool knockout) {
    // ovl06_161d, quick path: whole-match results (FUN_162c_1e9c), German clubs (team < 64) +6,
    // re-rolled up to min(4, |difference / 3|) times while the favourite does not win
    int sh = playing_strength(home, StrengthMode::Neutral, 0, 0, home.current_line[0], home.current_line[1]) + (home_german ? 6 : 0);
    int sa = playing_strength(away, StrengthMode::Neutral, 0, 0, away.current_line[0], away.current_line[1]) + (away_german ? 6 : 0);
    const int rerolls = std::min(4, std::abs(sh / 3 - sa / 3));
    if (std::getenv("EM_DEBUG_QUICK"))
        std::fprintf(stderr, "quick %s - %s: sh %d sa %d german %d/%d rerolls %d at G%zu lines %d/%d eff %d en %d %d %d %d / %d %d %d %d\n", home.name.c_str(), away.name.c_str(), sh, sa,
                     home_german, away_german, rerolls, g_game_log ? g_game_log->size() : size_t(0), home.current_line[0], home.current_line[1], home.effort_current, home.energy[0][0], home.energy[0][1], home.energy[0][2], home.energy[0][3], home.energy[1][0], home.energy[1][1], home.energy[1][2], home.energy[1][3]);
    MatchResult r;
    do {
        r = quick_match(home, away, rng, level);
        if (level == 0) {
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 2) != 0)) r.home -= int(rng.range(0, r.home));
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 1) != 0)) r.away += int(rng.range(0, 4));
        }
    } while (rng.range(0, rerolls) != 0 && ((r.home <= r.away && sa < sh) || (r.away <= r.home && sh < sa)));
    if (knockout && r.home == r.away) shootout(r, rng);
    return r;
}

void prepare_match_energy(HokTeam& team, std::vector<HokSquadPlayer>* squad, bool interval) {
    if (!interval) {
        if (team.flags & 0x08) team.current_line[0] = 2;
        if (!squad) team.flags &= 0xF8;  // native CPU line mode is disabled, existing lines remain
        team.shift[0] = team.shift[1] = 0;
    }
    for (auto& lines : team.energy)
        for (int& energy : lines) energy = interval ? std::min(90, energy + 19) : 90;
    if (!squad) return;
    for (auto& player : *squad) {
        if (player.player < 0) continue;
        player.fitness = interval ? std::min(90, player.fitness + 19)
                                  : std::min(90, 100 - player.raw_exhaustion / 8);
        if (!interval) {
            player.raw_exhaustion = std::clamp(player.raw_exhaustion, 50, 150);  // ovl14_2630
            player.exhaustion = player.raw_exhaustion * 2 / 3;
        }
    }
}

int selected_match_players(const HumanSquad& human) {
    if (!human.squad) return 0;
    int count = 0;
    for (size_t slot = 0; slot < std::min(size_t(29), human.squad->size()); slot++) {
        const auto& player = human.squad->at(slot);
        if (player.player >= 0 && player.role != 0 && player.line != 0) count++;
    }
    return count;
}

int squad_match_load(const HokTeam& team, const HumanSquad& human) {
    if (selected_match_players(human) < 9) return 100;
    int difference = 0;
    for (int kind = 0; kind < 2; kind++) {
        int skill1 = 0, skill2 = 0;
        for (int line = 0; line < 3; line++) {
            const auto values = line_strength(team, human, line, kind, false, 0);
            skill1 += values.skill1;
            skill2 += values.skill2;
            difference += skill1 - skill2;
        }
    }
    if (std::getenv("EM_DEBUG_LOAD")) std::fprintf(stderr, "load difference %d -> %d\n", difference, std::clamp(difference / 5 + 20, 0, 40));
    return std::clamp(difference / 5 + 20, 0, 40);
}

// One league match as a sequence of minutes (ovl06_2522 plays all matches of the day minute by minute).
struct MatchRun::Impl {
    GameRandom& rng;
    int level;
    MatchStats* stats;
    MatchOptions opts;
    HokTeam h, a;
    std::vector<HokSquadPlayer> hsq, asq;
    HumanSquad hh2, ha2;
    const HumanSquad* hp = nullptr;
    const HumanSquad* ap = nullptr;
    LineRotation hlocal, alocal;
    PenaltyBox hbox, abox;
    BorlandRandom fallback_selection{1};
    std::unique_ptr<PenaltySide> hpen, apen;
    MatchResult score;
    int last = 60;
    int minute = 0;
    bool forfeit = false;
    MatchResult forfeit_result;

    Impl(const HokTeam& home, const HokTeam& away, GameRandom& r, int lvl, const HumanSquad* hh, const HumanSquad* ha, MatchStats* st,
         const MatchOptions& o)
        : rng(r), level(lvl), stats(st), opts(o), h(home), a(away) {
        if (stats) {
            stats->forfeit_side = 0;
            stats->initial_load[0] = hh ? squad_match_load(home, *hh) : -1;
            stats->initial_load[1] = ha ? squad_match_load(away, *ha) : -1;
        }
        const int forfeited = hh && hh->minimum_players > 0 && selected_match_players(*hh) < hh->minimum_players ? 1 :
                              ha && ha->minimum_players > 0 && selected_match_players(*ha) < ha->minimum_players ? 2 : 0;
        if (forfeited) {
            if (stats) stats->forfeit_side = forfeited;
            forfeit = true;
            forfeit_result = {forfeited == 1 ? 0 : 2, forfeited == 1 ? 2 : 0};
            return;
        }
        // Working copies: the set-up (FUN_162c_02a5 for computer teams, 5 skaters for everybody), then
        // per minute chances and goals, energy (0737), line changes (0e32), effort (12da) and
        // penalties (ovl06_02d8, not in minute 60) as in the matchday loop ovl06_2522.
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
        hp = hh ? &hh2 : nullptr;
        ap = ha ? &ha2 : nullptr;
        // FUN_162c_02a5 also sets up a human team whose manager leaves effort and offence on automatic
        // (DS:A6FA / DS:A706 per manager; Kölner EC in MONAT1 carries exactly the automatic values).
        // Each component follows its independent saved AUTOM./SEMI/MANUAL mode.
        // SETUPDUMP of the original: both team records hold line energy 90 when 02a5 runs (a human team's tired values from the
        // previous day are already refreshed); the strength comparison of the set-up reads them.
        for (HokTeam* r : {&h, &a}) {
            if (r->flags & 0x08) r->current_line[0] = 2;  // a goalkeeper pulled at the end of the last game starts on line 2 (REAL-CODE)
            for (auto& kind : r->energy)
                for (auto& e : kind) e = 90;
        }
        setup_computer_team(h, a, hp, ap);
        setup_computer_team(a, h, ap, hp);
        prepare_match_energy(h, hp ? &hsq : nullptr);
        prepare_match_energy(a, ap ? &asq : nullptr);
        h.skaters = a.skaters = 5;
        h.penalty_code = a.penalty_code = 0;
        for (int kind = 0; kind < 2; kind++) h.shift[kind] = a.shift[kind] = 0;
        hpen = std::make_unique<PenaltySide>(PenaltySide{h, hbox, hp ? &hsq : nullptr, hp,
                                                          hp && hp->selection_rng ? *hp->selection_rng : (rng.borland ? *rng.borland : fallback_selection), st, 0});
        apen = std::make_unique<PenaltySide>(PenaltySide{a, abox, ap ? &asq : nullptr, ap,
                                                          ap && ap->selection_rng ? *ap->selection_rng : (rng.borland ? *rng.borland : fallback_selection), st, 1});
    }

    // ovl06_161d: three periods 1-20, 21-40, 41-60; a drawn knock-out match goes on 61-65 and 66-70
    bool watched = false;
    bool shootout_done = false;
    MatchProgress shown;  // display bookkeeping only (ovl14_1dc7), never read by the simulation
    int kinds_h[2] = {0, 0}, kinds_a[2] = {0, 0};
    bool step() {
        if (forfeit || minute >= last) return false;
        minute++;
        LineRotation& hrotation = hp && hp->rotation ? *hp->rotation : hlocal;
        LineRotation& arotation = ap && ap->rotation ? *ap->rotation : alocal;
        if (minute == 21 || minute == 41 || ((minute == 61 || minute == 66) && (hp || ap))) {  // the breaks of the overtime reach the managed match here, the other pairs through overtime_break()
            const int times = 1;
            for (int i = 0; i < times; i++) {
                prepare_match_energy(h, hp ? &hsq : nullptr, true);
                prepare_match_energy(a, ap ? &asq : nullptr, true);
            }
        }
        // ovr030:3680..3772 (called by the minute loop for a match with a manager): one draw rand(0, 200 + 200 * level + 20 * v) per
        // human manager of the match, v = min(40, 60 - byte); a zero starts an interactive event (not simulated)
        for (int side = 0; side < 2; side++) {
            const HumanSquad* m = side == 0 ? hp : ap;
            const int load = stats && stats->initial_load[side] >= 0 ? stats->initial_load[side] : 0;
            if (m && m->on_minute) m->on_minute(minute, load, side == 0 ? h : a);
        }
        const int shown_home = score.home, shown_away = score.away;  // the original passes the score of the previous minute to the mode and effort updates
        if (hp) recount_on_ice(h, hsq, *hh2.players, hbox.running(), hh2.per_line);
        if (ap) recount_on_ice(a, asq, *ha2.players, abox.running(), ha2.per_line);
        const int events = minute_events(h, a, minute, rng, level, hp, ap, opts.playoff, minute > 65 ? 70 : minute > 60 ? 65 : 60);
        for (int k = 0; k < 2; k++) { kinds_h[k] = g_goal_kinds[0][k]; kinds_a[k] = g_goal_kinds[1][k]; }  // the mode update reads the cached values
        bool home_shot = false, away_shot = false;
        const bool home_goal = (events & 1) && chance_is_goal(h, a, 0, minute, rng, level, hp, ap, &home_shot, kinds_h, kinds_a);
        if ((events & 1) && hp && hp->on_chance) hp->on_chance(home_goal, h, hsq);  // 23c4 books every chance of a match with a manager
        if ((events & 1) && ap && ap->on_opponent_chance) ap->on_opponent_chance(home_goal, h);
        if ((events & 1) && minute <= 60) { shown.period_chances[(minute - 1) / 20][0]++; if (home_goal) shown.period_goals[(minute - 1) / 20][0]++; }
        if (home_goal) {
            score.home++;
            if (stats && h.skaters + a.skaters != 11 && h.skaters > a.skaters) stats->power_play_goals[0]++;
            if (stats && h.skaters + a.skaters != 11 && h.skaters < a.skaters) stats->short_handed_goals[0]++;
            const bool equal = h.skaters == a.skaters;
            { const int before = abox.code(); abox.goal_against(a, ap ? &asq : nullptr);
              if (ap && abox.code() != before) recount_on_ice(a, asq, *ha2.players, abox.running(), ha2.per_line); }  // ovr030:0151 (sub_42FE1) ends with the recount (REAL-CODE)
            if (stats) stats->after_goal(0, equal, h.skaters, a.skaters, hp != nullptr, ap != nullptr);
        }
        const bool away_goal = (events & 2) && chance_is_goal(h, a, 1, minute, rng, level, hp, ap, &away_shot, kinds_h, kinds_a);
        if ((events & 2) && ap && ap->on_chance) ap->on_chance(away_goal, a, asq);
        if ((events & 2) && hp && hp->on_opponent_chance) hp->on_opponent_chance(away_goal, a);
        if ((events & 2) && minute <= 60) { shown.period_chances[(minute - 1) / 20][1]++; if (away_goal) shown.period_goals[(minute - 1) / 20][1]++; }
        if (away_goal) {
            score.away++;
            if (stats && h.skaters + a.skaters != 11 && a.skaters > h.skaters) stats->power_play_goals[1]++;
            if (stats && h.skaters + a.skaters != 11 && a.skaters < h.skaters) stats->short_handed_goals[1]++;
            const bool equal = h.skaters == a.skaters;
            { const int before = hbox.code(); hbox.goal_against(h, hp ? &hsq : nullptr);
              if (hp && hbox.code() != before) recount_on_ice(h, hsq, *hh2.players, hbox.running(), hh2.per_line); }
            if (stats) stats->after_goal(1, equal, a.skaters, h.skaters, ap != nullptr, hp != nullptr);
        }
        // ovl06_2522 (whole-program differential test): energy, line changes and effort only run for a match with a manager
        // (var_F); computer against computer matches skip them (their draws do not appear in the original's stream).
        // Order of the original minute (ovr030:2A47..2B64): energy, line-energy refresh, tactics mode (odd minutes, 162c:000b),
        // line changes, effort of the situation (even minutes), penalties.
        bool swap_sides = false;
        if (hp && std::getenv("EM_DEBUG_SCORE")) std::fprintf(stderr, "minute %d score %d:%d draws %zu\n", minute, score.home, score.away, g_game_log ? g_game_log->size() : size_t(0));
        if (hp || ap || (watched && minute <= 60)) {  // REAL-CODE: the watched list of the other matches ends with the regular time (the overtime minutes of the other pairs skip energy, swap and line changes)
            const bool decided_now = opts.knockout && minute > 60 && score.home != score.away;  // the golden goal of the overtime: the minute ends without energy (REAL-CODE)
            if (!decided_now) {
                energy_minute(h, hp ? &hsq : nullptr, rng);
                energy_minute(a, ap ? &asq : nullptr, rng);
                if (hp) refresh_line_energy(h, *hp);
                if (ap) refresh_line_energy(a, *ap);
            }
            swap_sides = !decided_now && (rng.borland ? rng.borland->range(0, 1) : 0) != 0;  // ovr030:2A78: the original swaps A and B for the rest of the minute
            if (minute % 2 != 0) {  // the swapped side is team A of the routine (its rules are not symmetric)
                if (swap_sides) update_tactics_mode(a, h, ap, hp, shown_away, shown_home, minute, kinds_a, kinds_h);
                else update_tactics_mode(h, a, hp, ap, shown_home, shown_away, minute, kinds_h, kinds_a);
            }
            if (swap_sides) {  // ovr030:2AD9: the swapped side changes lines first (mode 3 follows the already changed line)
                change_lines(a, h, ap, arotation);
                change_lines(h, a, hp, hrotation);
            } else {
                change_lines(h, a, hp, hrotation);
                change_lines(a, h, ap, arotation);
            }
            if (minute % 2 == 0) {
                apply_effort(h, shown_home, shown_away, hp ? &hsq : nullptr, hp ? hp->effort_control : 0, hp ? hp->offence_control : 0);
                apply_effort(a, shown_away, shown_home, ap ? &asq : nullptr, ap ? ap->effort_control : 0, ap ? ap->offence_control : 0);
            }
        }
        if (minute != 60 && minute != 70 && !(opts.knockout && minute > 60 && score.home != score.away)) {  // no penalties in the minute of the golden goal (REAL-CODE)
            penalty_minute(swap_sides ? *apen : *hpen, swap_sides ? *hpen : *apen, true, rng);
            penalty_minute(swap_sides ? *hpen : *apen, swap_sides ? *apen : *hpen, true, rng);
        }
        if (stats)
            for (int kind = 0; kind < 2; kind++) stats->line_minutes[kind][h.current_line[kind]]++;
        // ovl06_161d checks the completed third period, including minute 60.
        // A last-minute equaliser must extend; a last-minute winner must not.
        if (minute == 60 && opts.knockout && score.home == score.away) {
            last = 70;
            score.decision_marker = 30;  // ovl05_3573, before minutes 61-70
        }
        if (opts.knockout && minute > 60 && score.home != score.away) last = minute;  // sudden death: the first goal of the overtime decides (REAL-CODE)
        const bool more = minute < last;
        // a knock-out game that is still level after its last minute goes to the shoot-out at once (the other games go on)
        if (!more && opts.knockout && !shootout_done && score.home == score.away) {
            shootout(score, rng, true, hp || ap);
            shootout_done = true;
        }
        return more;
    }

    MatchResult finish() {
        if (forfeit) return forfeit_result;
        while (step()) {}
        // the original leaves running penalties of a team without manager on the record (skaters, +0x33)
        if (std::getenv("EM_DEBUG_FIN"))
            std::fprintf(stderr, "finish %s - %s: code %d/%d skaters %d/%d score %d:%d\n", h.name.c_str(), a.name.c_str(), hbox.code(), abox.code(), h.skaters, a.skaters, score.home, score.away);
        if (hp) hbox.finish(h, &hsq); else h.penalty_code = hbox.code();
        if (ap) abox.finish(a, &asq); else a.penalty_code = abox.code();
        if (stats) {
            stats->final_teams = {h, a};
            stats->penalties[0] += hbox.started();
            stats->penalties[1] += abox.started();
            stats->penalty_minutes[0] += hbox.minutes_assessed();
            stats->penalty_minutes[1] += abox.minutes_assessed();
            stats->final_squads[0] = std::move(hsq);
            stats->final_squads[1] = std::move(asq);
        }
        return score;
    }
};

MatchRun::MatchRun(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level, const HumanSquad* hh, const HumanSquad* ha,
                   MatchStats* stats, const MatchOptions& opts)
    : p_(std::make_unique<Impl>(home, away, rng, level, hh, ha, stats, opts)) {}
MatchRun::~MatchRun() = default;
bool MatchRun::step() { return p_->step(); }
void MatchRun::set_watched(bool on) { p_->watched = on; }
MatchProgress MatchRun::progress() const {
    MatchProgress m = p_->shown;
    m.minute = p_->minute;
    m.score = p_->forfeit && p_->minute == 0 ? MatchResult{} : p_->score;
    m.penalty_minutes[0] = p_->hbox.minutes_assessed();
    m.penalty_minutes[1] = p_->abox.minutes_assessed();
    m.penalty_code[0] = p_->hbox.code();
    m.penalty_code[1] = p_->abox.code();
    return m;
}
void MatchRun::overtime_break() { if (p_->hp || p_->ap) return; prepare_match_energy(p_->h, nullptr, true); prepare_match_energy(p_->a, nullptr, true); }
MatchResult MatchRun::finish() { return p_->finish(); }
const HokTeam& MatchRun::home_state() const { return p_->h; }
const HokTeam& MatchRun::away_state() const { return p_->a; }

MatchResult league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level, const HumanSquad* hh,
                         const HumanSquad* ha, MatchStats* stats, const MatchOptions& opts) {
    MatchRun run(home, away, rng, level, hh, ha, stats, opts);
    return run.finish();
}

void match_energy_minute(HokTeam& team, std::vector<HokSquadPlayer>* squad, GameRandom& rng) { energy_minute(team, squad, rng); }

void match_change_lines(HokTeam& team, const HokTeam& opponent, LineRotation& rotation) { change_lines(team, opponent, nullptr, rotation); }

bool match_chance_is_goal(const HokTeam& home, const HokTeam& away, int side, int minute, GameRandom& rng, int level) {
    return chance_is_goal(home, away, side, minute, rng, level, nullptr, nullptr);
}

}  // namespace em
