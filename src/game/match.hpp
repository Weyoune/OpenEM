// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match simulation of EM.EXE for computer-controlled teams (the quick result of a whole match).
//
// Random numbers: FUN_199f_07e5(long lo, long hi), state at DS:E017:
//   s = (16 * s + 167) mod 4 000 000;  result = lo + s * (hi - lo + 1) / 4 000 000
// (the Borland RTL random at 1000:0bc7 is used for a few 50:50 decisions; GameRandom stands in).
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
#include <cstdint>
#include <vector>

#include "formats/hok.hpp"

namespace em {

class GameRandom {
public:
    explicit GameRandom(uint32_t seed = 1) : s_(seed % kModulus) {}
    // FUN_199f_07e5: a value in lo..hi (inclusive)
    long range(long lo, long hi);

private:
    static constexpr uint32_t kModulus = 4000000;
    uint32_t s_;
};

struct LineValues {
    int skill1 = 0, skill2 = 0, form = 0;
};

// FUN_162c_16bf for a computer team.
LineValues line_strength(const HokTeam& team, int line, int kind, bool in_play, int man_advantage);

// The squad of a human manager, for line strengths from the players (FUN_162c_16bf, human branch;
// squad records at DS:5DBB + manager * 0x618, see hok.hpp):
//   members of line l of kind k: role != 0, (block - 1) & 0x7F == l and block >> 7 == k; the
//   goalkeeper (block 1, position value 0) belongs to every defence line. At most DS:CC37
//   players per line (2 defenders, 3 forwards; the goalkeeper extra).
//   per player: s1 += skill 1; s2 += skill 2 * m / 100 with m = fitness / 2 + 50 (100 for the
//   goalkeeper or outside play); -15 if his natural position (position value / 25, max 2) is not
//   the kind (level DS:0ED8 < 5); defence: s2 weighted 80 % (140 % goalkeeper, DS:4946);
//   f += form. The sums are divided by 3. Without a goalkeeper the defence s2 drops by
//   (45 - low nibbles) + 16 and is divided by 3, and in play s1 = s2 = 0 (empty net).
struct HumanSquad {
    const std::vector<HokSquadPlayer>* squad = nullptr;
    const std::vector<HokPlayer>* players = nullptr;
    int per_line[2] = {2, 3};  // DS:CC37 for this manager
    int level = 2;             // DS:0ED8
};

LineValues line_strength(const HokTeam& team, const HumanSquad& human, int line, int kind, bool in_play,
                         int man_advantage);

enum class StrengthMode { Defensive = 0, Neutral = 1, Offensive = 2 };

// ovl06_1371 with the given lines on the ice (0 = first line); `human` = the squad if a human manager
// leads the team (nullptr for computer teams).
int playing_strength(const HokTeam& team, const HumanSquad* human, StrengthMode mode, int minute, int man_advantage,
                     int defence_line = 0, int attack_line = 0);
int playing_strength(const HokTeam& team, StrengthMode mode, int minute, int man_advantage, int defence_line = 0,
                     int attack_line = 0);

struct MatchResult {
    int home = 0, away = 0;
    bool shootout = false;  // decided by penalty shoot-out (the goals include the shoot-out)
};

// DS:76B2 = 1 (play-offs) halves the goal points (+1); knock-out matches go to overtime (61-70)
// and a shoot-out.
struct MatchOptions {
    bool playoff = false;
    bool knockout = false;
};

// FUN_162c_1e9c over the whole match (minute 0 to 60) for two computer teams.
MatchResult quick_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0, bool playoff = false);

// Knock-out match between computer teams without display (ovl06_161d quick path): whole-match
// results, German clubs +6 strength, re-rolled up to min(4, |strength difference / 3|) times while
// the favourite does not win; a draw goes to a shoot-out (both sides rand(0, 2) until different).
MatchResult knockout_quick(const HokTeam& home, const HokTeam& away, bool home_german, bool away_german,
                           GameRandom& rng, int level = 0);

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
void apply_effort(HokTeam& team, int own_goals, int opponent_goals, const std::vector<HokSquadPlayer>* squad);

// FUN_162c_02a5: match set-up of a computer team against `opponent` - neutral strengths decide
// effort (60 / 0 25 20 30 0 if the doubled strengths differ by less than 25, else 40 / 20 20 15 20 0,
// DS:4912) and offence (stronger 4 5 6 7 4, weaker 5 6 4 4 0, DS:491C), 5 skaters, no penalties.
void setup_computer_team(HokTeam& team, const HokTeam& opponent);

// Optional counters of a league_match run (index 0 = home, 1 = away).
struct MatchStats {
    int penalties[2] = {};
    int power_play_goals[2] = {};
    int short_handed_goals[2] = {};  // goals scored with fewer skaters on the ice
    int line_minutes[2][4] = {};  // home team: minutes per defence / forward line
};

MatchResult league_match(const HokTeam& home, const HokTeam& away, GameRandom& rng, int level = 0,
                         const HumanSquad* home_human = nullptr, const HumanSquad* away_human = nullptr,
                         MatchStats* stats = nullptr, const MatchOptions& options = {});

}  // namespace em
