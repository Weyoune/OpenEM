// SPDX-License-Identifier: GPL-3.0-only
// Match simulation (see match.hpp): the game's random generator, line and playing strengths,
// goal points and chances, the whole-match quick results and MatchRun, the minute-by-minute
// match with penalties, line changes, effort, injuries and goal scenes. The order of all random
// draws follows the original game exactly, so that a seeded run reproduces its results.
// Several EM_DEBUG_* environment variables print diagnostic traces to stderr.
#include "game/match.hpp"
#include <set>
#include <string>
#include <cstdio>
#include <cstdlib>
#include "game/penalties.hpp"
#include "game/scorers.hpp"

#include <algorithm>
#include <cstdlib>

namespace em {
const std::vector<HokSquadPlayer>* g_minute_lineup = nullptr;

namespace {
// Share of defence / attack lines per strength mode (percent).
constexpr int kModeWeight[3][2] = {{85, 15}, {50, 50}, {25, 75}};
// Goal range of the stronger side by strength difference 0..19.
constexpr int kGoalRange[20] = {2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4};

int clamp_line(int v) { return std::clamp(v, 0, 100); }  // line values are bounded to 0..100

// Half the time the larger of two rolls, otherwise the second one (the coin comes from the
// Borland stream when attached).
int better_of(int a, int b, GameRandom& rng) {
    if (std::getenv("EM_DEBUG_BO")) std::fprintf(stderr, "better_of B%zu G%zu borland %d\n", g_borland_log ? g_borland_log->size() : size_t(0), g_game_log ? g_game_log->size() : size_t(0), rng.borland != nullptr);
    const long coin = rng.borland ? rng.borland->range(0, 1) : rng.range(0, 1);
    return coin == 0 ? b : std::max(a, b); }

// Offence bonus 0..2 of a team: rand(offence setting (flags bits 4-6), 7) - 5, at least 0.
int offence_bonus(const HokTeam& t, GameRandom& rng) {
    if (std::getenv("EM_DEBUG_OB") && g_game_log && g_game_log->size() + size_t(std::getenv("EM_DEBUG_OBW") ? std::atoi(std::getenv("EM_DEBUG_OBW")) : 40) > size_t(std::atoi(std::getenv("EM_DEBUG_OB"))) &&
        g_game_log->size() < size_t(std::atoi(std::getenv("EM_DEBUG_OB"))) + 5)
        std::fprintf(stderr, "offence %s flags %02x base %d eff %d at G%zu\n", t.name.c_str(), t.flags, t.offence_base, t.effort_current, g_game_log->size());
    int r = int(rng.range((t.flags & 0x70) >> 4, 7));
    return std::max(0, r - 5);
}
}  // namespace

int g_goal_scenes = 0;
int g_try_round = -1;  // round index used by the EM_TRY_NATCPU experiment switch
int g_match_speed = 70;
std::array<uint8_t, 14> g_display_flags{};

int reporter_minute_draw(GameRandom& rng, bool display) {
    if (!display || g_match_speed >= 70 || std::getenv("EM_OLD_NOMSGDRAWS")) return -1;  // only with the display on in normal play and speed < 70
    return int(rng.range(0, 4));
}

// The goal scene of one chance, drawn with Borland rand: number rand(2, 11), then
// rand(0, goal ? 0xD2 : 0xFF) whose 0 forces the penalty variant e, then for e (or else for six skaters, j) rand(2, 2)
GoalScene draw_goal_scene(BorlandRandom& b, int side, bool goal, bool penalty, bool six_skaters) {
    GoalScene s;
    s.side = side; s.goal = goal; s.penalty = penalty;
    s.number = int(b.range(2, 11));
    const bool forced = b.range(0, goal ? 0xD2 : 0xFF) == 0;
    if (penalty || forced) { s.variant = 'e'; s.number = int(b.range(2, 2)); }
    else if (six_skaters) { s.variant = 'j'; s.number = int(b.range(2, 2)); }
    return s;
}

std::function<int(const SceneCall&, const GoalScene&, int)> g_scene_replay;
namespace {
SceneCall g_scene_call;  // the chance of the scene being drawn (set by MatchRun before the draw)
// draw_goal_scene with the replays of the match display (g_scene_replay): every play draws again; all plays go to the log
void log_goal_scene(BorlandRandom& b, int side, bool goal, bool penalty, bool six_skaters, std::vector<GoalScene>* log) {
    for (int play = 1;; play++) {
        GoalScene s = draw_goal_scene(b, side, goal, penalty, six_skaters);
        const int answer = g_scene_replay ? g_scene_replay(g_scene_call, s, play) : -1;
        s.shown = answer >= 0;
        if (log) log->push_back(s);
        if (answer != 1 || play >= 3) break;
    }
}
}  // namespace

std::vector<std::pair<long, long>>* g_game_log = nullptr;
std::vector<const char*>* g_game_scopes = nullptr;
std::vector<uint32_t>* g_game_states = nullptr;

long GameRandom::range(long lo, long hi) {
    if (g_game_states) g_game_states->push_back(s_);  // state before the draw (draw tracing)
    if (g_game_log) { g_game_log->push_back({lo, hi}); if (g_game_scopes) g_game_scopes->push_back(g_draw_scope); }
    s_ = uint32_t((uint64_t(s_) * 16 + 167) % kModulus);
    // 32-bit signed product that wraps like the original: negative results appear when
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
    if (std::getenv("EM_DEBUG_LS"))
        std::fprintf(stderr, "ls %s line %d kind %d play %d man %d -> s1 %d s2 %d form %d (eff %d en %d)\n", t.name.c_str(), line, kind, int(in_play), man_advantage, v.skill1, v.skill2, v.form,
                     t.effort_current, t.energy[kind][line]);
    return v;
}

LineValues line_strength(const HokTeam& t, const HumanSquad& hs, int line, int kind, bool in_play, int man_advantage, int minute) {
    if (in_play && g_try_round == 7 && std::getenv("EM_TRY_NATCPU") && t.name == std::getenv("EM_TRY_NATCPU")) return line_strength(t, line, kind, in_play, man_advantage);
    constexpr int kGoalieWeight[2] = {80, 140};  // defence weight of a skater / the goalkeeper (percent)
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
        // the national team (manager index 5) in play: + player record +0x19 >> 3, at most 99
        if (hs.national && in_play && !std::getenv("EM_OLD_NATBONUS")) v = std::min(99, v + (players[size_t(p.player)].raw[0x19] >> 3));
        if (kind == 0) v = kGoalieWeight[goalie ? 1 : 0] * v / 100;
        s2 += v;
        f += p.form;
        if (!goalie) count++;
    }
    // Both kinds read the defence players-per-line value here, including
    // the attack bonus. Four attackers alone do not select this rule.
    if (hs.per_line[0] == 4) s2 = kind == 0 ? 0 : s2 + (s2 >> 1);
    s1 /= 3;
    s2 /= 3;
    f /= 3;
    if (kind == 0) hs.goalie_found = goalie_found;
    if (kind == 0 && (man_advantage < 0 || !goalie_found)) {
        s2 -= (45 - lo_nibbles) + (-1 - man_advantage) * 16;
        if (!goalie_found) s2 /= 3;
        s2 = std::max(0, s2);
    }
    // The goalie-found flag is only rewritten by defence calls (kind 0); an attack call reads the one left by
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
    if (sum != 0) sum += (t.bonus - 44) / 2;  // C division truncates towards 0, as in the original
    return sum;
}

int playing_strength(const HokTeam& t, StrengthMode mode, int minute, int man_advantage, int defence_line,
                     int attack_line) {
    return playing_strength(t, nullptr, mode, minute, man_advantage, defence_line, attack_line);
}

