// SPDX-License-Identifier: GPL-3.0-only
// Training tables, default plan, recovery costs, weekly training and morale; see training.hpp.
#include "game/training.hpp"

#include <algorithm>
#include <cstdlib>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // file offset of the EM.EXE data segment, see schedule.cpp
// Offsets of the training tables within the data segment.
constexpr size_t kEffect = 0x4861, kNorm = 0x48CD, kRecoveryNorm = 0x48E5, kFreeUnits = 0x48EB, kLevel = 0x0ED8;
constexpr int kWednesday = 2, kSunday = 6;  // the match days of the week (day indices)

// The step function of the training formula (training.hpp): step(v, 0) for the areas,
// step(v, 1) (`recovery`) for the exhaustion change.
long step(long v, bool recovery, BorlandRandom& rng) {
    int sign = 1;
    v = recovery ? std::clamp(v, 100L, 300L) - 200 : std::clamp(v, 80L, 120L) - 100;
    if (v < 0) sign = -1;
    v = std::labs(v);
    if (v < 3) sign = 0;
    if (recovery) return sign * (v / 5);
    if (rng.range(0, 100) > v) sign = 0;
    return sign;
}
}  // namespace

TrainingTables load_training_tables(const Bytes& exe) {
    TrainingTables t;
    for (size_t k = 0; k < size_t(kTrainingKinds); k++)
        for (size_t a = 0; a < 4; a++)
            for (size_t p = 0; p < 3; p++) t.effect[k][a][p] = exe.at(kDgroupFile + kEffect + k * 12 + a * 3 + p);
    for (size_t a = 0; a < 4; a++)
        for (size_t p = 0; p < 3; p++) t.norm[a][p] = le16(exe, kDgroupFile + kNorm + a * 6 + p * 2);
    for (size_t p = 0; p < 3; p++) t.recovery_norm[p] = le16(exe, kDgroupFile + kRecoveryNorm + p * 2);
    for (size_t i = 0; i < t.free_units.size(); i++)
        t.free_units[i] = {exe.at(kDgroupFile + kFreeUnits + 2 * i), exe.at(kDgroupFile + kFreeUnits + 2 * i + 1)};
    t.level = exe.at(kDgroupFile + kLevel);
    t.recovery_cost_factor = int32_t(le32(exe, kDgroupFile + 0x02B8));
    if (t.norm[0][0] != 83 || t.recovery_norm[0] != 111) throw FormatError("EM.EXE: training tables not found");
    return t;
}

long training_recovery_cost(const TrainingTables& t, const TrainingPlan& plan, int league) {
    league = std::clamp(league, 0, 2);
    const long unit = (12000L - league * 1200L) * t.recovery_cost_factor / 100;
    long cost = 0;
    for (const auto& day : plan) for (uint8_t kind : day) if (kind == kRecovery) cost += unit;
    return cost != 0 ? cost + 21000L - league * 6000L : 0;
}

TrainingPlan default_training_plan(const TrainingTables& t) {
    TrainingPlan plan{};
    for (auto& day : plan) day.fill(kFreeTime);
    if (t.level == 0) return plan;
    int kind = kPenalty, skips = 2;
    for (int d = 0; d < kTrainingDays; d++)
        for (int u = 0; u < kTrainingUnits; u++) {
            plan[size_t(d)][size_t(u)] = uint8_t(kind);
            kind++;
            if (kind == kStrength && skips != 0) {
                skips--;
                kind++;
            }
            if (kind == kTrainingKinds) kind = kPenalty;
            for (const auto& fu : t.free_units)
                if (fu[0] == d && fu[1] == u) plan[size_t(d)][size_t(u)] = kFreeTime;
        }
    return plan;
}

