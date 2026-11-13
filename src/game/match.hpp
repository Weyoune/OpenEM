// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match simulation: line and playing strengths, the quick result of a whole match between
// computer teams, the minute-by-minute league match (MatchRun) with human squads, penalties,
// injuries and goal scenes, and the hooks the match display and Game use to follow a match.
//
// Random numbers: GameRandom::range(long lo, long hi), the game's own generator:
//   s = (16 * s + 167) mod 4 000 000;  result = lo + s * (hi - lo + 1) / 4 000 000
// (the Borland RTL rand is used for a few 50:50 decisions; see GameRandom::borland).
//
// Line strength (line_strength) for a computer team, kind k (0 = defence, 1 = attack), line l:
//   s1 = skill1[k] + 4 - 4l, s2 = skill2[k] + 4 - 4l, f = form[k] + 4 - 4l (f >= 0)
//   man advantage c (skaters - opponent skaters): defence with c < 0: s2 >>= -c;
//   attack with c > 0: s2 += s2 >> (3 - c)
//   in play: s2 = s2 * ((energy[k][l] >> 2) + 75) / 100, s1 -= (100 - effort * s1 / 100) / 4
// Playing strength (playing_strength, team, mode, minute, c): for both kinds
//   v = s1 + f + max(0, s2 - minute * 10 / (s2 + 1)), at most 255,
//   S = sum v * weight[mode][k] / 100 + (bonus - 44) / 2   (weights: mode 0 = 85/15,
//   mode 1 = 50/50, mode 2 = 25/75; the bonus only if the sum is not 0)
// Whole match (quick_match, minute 0 to 60):
//   d = S_home(mode 1) / 3 - S_away(mode 1) / 3, level L (0 = default)
//   home = (d < -18) ? 0 : rand(1, 2); away = (L == 0 || d > 18) ? 0 : rand(1, 2)
//   the stronger side gets rand(1, m) more goals, m = a table indexed by |d| (2..4) or |d| / 5;
//   for |d| > 8 one chance in 3 (home) / 3-L (away) to take max of two rolls half the time;
//   the weaker side gets rand(1, 2) (home) / rand(1, 3-L) (away) more if |d| < 25;
//   two more goals are handed out by coin and the offence setting (bits 4-6 of +0x2C);
//   a team without strength scores 0 (and gives 9 if the other is above 20); a team with fewer
//   skaters on the ice (+0x32) has its goals divided by 4.
// Result for a goal chance of a live match: see league_match below and match_chance_is_goal.
#include <functional>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "formats/hok.hpp"
#include "game/penalties.hpp"