namespace {
// The goal-point computation shared by the whole-match and per-minute modes: goal points of both
// sides for the given minute (whole = the call with minutes 0..60).
// `special`: play-off mode or overtime (minute > 60) - halves the points (+1) and, per
// minute, adds one point each.
static int g_goal_kinds[2][2];  // per-kind values of the last goal_points call (the original caches them too)
// Neutral-mode playing strength from cached per-kind values (as playing_strength computes it).
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
    if (std::getenv("EM_DEBUG_GP")) std::fprintf(stderr, "G=%zu goal_points min %d: sh %d [k %d/%d] (cpu %d) sa %d lines %d/%d %d/%d skaters %d/%d human %d/%d eff %d/%d en %d %d %d %d %d %d %d %d\n", g_game_log ? g_game_log->size() : size_t(0), minute, sh, g_goal_kinds[0][0], g_goal_kinds[0][1], playing_strength(h, nullptr, StrengthMode::Neutral, minute, h.skaters - a.skaters, h.current_line[0], h.current_line[1]), sa, h.current_line[0], h.current_line[1], a.current_line[0], a.current_line[1], h.skaters, a.skaters, hh != nullptr, ha != nullptr, h.effort_current, a.effort_current, h.energy[0][0], h.energy[0][1], h.energy[0][2], h.energy[0][3], h.energy[1][0], h.energy[1][1], h.energy[1][2], h.energy[1][3]);
    if (hh && minute == (std::getenv("EM_DEBUG_MIN") ? std::atoi(std::getenv("EM_DEBUG_MIN")) : 2) && std::getenv("EM_DEBUG_GP")) {
        std::fprintf(stderr, "globals: level %d per %d/%d minute %d\n", hh->level, hh->per_line[0], hh->per_line[1], minute);
        std::fprintf(stderr, "team11: flags %d effort %d lines %d/%d energy %d %d %d %d %d %d %d %d skill1 %d/%d skill2 %d/%d form %d/%d bonus %d\n", h.flags, h.effort_current,
                     h.current_line[0], h.current_line[1], h.energy[0][0], h.energy[0][1], h.energy[0][2], h.energy[0][3], h.energy[1][0], h.energy[1][1], h.energy[1][2], h.energy[1][3],
                     h.skill1[0], h.skill1[1], h.skill2[0], h.skill2[1], h.form[0], h.form[1], h.bonus);
    }
    if (hh && minute == (std::getenv("EM_DEBUG_MIN") ? std::atoi(std::getenv("EM_DEBUG_MIN")) : 2) && std::getenv("EM_DEBUG_GP"))
        for (size_t s = 0; s < hh->squad->size() && s < 26; s++) {
            const auto& p = (*hh->squad)[s];
            std::fprintf(stderr, "squad %2zu: %3d %3d %3d %3d %3d %3d %3d %3d %3d exh %d/%d\n", s, p.home_line, p.role, p.player + 1, p.skill_a, p.skill_b, p.form, p.line, p.nibbles, p.fitness, p.raw_exhaustion, p.exhaustion);
        }
    int d = sh / 3 - sa / 3;
    if (const char* dt = std::getenv("EM_DEBUG_TEAMS"); dt && h.name.find(dt) != std::string::npos)  // the records of a named pair
        for (const HokTeam* t : {&h, &a})
            std::fprintf(stderr, "teams min %d %s: skill1 %d/%d skill2 %d/%d form %d/%d energy %d %d %d %d / %d %d %d %d lines %d/%d effort %d flags %02x skaters %d bonus %d\n",
                         minute, t->name.c_str(), t->skill1[0], t->skill1[1], t->skill2[0], t->skill2[1], t->form[0], t->form[1],
                         t->energy[0][0], t->energy[0][1], t->energy[0][2], t->energy[0][3], t->energy[1][0], t->energy[1][1],
                         t->energy[1][2], t->energy[1][3], t->current_line[0], t->current_line[1], t->effort_current, t->flags, t->skaters, t->bonus);
    if (std::getenv("EM_DEBUG_D"))
        std::fprintf(stderr, "d B%zu min %d %s-%s sh %d sa %d d %d lines %d/%d %d/%d\n", g_borland_log ? g_borland_log->size() : size_t(0), minute, h.name.c_str(), a.name.c_str(), sh, sa, d,
                     h.current_line[0], h.current_line[1], a.current_line[0], a.current_line[1]);
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
    if (special || minute > 60) {  // play-off mode or overtime
        if (sh != 0) home = home / 2 + 1;
        if (sa != 0) away = away / 2 + 1;
    }
    if (h.skaters < a.skaters) home >>= 2;
    if (a.skaters < h.skaters) away >>= 2;
    if (minute < 58 && ((h.flags | a.flags) & 0x08)) home = away = 9;  // bit 3: goalkeeper pulled
    if (!whole && (special || total > 60)) {  // play-off mode, or a total match length above 60 (the overtime loop of the cup)
        home++;
        away++;
    }
    if (std::getenv("EM_DEBUG_GOALCALLS"))  // trace line: minute@draw count=home:away
        std::fprintf(stderr, "goalcall %s-%s %d@%zu=%d:%d sh %d sa %d\n", h.name.c_str(), a.name.c_str(), minute,
                     g_game_log ? g_game_log->size() : size_t(0), home, away, sh, sa);
    return {home, away};
}

// The goal decision of one chance: does the chance of side 0 (home) or 1 (away) become a goal?
// `shot` (optional) receives whether the chance became a shot; `kinds_h` / `kinds_a` are the
// cached per-kind strengths of the goal-point computation.
bool chance_roll(const HokTeam& h, const HokTeam& a, int side, int minute, GameRandom& rng, int level,
                 const HumanSquad* hh, const HumanSquad* ha, bool* shot, const int* kinds_h, const int* kinds_a);

// After the rolls, a chance against a manager's club whose last defence strength call found no goalie
// on the ice is a goal in any case (even when the rolls said otherwise).
bool chance_is_goal(const HokTeam& h, const HokTeam& a, int side, int minute, GameRandom& rng, int level,
                    const HumanSquad* hh, const HumanSquad* ha, bool* shot = nullptr, const int* kinds_h = nullptr,
                    const int* kinds_a = nullptr) {
    if (chance_roll(h, a, side, minute, rng, level, hh, ha, shot, kinds_h, kinds_a)) return true;
    const HumanSquad* defending = side == 0 ? ha : hh;
    return defending && !defending->goalie_found && !std::getenv("EM_OLD_NOGOALIE");
}

