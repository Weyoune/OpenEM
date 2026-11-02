// SPDX-License-Identifier: GPL-3.0-only
#include "game/injuries.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;
constexpr size_t kWeights = 0x01B9, kWeeks = 0x0181, kCareer = 0x483B, kNames = 0x0A00;
}  // namespace

InjuryTables load_injury_tables(const Bytes& exe) {
    InjuryTables t;
    for (size_t i = 0; i < kInjuryTypes; i++) {
        t.weight[i] = exe.at(kDgroupFile + kWeights + i);
        t.weeks[i] = {exe.at(kDgroupFile + kWeeks + 2 * i), exe.at(kDgroupFile + kWeeks + 2 * i + 1)};
        const size_t off = le16(exe, kDgroupFile + kNames + 4 * i);  // far pointer into DGROUP
        std::string raw;
        for (size_t o = kDgroupFile + off; o < exe.size() && exe[o] != 0; o++) raw += char(exe[o]);
        t.names[i] = iso646de_to_utf8(raw.c_str());
    }
    for (size_t i = 0; i < t.career_types.size(); i++) t.career_types[i] = exe.at(kDgroupFile + kCareer + i);
    if (t.weeks[0][0] != 14 || t.weeks[0][1] != 28 || t.career_types[8] != 19)
        throw FormatError("EM.EXE: injury tables not found");
    return t;
}

Injury draw_injury(const InjuryTables& t, int kind, int level, BorlandRandom& rng) {
    Injury in;
    do in.type = rng.range(0, kInjuryTypes - 1 - 6 * kind);
    while (rng.range(0, 15) > t.weight[size_t(in.type)]);
    const auto& w = t.weeks[size_t(in.type)];
    in.weeks = rng.range(w[0], w[1]);
    for (int c : t.career_types)
        if (c == in.type && rng.range(0, level * 2 + 10 - w[0] / 4) == 0) in.weeks = kCareerOver;
    return in;
}

bool training_injury(int exhaustion_raw, bool no_match_today, int level, BorlandRandom& rng) {
    const int d = std::max(320 - 2 * exhaustion_raw + (no_match_today ? 50 : 0), 40);
    return rng.range(0, level * 30 + d + 100) == 0;
}

bool match_injury_minute(int load, int level, BorlandRandom& rng) {
    if (load == 100) return false;
    const int x = std::clamp(40 - load + 20, 0, 40);
    return rng.range(0, x * 20 + level * 200 + 200) == 0;
}

}  // namespace em
