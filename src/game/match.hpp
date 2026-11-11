// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match simulation of EM.EXE for computer-controlled teams (the quick result of a whole match).
//
// Random numbers: FUN_199f_07e5(long lo, long hi), state at DS:E017:
//   s = (16 * s + 167) mod 4 000 000;  result = lo + s * (hi - lo + 1) / 4 000 000
// (the Borland RTL random at 1000:06df is used for a few 50:50 decisions; GameRandom stands in).
//
// Line strength, FUN_162c_16bf for a computer team, kind k (0 = defence, 1 = attack), line l:
//   s1 = skill1[k] + 4 - 4l, s2 = skill2[k] + 4 - 4l, f = form[k] + 4 - 4l (f >= 0)
//   man advantage c (skaters - opponent skaters): defence with c < 0: s2 >>= -c;
//   attack with c > 0: s2 += s2 >> (3 - c)
//   in play: s2 = s2 * ((energy[k][l] >> 2) + 75) / 100, s1 -= (100 - effort * s1 / 100) / 4
// Playing strength, ovl06_1371(team, mode, minute, c): for both kinds
//   v = s1 + f + max(0, s2 - minute * 10 / (s2 + 1)), at most 255,
//   S = sum v * weight[mode][k] / 100 + (bonus - 44) / 2   (weights DS:4E99: mode 0 = 85/15,
//   mode 1 = 50/50, mode 2 = 25/75; the bonus only if the sum is not 0)
// Whole match, FUN_162c_1e9c(home, away, 0, 60):
//   d = S_home(mode 1) / 3 - S_away(mode 1) / 3, level L = DS:012B (0 = default)
//   home = (d < -18) ? 0 : rand(1, 2); away = (L == 0 || d > 18) ? 0 : rand(1, 2)
//   the stronger side gets rand(1, m) more goals, m = table DS:4948[|d|] (2..4) or |d| / 5;
//   for |d| > 8 one chance in 3 (home) / 3-L (away) to take max of two rolls half the time;
//   the weaker side gets rand(1, 2) (home) / rand(1, 3-L) (away) more if |d| < 25;
//   two more goals are handed out by coin and the offence setting (bits 4-6 of +0x2C);
//   a team without strength scores 0 (and gives 9 if the other is above 20); a team with fewer
//   skaters on the ice (+0x32) has its goals divided by 4.
// Result for a goal chance of a live match: FUN_162c_23c4, see docs/logic/SPIELSIMULATION.md.
#include <functional>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "formats/hok.hpp"
#include "game/penalties.hpp"

