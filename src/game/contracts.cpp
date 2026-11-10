// SPDX-License-Identifier: GPL-3.0-only
#include "game/contracts.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kWeights = 0x5312, kLeagueBonus = 0x52C2, kLevel = 0x0ED8;
constexpr size_t kClassLimit1 = 0x07C1, kClassLimit2 = 0x07C2, kClassLimit3 = 0x06AD;
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34, kSquadBlock = 0x618;
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21;

int team_class(const ContractTables& t, int team) {  // ovl05_5aa5 / 2
    if (team < t.class_limit[0]) return 0;
    if (team < t.class_limit[1]) return 1;
    if (team <= t.class_limit[2]) return 2;
    return 0;
}
}  // namespace

ContractTables load_contract_tables(const Bytes& exe) {
    ContractTables t;
    for (size_t k = 0; k < 2; k++)
        for (size_t i = 0; i < 6; i++) t.weights[k][i] = exe.at(kDgroupFile + kWeights + k * 6 + i);
    for (size_t i = 0; i < 3; i++) t.league_bonus[i] = long(int32_t(le32(exe, kDgroupFile + kLeagueBonus + 4 * i)));
    t.level = exe.at(kDgroupFile + kLevel);
    t.class_limit = {exe.at(kDgroupFile + kClassLimit1), exe.at(kDgroupFile + kClassLimit2),
                     exe.at(kDgroupFile + kClassLimit3)};
    if (t.weights[0][0] != 58 || t.weights[1][0] != 70) throw FormatError("EM.EXE: contract weights not found (not V1.00?)");
    return t;
}

ContractPlayer contract_player(const HokFile& h, int m, int slot, int* player_index) {
    const Bytes& s = h.state;
    const size_t r = kSquad + size_t(m) * kSquadBlock + size_t(slot) * kSquadRecord;
    ContractPlayer c;
    c.bonus_a = s[r + 0x00];
    c.bonus_b = s[r + 0x01];
    c.league_goals = s[r + 0x03];
    c.cup_goals = s[r + 0x04];
    c.other_goals = s[r + 0x05];
    c.flags = s[r + 0x09];
    c.years = s[r + 0x0B];
    c.experience = s[r + 0x0E];
    c.skill1 = s[r + 0x10];
    c.skill2 = s[r + 0x11];
    c.open16 = s[r + 0x16];
    for (size_t i = 0; i < 3; i++) c.games[i] = le16(s, r + 0x1C + 2 * i);
    c.salary = long(int32_t(le32(s, r + 0x28)));
    c.value = long(int32_t(le32(s, r + 0x2C)));
    const int p = s[r + 0x0F] - 1;  // player index + 1, 0 = free slot
    c.player = p;
    if (player_index) *player_index = p;
    if (p >= 0) {
        const size_t pr = kPlayers + size_t(p) * kPlayerRecord;
        c.age = s[pr + 0x14];
        c.position = s[pr + 0x18];
        c.team = s[pr + 0x1D];
    }
    return c;
}

long player_worth(const ContractTables& t,const ContractPlayer& p,Worth kind,const ContractRandomDraw& draw,bool value_third) {
    const auto& w = t.weights[size_t(kind)];
    long v = (p.skill1 + p.skill2) / 2 * w[0] / 100;
    const int goals = std::min((p.league_goals * 30 + p.cup_goals * 30 + p.other_goals * 40) / 100, 20);
    // 16-bit arithmetic as in the original (IMUL/ADD on AX, then unsigned DIV): the sum wraps at 65536
    // (found by the REAL-CODE differential test; irrelevant below about 650 games).
    const int games = std::min(int(uint16_t(p.games[0] * 50 + p.games[1] * 20 + p.games[2] * 30) / 100), 200);
    v += w[1] * (goals * 100 / 20) / 100;
    v += w[2] * (games * 100 / 200) / 100;
    v += p.experience * w[3] / 100;
    const int bonus = std::clamp(p.bonus_a * 6 + p.bonus_b, 0, 20);
    v += w[4] * (bonus * 100 / 20) / 100;
    const int youth = std::clamp(45 - p.age, 0, 27);
    v += w[5] * (youth * 100 / 27) / 100;

    if (kind == Worth::Salary) {
        v = v * v * v / (t.level * 200L + 1400);
        long s = p.skill1 + p.skill2;
        if (s > 100) {
            s = (s - 100) / 2 + 100;
            if (s > 135) v = v * (s - 35) / 100;
        }
        v = std::max(v, 2L) * 85;
    } else {
        v *= 5000;
        if (v > 55000) v -= 50000;
        if (value_third) v /= 3;
        if (p.flags & 0x80) v = p.open16 > 63 ? v * 160 / 100 : v * draw(95, 100) / 100;
    }
    v = v * 10 / 3;
    if (p.position == 0) v = (v << 4) / 10;  // goalkeepers
    if (kind == Worth::Salary) return (v + t.league_bonus[size_t(team_class(t, p.team))]) / 300 * 300;
    return v / 3000 * 3000;
}
long player_worth(const ContractTables& t,const ContractPlayer& p,Worth kind,BorlandRandom& rng,bool value_third){
    return player_worth(t,p,kind,ContractRandomDraw{[&](int lo,int hi){return rng.range(lo,hi);}},value_third);
}

long renewal_demand(const ContractTables& t, const ContractPlayer& p, int years, int mood, int matchday,
                    int matchdays, int season_day, BorlandRandom& rng) {
    long demand = player_worth(t, p, Worth::Salary, rng) * mood / 100;
    const long left = 100 - long(matchday) * 100 / std::max(matchdays, 1);  // rest of the season, %
    long f = ((years - 1) * 100L + left) / 6 + 122;
    f = (f << 3) / 10;
    demand = demand * (f + 4) / 100;
    if (season_day > 232) demand = demand * (27 - p.age + 125) / 100;
    return demand;
}

}  // namespace em

namespace em {
long season_change_demand(const ContractTables& t, const HokFile& h, int m, const ContractPlayer& p, int years, int mood,
                          BorlandRandom& rng) {
    ContractPlayer aged = p;
    const int id = h.state.at(kSquad + size_t(m) * kSquadBlock + 3 * kSquadRecord + 0x0F);
    if (id) aged.age = h.state.at(kPlayers + size_t(id - 1) * kPlayerRecord + 0x14);
    return renewal_demand(t, aged, years, mood, 0, 44, 233, rng);
}
}  // namespace em
