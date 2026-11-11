// SPDX-License-Identifier: GPL-3.0-only
#include "game/penalties.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"
#include <cstdio>

#include <algorithm>
#include <cstdlib>

namespace em {

int penalty_effort_adjustment(const HokFile& h, int team) {
    if (team < 0 || team >= kHokTeams) throw FormatError("HOK: invalid penalty team");
    const int owner = h.state.at(0x17E7 + size_t(team) * 61 + 0x16);
    if (!owner) return 0;
    if (owner > 6) throw FormatError("HOK: invalid penalty owner");
    // DS:974B + owner*1AF. For owners 1..5 this is inside the contiguous
    // manager block. For owner 6 it is DS:A165, mapped at HOK 0796+(A165-A0CB).
    const size_t at = owner == 6 ? 0x0830 : 0x0DCD + size_t(owner) * 0x1AF + 0xF0;
    return int(h.state.at(at)) - 20;
}

int PenaltyOpportunities::record(int side, int skaters, int other_skaters, bool human, bool other_human) {
    if (side < 0 || side > 1) throw FormatError("Invalid penalty side");
    if (std::getenv("EM_DEBUG_PK")) std::fprintf(stderr, "pk try side %d skaters %d/%d human %d/%d at %zu\n", side, skaters, other_skaters, int(human), int(other_human), (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    if (std::abs(skaters - other_skaters) != 1) return -1;
    int token = -1;
    if (other_human) { power_plays[1 - side]++; token = 1 - side; }
    if (human) { penalty_kills[side]++; token = 2 + side; }
    if (human && std::getenv("EM_DEBUG_PK")) std::fprintf(stderr, "pk record side %d at %zu\n", side, (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    return token;
}

void PenaltyOpportunities::rollback(int token) {
    if (token >= 0 && token < 2) power_plays[token]--;
    else if (token >= 2 && token < 4) penalty_kills[token - 2]--;
    if (token >= 2 && std::getenv("EM_DEBUG_PK")) std::fprintf(stderr, "pk rollback at %zu\n", (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
}

void PenaltyOpportunities::after_goal(int attacker, bool previously_equal, int skaters, int other_skaters,
                                      bool human, bool other_human) {
    if (attacker < 0 || attacker > 1) throw FormatError("Invalid goal side");
    // FUN_162c_23c4: an equal-strength goal can release an opposing penalty and
    // create a new short-handed phase for the scoring team (e.g. 4:4 -> 4:5).
    if (!previously_equal || skaters >= other_skaters) return;
    if (human) penalty_kills[attacker]++;
    if (human && std::getenv("EM_DEBUG_PK")) std::fprintf(stderr, "pk goal at %zu\n", (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    if (other_human) power_plays[1 - attacker]++;
}

int pick_penalized_player(const HokTeam& team, const std::vector<HokSquadPlayer>& squad,
                          const std::vector<HokPlayer>& players, BorlandRandom& rng, bool displayed) {
    const int occupied = int(std::count_if(squad.begin(), squad.begin() + std::min(size_t(29), squad.size()),
                                         [](const auto& p) { return p.player >= 0; }));
    if (occupied < 6) return -1;
    for (int tries = 0; tries < 150; tries++) {
        const int slot = rng.range(0, occupied - 1);
        const auto& p = squad.at(size_t(slot));
        if (p.player < 0 || p.role == 0 || (p.line & 0x7F) == 0) continue;
        const int kind = (p.line & 0x80) != 0;
        if (displayed && (p.line & 0x7F) != team.current_line[kind] + 1) continue;
        if (rng.range(0, 10) <= (p.skill_b < p.skill_a ? 3 : 7)) continue;
        if (players.at(size_t(p.player)).position == 0 && rng.range(0, 2) != 0) continue;
        return slot;
    }
    return -1;
}

int PenaltyBox::running() const { return (entries_[0].minutes != 0) + (entries_[1].minutes != 0); }

int PenaltyBox::code() const {
    int code = 0;
    for (int i = 0; i < 2; i++)
        if (entries_[i].minutes) code |= ((entries_[i].minutes & 7) | (entries_[i].major ? 8 : 0)) << (4 * i);
    return code;
}

bool PenaltyBox::start(HokTeam& team, bool major, int player, std::vector<HokSquadPlayer>* squad) {
    last_player_ = player;
    compact();
    const int next = running();
    if (next == 2) return false;
    Entry e;
    e.minutes = major ? 5 : 2;
    e.major = major;
    if (squad && player >= 0) {
        auto& p = squad->at(size_t(player));
        if (p.player < 0 || p.role == 0) return false;
        // ovr030 (43450..): only a bound player writes slot and role of the timer; without one both stay as they were
        // (the role of an earlier penalty, REAL-CODE periods 28 and 31)
        timers_->slot[next] = player;
        timers_->role[next] = p.role;
        p.role = 0;
        if (major) p.major_penalties = uint8_t(p.major_penalties + 1);
        else p.minor_penalties = uint8_t(p.minor_penalties + 1);
    }
    entries_[next] = e;
    if (squad && std::getenv("EM_DEBUG_BOX")) std::fprintf(stderr, "box start %s timer %d slot %d role %d major %d\n", team.name.c_str(), next, timers_->slot[next], timers_->role[next], int(major));
    team.skaters--;
    started_++;
    assessed_ += e.minutes;
    return true;
}

void PenaltyBox::release(int entry, HokTeam& team, std::vector<HokSquadPlayer>* squad) {
    Entry& e = entries_[entry];
    if (!e.minutes) return;
    if (squad && std::getenv("EM_DEBUG_BOX")) std::fprintf(stderr, "box release %s timer %d minutes %d major %d at %zu\n", team.name.c_str(), entry, e.minutes, int(e.major), (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    e = Entry{};
    team.skaters++;
    if (squad) release_slot(entry, *squad);
}

// sub_42FE1 (ovr030:0151): the role goes back to the player of the slot (slot 0 = none: the write lands in the pair table
// through DS:5D8F), the slot is cleared, and a released lower timer takes slot and role of the upper one (which keeps both)
void PenaltyBox::release_slot(int entry, std::vector<HokSquadPlayer>& squad) {
    PenaltyTimers& t = *timers_;
    if (t.slot[entry] >= 0 && size_t(t.slot[entry]) < squad.size()) squad[size_t(t.slot[entry])].role = t.role[entry];
    else if (t.slot[entry] < 0) phantom_role_ = t.role[entry];
    t.slot[entry] = -1;
    if (entry == 0) { t.slot[0] = t.slot[1]; t.role[0] = t.role[1]; }
}

// sub_42E90: a free lower timer takes the minutes of the upper one, with its slot and role (the upper timer keeps them)
void PenaltyBox::compact() {
    if (!entries_[0].minutes && entries_[1].minutes) {
        entries_[0] = entries_[1];
        entries_[1] = Entry{};
        timers_->slot[0] = timers_->slot[1];
        timers_->role[0] = timers_->role[1];
    }
}

int PenaltyBox::tick(HokTeam& team, std::vector<HokSquadPlayer>* squad) {
    for (int i = 0; i < 2; i++) {
        if (entries_[i].minutes == 1) release(i, team, squad);
        else if (entries_[i].minutes) entries_[i].minutes--;
    }
    compact();
    return running();
}

void PenaltyBox::goal_against(HokTeam& team, std::vector<HokSquadPlayer>* squad) {
    if (!entries_[0].minutes) return;
    if (!entries_[1].minutes) {
        if (!entries_[0].major) release(0, team, squad);
        return;
    }
    if (entries_[1].major) return;
    // ovl06_0151(team, 0, 0), 01C6..02C6: with two timers the original
    // clears the upper timer but restores the FIRST queued player. The second
    // player inherits the lower timer, including its major flag. Keep this
    // asymmetric association instead of silently substituting hockey rules.
    entries_[1] = Entry{};
    team.skaters++;
    if (squad) release_slot(0, *squad);
}

// ovr0xx:61982 (match end): every timer with a slot gives the role back and clears the slot; a timer without slot
// writes nothing (no pair-table write at the end), the roles stay for the next match
void PenaltyBox::finish(HokTeam& team, std::vector<HokSquadPlayer>* squad) {
    for (int i = 0; i < 2; i++)
        if (entries_[i].minutes) { entries_[i] = Entry{}; team.skaters++; }
    if (!squad) return;
    PenaltyTimers& t = *timers_;
    if (std::getenv("EM_DEBUG_BOX")) {
        std::fprintf(stderr, "box finish %s skaters %d timers slot %d/%d role %d/%d; role-0 lineup players:", team.name.c_str(), team.skaters, t.slot[0], t.slot[1], t.role[0], t.role[1]);
        for (size_t s = 0; s < squad->size() && s < 29; s++) if ((*squad)[s].player >= 0 && (*squad)[s].role == 0 && (*squad)[s].line != 0) std::fprintf(stderr, " %zu", s);
        std::fprintf(stderr, "\n");
    }
    for (int i = 0; i < 2; i++) {
        if (t.slot[i] >= 0 && size_t(t.slot[i]) < squad->size()) (*squad)[size_t(t.slot[i])].role = t.role[i];
        t.slot[i] = -1;
    }
}

}  // namespace em