namespace em {

class BorlandRandom;

// When set, every GameRandom::range call appends (lo, hi) (whole-program differential test).
extern std::vector<std::pair<long, long>>* g_game_log;
extern std::vector<uint32_t>* g_game_states;
extern std::vector<const char*>* g_game_scopes;
extern const char* g_draw_scope;

class GameRandom {
public:
    explicit GameRandom(uint32_t seed = 1) : s_(seed % kModulus) {}
    // FUN_199f_07e5: a value in lo..hi (inclusive)
    long range(long lo, long hi);
    uint32_t state() const { return s_; }
    // The original takes its few 50:50 decisions from the Borland rand() (1000:06df via 199f:0847), a second
    // stream. When set, those decisions draw from it (REAL-CODE test: FUN_162c_1e9c); otherwise this stream.
    BorlandRandom* borland = nullptr;

private:
    static constexpr uint32_t kModulus = 4000000;
    uint32_t s_;
};

struct LineValues {
    int skill1 = 0, skill2 = 0, form = 0;
};

// DS:4926, transient counters of the native minutes-based line rotation. They
// survive uninterrupted matches but are absent from native HOK save blocks.
struct LineRotation {
    std::array<std::array<uint8_t, 4>, 2> used{};
};

// FUN_162c_16bf for a computer team.
LineValues line_strength(const HokTeam& team, int line, int kind, bool in_play, int man_advantage);

// The squad of a human manager, for line strengths from the players (FUN_162c_16bf, human branch;
// squad records at DS:5DC3 + manager * 0x618, see hok.hpp):
//   members of line l of kind k: role != 0, (block - 1) & 0x7F == l and block >> 7 == k; the
//   goalkeeper (block 1, position value 0) belongs to every defence line. At most DS:CC37
//   players per line (2 defenders, 3 forwards; the goalkeeper extra).
//   per player: s1 += skill 1; s2 += skill 2 * m / 100 with m = fitness / 2 + 50 (100 for the
//   goalkeeper or outside play); -15 if his natural position (position value / 25, max 2) is not
//   the kind (level DS:0ED8 < 5); defence: s2 weighted 80 % (140 % goalkeeper, DS:4946);
//   f += form. The sums are divided by 3. Without a goalkeeper the defence s2 drops by
//   (45 - low nibbles) + 16 and is divided by 3, and in play s1 = s2 = 0 (empty net).
// the running lineup of the manager match whose per-minute check (on_minute) is being called: the original reads the
// squad record it plays with, in which penalised players have role 0 (REAL-CODE Köln period 196)
extern const std::vector<HokSquadPlayer>* g_minute_lineup;

struct HumanSquad {
    const std::vector<HokSquadPlayer>* squad = nullptr;
    const std::vector<HokPlayer>* players = nullptr;
    int per_line[2] = {2, 3};  // DS:CC37 for this manager
    int level = 2;             // DS:0ED8
    int penalty_effort_adjustment = 0;  // ovl06_02d8, read through the native owner index
    int effort_control = 0, offence_control = 0;  // 0 automatic, 1 semi, 2 manual/live value
    int line_control = 0;  // 0 follows team mode, 1 two lines, 2 auto, 3 opponent, 4 minutes, 5 manual
    std::array<std::array<int, 4>, 2> line_quotas{};  // DS:CC73, normalized user minutes
    LineRotation* rotation = nullptr;
    BorlandRandom* selection_rng = nullptr;  // separate native stream for individual player selection
    PenaltyTimers* timers = nullptr;  // the manager's penalty timers DS:D096 (they outlive the match; nullptr: per match)
    int minimum_players = 0;  // career policy: 9; 0 permits isolated small-squad physics simulations
    mutable bool goalie_found = true;  // DS:DE16+manager: the last defence line_strength call found a goalie on the ice
    std::function<void(bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working)> on_chance;  // scorer and assist pick of one chance of this manager team (ovl14_2349)
    std::function<void(bool goal, const HokTeam& scorer)> on_opponent_chance;  // the opponent's chance in the same match (ovl14_2349 looks up a manager of the scoring club)
    std::function<void(bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working)> on_shot;  // one shoot-out shot of this manager team (ovl14_2349 with param_3 = 1)
    std::function<void(int minute, int load, const HokTeam& team)> on_minute;  // per-minute injury check of the manager's match (ovr030:3706)
    std::function<void(int conceded)> on_minute_end;  // after the chances of a minute (ovr030:2A47): goalie minutes and goals against
};

// Native independent control bytes DS:A6FB/A707 + manager (HOK 01FF/01F3).
void configure_human_tactics(HumanSquad& human, const HokFile& save, int manager);

// FUN_162c_0737: human line-energy cache, excluding natural goalkeepers;
// fixed divisor 3, with a 90-point defence allowance when per_line is 2.
void refresh_line_energy(HokTeam& team, const HumanSquad& human);

LineValues line_strength(const HokTeam& team, const HumanSquad& human, int line, int kind, bool in_play,
                         int man_advantage, int minute = 0);

enum class StrengthMode { Defensive = 0, Neutral = 1, Offensive = 2 };

// ovl06_1371 with the given lines on the ice (0 = first line); `human` = the squad if a human manager
// leads the team (nullptr for computer teams).
int playing_strength(const HokTeam& team, const HumanSquad* human, StrengthMode mode, int minute, int man_advantage,
                     int defence_line = 0, int attack_line = 0, int* kinds = nullptr);
int playing_strength(const HokTeam& team, StrengthMode mode, int minute, int man_advantage, int defence_line = 0,
                     int attack_line = 0);

struct MatchResult {
    int home = 0, away = 0;
    bool shootout = false;  // known shoot-out; imported marker 30 alone cannot classify the decision
    // Native score marker: 30 for overtime or a quick-path shoot-out, 60 for
    // a shoot-out after overtime. Preserve it separately: 30 alone is ambiguous.
    int decision_marker = 0;
    int shootout_goals[2] = {};  // additions to the result, excluded from normal scorer goals
};

// DS:76B2 = 1 (play-offs) halves the goal points (+1); knock-out matches go to overtime (61-70)
// and a shoot-out.
struct MatchOptions {
    bool playoff = false;
    bool knockout = false;
};

// FUN_162c_1e9c over the whole match (minute 0 to 60) for two computer teams.
// FUN_162c_0737: one minute of line energy and shift counters (REAL-CODE tests call it directly).
void match_energy_minute(HokTeam& team, std::vector<HokSquadPlayer>* squad, GameRandom& rng);
// FUN_162c_0e32 for a computer team: lines on the ice and shift counters (REAL-CODE tests call it directly).
void match_change_lines(HokTeam& team, const HokTeam& opponent, LineRotation& rotation);
// FUN_162c_23c4 for two computer teams: does the chance of side 0 (home) or 1 (away) become a goal?
bool match_chance_is_goal(const HokTeam& home, const HokTeam& away, int side, int minute, GameRandom& rng, int level);

int minute_events(const HokTeam& home, const HokTeam& away, int minute, GameRandom& rng, int level,
                  const HumanSquad* home_squad = nullptr, const HumanSquad* away_squad = nullptr, bool special = false, int total = 60);

MatchResult quick_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0, bool playoff = false);