bool chance_roll(const HokTeam& h, const HokTeam& a, int side, int minute, GameRandom& rng, int level,
                 const HumanSquad* hh, const HumanSquad* ha, bool* shot, const int* kinds_h, const int* kinds_a) {
    if (shot) *shot = false;
    const int ch = h.skaters - a.skaters, ca = a.skaters - h.skaters;
    // Playing strengths without recomputation: the per-kind values come from the goal-point step of the same minute
    const int h_def = kinds_h ? strength_from_kinds(h, kinds_h, StrengthMode::Defensive) : playing_strength(h, hh, StrengthMode::Defensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_off = kinds_a ? strength_from_kinds(a, kinds_a, StrengthMode::Offensive) : playing_strength(a, ha, StrengthMode::Offensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int h_off = kinds_h ? strength_from_kinds(h, kinds_h, StrengthMode::Offensive) : playing_strength(h, hh, StrengthMode::Offensive, minute, ch, h.current_line[0], h.current_line[1]);
    const int a_def = kinds_a ? strength_from_kinds(a, kinds_a, StrengthMode::Defensive) : playing_strength(a, ha, StrengthMode::Defensive, minute, ca, a.current_line[0], a.current_line[1]);
    const int away_edge = (a_off - h_def) / 3, home_edge = (h_off - a_def) / 3;
    const int q = (h_def + a_off + a_def + h_off) < rng.range(550, 850) ? 1 : 0;
    int d;
    if (side == 0) {  // home: first hurdle rand(0, 2q + 4) > 1
        if (rng.range(0, 2 * q + 4) <= 1) return false;
        if (shot) *shot = true;
        d = home_edge;
    } else {  // away: the side has the game level in its first hurdle
        if (rng.range(0, 3 * level + 2 * q + 1) <= level) return false;
        if (shot) *shot = true;
        d = away_edge;
    }
    if (d >= 0) return rng.range(0, (d >> 2) + q + 2) != 0;
    if (rng.range(0, -d / 4 + 1) == 0) return true;
    return rng.range(0, 6 - q) == 0;
}
}  // namespace

// The goal points of one minute give chances (1 in 20 per point); a home chance sets the event
// value to 1, each away chance adds 2. Returns the event value (0 = no chance).
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

bool g_track_guard = false;
int g_manager_guard = 5;

namespace {
// Score situation of a side for the effort/offence tables: a high lead is more than 2 goals.
int situation_of(int own, int opp) {
    if (own - 2 > opp) return kHighLead;
    if (own > opp) return kNarrowLead;
    if (own == opp) return kDraw;
    if (opp - 2 > own) return kHighDeficit;
    return kNarrowDeficit;
}

// Energy of a line. Computer and human teams both read the cached team value, which the energy
// minute refreshes after updating the players' fitness.
int line_energy(const HokTeam& t, const std::vector<HokSquadPlayer>*, int kind, int line) {
    return t.energy[kind][line];
}

// One minute of energy - the lines on the ice lose
// (rand(0, effort) + 75) / 40 + 1, +4 if the shift is at least rand(0, 5) minutes old, +8 if it is
// longer than rand(5, 15); lines on the bench below 90 win 1 (+1 more if rand(0, 4) == 0 and
// skill 1 > rand(0, 100)). drain() is the loss of one line or player on the ice.
int drain(const HokTeam& t, int shift, GameRandom& rng) {
    int d = int((rng.range(0, t.effort_current) + 75) / 40) + 1;
    if (shift >= rng.range(0, 5)) d += 4;
    if (shift > rng.range(5, 15)) d += 8;
    return d;
}


// The energy minute for a computer team (line energies) or a human team (per player fitness,
// plus the occasional exhaustion increase of a player on the ice).
void energy_minute(HokTeam& t, std::vector<HokSquadPlayer>* squad, GameRandom& rng) {
    note_manager_guard(t.raw);
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
    // The original walks the packed occupied prefix once, in squad-slot order. Each
    // kind's shift advances only when its first on-ice player is encountered.
    // It keeps the advanced value in a single variable: the drain of every later player uses the shift that
    // was advanced last, whichever kind it belongs to (a defender listed after the first
    // forward on the ice gives the forwards behind him the defence shift)
    bool advanced[2] = {};
    int last_shift = -1;
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
                last_shift = t.shift[kind];
            }
            if (rng.range(0, 4 + (p.skill_b < p.skill_a)) == 0) {
                p.raw_exhaustion = uint8_t(p.raw_exhaustion + 1);
                p.exhaustion = p.raw_exhaustion * 2 / 3;
            }
            const int shift = last_shift >= 0 && !std::getenv("EM_OLD_DRAINSHIFT") ? last_shift : t.shift[kind];
            const int d = drain(t, shift, rng);
            if (const char* ds = std::getenv("EM_DEBUG_DRAIN"); ds && int(slot) == std::atoi(ds))
                std::fprintf(stderr, "drain slot %zu G%zu effort %d shift %d (own %d) -> %d (fitness %d)\n", slot,
                             g_game_log ? g_game_log->size() : size_t(0), t.effort_current, shift, t.shift[kind], d, e);
            e = std::clamp(e - d, 1, 99);
        } else if (e < 90) {
            int bonus = (rng.range(0, 4) == 0 && p.skill_a > rng.range(0, 100)) ? 1 : 0;
            e += 1 + bonus;
        }
        p.fitness = e;
    }
}

// Line changes by the tactics mode (+0x2C bits 0-2). Modes 1 and 2
// (two lines / automatic): after more than 3 minutes on the ice the freshest line comes on (the
// second freshest if the freshest is already playing); mode 3 follows the opponent's line of the
// other kind unless that is line 3; mode 4 rotates by the manager's minute quotas; mode 0 (all
// computer teams in the saves) never changes.
// The original also compares "energy + 4 < best line" - a line index, so it never fires.
// has_fourth_line: whether the human squad has a player in line 4 of `kind`.
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
    if (human && human->line_control == 5) return;  // manual/live control: the manager changes lines himself
    if (human && human->line_control != 0) t.flags = (t.flags & 0xF8) | human->line_control;
    const int mode = t.flags & 7;
    const auto* squad = human ? human->squad : nullptr;
    for (int kind = 0; kind < 2; kind++) {
        if (kind == 0 && (t.flags & 0x08)) continue;
        const int current = t.current_line[kind];
        int next = current;
        const int lines = mode == 1 ? 2 : 3 + int(has_fourth_line(human, kind));
        // best/runner-up in one pass, as in the original: a new best pushes the old best down only if it
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
// Called by the match minute on odd minutes: tactics mode (+0x2C bits 0-2) of both sides from the score,
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
        if (std::getenv("EM_DEBUG_MODE")) std::fprintf(stderr, "mode min %d pa %d pb %d (kinds %d/%d, %d/%d; full %d/%d) sk %d/%d score %d:%d lines A %d/%d B %d/%d\n", minute, pa, pb, kinds_a ? kinds_a[0] : -1, kinds_a ? kinds_a[1] : -1, kinds_b ? kinds_b[0] : -1, kinds_b ? kinds_b[1] : -1, playing_strength(a, ha, StrengthMode::Neutral, minute, skaters_a - skaters_b, a.current_line[0], a.current_line[1]), playing_strength(b, hb, StrengthMode::Neutral, minute, skaters_b - skaters_a, b.current_line[0], b.current_line[1]) * 1000 + playing_strength(b, nullptr, StrengthMode::Neutral, minute, skaters_b - skaters_a, b.current_line[0], b.current_line[1]), skaters_a, skaters_b, score_a, score_b, a.current_line[0], a.current_line[1], b.current_line[0], b.current_line[1]);
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

// Called by the match minute for a team with a manager: the players on the ice per category
// (goalies counted on any line, skaters only on the current line of their kind) set the per-line counts and the
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
    // the recount overwrites the skater count of the record (defenders + attackers) and sets/clears the pulled-goalie bit (six skaters)
    t.skaters = defenders + attackers;
    if (defenders + attackers == 6) t.flags |= 0x08; else t.flags &= uint8_t(~0x08);
}

// One side of a match as seen by the penalty rules.
struct PenaltySide {
    HokTeam& team;                       // working team record
    PenaltyBox& box;                     // running penalties of this side
    std::vector<HokSquadPlayer>* squad;  // working squad of a manager's side, else nullptr
    const HumanSquad* human;             // the manager's side, else nullptr
    BorlandRandom& selection_rng;        // stream for the selection of the penalised player
    MatchStats* stats;                   // optional statistics
    int side;                            // 0 home, 1 away
    bool same_club = false;  // both sides are one club (possible in a replayed cup final)
    std::function<void()> recount;  // a manager's side: the release of a player ends with the recount of the players on the ice
};

// The penalty check of one side for one minute: ticks the running penalties (when `tick`) and,
// with at most one penalty running, draws whether a new penalty (2 minutes, or major when none
// is running) is given. With `may_pair` a quarter of the penalties also penalise the other side
// (the paired call also ticks that side, as in the original). Returns whether a penalty began.
bool penalty_minute(PenaltySide& side, PenaltySide& other, bool may_pair, GameRandom& rng, bool tick = true) {
    const int before_tick = side.box.running();
    const int running = tick ? side.box.tick(side.team, side.squad) : side.box.running();
    // a timer ran out: the release recounts the players on the ice, so an empty place in the line on the ice (an
    // injured player still assigned) is not filled by the returning player
    if (tick && running < before_tick && side.recount && !std::getenv("EM_OLD_TICKRECOUNT")) side.recount();
    if (side.squad && std::getenv("EM_DEBUG_BOX")) std::fprintf(stderr, "pmin %s tick %d may_pair %d running %d at %zu\n", g_draw_scope, int(tick), int(may_pair), running, (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
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
        }) <= 7) return false;  // protect a depleted human lineup (at most 7 players in lines)
    const bool major = running == 0 && rng.range(0, 15) == 0;
    // the pair roll is skipped when both sides are the same club
    const bool pair = may_pair && !side.same_club && rng.range(0, 3) == 0 && running == 0;
    // The original books opportunities before attempting to select the player.
    // With the goalkeeper pulled (six skaters) the penalty brings him back: 4 skaters remain.
    // For the opportunity count a pulled goalkeeper counts as a skater: 6 - 1 = 5 (so 6 against
    // 3 is no power play for the side two men down).
    const int count_skaters = side.team.skaters == 6 && std::getenv("EM_OLD_PPGOALIE") ? 4 : side.team.skaters - 1;
    const int token = side.stats ? side.stats->record(side.side, count_skaters,
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
    if (selection_failed) return false;  // a failed selection aborts the paired call after the team penalty, as in the original
    if (pair && !penalty_minute(other, side, false, rng, true) && side.stats) side.stats->rollback(token);
    return true;
}
}  // namespace

