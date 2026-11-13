// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Penalties during a match: the penalty box with its two timers per team, choosing the penalised
// player, the per-manager power-play / penalty-kill statistics and the effort adjustment a
// manager's setting applies to penalty chances. The match simulation (game/match.cpp) drives these.
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// The penalty adjustment of a team: (byte - 20) where the byte is read from a manager block +0xF0;
// 0 for computer teams. The original indexes the manager blocks with the team's stored ONE-based
// owner byte (team record +0x16), so it actually reads the block after the owner's; for owner 6 the
// read lands outside the manager blocks, in the league-results area (state 0x0830). That address
// calculation is preserved exactly rather than replaced by a guessed setting. Throws FormatError for
// an invalid team or owner byte.
int penalty_effort_adjustment(const HokFile& h, int team);

// Power plays and penalty kills counted for the human managers' statistics, per match side.
// Only one-skater differences after the penalty count. Paired penalties have an asymmetric
// rollback, as in the original: if the second call fails, only the last human counter touched
// by the first call is undone.
struct PenaltyOpportunities {
    int power_plays[2] = {}, penalty_kills[2] = {};
    // Records a penalty against `side` given the skater counts after it. Counts a power play for the
    // other side if it is human and a penalty kill for `side` if human. Returns a token for
    // rollback(): -1 nothing counted, 0/1 power play of that side, 2/3 penalty kill of side 0/1
    // (the penalty kill wins when both were counted).
    int record(int side, int skaters, int other_skaters, bool human, bool other_human);
    // Undoes the single counter identified by a token from record().
    void rollback(int token);
    // After a goal: an equal-strength goal can release an opposing penalty and create a new
    // short-handed phase for the scoring team (e.g. 4:4 -> 4:5), which counts again.
    void after_goal(int attacker, bool previously_equal, int skaters, int other_skaters,
                    bool human, bool other_human);
};

// Chooses the penalised player: a random slot among the first <occupied count> slots, at most
// 150 tries. A try is rejected for an empty slot, a player without role or line, (in a displayed
// match) a player not in the current line, with a skill-based chance (rand(0, 10) <= 3 if skill 2
// < skill 1, else <= 7), and for a goalkeeper in two of three cases. Returns the squad slot, or -1
// when no player is found (fewer than 6 occupied slots, or all tries rejected).
int pick_penalized_player(const HokTeam& team, const std::vector<HokSquadPlayer>& squad,
                          const std::vector<HokPlayer>& players, BorlandRandom& rng,
                          bool displayed = true);

// The squad slot and saved role of the two penalty timers of a manager. They are not in the save
// file and outlive the match: the match end restores the players and clears the slots but keeps
// the roles, so the next match starts with the roles of the last penalties. slot -1 means "no
// player"; a release then writes the role into an unrelated table in the original (see
// PenaltyBox::take_phantom_release).
struct PenaltyTimers {
    int slot[2] = {-1, -1};  // squad slot of the penalised player, -1 = none
    int role[2] = {0, 0};    // the player's role (lineup position) before the penalty
};

// The two penalty timers of one team and the saved roles of their players. A running penalty
// removes a skater (HokTeam::skaters) and takes the player out of his role until the release.
// The original's asymmetric timer/player association after a goal with two active penalties is
// preserved (see goal_against).
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
    // Number of running timers (0..2).
    int running() const;
    // Starts a minor (2 minutes) or major (5 minutes) penalty: refused (false) when both timers
    // run, or when `squad` is given and the slot `player` is empty or has no role. Only a bound
    // player (squad given, player >= 0) writes slot and role of the timer and has his role cleared
    // and his penalty counter raised; otherwise the timer keeps the slot and role of an earlier
    // penalty. Removes one skater and adds the minutes to minutes_assessed().
    bool start(HokTeam& team, bool major, int player, std::vector<HokSquadPlayer>* squad);
    // One minute passes: timers at 1 minute are released, others count down; a free lower timer
    // then takes over the upper one. Returns the number of timers still running.
    int tick(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    // A goal against this team: with one running minor penalty it ends. With two timers and a
    // minor upper timer, the original clears the upper timer but gives the role back to the player
    // of the LOWER timer; the remaining player inherits the lower timer, including its major flag.
    void goal_against(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    // Match end for a club: running timers stop (skaters restored), every timer with a slot gives
    // the role back and clears the slot; the roles stay for the next match.
    void finish(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    // The national coach's box at the match end: running timers end on the normal release path, so
    // a stale slot can stay for the next national match and get its role back there.
    void finish_released(HokTeam& team, std::vector<HokSquadPlayer>* squad);
    int started() const { return started_; }  // penalties started in this match
    // Team record byte +0x33 for the running penalties: per timer (low nibble = lower timer)
    // the minutes in bits 0..2 and bit 3 for a major penalty.
    int code() const;
    int minutes_assessed() const { return assessed_; }  // penalty minutes assessed in this match
    int last_player() const { return last_player_; }  // the squad slot passed to the last start() (display only)
    // A manager penalty without a bound player ended during the game; the original then writes the
    // timer's saved role through the "no player" record into an unrelated table (a known deviation
    // the caller reproduces). Returns that role (the role of the last player penalised in that
    // timer, it is never cleared) or -1; reading clears the mark.
    int take_phantom_release() { const int r = phantom_role_; phantom_role_ = -1; return r; }
    void use_timers(PenaltyTimers* timers) { timers_ = timers ? timers : &local_; }  // the persistent timers of a manager

private:
    // One timer: remaining minutes (0 = free) and the major flag.
    struct Entry { int minutes = 0; bool major = false; };
    Entry entries_[2];
    PenaltyTimers local_;              // slots/roles for teams without persistent manager timers
    PenaltyTimers* timers_ = &local_;  // the slots/roles in use (local_ or a manager's)
    int started_ = 0, assessed_ = 0;
    int phantom_role_ = -1;            // see take_phantom_release
    int last_player_ = -1;
    bool finishing_ = false;
    // Ends timer `entry`: frees it, adds the skater back and (with a squad) restores the player.
    void release(int entry, HokTeam& team, std::vector<HokSquadPlayer>* squad);
    // Gives the role of timer `entry` back to its player (or records a phantom release when the
    // timer has no slot) and clears the slot; a released lower timer then takes slot and role of
    // the upper one, which keeps both.
    void release_slot(int entry, std::vector<HokSquadPlayer>& squad);
    // A free lower timer takes the minutes of the upper one, with its slot and role (the upper
    // timer's slot and role stay as they were).
    void compact();
};

}  // namespace em
