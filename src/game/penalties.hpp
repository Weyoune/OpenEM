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

// The squad slot and saved role of the two native timers of a manager (DS:D096 + 6 * manager, 3 bytes per timer: role,
// slot + 1, minutes). They are not in the save file and outlive the match: the match end (ovr0xx:6198x) restores the
// players and clears the slots but keeps the roles, so the next match starts with the roles of the last penalties
// (REAL-CODE period 31). slot -1 is the native 0 ("no player": a release then writes through DS:5D8F).
struct PenaltyTimers {
    int slot[2] = {-1, -1};
    int role[2] = {0, 0};
};

// The two native penalty timers and the saved roles of their players. Player
// removal affects live strength. Preserve the original's asymmetric timer/player
// association after a goal with two active penalties (ovl06_0151).
class PenaltyBox {
public:
    PenaltyBox() = default;
    PenaltyBox(const PenaltyBox& o) { *this = o; }
    PenaltyBox& operator=(const PenaltyBox& o) {
        entries_[0] = o.entries_[0]; entries_[1] = o.entries_[1];
        local_ = o.local_;
        timers_ = o.timers_ == &o.local_ ? &local_ : o.timers_;
        started_ = o.started_; assessed_ = o.assessed_; phantom_role_ = o.phantom_role_; finishing_ = o.finishing_; last_player_ = o.last_player_;
        return *this;
    }
    int running() const;
    bool start(HokTeam& team, bool major, int player, std::vector<HokSquadPlayer>* squad);
    int tick(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void goal_against(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void finish(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    int started() const { return started_; }
    int code() const;  // team record byte +0x33 for the running penalties
    int minutes_assessed() const { return assessed_; }
    int last_player() const { return last_player_; }  // the squad slot passed to the last start() (display only)
    // A manager penalty without a bound player ended during the game (ovr030:0151 then writes through the slot -1 record
    // DS:5D8F into the pair table, DEVIATIONS.md); returns the saved role it writes there (the role of the last player
    // penalised in that timer, it is never cleared) or -1; reading clears the mark.
    int take_phantom_release() { const int r = phantom_role_; phantom_role_ = -1; return r; }
    void use_timers(PenaltyTimers* timers) { timers_ = timers ? timers : &local_; }  // the persistent timers of a manager

private:
    struct Entry { int minutes = 0; bool major = false; };
    Entry entries_[2];
    PenaltyTimers local_;
    PenaltyTimers* timers_ = &local_;
    int started_ = 0, assessed_ = 0;
    int phantom_role_ = -1;
    int last_player_ = -1;
    bool finishing_ = false;
    void release(int entry, HokTeam& team, std::vector<HokSquadPlayer>* squad);
    void release_slot(int entry, std::vector<HokSquadPlayer>& squad);
    void compact();
};

}  // namespace em