// Knock-out match between computer teams without display (ovl06_161d quick path): whole-match
// results, German clubs +6 strength, re-rolled up to min(4, |strength difference / 3|) times while
// the favourite does not win; a draw goes to a shoot-out (both sides rand(0, 2) until different).
MatchResult knockout_quick(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                           GameRandom& rng, int level = 0, bool playoff = false);

// Same ovl06_161d fast event path; group matches permit draws and omit the
// shoot-out. Its postprocessing can add away goals even after a zero-strength 0:0.
MatchResult quick_competition_match(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                                    GameRandom& rng, int level, bool knockout, bool playoff = false, bool wm = false);

// A league match as the matchday loop ovl06_2522 plays it: for every minute 1..60 the goal points
// of FUN_162c_1e9c (with the fatigue of that minute, capped at 10) give chances (1 in 20 per
// point; a home chance sets the event value to 1, every away chance adds 2, so two away chances
// in one minute cancel out), and FUN_162c_23c4 decides each chance: first hurdle
// rand(0, 2q+4) > 1 for the home side, rand(0, 3L+2q+1) > L for the away side (FUN_162c_26cf),
// then with d = (offence of the attacker - defence of the other) / 3: d >= 0 goal if
// rand(0, d/4 + q + 2) != 0, d < 0 goal if rand(0, |d|/4 + 1) == 0 or rand(0, 6 - q) == 0;
// q = 1 if the four strengths sum up to less than rand(550, 850).
// FUN_162c_12da: effort = base + value for the score situation (high lead = more than 2 goals
// ahead ... high deficit), minus (50 - energy) if the lines on the ice have less than 50, clamped to
// 0..100; offence (flags bits 4-6) = offence base + value for the situation.
void apply_effort(HokTeam& team, int own_goals, int opponent_goals);
void apply_effort(HokTeam& team, int own_goals, int opponent_goals, const std::vector<HokSquadPlayer>* squad,
                  int effort_control = 0, int offence_control = 0);