namespace em {

class BorlandRandom;

// Draw tracing for tests that compare a run against the original game: when set, every
// GameRandom::range call appends (lo, hi), the state before the draw and the current scope label.
extern std::vector<std::pair<long, long>>* g_game_log;
extern std::vector<uint32_t>* g_game_states;
extern std::vector<const char*>* g_game_scopes;
extern const char* g_draw_scope;  // label of the current simulation phase (for the trace and debug output)

// The game's own random generator (see the file comment for the formula).
class GameRandom {
public:
    explicit GameRandom(uint32_t seed = 1) : s_(seed % kModulus) {}
    // A value in lo..hi (inclusive).
    long range(long lo, long hi);
    uint32_t state() const { return s_; }
    // The original takes its few 50:50 decisions from the Borland rand(), a second stream.
    // When set, those decisions draw from it; otherwise from this stream.
    BorlandRandom* borland = nullptr;

private:
    static constexpr uint32_t kModulus = 4000000;
    uint32_t s_;
};

// Strength values of one line of one kind (see line_strength).
struct LineValues {
    int skill1 = 0, skill2 = 0, form = 0;
};

// Transient counters of the minutes-based line rotation (used minutes per kind and line).
// They survive uninterrupted matches but are not part of a HOK save.
struct LineRotation {
    std::array<std::array<uint8_t, 4>, 2> used{};
};

// Line strength of a computer team (formula in the file comment); `line` 0..3, `kind` 0
// defence / 1 attack, `man_advantage` = own skaters - opponent skaters.
LineValues line_strength(const HokTeam& team, int line, int kind, bool in_play, int man_advantage);

// The squad of a human manager, for line strengths from the players (squad records in the HOK
// state at 0x696D + manager * 0x618, see hok.hpp):
//   members of line l of kind k: role != 0, (block - 1) & 0x7F == l and block >> 7 == k; the
//   goalkeeper (block 1, position value 0) belongs to every defence line. At most per_line
//   players per line (2 defenders, 3 forwards; the goalkeeper extra).
//   per player: s1 += skill 1; s2 += skill 2 * m / 100 with m = fitness / 2 + 50 (100 for the
//   goalkeeper or outside play); -15 if his natural position (position value / 25, max 2) is not
//   the kind (game level < 5); defence: s2 weighted 80 % (140 % goalkeeper);
//   f += form. The sums are divided by 3. Without a goalkeeper the defence s2 drops by
//   (45 - low nibbles) + 16 and is divided by 3, and in play s1 = s2 = 0 (empty net).
// The running lineup of the manager match whose per-minute check (on_minute) is being called:
// the original reads the squad record it plays with, in which penalised players have role 0.
extern const std::vector<HokSquadPlayer>* g_minute_lineup;
// The "current manager" value as the original's energy minute and penalty start leave it: the
// manager byte +0x16 of the team minus 1, 0 for a team without manager (tracked during a
// national live round only, where it decides which manager's bookings run).
extern bool g_track_guard;
extern int g_manager_guard;
inline void note_manager_guard(const uint8_t* team_raw) {
    if (g_track_guard && team_raw) g_manager_guard = team_raw[0x16] ? team_raw[0x16] - 1 : 0;
}

// A human manager's side in a match: squad, controls and the hooks Game uses to book the
// events of the match (scorers, injuries, goalkeeper statistics).
struct HumanSquad {
    const std::vector<HokSquadPlayer>* squad = nullptr;  // the manager's squad
    const std::vector<HokPlayer>* players = nullptr;     // all player records
    int per_line[2] = {2, 3};  // players per defence / attack line for this manager
    int level = 2;             // game level
    bool national = false;     // manager index 5 (national coach): adds player +0x19 >> 3 per player in play
    int penalty_effort_adjustment = 0;  // effort change from the penalty rules of this side
    int effort_control = 0, offence_control = 0;  // 0 automatic, 1 semi, 2 manual/live value
    int line_control = 0;  // 0 follows team mode, 1 two lines, 2 auto, 3 opponent, 4 minutes, 5 manual
    std::array<std::array<int, 4>, 2> line_quotas{};  // normalized user minutes per kind and line
    LineRotation* rotation = nullptr;                 // rotation counters (nullptr: per match)
    BorlandRandom* selection_rng = nullptr;  // separate stream for the selection of individual players
    PenaltyTimers* timers = nullptr;  // the manager's penalty timers (they outlive the match; nullptr: per match)
    int minimum_players = 0;  // career policy: 9; 0 permits isolated small-squad physics simulations
    mutable bool goalie_found = true;  // the last defence line_strength call found a goalie on the ice
    // scorer and assist pick of one chance of this manager's team
    std::function<void(bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working)> on_chance;
    // the opponent's chance in the same match (the original looks up a manager of the scoring club)
    std::function<void(bool goal, const HokTeam& scorer)> on_opponent_chance;
    // one shoot-out shot of this manager's team
    std::function<void(bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working)> on_shot;
    // per-minute injury check of the manager's match
    std::function<void(int minute, int load, const HokTeam& team)> on_minute;
    // A penalty of this manager without a bound player ended (the original then writes through
    // the record just before his squad block): for manager 0 that is the pair table (season.cpp
    // corrupt_pairs); for manager m >= 1 the record 29 of manager m - 1, set here.
    std::function<void(int role)> on_phantom;
    std::function<void(int conceded)> on_minute_end;  // after the chances of a minute: goalie minutes and goals against
};

// Reads the manager's independent effort/offence/line control settings from the save (state
// 0x01FF / 0x01F3 + manager) into `human`.
void configure_human_tactics(HumanSquad& human, const HokFile& save, int manager);

// Refreshes the cached line energies of a human team from its players, excluding natural
// goalkeepers; fixed divisor 3, with a 90-point defence allowance when per_line is 2.
void refresh_line_energy(HokTeam& team, const HumanSquad& human);

// Line strength of a human team from its players (rules above the HumanSquad struct).
LineValues line_strength(const HokTeam& team, const HumanSquad& human, int line, int kind, bool in_play,
                         int man_advantage, int minute = 0);

// Weighting of defence/attack in playing_strength: 85/15, 50/50, 25/75.
enum class StrengthMode { Defensive = 0, Neutral = 1, Offensive = 2 };

// Playing strength with the given lines on the ice (0 = first line); `human` = the squad if a
// human manager leads the team (nullptr for computer teams). `kinds` (optional) receives the
// two per-kind values.
int playing_strength(const HokTeam& team, const HumanSquad* human, StrengthMode mode, int minute, int man_advantage,
                     int defence_line = 0, int attack_line = 0, int* kinds = nullptr);
int playing_strength(const HokTeam& team, StrengthMode mode, int minute, int man_advantage, int defence_line = 0,
                     int attack_line = 0);

// Result of a match.
struct MatchResult {
    int home = 0, away = 0;  // goals, including shoot-out goals
    bool shootout = false;  // known shoot-out; an imported marker 30 alone cannot classify the decision
    // Score marker of the original: 30 for overtime or a quick-path shoot-out, 60 for
    // a shoot-out after overtime. Preserve it separately: 30 alone is ambiguous.
    int decision_marker = 0;
    int shootout_goals[2] = {};  // additions to the result, excluded from normal scorer goals
};

// Competition rules of a match: in play-off mode the goal points are halved (+1); knock-out
// matches go to overtime (61-70) and a shoot-out.
struct MatchOptions {
    bool playoff = false;
    bool knockout = false;
};

// One minute of line energy and shift counters (energy rules in match.cpp); exposed for tests.
void match_energy_minute(HokTeam& team, std::vector<HokSquadPlayer>* squad, GameRandom& rng);
// Line changes of a computer team: lines on the ice and shift counters; exposed for tests.
void match_change_lines(HokTeam& team, const HokTeam& opponent, LineRotation& rotation);
// For two computer teams: does the chance of side 0 (home) or 1 (away) become a goal?
bool match_chance_is_goal(const HokTeam& home, const HokTeam& away, int side, int minute, GameRandom& rng, int level);

// The chances of one minute: bit 0 = a home chance, the value + 2 per away chance (see league_match).
// `special` = play-off mode; `total` = the match length (60, 65 or 70) for the overtime rules.
int minute_events(const HokTeam& home, const HokTeam& away, int minute, GameRandom& rng, int level,
                  const HumanSquad* home_squad = nullptr, const HumanSquad* away_squad = nullptr, bool special = false, int total = 60);

// Whole-match result for two computer teams (minute 0 to 60, formula in the file comment).
MatchResult quick_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0, bool playoff = false);

