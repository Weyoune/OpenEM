// SPDX-License-Identifier: GPL-3.0-only
// Redistribution of free players to computer teams; see redistribution.hpp.
#include "game/redistribution.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21;  // HOK state: player records
}

// Every free player (owner +0x1A = 6) of a range whose team is a computer club gets a random team
// and a new name from that team's roster in KB\23.vga. Ranges (player record numbers here are one
// lower than the 1-based player IDs):
//   mode 0: players 178-198 to the national teams 52-63
//   mode 1: players 0-178 to the clubs 0-23 (first and second division)
// The roster slot range depends on the position group c of the player, given by two markers in the
// range (mode 0: 183 and 190, mode 1: 20 and 96): slots 0-1 (goalkeepers), 2-11 (defence), 12-24
// (attack); one draw in 16 takes slot 0-1 instead. A vacant slot or a name already held by a player
// that keeps it (not free, or handled earlier in this pass) is drawn again, team and slot. The coached
// nation is redrawn while no manager holds it. The 20 copied bytes also set +0x13 (foreigner) from
// the byte after the name. All draws use the Borland generator.
int redistribute_free_players(HokFile& save, const Database& db, BorlandRandom& rng, int mode,
                              const std::function<bool(int)>& manager_team) {
    if (db.clubs.empty()) return 0;
    // Player record range [first, end), the starts of the defence and attack groups, and the team range.
    struct Range {
        int first, end, marker1, marker2, team_lo, team_hi;
    };
    static constexpr Range kRanges[2] = {{178, 199, 183, 190, 52, 63}, {0, 179, 20, 96, 0, 23}};
    // Roster slots per position group: goalkeepers 0-1, defence 2-11, attack 12-24.
    static constexpr int kSlotFrom[3] = {0, 2, 12}, kSlotTo[3] = {1, 11, 24};
    const Range& r = kRanges[mode];
    // record number of a player (decode_hok skips vacant records, so it is not the vector index)
    auto record = [&](const HokPlayer& pl) { return int((pl.raw - save.state.data() - kPlayers) / kPlayerRecord); };
    auto raw = [&](const HokPlayer& pl) { return save.state.data() + (pl.raw - save.state.data()); };
    auto db_club = [&](int team) -> const DbClub* {  // the database entry is found by the team name
        for (const DbClub& c : db.clubs)
            if (c.name == save.teams[size_t(team)].name) return &c;
        return nullptr;
    };
    int c = 0, renamed = 0;
    for (HokPlayer& pl : save.players) {
        const int p = record(pl);
        if (p < r.first || p >= r.end) continue;
        c = p >= r.marker2 ? 2 : p >= r.marker1 ? 1 : 0;
        if (raw(pl)[0x1A] != 6) continue;
        if (manager_team(pl.team)) continue;  // a manager's club keeps its players (every human manager)
        for (;;) {
            const int team = rng.range(r.team_lo, r.team_hi);
            // the coached nation (state 0x068C) gets no new players while no manager holds it (+0x16 = 0): redraw
            if (team == save.state.at(0x068C) && save.teams[size_t(team)].raw && save.teams[size_t(team)].raw[0x16] == 0) continue;
            int slot = rng.range(kSlotFrom[c], kSlotTo[c]);
            if (rng.range(0, 15) == 0) slot = rng.range(0, 1);
            const DbClub* club = db_club(team);
            if (!club || size_t(slot) >= club->players.size() || club->players[size_t(slot)].name.empty()) continue;
            const DbPlayer& name = club->players[size_t(slot)];
            bool taken = false;
            for (const HokPlayer& other : save.players) {
                const int q = record(other);
                if (q >= r.first && q < r.end && other.name == name.name && (raw(other)[0x1A] != 6 || q < p)) taken = true;
            }
            if (taken) continue;
            std::copy(name.copy.begin(), name.copy.end(), raw(pl));
            pl.name = name.name;
            pl.foreigner = name.copy[19] != 0;
            pl.team = team;
            renamed++;
            break;
        }
    }
    return renamed;
}

}  // namespace em