// FUN_162c_02a5: match set-up of a computer team against `opponent` - neutral strengths decide
// effort (60 / 0 25 20 30 0 if the doubled strengths differ by less than 25, else 40 / 20 20 15 20 0,
// DS:4912) and offence (stronger 4 5 6 7 4, weaker 5 6 4 4 0, DS:491C), 5 skaters, no penalties.
// For a human side only AUTOM. components are replaced; SEMI/MANUAL values survive.
void setup_computer_team(HokTeam& team, const HokTeam& opponent, const HumanSquad* human = nullptr,
                         const HumanSquad* opponent_human = nullptr);

// Optional counters of a league_match run (index 0 = home, 1 = away).
struct MatchStats : PenaltyOpportunities {
    int forfeit_side = 0;  // 1 home / 2 away; no minute simulation, separate career fine
    int initial_load[2] = {-1, -1};  // native manager +0xF0, before the minute simulation
    int penalties[2] = {};
    int penalty_minutes[2] = {};  // all assessed penalties, including expiry/replacement in one minute
    int power_play_goals[2] = {};
    int short_handed_goals[2] = {};  // goals scored with fewer skaters on the ice
    int line_minutes[2][4] = {};  // home team: minutes per defence / forward line
    std::array<HokTeam, 2> final_teams;  // completed live tactic values of both working copies
    std::array<std::vector<HokSquadPlayer>, 2> final_squads;  // human working copies after the match
};

// ovl06_33b6(..., 0xFF, 0): a selected role AND a nonzero line, first 29 slots.
int selected_match_players(const HumanSquad& human);
// FUN_162c_146c(..., 1): cumulative line skill1 minus skill2, /5 +20,
// clamped to 0..40, or 100 for fewer than nine selected players.
int squad_match_load(const HokTeam& team, const HumanSquad& human);

// FUN_162c_0417: exact preparation/interval energy, including its unconditional decrement.
void prepare_match_energy(HokTeam& team, std::vector<HokSquadPlayer>* squad, bool interval = false);

// ovr030:34F6 (sub_46386): the players of a manager's team on the ice (current lines, running penalties) -> the skaters of
// the record (+0x32), the pulled-goalie bit and the per-line counts DS:CC35 + 2 * manager
void recount_skaters(HokTeam& t, const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int penalties,
                     int per_line[2]);

// The human clubs of a match: the first manager's club (or -1) with its squad and statistics, and the clubs of further
// managers (docs/MEHRSPIELER_PLAN.md step 6; team -1 = unused). A side whose club belongs to a manager plays with his squad.
struct HumanSide {
    int team = -1;
    const HumanSquad* squad = nullptr;
    MatchStats* stats = nullptr;
    struct Club {
        int team = -1;
        const HumanSquad* squad = nullptr;
        MatchStats* stats = nullptr;
    };
    std::array<Club, 3> others{};
    bool is_human(int t) const {
        if (t == team) return true;
        for (const Club& c : others) if (c.team >= 0 && c.team == t) return true;
        return false;
    }
    const HumanSquad* squad_of(int t) const {
        if (t == team) return squad;
        for (const Club& c : others) if (c.team >= 0 && c.team == t) return c.squad;
        return nullptr;
    }
    MatchStats* stats_of(int t) const {
        if (t == team) return stats;
        for (const Club& c : others) if (c.team >= 0 && c.team == t) return c.stats;
        return nullptr;
    }
    // The statistics of a match between home and away: those of the side that plays with a squad, home first.
    MatchStats* match_stats(int home, int away) const {
        if (squad_of(home)) return stats_of(home);
        if (squad_of(away)) return stats_of(away);
        return nullptr;
    }
};