// Knock-out match between computer teams without display (quick path): whole-match
// results, German clubs +6 strength, re-rolled up to min(4, |strength difference / 3|) times while
// the favourite does not win; a draw goes to a shoot-out (both sides rand(0, 2) until different).
MatchResult knockout_quick(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                           GameRandom& rng, int level = 0, bool playoff = false);

// The same quick path for competitions; group matches permit draws and omit the
// shoot-out. Its postprocessing can add away goals even after a zero-strength 0:0.
// `wm`: a world championship game, whose postprocessing moves only the home score.
MatchResult quick_competition_match(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                                    GameRandom& rng, int level, bool knockout, bool playoff = false, bool wm = false);

// A league match as the original's matchday loop plays it: for every minute 1..60 the goal
// points of the whole-match formula (with the fatigue of that minute, capped at 10) give chances
// (1 in 20 per point; a home chance sets the event value to 1, every away chance adds 2, so two
// away chances in one minute cancel out), and each chance is decided: first hurdle
// rand(0, 2q+4) > 1 for the home side, rand(0, 3L+2q+1) > L for the away side,
// then with d = (offence of the attacker - defence of the other) / 3: d >= 0 goal if
// rand(0, d/4 + q + 2) != 0, d < 0 goal if rand(0, |d|/4 + 1) == 0 or rand(0, 6 - q) == 0;
// q = 1 if the four strengths sum up to less than rand(550, 850).
// apply_effort: effort = base + value for the score situation (high lead = more than 2 goals
// ahead ... high deficit), minus (50 - energy) if the lines on the ice have less than 50, clamped to
// 0..100; offence (flags bits 4-6) = offence base + value for the situation. A control value of 2
// (manual) leaves the effort, a value >= 2 the offence unchanged.
void apply_effort(HokTeam& team, int own_goals, int opponent_goals);
void apply_effort(HokTeam& team, int own_goals, int opponent_goals, const std::vector<HokSquadPlayer>* squad,
                  int effort_control = 0, int offence_control = 0);