void recount_skaters(HokTeam& t, const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int penalties,
                     int per_line[2]) {
    recount_on_ice(t, squad, players, penalties, per_line);
}

void apply_effort(HokTeam& t, int own, int opp) { apply_effort(t, own, opp, nullptr); }

void apply_effort(HokTeam& t, int own, int opp, const std::vector<HokSquadPlayer>* squad,
                  int effort_control, int offence_control) {
    const int situation = situation_of(own, opp);
    int effort = t.effort_base + t.effort[situation];
    const int energy = (line_energy(t, squad, 0, t.current_line[0]) + line_energy(t, squad, 1, t.current_line[1])) >> 1;
    if (energy < 50) effort += energy - 50;
    if (const char* de = std::getenv("EM_DEBUG_EFF"); de && g_game_log && g_game_log->size() + 200 > size_t(std::atoll(de)) && g_game_log->size() < size_t(std::atoll(de)) + 50)
        std::fprintf(stderr, "eff G%zu %s: base %d sit %d (%d) energy %d lines %d/%d en %d %d / %d %d control %d -> %d\n", g_game_log->size(), t.name.c_str(),
                     t.effort_base, situation, t.effort[situation], energy, t.current_line[0], t.current_line[1], t.energy[0][t.current_line[0]],
                     t.energy[0][0], t.energy[1][t.current_line[1]], t.energy[1][0], effort_control, effort);
    if (effort_control != 2) t.effort_current = std::clamp(effort, 0, 100);
    if (offence_control < 2)
        t.flags = (t.flags & 0x8F) | (((t.offence_base + t.offence[situation]) << 4) & 0x70);
}

void setup_computer_team(HokTeam& t, const HokTeam& opp, const HumanSquad* human, const HumanSquad* /*opponent_human*/) {
    // Neutral strengths (doubled) decide; tables of effort per situation ([close], [not close])
    // and offence per situation ([weaker], [stronger])
    static constexpr int kEffortTable[2][kTacticSituations] = {{0, 25, 20, 30, 0}, {20, 20, 15, 20, 0}};
    static constexpr int kOffenceTable[2][kTacticSituations] = {{5, 6, 4, 4, 0}, {4, 5, 6, 7, 4}};
    // The playing strength takes the lines on the ice from the team records.
    // At the time of the setup the team is not yet in the match pair list, so the line strength
    // takes its computer branch (team record) even for the human team.
    const int own = playing_strength(t, nullptr, StrengthMode::Neutral, 0, 0, t.current_line[0], t.current_line[1]) * 2;
    const int other = playing_strength(opp, nullptr, StrengthMode::Neutral, 0, 0, opp.current_line[0], opp.current_line[1]) * 2;
    const bool stronger = other <= own;
    if ((human || std::getenv("EM_DEBUG_ALL")) && std::getenv("EM_DEBUG_SETUP")) std::fprintf(stderr, "setup %s %s vs %s: own %d other %d lines %d/%d en %d %d %d %d form %d/%d bonus %d s1 %d/%d s2 %d/%d, effort before %d/%d, close=%d\n", human ? "human" : "cpu", t.name.c_str(), opp.name.c_str(), own, other, t.current_line[0], t.current_line[1], t.energy[0][0], t.energy[0][1], t.energy[1][0], t.energy[1][1], t.form[0], t.form[1], t.bonus, t.skill1[0], t.skill1[1], t.skill2[0], t.skill2[1], t.effort_current, opp.effort_current, std::abs(own - other) < 25);
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

// Penalty shoot-out deciding a drawn knock-out match, adding the shoot-out goals to `r`: the
// quick path (no display) and the shoot-out after a full minute game. `human`: a manager's side
// takes part (shot by shot, with goal scenes when `scenes`, logged to `scene_log`, and the
// player picks of `after_shot`).
void shootout(MatchResult& r, GameRandom& rng, bool minute_game = false, bool human = false, bool scenes = false,
              std::vector<GoalScene>* scene_log = nullptr, const std::function<void(int, bool)>* after_shot = nullptr) {
    int x = 0, y = 0;
    // the original draws the shoot-out with Borland rand
    auto draw = [&](int lo, int hi) { return rng.borland ? rng.borland->range(lo, hi) : int(rng.range(lo, hi)); };
    if (minute_game && human) {
        // Five complete pairs, then complete sudden-death pairs; a coin decides who shoots first.
        // A shot succeeds when Borland rand(0,10) > 6. Without an attached Borland stream the
        // shots draw from the supplied GameRandom stream.
        const int first = draw(0, 1);
        int goals[2] = {};
        // with goal scenes every shot then draws its scene with the forced variant e
        auto shot = [&](int side) {
            const bool hit = draw(0, 10) > 6;
            if (scenes && rng.borland) {
                g_scene_call.goals[0] = r.home + goals[0]; g_scene_call.goals[1] = r.away + goals[1]; g_scene_call.minute = 0x78;
                log_goal_scene(*rng.borland, side, hit, true, false, scene_log);
            }
            goals[side] += hit;
            // the players of a managed side are picked after each of its shots (goal weights after a hit,
            // rand(120,170) after a miss; the goal itself is not booked)
            if (after_shot && *after_shot) (*after_shot)(side, hit);
        };
        for (int pair = 0; pair < 5 || goals[0] == goals[1]; pair++) {
            shot(first);
            shot(1 - first);
        }
        x = goals[0]; y = goals[1];
    } else {
        // Minute game between computer teams: a coin (0,1), then rand(2,5) pairs until the totals differ. Quick path: rand(0,2).
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
                           GameRandom& rng, int level, bool playoff) {
    return quick_competition_match(home, away, home_german, away_german, rng, level, true, playoff);
}

MatchResult quick_competition_match(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                                    GameRandom& rng, int level, bool knockout, bool playoff, bool wm) {
    // Quick path: whole-match results (quick_match), German clubs (team < 64) +6,
    // re-rolled up to min(4, |difference / 3|) times while the favourite does not win
    int sh = playing_strength(home, StrengthMode::Neutral, 0, 0, home.current_line[0], home.current_line[1]) + (home_german ? 6 : 0);
    int sa = playing_strength(away, StrengthMode::Neutral, 0, 0, away.current_line[0], away.current_line[1]) + (away_german ? 6 : 0);
    const int rerolls = std::min(4, std::abs(sh / 3 - sa / 3));
    if (std::getenv("EM_DEBUG_QUICK"))
        std::fprintf(stderr, "quick %s - %s: sh %d sa %d german %d/%d rerolls %d at G%zu lines %d/%d eff %d en %d %d %d %d / %d %d %d %d\n", home.name.c_str(), away.name.c_str(), sh, sa,
                     home_german, away_german, rerolls, g_game_log ? g_game_log->size() : size_t(0), home.current_line[0], home.current_line[1], home.effort_current, home.energy[0][0], home.energy[0][1], home.energy[0][2], home.energy[0][3], home.energy[1][0], home.energy[1][1], home.energy[1][2], home.energy[1][3]);
    MatchResult r;
    do {
        r = quick_match(home, away, rng, level, playoff);  // play-offs: halved goal points
        if (wm) {
            // The WM branch: both blocks move the home score only (a quirk of the original)
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 2) != 0)) {
                r.home -= int(rng.range(0, r.home));
                r.home += int(rng.range(0, r.home));
            }
            if (rng.range(0, 3) == 0 || (sh > sa && rng.range(0, 1) != 0)) {
                r.home += int(rng.range(0, r.home));
                r.home -= int(rng.range(0, r.home));
            }
        } else if (level == 0) {
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 2) != 0)) r.home -= int(rng.range(0, r.home));
            if (rng.range(0, 3) == 0 || (sh < sa && rng.range(0, 1) != 0)) r.away += int(rng.range(0, 4));
        }
    } while (rng.range(0, rerolls) != 0 && ((r.home <= r.away && sa < sh) || (r.away <= r.home && sh < sa)));
    if (std::getenv("EM_DEBUG_QUICK")) std::fprintf(stderr, "quick result %d:%d knockout %d\n", r.home, r.away, int(knockout));
    if (knockout && r.home == r.away) shootout(r, rng);
    return r;
}

