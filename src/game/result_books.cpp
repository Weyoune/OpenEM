// SPDX-License-Identifier: GPL-3.0-only
#include "game/result_books.hpp"
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <vector>

namespace em {

namespace {
constexpr size_t kSeries = 0x9988, kSeriesRecord = 0x9EC8, kHeadToHead = 0x8F88;
constexpr size_t kRecordOpponents = 0x9F1C, kRecords = 0xA11C;
constexpr int kSeriesTeams = 64;        // DS:B57B .. DS:BAB7
constexpr int kFirstNationalTeam = 52;  // DS:06AD
}  // namespace

// Series (shown on "Totale"): DS:B57B + team * 0x15 (state 0x9988), 7 kinds x {all, home, away}; the manager's
// records at DS:BABB (state 0x9EC8). Kinds: won, lost, drawn, not won, not lost, without goal against,
// without goal scored.
void book_series(Bytes& state, int team, int side, int goals_for, int goals_against) {
    if (team < 0 || team >= kSeriesTeams) return;
    const bool cond[7] = {goals_for > goals_against, goals_for < goals_against, goals_for == goals_against,
                          goals_for <= goals_against, goals_for >= goals_against, goals_against == 0, goals_for == 0};
    uint8_t* cur = state.data() + kSeries + size_t(team) * 0x15;
    for (size_t k = 0; k < 7; k++)
        for (size_t col : {size_t(0), size_t(side) + 1}) {
            uint8_t& v = cur[k * 3 + col];
            v = cond[k] ? uint8_t(std::min(255, v + 1)) : 0;
        }
}

void update_series_records(Bytes& state, int team) {
    if (team < 0 || team >= kSeriesTeams) return;
    const uint8_t* cur = state.data() + kSeries + size_t(team) * 0x15;
    uint8_t* rec = state.data() + kSeriesRecord;
    for (size_t k = 0; k < 21; k++) rec[k] = std::max(rec[k], cur[k]);
}

void book_head_to_head(Bytes& state, int home, int away, int home_goals, int away_goals, int human_team, int manager) {
    auto put = [&](int opp, int side, int own, int against) {
        if (opp < 0 || opp >= kSeriesTeams) return;  // the block has 64 rows
        uint8_t* row = state.data() + kHeadToHead + size_t(manager) * 0x280 + size_t(opp) * 10;
        const uint8_t v = uint8_t(std::min(against, 15) << 4 | std::min(own, 15));
        std::vector<uint8_t*> slots;  // the column, or both for nations and later teams
        if (opp < kFirstNationalTeam) {
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i + side);
        } else {
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i);
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i + 1);
        }
        if (std::getenv("EM_DEBUG_H2H") && opp >= 52) std::fprintf(stderr, "h2h opp %d side %d value %02x\n", opp, side, v);
        for (uint8_t* p : slots)
            if (*p == 0xFF) {
                *p = v;
                return;
            }
        for (size_t i = 0; i + 1 < slots.size(); i++) *slots[i] = *slots[i + 1];
        *slots.back() = v;
    };
    if (home == human_team) put(away, 0, home_goals, away_goals);
    if (away == human_team) put(home, 1, away_goals, home_goals);
}

// ovl13_3e43, the club records of the opponent info (ovl13_12e5), for every club below 64. Per team (state
// 0xA11C = DS:BD0F, 8 x u16; opponents at 0x9F1C = DS:BB0F, 8 bytes):
//   0 highest home win, 1 highest home defeat, 2 home goals scored, 3 home goals conceded,
//   4 highest away win, 5 highest away defeat, 6 away goals scored, 7 away goals conceded.
// A record keeps (a * wa) << 8 | b * wb of the game (a = home goals, b = away goals) and is replaced when
// a * wa - b * wb beats the stored value, compared as unsigned 16-bit numbers (records 1 and 4 look for the
// smallest difference and so never replace an empty 0:0). wb < 0 is stored as 1.
void book_records(Bytes& state, int home, int away, int home_goals, int away_goals) {
    auto record = [&](int team, int rec, int a, int b, bool less, int wa, int wb, int opp) {
        if (team < 0 || team >= kSeriesTeams) return;
        uint8_t* v = state.data() + kRecords + size_t(team) * 16 + size_t(rec) * 2;
        const int hi = v[1], lo = v[0];
        const uint16_t now = uint16_t(a * wa - b * wb), old = uint16_t(hi * wa - lo * wb);
        if (less ? !(now < old) : !(now > old)) return;
        if (wb < 0) wb = 1;
        const int x = (a * wa) << 8 | (b * wb);
        v[0] = uint8_t(x);
        v[1] = uint8_t(x >> 8);
        state[kRecordOpponents + size_t(team) * 8 + size_t(rec)] = uint8_t(opp);
    };
    const int hg = std::min(home_goals, 255), ag = std::min(away_goals, 255);
    if (hg > ag) {
        record(home, 0, hg, ag, false, 1, 1, away);
        record(away, 5, hg, ag, false, 1, 1, home);
    } else if (hg < ag) {
        record(home, 1, hg, ag, true, 1, 1, away);
        record(away, 4, hg, ag, true, 1, 1, home);
    }
    record(home, 2, hg, ag, false, 1, 0, away);
    record(away, 7, hg, ag, false, 1, 0, home);
    record(home, 3, hg, ag, false, 0, -1, away);
    record(away, 6, hg, ag, false, 0, -1, home);
}

}  // namespace em