// Match set-up of a team against `opponent` - neutral strengths decide effort (60 / 0 25 20 30 0
// if the doubled strengths differ by less than 25, else 40 / 20 20 15 20 0) and offence
// (stronger 4 5 6 7 4, weaker 5 6 4 4 0), 5 skaters, no goalkeeper pulled.
// For a human side only AUTOM. components are replaced; SEMI/MANUAL values survive.
void setup_computer_team(HokTeam& team, const HokTeam& opponent, const HumanSquad* human = nullptr,
                         const HumanSquad* opponent_human = nullptr);

// Optional counters of a league_match run (index 0 = home, 1 = away).
struct MatchStats : PenaltyOpportunities {
    int forfeit_side = 0;  // 1 home / 2 away; no minute simulation, separate career fine
    int initial_load[2] = {-1, -1};  // match load (manager block +0xF0) before the minute simulation, -1 computer side
    int penalties[2] = {};        // penalties started
    int penalty_minutes[2] = {};  // all assessed penalties, including expiry/replacement in one minute
    int power_play_goals[2] = {};
    int short_handed_goals[2] = {};  // goals scored with fewer skaters on the ice
    int line_minutes[2][4] = {};  // home team: minutes per defence / forward line
    std::array<HokTeam, 2> final_teams;  // completed live tactic values of both working copies
    std::array<std::vector<HokSquadPlayer>, 2> final_squads;  // human working copies after the match
};

// Players with a selected role AND a nonzero line among the first 29 slots.
int selected_match_players(const HumanSquad& human);
// The match load of a human team: cumulative line skill1 minus skill2, /5 +20,
// clamped to 0..40, or 100 for fewer than nine selected players.
int squad_match_load(const HokTeam& team, const HumanSquad& human);

// Energy before a match (all lines 90, a player's fitness min(90, 100 - raw exhaustion / 8),
// raw exhaustion bounded to 50..150, shift counters reset) or at a period break (`interval`:
// +19, at most 90).
void prepare_match_energy(HokTeam& team, std::vector<HokSquadPlayer>* squad, bool interval = false);

// The players of a manager's team on the ice (current lines, running penalties) -> the skaters of
// the record (+0x32), the pulled-goalie bit and the per-line counts `per_line`. Squads with fewer
// than nine players with a role are left unchanged.
void recount_skaters(HokTeam& t, const std::vector<HokSquadPlayer>& squad, const std::vector<HokPlayer>& players, int penalties,
                     int per_line[2]);

// The human clubs of a match: the first manager's club (or -1) with its squad and statistics, and the clubs of further
// managers (team -1 = unused). A side whose club belongs to a manager plays with his squad.
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

// What the original's match display shows of one running match: pure bookkeeping of the minute,
// no random numbers. Chances and goals per period count only in the regular time (minute < 61; the
// overtime keeps the third period slot but books nothing there); penalty_code is the live team byte +0x33.
struct MatchProgress {
    int minute = 0;                 // last played minute
    MatchResult score;              // running score
    int period_goals[3][2] = {};    // [period][side]
    int period_chances[3][2] = {};  // [period][side]
    int penalty_minutes[2] = {};    // assessed penalty minutes per side
    int penalty_code[2] = {};       // running-penalty code per side
    std::vector<struct GoalScene> scenes;  // the goal scenes drawn in the last minute (empty without goal scenes)
    int penalty_reason[2] = {-1, -1};  // the reason of a two-minute penalty given in the last minute (index of the reason text, -1 none)
    int reporter = -1;                 // rand(0, 4) of the minute loop before the last minute (0: the reporter blinks), -1 none
    int penalty_slot[2] = {-1, -1};    // squad slot of the player penalised in the last minute (manager sides with message draws)
    std::vector<std::pair<int, bool>> shots;  // the shoot-out shots of the last minute in order: side, goal (minute games)
};

// A goal scene of a chance (the animation file TORE\<number>.<h|a><v|t>[e|j] of the original game).
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
    int goals[2] = {};  // score before the chance
    int minute = 0;     // minute, 0x78 for a shoot-out shot
};
// In the original, after a scene a typed r / w plays the chance again, at most three plays, and every play draws its scene
// anew from the Borland stream before the scorer pick. When set (the live match display), this hook is called right after a
// scene was drawn with its play number 1..3 and shows it at once: -1 not shown, 0 shown, 1 shown and the chance plays again.
// Unset (tests, no frontend): no replay and no extra draws.
extern std::function<int(const SceneCall&, const GoalScene&, int play)> g_scene_replay;

// Goal scenes setting (state 0xA525): 0 off, 1 on, 2 "ice stays on". With scenes on, every chance of a match with a board
// (team byte +0x34 bit 7 at home or away) draws the scene with Borland rand between the goal decision and the scorer pick;
// set by Game from the settings before the matches of a day.
extern int g_goal_scenes;

