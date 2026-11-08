// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// The native penalty routine indexes DS:974B by the team's stored ONE-based
// owner byte. Preserve that address calculation, including owner 6's read from
// the league-results block, rather than replacing it with a guessed UI setting.
int penalty_effort_adjustment(const HokFile& h, int team);

// Native manager totals count only one-skater differences after the penalty.
// Paired penalties have an asymmetric rollback: if the second call fails, only
// the last human counter touched by the first call is undone (ovl06_02d8).
struct PenaltyOpportunities {
    int power_plays[2] = {}, penalty_kills[2] = {};
    int record(int side, int skaters, int other_skaters, bool human, bool other_human);
    void rollback(int token);
    void after_goal(int attacker, bool previously_equal, int skaters, int other_skaters,
                    bool human, bool other_human);
};

// ovl06_2385. Select among occupied native slots, retry at most 150 times;
// prefer the current lines in a displayed match and reject most goalkeeper draws.
// Returns -1 when no player is found (native sentinel 100).
int pick_penalized_player(const HokTeam& team, const std::vector<HokSquadPlayer>& squad,
                          const std::vector<HokPlayer>& players, BorlandRandom& rng,
                          bool displayed = true);

// The two native penalty timers and the saved roles of their players. Player
// removal affects live strength. Preserve the original's asymmetric timer/player
// association after a goal with two active penalties (ovl06_0151).
class PenaltyBox {
public:
    int running() const;
    bool start(HokTeam& team, bool major, int player, std::vector<HokSquadPlayer>* squad);
    int tick(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void goal_against(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void finish(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    int started() const { return started_; }
    int code() const;  // team record byte +0x33 for the running penalties
    int minutes_assessed() const { return assessed_; }
    // A manager penalty without a bound player ended during the game (ovr030:0151 then writes through the slot -1 record
    // DS:5D8F into the pair table, DEVIATIONS.md); reading clears the mark.
    bool take_phantom_release() { const bool p = phantom_; phantom_ = false; return p; }

private:
    struct Entry { int minutes = 0; bool major = false; int player = -1, role = 0; };
    Entry entries_[2];
    int started_ = 0, assessed_ = 0;
    bool phantom_ = false, finishing_ = false;
    void release(int entry, HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void compact();
};

}  // namespace em