// What the original's match display shows of one running match (ovl14_1dc7 / ovl14_0d46): pure bookkeeping of the minute,
// no random numbers. Chances and goals per period count only in the regular time (ovl14_1dc7 checks minute < 61; the
// overtime keeps the third period slot DS:4E28 = 2 but books nothing there); penalty_code is the live team byte +0x33.
struct MatchProgress {
    int minute = 0;
    MatchResult score;
    int period_goals[3][2] = {};
    int period_chances[3][2] = {};
    int penalty_minutes[2] = {};
    int penalty_code[2] = {};
    std::vector<struct GoalScene> scenes;  // the goal scenes drawn in the last minute (empty without goal scenes)
    int penalty_reason[2] = {-1, -1};  // the reason of a two-minute penalty given in the last minute (DS:05CC table index, -1 none)
    int reporter = -1;                 // rand(0, 4) of the minute loop before the last minute (0: the reporter blinks), -1 none
    int penalty_slot[2] = {-1, -1};    // squad slot of the player penalised in the last minute (manager sides with message draws)
    std::vector<std::pair<int, bool>> shots;  // the shoot-out shots of the last minute in order: side, goal (minute games)
};

// A goal scene of a chance (ovl15_0143, docs/logic/TORSZENEN_HOOK.md): file TORE\<number>.<h|a><v|t>[e|j].
struct GoalScene {
    int side = 0;          // 0 home attacks, 1 away attacks
    bool goal = false;
    int number = 2;        // 2..11, or 2 for the special variants
    char variant = 0;      // 0, 'e' (penalty shot / forced) or 'j' (six skaters)
    bool penalty = false;  // a shot of the shoot-out
    bool shown = false;    // the display already showed it while the minute ran (g_scene_replay)
};

// The chance behind a goal scene for the display hook: the teams on the ice, the score before the chance, the minute (0x78
// for a shot of the shoot-out).
struct SceneCall {
    const HokTeam* home = nullptr;
    const HokTeam* away = nullptr;
    int goals[2] = {};
    int minute = 0;
};
// ovl14_1dc7: after a scene a typed r / w plays the chance again, at most three plays, and every play draws its scene anew
// from the Borland stream before the scorer pick. When set (the live match display), this hook is called right after a scene
// was drawn with its play number 1..3 and shows it at once: -1 not shown, 0 shown, 1 shown and the chance plays again.
// Unset (tests, REAL-CODE, no frontend): no replay and no extra draws.
extern std::function<int(const SceneCall&, const GoalScene&, int play)> g_scene_replay;

// DS:0226 (state 0xA525): goal scenes 0 off, 1 on, 2 "ice stays on". With scenes on, every chance of a match with a board
// (team byte +0x34 bit 7 at home or away) draws the scene with Borland rand between the goal decision and the scorer pick;
// set by Game from the settings before the matches of a day.
extern int g_goal_scenes;

// DS:0262 game speed (state 0xB94F, 0..70) and the display switches DS:021D.. (state 0xA51C, GameSettings::flags), set by Game
// with g_goal_scenes. Below speed 70 the original's match display draws while the minutes run (docs/logic/TORSZENEN_HOOK.md
// section 2): rand(0, 4) per displayed minute (reporter), rand(0, 1) twice per board message (once for an injury; none with
// goal scenes) and Borland rand(0, 12) for the reason of a two-minute penalty. Speed 70 (the REAL-CODE setting) draws none.
extern int g_match_speed;
extern std::array<uint8_t, 14> g_display_flags;
// ovl06_2522: the reporter draw at the start of a displayed minute (game stream rand(0, 4), speed < 70); the value or -1
int reporter_minute_draw(GameRandom& rng, bool display);