// Game speed (state 0xB94F, 0..70) and the display switches (state 0xA51C, GameSettings::flags), set by Game
// with g_goal_scenes. Below speed 70 the original's match display draws while the minutes run: rand(0, 4) per displayed
// minute (reporter), rand(0, 1) twice per board message (once for an injury; none with goal scenes) and Borland
// rand(0, 12) for the reason of a two-minute penalty. Speed 70 (the fastest setting) draws none.
extern int g_match_speed;
extern std::array<uint8_t, 14> g_display_flags;
// The reporter draw at the start of a displayed minute (game stream rand(0, 4), speed < 70); the value or -1
int reporter_minute_draw(GameRandom& rng, bool display);

// One league match played minute by minute: the original plays all matches of a day interleaved (minute 1 of every
// match, then minute 2 ...), see Game::play_league_day. The run works on copies of the team records and squads.
class MatchRun {
public:
    // Sets the match up (or records a forfeit when a managed side has too few players). `stats` (optional) collects counters.
    MatchRun(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0, const HumanSquad* home_human = nullptr,
             const HumanSquad* away_human = nullptr, MatchStats* stats = nullptr, const MatchOptions& options = {});
    ~MatchRun();
    MatchRun(const MatchRun&) = delete;
    MatchRun& operator=(const MatchRun&) = delete;
    bool step();           // plays the next minute; false when no minute is left
    // The next minute passes without play: the original's minute loop opened the Einstellungen instead (space key);
    // only the period start and the end of the game apply.
    void idle_next_minute();
    // The manager's squad was changed between two minutes (lineup screen from the board): the working squad
    // takes the roles and lines, the effort is set again as after leaving the lineup screen. No change: nothing happens.
    void take_lineup();
    // The manager's tactics were changed between two minutes (tactics screen from the board): the side takes
    // the controls of its squad and the tactic bytes (+0x26..+0x2B, +0x2D, +0x35..+0x3A) of `record`. No draws.
    void take_tactics(int side, const HokTeam& record);
    MatchResult finish();  // plays the remaining minutes and returns the result
    HokTeam& home_mut();  // the working copies for the pair-table error of the original (shared team records, play_matchdays)
    HokTeam& away_mut();
    PenaltyBox& home_box();  // the running penalties travel with the team record in the original (+0x32/+0x33)
    PenaltyBox& away_box();
    // A manager penalty without a bound player ended in the last minute: its saved role, else -1. For a manager >= 1 the
    // release goes to his on_phantom hook instead and is not returned.
    int take_phantom_release();
    // the pair-table error replaced a manager's club in its own pairing: the match stays managed (squad, energy, lines), but
    // the per-minute check compares the club id and stops (no injuries, no chance bookings for that side)
    void drop_human(int side);
    void mark_same_club();  // the pair-table error made both sides one club: the penalty pair roll stops (the original compares the team ids)
    const HokTeam& home_state() const;  // the working copies of the teams (final state after finish())
    const HokTeam& away_state() const;
    void overtime_break(bool ended_managed = false);  // minutes 61 / 66: the team energies of every pair of the round (finished or not) recover by 19 (the squad fitness does not)
    void tactics_after_end(int minute);  // WM overtime decided early: the odd-minute tactics update still runs with the final score
    void set_watched(bool on);  // from minute 21 the original runs the full minute (energy, lines, effort, side swap) for every match of the day
    void set_display(bool on);  // the full minute belongs to the pairs whose home team is in the display list (overrides the manager test)
    // For the day of this match: the display is on and a match of the day has a board. With it and
    // g_match_speed < 70 the board messages draw random numbers.
    void set_messages(bool on);
    bool has_board() const;  // team byte +0x34 bit 7 at home or away
    bool minutes_left() const;  // the next step() plays a minute
    void set_reporter(int value);  // the reporter draw of the coming minute (display only)
    MatchProgress progress() const;  // display state after the last played minute (read-only observer)

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Observer of a day played minute by minute (the match display). Called before
// minute 1 (minute 0) and after every minute with all matches of the day; it must not change the game.
struct LiveMatch {
    int home = -1, away = -1;       // team indices
    const MatchRun* run = nullptr;  // the running match (progress(), states)
};
using MatchMinuteObserver = std::function<void(int minute, const std::vector<LiveMatch>& matches)>;

// A whole match played minute by minute (MatchRun to the end); see the rules above apply_effort.
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