void prepare_match_energy(HokTeam& team, std::vector<HokSquadPlayer>* squad, bool interval) {
    if (!interval) {
        if (team.flags & 0x08) team.current_line[0] = 2;
        if (!squad) team.flags &= 0xF8;  // a computer team's line mode is disabled, existing lines remain
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
            player.raw_exhaustion = std::clamp(player.raw_exhaustion, 50, 150);  // bounded before every match
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

// One league match as a sequence of minutes (the original plays all matches of the day minute by minute).
struct MatchRun::Impl {
    GameRandom& rng;
    int level;
    MatchStats* stats;
    MatchOptions opts;
    bool same_club = false;  // one club on both sides: the original plays both on the same team record
    HokTeam h, a_own;        // working copies of the home and away team records
    HokTeam& a;              // the away record: a_own, or h when both sides are one club
    std::vector<HokSquadPlayer> hsq, asq;  // working squads of managed sides
    HumanSquad hh2, ha2;                   // copies of the managed sides pointing at the working squads
    const HumanSquad* hp = nullptr;        // &hh2 for a managed home side, else nullptr
    const HumanSquad* ap = nullptr;        // &ha2 for a managed away side, else nullptr
    const std::vector<HokSquadPlayer>* outer_squad[2] = {nullptr, nullptr};  // the squads of the game (an injury in the minute changes them)
    const HumanSquad* outer_human[2] = {nullptr, nullptr};  // the managers' squads of the game (tactics set during the match)
    LineRotation hlocal, alocal;  // rotation counters when a side brings none
    PenaltyBox hbox, abox;        // running penalties of both sides
    BorlandRandom fallback_selection{1};  // selection stream when neither side nor rng supplies one
    std::unique_ptr<PenaltySide> hpen, apen;
    bool minute_check_off[2] = {false, false};  // the club left the pairing (pair-table error): no injury / event check per minute
    MatchResult score;     // running score
    int last = 60;         // last minute to play (70 with overtime)
    int minute = 0;        // last minute played
    bool forfeit = false;  // a side had too few players: no minutes are played
    MatchResult forfeit_result;

    Impl(const HokTeam& home, const HokTeam& away, GameRandom& r, int lvl, const HumanSquad* hh, const HumanSquad* ha, MatchStats* st,
         const MatchOptions& o)
        : rng(r), level(lvl), stats(st), opts(o), same_club(&home == &away), h(home), a_own(away), a(same_club ? h : a_own) {
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
        // Working copies: the set-up (setup_computer_team, 5 skaters for everybody), then
        // per minute chances and goals, energy, line changes, effort and
        // penalties (not in minute 60) as in the original's matchday loop.
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
        if (hh) outer_squad[0] = hh->squad;
        if (ha) outer_squad[1] = ha->squad;
        outer_human[0] = hh;
        outer_human[1] = ha;
        hp = hh ? &hh2 : nullptr;
        ap = ha ? &ha2 : nullptr;
        // The penalty effort adjustment reads the match load (manager block +0xF0) of the manager
        // with the side's raw owner byte as index, i.e. the next manager after the side's own (a quirk of
        // the original). The match start writes the loads of both human sides first: when that next
        // manager is the opponent, the side reads his load of this match (e.g. effort 60 + 21 - 20 = 61).
        if (hh && ha && stats && h.raw && a.raw && !std::getenv("EM_OLD_PAIRLOAD")) {
            const int owner_home = h.raw[0x16], owner_away = a.raw[0x16];
            const int load_home = stats->initial_load[0], load_away = stats->initial_load[1];
            if (owner_home >= 1 && owner_home <= 5 && owner_away == owner_home + 1) hh2.penalty_effort_adjustment = load_away - 20;
            if (owner_away >= 1 && owner_away <= 5 && owner_home == owner_away + 1) ha2.penalty_effort_adjustment = load_home - 20;
        }
        // The set-up also covers a human team whose manager leaves effort and offence on automatic.
        // Each component follows its independent saved AUTOM./SEMI/MANUAL mode.
        // In the original both team records hold line energy 90 when the set-up runs (a human team's tired values from the
        // previous day are already refreshed); the strength comparison of the set-up reads them.
        for (HokTeam* r : {&h, &a}) {
            if (r->flags & 0x08) r->current_line[0] = 2;  // a goalkeeper pulled at the end of the last game starts on line 2
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
        hbox.use_timers(hp ? hp->timers : nullptr);
        abox.use_timers(ap ? ap->timers : nullptr);
        hpen = std::make_unique<PenaltySide>(PenaltySide{h, hbox, hp ? &hsq : nullptr, hp,
                                                          hp && hp->selection_rng ? *hp->selection_rng : (rng.borland ? *rng.borland : fallback_selection), st, 0});
        apen = std::make_unique<PenaltySide>(PenaltySide{a, abox, ap ? &asq : nullptr, ap,
                                                          ap && ap->selection_rng ? *ap->selection_rng : (rng.borland ? *rng.borland : fallback_selection), st, 1});
        hpen->same_club = apen->same_club = same_club;
        if (hp) hpen->recount = [this] { recount_on_ice(h, hsq, *hh2.players, hbox.running(), hh2.per_line); };
        if (ap) apen->recount = [this] { recount_on_ice(a, asq, *ha2.players, abox.running(), ha2.per_line); };
        if (same_club) share_box();
    }

    // One club on both sides: the running penalties live in the one team record (+0x32/+0x33), so both sides tick and
    // fill the same timers (the original handles penalties per team id). The away side then works on the home record and box.
    bool box_shared = false;
    bool synced_away = false;  // the pair-table error made the pair one club after the start: a_own follows h
    void share_box() {
        box_shared = true;
        apen = std::make_unique<PenaltySide>(PenaltySide{h, hbox, hp ? &hsq : nullptr, hp, hpen->selection_rng, stats, 1});
        apen->recount = hpen->recount;
        hpen->same_club = apen->same_club = true;
    }
    void sync_away() { if (synced_away) a_own = h; }
    PenaltyBox& away_penalties() { return box_shared ? hbox : abox; }

    // Three periods 1-20, 21-40, 41-60; a drawn knock-out match goes on 61-65 and 66-70
    bool watched = false;       // set_watched: full minute for every match from minute 21
    int display_override = -1;  // >= 0: the home team is (1) or is not (0) in the display list (pair-table error, see season.cpp)
    bool shootout_done = false;
    MatchProgress shown;  // display bookkeeping only, never read by the simulation
    // a match with a board (team byte +0x34 bit 7 at home or away) draws goal scenes when they are switched on
    bool board() const { return (h.raw && (h.raw[0x34] & 0x80)) || (a.raw && (a.raw[0x34] & 0x80)); }
    bool scenes_on() const { return g_goal_scenes != 0 && rng.borland && board() && !std::getenv("EM_OLD_NOSCENES"); }
    // set_messages and speed < 70: the board messages draw random numbers
    bool messages = false;
    bool message_draws() const { return messages && g_match_speed < 70 && !std::getenv("EM_OLD_NOMSGDRAWS"); }
    // A board message draws rand(0, 1) of the game stream before the blinking caption and before
    // the blinking result (not for an injury), both only without goal scenes
    void message_coins(bool injury) {
        if (g_goal_scenes) return;
        rng.range(0, 1);
        if (!injury) rng.range(0, 1);
    }
    int kinds_h[2] = {0, 0}, kinds_a[2] = {0, 0};
    bool idle_next = false;  // MatchRun::idle_next_minute
    // the player picks after each shot of a managed side in the shoot-out (a chance without a goal)
    std::function<void(int, bool)> shootout_picks() {
        if (std::getenv("EM_OLD_SHOOTPICKS")) return {};
        return [this](int side, bool hit) {
            if (side == 0 && hp && hp->on_shot) hp->on_shot(hit, h, hsq);
            if (side == 1 && ap && ap->on_shot) ap->on_shot(hit, a, asq);
            shown.shots.push_back({side, hit});
            if (message_draws() && board()) message_coins(false);  // the shot's board message after the pick
        };
    }
    // MatchRun::take_lineup: the lineup screen opened from the board changed the manager's squad between two minutes. The
    // working squad takes the roles and lines; leaving the lineup screen sets the effort again, like the lineup after an
    // injury in step().
    void take_lineup() {
        for (int side = 0; side < 2; side++) {
            const std::vector<HokSquadPlayer>* outer = outer_squad[side];
            const HumanSquad* m = side == 0 ? hp : ap;
            if (!outer || !m) continue;
            std::vector<HokSquadPlayer>& working = side == 0 ? hsq : asq;
            bool changed = false;
            for (size_t s = 0; s < working.size() && s < outer->size(); s++)
                if (working[s].role != (*outer)[s].role || working[s].line != (*outer)[s].line) {
                    working[s].role = (*outer)[s].role;
                    working[s].line = (*outer)[s].line;
                    changed = true;
                }
            // the effort follows the situation of the running score (e.g. a goalie change at 3:1 keeps
            // 60 = base + lead, where the level situation would give 80)
            const int own = std::getenv("EM_OLD_LINEUPSCORE") ? 0 : (side == 0 ? score.home : score.away);
            const int other = std::getenv("EM_OLD_LINEUPSCORE") ? 0 : (side == 0 ? score.away : score.home);
            if (changed) apply_effort(side == 0 ? h : a, own, other, &working, m->effort_control, 2);
        }
    }
    // MatchRun::take_tactics: the tactics screen opened from the board changed the manager's controls
    // and the tactic bytes of his team record (commit_tactics); the original plays on the record itself, so the next
    // minute uses them. No draws.
    void take_tactics(int side, const HokTeam& record) {
        const HumanSquad* outer = outer_human[side];
        if (!outer) return;
        HumanSquad& own = side == 0 ? hh2 : ha2;
        own.effort_control = outer->effort_control;
        own.offence_control = outer->offence_control;
        own.line_control = outer->line_control;
        own.line_quotas = outer->line_quotas;
        own.penalty_effort_adjustment = outer->penalty_effort_adjustment;
        HokTeam& t = side == 0 ? h : a;
        t.effort_current = record.effort_current;
        t.effort_base = record.effort_base;
        t.offence_base = record.offence_base;
        for (int i = 0; i < kTacticSituations; i++) { t.effort[i] = record.effort[i]; t.offence[i] = record.offence[i]; }
    }
    // Plays the next minute (see MatchRun::step): period breaks, the manager's per-minute
    // check, chances and goals, goalkeeper bookkeeping, energy, tactics mode, line changes,
    // effort, penalties, and at the end overtime or the shoot-out of a knock-out match.
    bool step() {
        if (std::getenv("EM_DEBUG_SQUADFIT"))  // the working squads of the human sides at the minute start: fitness, line, role
            for (int side = 0; side < 2; side++) {
                const std::vector<HokSquadPlayer>* sq = side == 0 ? (hp ? &hsq : nullptr) : (ap ? &asq : nullptr);
                if (!sq) continue;
                std::fprintf(stderr, "squadfit min %d %s:", minute + 1, (side == 0 ? h : a).name.c_str());
                for (size_t s = 0; s < sq->size() && s < 29; s++)
                    std::fprintf(stderr, " %zu:%d/%d/%d", s, (*sq)[s].fitness, (*sq)[s].line, (*sq)[s].role);
                std::fprintf(stderr, "\n");
            }
        if (forfeit || minute >= last) return false;
        shown.scenes.clear();
        shown.penalty_reason[0] = shown.penalty_reason[1] = -1;
        shown.penalty_slot[0] = shown.penalty_slot[1] = -1;
        shown.shots.clear();
        minute++;
        static std::set<std::string> scope_pool;
        const bool scope_dbg = std::getenv("EM_DEBUG_SCOPE") != nullptr;
        auto scope = [&](const char* ph) { if (!scope_dbg) return; char b[96]; std::snprintf(b, 96, "m%d:%s-%s:%s", minute, h.name.c_str(), a.name.c_str(), ph); for (char* c = b; *c; c++) if (*c == ' ') *c = '_'; g_draw_scope = scope_pool.insert(b).first->c_str(); };
        scope("start");
        LineRotation& hrotation = hp && hp->rotation ? *hp->rotation : hlocal;
        LineRotation& arotation = ap && ap->rotation ? *ap->rotation : alocal;
        if (minute == 21 || minute == 41 || ((minute == 61 || minute == 66) && (hp || ap))) {  // the breaks of the overtime reach the managed match here, the other pairs through overtime_break()
            const int times = 1;
            for (int i = 0; i < times; i++) {
                prepare_match_energy(h, hp ? &hsq : nullptr, true);
                prepare_match_energy(a, ap ? &asq : nullptr, true);
            }
        }
        if (idle_next) {
            // The minute loop opened the Einstellungen at the start of this minute (space key): the minute passes for every match
            // without events, energy, lines or penalties; the period start above and the end of the game still apply
            idle_next = false;
            if (minute == 60 && opts.knockout && score.home == score.away) { last = 70; score.decision_marker = 30; }
            const bool more = minute < last;
            if (!more && opts.knockout && !shootout_done && score.home == score.away) {
                { const std::function<void(int, bool)> picks = shootout_picks(); g_scene_call = {&h, &a, {score.home, score.away}, 0x78}; shootout(score, rng, true, hp || ap, scenes_on(), &shown.scenes, &picks); }
                shootout_done = true;
            }
            return more;
        }
        // The per-minute check of a match with a manager: one draw rand(0, 200 + 200 * level + 20 * v) per
        // human manager of the match, v = min(40, 60 - load); a zero hurts a player (see injuries.hpp). The draw itself is
        // made by the on_minute hook of the side.
        for (int side = 0; side < 2; side++) {
            const HumanSquad* m = side == 0 ? hp : ap;
            const int load = stats && stats->initial_load[side] >= 0 ? stats->initial_load[side] : 0;
            // no check in the overtime of a play-off game
            // nor in the overtime of a WM game of the national coach
            if (m && m->on_minute && !minute_check_off[side] && !(opts.playoff && minute > 60 && !std::getenv("EM_OLD_POOTCHECK")) &&
                !(m->national && minute > 60 && !std::getenv("EM_OLD_WMOTCHECK"))) {
                // the roles before the check: only a role the check itself clears is an injury (a penalty release may have
                // given a working slot a role the outer copy never had; the original keeps one record for both)
                std::vector<int> roles_before;
                if (const auto* outer = outer_squad[side])
                    for (const auto& p : *outer) roles_before.push_back(p.role);
                g_minute_lineup = side == 0 ? &hsq : &asq;
                m->on_minute(minute, load, side == 0 ? h : a);
                g_minute_lineup = nullptr;
                // a player injured in this minute leaves the running lineup at once (the original opens the
                // lineup screen; confirmed unchanged, the place stays empty)
                std::vector<HokSquadPlayer>& working = side == 0 ? hsq : asq;
                bool removed = false;
                if (const auto* outer = outer_squad[side])
                    for (size_t s = 0; s < working.size() && s < outer->size(); s++)
                        if ((*outer)[s].role == 0 && working[s].role != 0 &&
                            (std::getenv("EM_OLD_REMOVEDCHECK") || s >= roles_before.size() || roles_before[s] != 0)) {
                            working[s].role = 0;
                            removed = true;
                        }
                // leaving the lineup screen sets the effort again as in a drawn game (situation 2) whatever the
                // score, the offence stays (e.g. 60 + 20 = 80 also at 3:2)
                if (removed && message_draws() && board()) message_coins(true);  // the injury's board message
                if (removed) {
                    HokTeam& team = side == 0 ? h : a;
                    const int before = team.effort_current;
                    apply_effort(team, 0, 0, &working, m->effort_control, 2);
                    if (std::getenv("EM_DEBUG_INJ")) std::fprintf(stderr, "injury removal minute %d score %d:%d effort %d -> %d (control %d) lines %d/%d energy %d/%d base %d eff %d %d %d %d %d\n", minute, score.home, score.away, before, team.effort_current, m->effort_control, team.current_line[0], team.current_line[1], team.energy[0][team.current_line[0]], team.energy[1][team.current_line[1]], team.effort_base, team.effort[0], team.effort[1], team.effort[2], team.effort[3], team.effort[4]);
                }
            }
        }
        const int shown_home = score.home, shown_away = score.away;  // the original passes the score of the previous minute to the mode and effort updates
        if (hp) recount_on_ice(h, hsq, *hh2.players, hbox.running(), hh2.per_line);
        if (ap) recount_on_ice(a, asq, *ha2.players, abox.running(), ha2.per_line);
        scope("events");
        const int events = minute_events(h, a, minute, rng, level, hp, ap, opts.playoff, minute > 65 ? 70 : minute > 60 ? 65 : 60);
        for (int k = 0; k < 2; k++) { kinds_h[k] = g_goal_kinds[0][k]; kinds_a[k] = g_goal_kinds[1][k]; }  // the mode update reads the cached values
        bool home_shot = false, away_shot = false;
        const bool home_goal = (events & 1) && chance_is_goal(h, a, 0, minute, rng, level, hp, ap, &home_shot, kinds_h, kinds_a);
        if ((events & 1) && scenes_on()) {
            g_scene_call = {&h, &a, {score.home, score.away}, minute};
            log_goal_scene(*rng.borland, 0, home_goal, false, h.skaters + hbox.running() == 6, &shown.scenes);
        }
        if ((events & 1) && hp && hp->on_chance && !(minute_check_off[0] && !std::getenv("EM_OLD_DROPCREDIT"))) hp->on_chance(home_goal, h, hsq);  // every chance of a match with a manager is booked
        if ((events & 1) && ap && ap->on_opponent_chance) ap->on_opponent_chance(home_goal, h);
        if ((events & 1) && message_draws() && board()) message_coins(false);  // the chance's board message
        if ((events & 1) && minute <= 60) { shown.period_chances[(minute - 1) / 20][0]++; if (home_goal) shown.period_goals[(minute - 1) / 20][0]++; }
        if (home_goal) {
            score.home++;
            if (stats && h.skaters + a.skaters != 11 && h.skaters > a.skaters) stats->power_play_goals[0]++;
            if (stats && h.skaters + a.skaters != 11 && h.skaters < a.skaters) stats->short_handed_goals[0]++;
            const bool equal = h.skaters == a.skaters;
            if (box_shared) { hbox.goal_against(h, hp ? &hsq : nullptr); sync_away(); }
            else { const int before = abox.code(); abox.goal_against(a, ap ? &asq : nullptr);
              if (ap && abox.code() != before) recount_on_ice(a, asq, *ha2.players, abox.running(), ha2.per_line); }  // a release ends with the recount
            if (stats) stats->after_goal(0, equal, h.skaters, a.skaters, hp != nullptr, ap != nullptr);
        }
        const bool away_goal = (events & 2) && chance_is_goal(h, a, 1, minute, rng, level, hp, ap, &away_shot, kinds_h, kinds_a);
        if ((events & 2) && scenes_on()) {
            g_scene_call = {&h, &a, {score.home, score.away}, minute};
            log_goal_scene(*rng.borland, 1, away_goal, false, a.skaters + away_penalties().running() == 6, &shown.scenes);
        }
        if ((events & 2) && ap && ap->on_chance && !(minute_check_off[1] && !std::getenv("EM_OLD_DROPCREDIT"))) ap->on_chance(away_goal, a, asq);  // a club that left the pair by the pair-table error is no longer looked up
        if ((events & 2) && hp && hp->on_opponent_chance) hp->on_opponent_chance(away_goal, a);
        if ((events & 2) && message_draws() && board()) message_coins(false);
        if ((events & 2) && minute <= 60) { shown.period_chances[(minute - 1) / 20][1]++; if (away_goal) shown.period_goals[(minute - 1) / 20][1]++; }
        // both sides score in the same overtime minute: the away goal decides, the home goal of the minute is withdrawn
        // (e.g. 6:6 after 60 minutes ends 6:7, neither 7:7 with a later winner nor a home win)
        if (away_goal && opts.knockout && minute > 60 && home_goal) score.home--;
        if (away_goal) {
            score.away++;
            if (stats && h.skaters + a.skaters != 11 && a.skaters > h.skaters) stats->power_play_goals[1]++;
            if (stats && h.skaters + a.skaters != 11 && a.skaters < h.skaters) stats->short_handed_goals[1]++;
            const bool equal = h.skaters == a.skaters;
            { const int before = hbox.code(); hbox.goal_against(h, hp ? &hsq : nullptr);
              if (hp && hbox.code() != before) recount_on_ice(h, hsq, *hh2.players, hbox.running(), hh2.per_line); }
            sync_away();
            if (stats) stats->after_goal(1, equal, a.skaters, h.skaters, ap != nullptr, hp != nullptr);
        }
        // after the chance routine (which counts a goal against the goalkeepers on the ice) both teams
        // add the minute to their goalkeepers with a role and a line
        if (hp && hp->on_minute_end) hp->on_minute_end(away_goal ? 1 : 0);
        if (ap && ap->on_minute_end) ap->on_minute_end(home_goal ? 1 : 0);
        // Energy, line changes and effort only run for a match with a manager (or a watched/displayed one);
        // computer against computer matches skip them (the original draws no random numbers for them).
        // Order of the original minute: energy, line-energy refresh, tactics mode (odd minutes),
        // line changes, effort of the situation (even minutes), penalties.
        bool swap_sides = false;
        if (hp && std::getenv("EM_DEBUG_SCORE")) std::fprintf(stderr, "minute %d score %d:%d draws %zu\n", minute, score.home, score.away, g_game_log ? g_game_log->size() : size_t(0));
        if (display_override >= 0 ? display_override != 0 : (hp || ap || (watched && minute <= 60))) {  // the watched list of the other matches ends with the regular time (the overtime minutes of the other pairs skip energy, swap and line changes)
            const bool decided_now = opts.knockout && minute > 60 && score.home != score.away;  // the golden goal of the overtime: the minute ends without energy
            scope("energy");
            if (!decided_now) {
                energy_minute(h, hp ? &hsq : nullptr, rng);
                energy_minute(a, ap ? &asq : nullptr, rng);
                if (hp) refresh_line_energy(h, *hp);
                if (ap) refresh_line_energy(a, *ap);
            }
            scope("swap");
            swap_sides = !decided_now && (rng.borland ? rng.borland->range(0, 1) : 0) != 0;  // the original swaps A and B for the rest of the minute
            const int mode_home = shown_home, mode_away = shown_away;
            // the golden-goal minute ends without the tactics update too
            if (minute % 2 != 0 && !(decided_now && !std::getenv("EM_OLD_GOLDENMODE"))) {  // the swapped side is team A of the routine (its rules are not symmetric)
                if (swap_sides) update_tactics_mode(a, h, ap, hp, mode_away, mode_home, minute, kinds_a, kinds_h);
                else update_tactics_mode(h, a, hp, ap, mode_home, mode_away, minute, kinds_h, kinds_a);
            }
            scope("lines");
            if (swap_sides) {  // the swapped side changes lines first (mode 3 follows the already changed line)
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
        scope("penalty");
        if (minute > 59 && std::getenv("EM_DEBUG_OT")) std::fprintf(stderr, "ot %s-%s minute %d score %d:%d knockout %d last %d events %d\n", h.name.c_str(), a.name.c_str(), minute, score.home, score.away, int(opts.knockout), last, events);
        if (minute != 60 && minute != 70 && !(opts.knockout && minute > 60 && score.home != score.away)) {  // no penalties in the minute of the golden goal
            // a penalty's board message follows after its player is booked, when a manager plays in the match
            const auto penalty_message = [&](int before_home, int before_away) {
                if (!message_draws() || !(hp || ap)) return;
                for (int side = 0; side < 2; side++) {
                    const int added = (side == 0 ? hbox.minutes_assessed() - before_home : away_penalties().minutes_assessed() - before_away);
                    if (added <= 0) continue;
                    message_coins(false);
                    shown.penalty_slot[side] = (side == 0 ? hbox : away_penalties()).last_player();
                    if (added % 5 != 0 && rng.borland) shown.penalty_reason[side] = rng.borland->range(0, 12);  // 2 minutes: the reason (index into the penalty reason texts)
                }
            };
            int before_home = hbox.minutes_assessed(), before_away = away_penalties().minutes_assessed();
            penalty_minute(swap_sides ? *apen : *hpen, swap_sides ? *hpen : *apen, true, rng);
            penalty_message(before_home, before_away);
            before_home = hbox.minutes_assessed(); before_away = away_penalties().minutes_assessed();
            penalty_minute(swap_sides ? *hpen : *apen, swap_sides ? *apen : *hpen, true, rng);
            penalty_message(before_home, before_away);
            sync_away();
        }
        if (stats)
            for (int kind = 0; kind < 2; kind++) stats->line_minutes[kind][h.current_line[kind]]++;
        // The original checks the completed third period, including minute 60.
        // A last-minute equaliser must extend; a last-minute winner must not.
        if (minute == 60 && opts.knockout && score.home == score.away) {
            last = 70;
            score.decision_marker = 30;  // set before minutes 61-70
        }
        if (opts.knockout && minute > 60 && score.home != score.away) last = minute;  // sudden death: the first goal of the overtime decides
        const bool more = minute < last;
        // a knock-out game that is still level after its last minute goes to the shoot-out at once (the other games go on)
        if (!more && opts.knockout && !shootout_done && score.home == score.away) {
            { const std::function<void(int, bool)> picks = shootout_picks(); g_scene_call = {&h, &a, {score.home, score.away}, 0x78}; shootout(score, rng, true, hp || ap, scenes_on(), &shown.scenes, &picks); }
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
        if (hp && hp->national && !std::getenv("EM_OLD_NATFINISH")) hbox.finish_released(h, &hsq);
        else if (hp) hbox.finish(h, &hsq); else h.penalty_code = hbox.code();
        if (box_shared) { a.penalty_code = h.penalty_code; sync_away(); }
        else if (ap && ap->national && !std::getenv("EM_OLD_NATFINISH")) abox.finish_released(a, &asq);
        else if (ap) abox.finish(a, &asq); else a.penalty_code = abox.code();
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
void MatchRun::idle_next_minute() { p_->idle_next = true; }
void MatchRun::take_lineup() { p_->take_lineup(); }
void MatchRun::set_messages(bool on) { p_->messages = on; }
bool MatchRun::has_board() const { return p_->board(); }
bool MatchRun::minutes_left() const { return !p_->forfeit && p_->minute < p_->last; }
void MatchRun::set_reporter(int value) { p_->shown.reporter = value; }
void MatchRun::take_tactics(int side, const HokTeam& record) { if (side == 0 || side == 1) p_->take_tactics(side, record); }
void MatchRun::set_watched(bool on) { p_->watched = on; }
void MatchRun::set_display(bool on) { p_->display_override = on ? 1 : 0; }
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
void MatchRun::tactics_after_end(int minute) {
    update_tactics_mode(p_->h, p_->a, p_->hp, p_->ap, p_->score.home, p_->score.away, minute, nullptr, nullptr);
}
void MatchRun::overtime_break(bool ended_managed) {
    // a running managed match takes the break in its own minute (step); one that already ended takes it here like the other
    // pairs, the managed squad included (e.g. a WM semi-final over at 60 while the other one goes into overtime: +2 x 19;
    // not on a club cup day)
    if ((p_->hp || p_->ap) && (!ended_managed || minutes_left() || std::getenv("EM_OLD_ENDEDBREAK"))) return;
    prepare_match_energy(p_->h, p_->hp ? &p_->hsq : nullptr, true);
    prepare_match_energy(p_->a, p_->ap ? &p_->asq : nullptr, true);
}
MatchResult MatchRun::finish() { return p_->finish(); }
HokTeam& MatchRun::home_mut() { return p_->h; }
HokTeam& MatchRun::away_mut() { return p_->a; }
PenaltyBox& MatchRun::home_box() { return p_->hbox; }
PenaltyBox& MatchRun::away_box() { return p_->away_penalties(); }
int MatchRun::take_phantom_release() {
    // a manager m >= 1 writes into the previous manager's squad block, not into the pair table
    const auto route = [](const HumanSquad* human, int role) {
        if (role >= 0 && human && human->on_phantom && !std::getenv("EM_OLD_PHANTOMMANAGER")) {
            human->on_phantom(role);
            return -1;
        }
        return role;
    };
    const int h = p_->hp ? route(p_->hp, p_->hbox.take_phantom_release()) : -1;
    const int a = p_->ap ? route(p_->ap, p_->abox.take_phantom_release()) : -1;
    return h >= 0 ? h : a;
}
void MatchRun::drop_human(int side) { if (side == 0 || side == 1) p_->minute_check_off[side] = true; }
void MatchRun::mark_same_club() {
    if (!p_->hpen || p_->box_shared) return;
    p_->synced_away = true;
    p_->share_box();
    p_->sync_away();
}
const HokTeam& MatchRun::home_state() const { return p_->h; }
const HokTeam& MatchRun::away_state() const { return p_->a; }

MatchResult league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level, const HumanSquad* hh,
                         const HumanSquad* ha, MatchStats* stats, const MatchOptions& opts) {
    MatchRun run(home, away, rng, level, hh, ha, stats, opts);
    return run.finish();
}

MatchResult observed_league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level, const HumanSquad* hh,
                                  const HumanSquad* ha, MatchStats* stats, const MatchOptions& opts,
                                  const MatchMinuteObserver* observer, int home_id, int away_id) {
    MatchRun run(home, away, rng, level, hh, ha, stats, opts);
    if (observer && *observer) {
        const std::vector<LiveMatch> live{{home_id, away_id, &run}};
        (*observer)(0, live);
        // step() also plays the last minute, it returns false there
        for (int minute = 1, more = 1; more; minute++) { more = run.step(); (*observer)(minute, live); }
    }
    return run.finish();
}

void match_energy_minute(HokTeam& team, std::vector<HokSquadPlayer>* squad, GameRandom& rng) { energy_minute(team, squad, rng); }

void match_change_lines(HokTeam& team, const HokTeam& opponent, LineRotation& rotation) { change_lines(team, opponent, nullptr, rotation); }

bool match_chance_is_goal(const HokTeam& home, const HokTeam& away, int side, int minute, GameRandom& rng, int level) {
    return chance_is_goal(home, away, side, minute, rng, level, nullptr, nullptr);
}

}  // namespace em