void train_week(const TrainingTables& t, const TrainingPlan& plan, TrainingPlayer& p, BorlandRandom& rng) {
    const int pos = std::clamp(p.position, 0, 2);
    // +0x13: the fatigue penalty uses the unsigned byte, but the stored result is clamped with a signed-char
    // comparison in the original: anything from 128 on counts as negative and ends at 50.
    const int exhaustion = p.exhaustion & 0xFF;
    long fresh = 0;
    long mood = -3 + (p.morale - 30) / 3;
    std::array<long, 4> acc{};
    std::array<int, kTrainingKinds> count{};
    for (int d = 0; d < kTrainingDays; d++) {
        const auto& day = plan[size_t(d)];
        for (int a = 0; a < 4; a++) {
            fresh += 10;
            for (int u = 0; u < kTrainingUnits; u++) {
                const int kind = std::min<int>(day[size_t(u)], kTrainingKinds - 1);
                count[size_t(kind)]++;
                const int e = t.effect[size_t(kind)][size_t(a)][size_t(pos)];
                fresh -= e;
                acc[size_t(a)] += e;
            }
        }
        if (day[3] == 0 || day[3] + day[2] < 3) fresh += 2;  // an easy afternoon
        int penalties = 0;
        for (int u = 0; u < kTrainingUnits; u++) {
            if (u < 3 && day[size_t(u)] > 1 && day[size_t(u)] == day[size_t(u) + 1]) mood -= 3;
            if (day[size_t(u)] == kPenalty) penalties++;
        }
        if (penalties > 1) fresh -= 10;
    }
    if (count[kRecovery] > 3) fresh += count[kRecovery];
    for (int d : {kWednesday, kSunday}) {  // match days
        const auto& day = plan[size_t(d)];
        if (day[0] > 1 && day[1] > 1) mood -= 6;
        if (day[2] > 1 || day[3] > 1) mood -= 20;
    }
    mood -= 5;
    for (int k = 1; k < kTrainingKinds; k++)
        if (count[size_t(k)] == 0) mood -= 5;
    if (t.level > 3) mood = 0;
    const int rec = std::min(count[kRecovery], 4);
    if (rec != 0) mood += rec + (rec - 1) / 2 + 2;

    for (size_t a = 0; a < 4; a++) {
        acc[a] = acc[a] * (mood + 100) / 100;
        if (exhaustion > 125) acc[a] -= acc[a] * ((exhaustion - 125) * 2) / 100;
        acc[a] = step(acc[a] * 100 / t.norm[a][size_t(pos)], false, rng);
        if (a == 0) p.condition = std::clamp(int(p.condition + acc[a]), 1, 99);
        if (a == 1) p.technique = std::clamp(int(p.technique + acc[a]), 1, 99);
    }
    fresh = fresh * 100 / t.recovery_norm[size_t(pos)];
    fresh = step(fresh * 2, true, rng);
    const int hi = std::clamp(int((p.nibbles >> 4) + acc[3]), 0, 15);
    const int lo = std::clamp(int((p.nibbles & 15) + acc[2]), 0, 15);
    if (pos != 0) p.nibbles = hi << 4 | lo;
    p.exhaustion = std::clamp(int(int8_t(uint8_t(exhaustion - fresh))), 50, 150);
}

void morale_week(TrainingPlayer& p, BorlandRandom& rng) {
    int m = p.morale;
    if (rng.range(0, 3) != 0) {
        const int amount = (p.trend >> 4) & 7;
        m += (p.trend & 0x80) ? amount : -amount;
    }
    const int weeks = (p.trend & 15) - 1;
    p.trend = (p.trend & 0xF0) + (weeks & 0xFF);
    p.trend &= 0xFF;
    if (weeks == 0) {
        // The original does not store the movement here but rolls a new player profile:
        // both morale and trend are replaced, in this four-draw order.
        p.morale = rng.range(35, 65);
        p.trend = rng.range(1, 13);
        p.trend |= rng.range(1, 3) << 4;
        p.trend |= rng.range(0, 1) << 7;
    } else {
        p.morale = std::clamp(m, 30, 68);
    }
}

}  // namespace em
