// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Weekly training, morale and injuries of a human manager's squad.
//
// Plan: 7 days x 4 units per manager (HOK state 0x070C + manager * 0x1C), one byte
// per unit with the training kind in the order of the icon bar (manual p. 31):
//   0 recovery (sauna / masseur, paid), 1 free time, 2 penalty shooting, 3 strength, 4 tactics,
//   5 practice match, 6 shooting, 7 skating, 8 stick handling.
// The default plan: on game level 0 free time everywhere; otherwise kinds 2..8 in
// turn (3 skipped the first two times) with free time on Wednesday / Sunday afternoons and the
// (day, unit) pairs of a table in EM.EXE (Wednesday 1-3, Sunday 1-3, Saturday 2-3) - exactly the
// plan of a new game of the original.
//
// The week (called by the day loop): the balance pays the weekly recovery costs (the monthly
// expense view counts them 8.5 times), then for every squad player the injury countdown, the
// training (below) and the morale.
//
// Training, only for human managers:
//   mood M = -3 + (morale (+0x0E) - 30) / 3; recovery value F = 0
//   for every day, area k = 0..3 (condition, technique, two nibble values) and unit u:
//     F += 10 (once per day and area), count[kind]++, e = effect[kind][k][position]
//     (position = natural position 0 goalkeeper / 1 defence / 2 forward), acc[k] += e, F -= e
//   per day: F += 2 when both afternoon units are recovery / free time (sum < 3) or the last is
//   recovery; M -= 3 for the same hard kind twice in a row; F -= 10 for two penalty units
//   F += count[recovery] if more than 3; Wednesday / Sunday: M -= 6 for two hard morning units,
//   M -= 20 for a hard afternoon unit; M -= 5, and 5 more for every kind 1..8 never used;
//   level > 3: M = 0; any recovery: M += n + (n - 1) / 2 + 2 with n = recovery count capped at 4
//   for each area: acc = acc * (100 + M) / 100, minus acc * (exhaustion - 125) * 2 / 100 above
//   125 exhaustion, then step(acc * 100 / norm[k][position], 0); condition / technique
//   (+0x10/+0x11) change by that step (kept in 1..99), the nibbles of +0x19 (not for
//   goalkeepers) by the steps of areas 2 and 3 (kept in 0..15)
//   exhaustion (+0x13) -= step(F * 100 / recovery_norm[position] * 2, 1), kept in 50..150
//   step(v, 0): v = clamp(v, 80, 120) - 100; |v| < 3 gives 0, else +-1 if rand(0, 100) <= |v|
//   step(v, 1): v = clamp(v, 100, 300) - 200; |v| < 3 gives 0, else +-|v| / 5
// Morale: with chance 3/4 the morale moves by bits 4-6 of +0x14 (up if bit 7 is
// set, down otherwise), the low nibble of +0x14 counts the updates of that trend. At 1 -> 0,
// morale is replaced with rand(35,65) and a new trend is drawn: duration 1..13,
// amount 1..3, direction 0..1. Otherwise morale is kept in 30..68.
// Injuries: while +0x09 bit 1 is set, +0x0D counts down once a week; at 0 the
// injury bits 0-1 are cleared.
#include <array>
#include <cstdint>

#include "formats/binary.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kTrainingKinds = 9, kTrainingDays = 7, kTrainingUnits = 4;
// Training kinds in the order of the icon bar.
enum TrainingKind : uint8_t {
    kRecovery, kFreeTime, kPenalty, kStrength, kTactics, kPracticeMatch, kShooting, kSkating, kStickHandling
};
// A week plan: [day][unit] = TrainingKind (day 0 = Monday, units 0/1 morning, 2/3 afternoon).
using TrainingPlan = std::array<std::array<uint8_t, kTrainingUnits>, kTrainingDays>;

// Training constants read from the EM.EXE data segment (offsets given per field).
struct TrainingTables {
    std::array<std::array<std::array<int, 3>, 4>, kTrainingKinds> effect{};  // 0x4861 bytes [kind][area][pos]: effect of one unit
    std::array<std::array<int, 3>, 4> norm{};                                 // 0x48CD u16 [area][pos]: divisor of the area sums
    std::array<int, 3> recovery_norm{};                                       // 0x48E5 u16 [pos]: divisor of the recovery sum
    std::array<std::array<uint8_t, 2>, 8> free_units{};                       // 0x48EB (day, unit) pairs left free in the default plan
    int level = 2;                                                            // 0x0ED8 game level byte as stored in the EXE
    long recovery_cost_factor = 100;                                          // 0x02B8 i32, percent
};

// Reads the training tables; throws FormatError if they do not look like V1.00.
TrainingTables load_training_tables(const Bytes& exe);

// The default plan described above.
TrainingPlan default_training_plan(const TrainingTables& t);

// The recovery costs charged at each of the two weekly club updates (raw): per recovery unit
// (12000 - 1200 * league) * cost factor / 100, plus a base fee of 21000 - 6000 * league. The
// dialog shows twice this amount. Without paid recovery there is no base fee.
long training_recovery_cost(const TrainingTables& t, const TrainingPlan& plan, int league);

// The squad record fields the week changes.
struct TrainingPlayer {
    int position = 2;            // natural position 0..2 (position value / 25, at most 2)
    int condition = 0, technique = 0;  // +0x10, +0x11
    int nibbles = 0;             // +0x19
    int exhaustion = 90;         // +0x13
    int morale = 50;             // +0x0E
    int trend = 0;               // +0x14
};

// One week of training for one player, as described above.
void train_week(const TrainingTables& t, const TrainingPlan& plan, TrainingPlayer& p, BorlandRandom& rng);
// The weekly morale update of one player, as described above (one draw, plus four when a new
// trend is drawn).
void morale_week(TrainingPlayer& p, BorlandRandom& rng);

}  // namespace em