// One league match played minute by minute: the original plays all matches of a day interleaved (minute 1 of every
// match, then minute 2 ...), see Game::play_league_day.
class MatchRun {
public:
    MatchRun(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0, const HumanSquad* home_human = nullptr,
             const HumanSquad* away_human = nullptr, MatchStats* stats = nullptr, const MatchOptions& options = {});
    ~MatchRun();
    MatchRun(const MatchRun&) = delete;
    MatchRun& operator=(const MatchRun&) = delete;
    bool step();           // plays the next minute; false when no minute is left
    // The next minute passes without play: the original's minute loop opened the Einstellungen instead (space key,
    // ovl06_2522); only the period start and the end of the game apply (docs/logic/SPIELANSICHT_LIVE.md).
    void idle_next_minute();
    // The manager's squad was changed between two minutes (lineup screen from the board, ovl14_24bc): the working squad
    // takes the roles and lines, the effort is set again as after leaving the lineup screen. No change: nothing happens.
    void take_lineup();
    // The manager's tactics were changed between two minutes (tactics screen from the board, ovl14_24bc): the side takes
    // the controls of its squad and the tactic bytes (+0x26..+0x2B, +0x2D, +0x35..+0x3A) of `record`. No draws.
    void take_tactics(int side, const HokTeam& record);
    MatchResult finish();  // plays the remaining minutes and returns the result
    HokTeam& home_mut();  // the working copies for the pair-table error of the original (shared team records, play_matchdays)
    HokTeam& away_mut();
    PenaltyBox& home_box();  // the running penalties travel with the team record in the original (+0x32/+0x33)
    PenaltyBox& away_box();
    int take_phantom_release();
    // the pair-table error replaced a manager's club in its own pairing: the match stays managed (squad, energy, lines), but
    // the per-minute check of ovr030:3680 (rand(0, 200 + ...)) compares the club id and stops (REAL-CODE period 98)
    void drop_human(int side);
    void mark_same_club();  // the pair-table error made both sides one club: the penalty pair roll stops (ovr030:04BA compares the team ids)  // a manager penalty without bound player ended in the last minute: its saved role, else -1 (DEVIATIONS.md)
    const HokTeam& home_state() const;  // the working copies of the teams (final state after finish())
    const HokTeam& away_state() const;
    void overtime_break();  // minutes 61 / 66: the team energies of every pair of the round (finished or not) recover by 19 (REAL-CODE; the squad fitness does not)
    void set_watched(bool on);  // from minute 21 the original runs the full minute (energy, lines, effort, side swap) for every match of the day
    void set_display(bool on);  // ovr030:2937: the full minute belongs to the pairs whose home team is in the display list (overrides the manager test)
    // DS:5A56 for the day of this match: the display is on and a match of the day has a board (ovl14_2630). With it and
    // g_match_speed < 70 the board messages draw (ovl14_0690).
    void set_messages(bool on);
    bool has_board() const;  // team byte +0x34 bit 7 at home or away
    bool minutes_left() const;  // the next step() plays a minute
    void set_reporter(int value);  // the reporter draw of the coming minute (display only)
    MatchProgress progress() const;  // display state after the last played minute (read-only observer)

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Observer of a day played minute by minute (match display of the original, ovl06_2522 -> ovl14_0d46). Called before
// minute 1 (minute 0) and after every minute with all matches of the day; it must not change the game.
struct LiveMatch {
    int home = -1, away = -1;
    const MatchRun* run = nullptr;
};
using MatchMinuteObserver = std::function<void(int minute, const std::vector<LiveMatch>& matches)>;

MatchResult league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0,
                         const HumanSquad* home_human = nullptr, const HumanSquad* away_human = nullptr,
                         MatchStats* stats = nullptr, const MatchOptions& options = {});

// league_match with the match display (EuroCup and WM games, which the simulation plays one after another): the same
// minutes; the observer sees the single match before minute 1 and after every minute (no extra draws; without an observer
// this is league_match).
MatchResult observed_league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level,
                                  const HumanSquad* home_human, const HumanSquad* away_human, MatchStats* stats,
                                  const MatchOptions& options, const MatchMinuteObserver* observer, int home_id, int away_id);

}  // namespace em
