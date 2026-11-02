// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Weekly training, morale and injuries of a human manager's squad (segment 146e of EM.EXE).
//
// Plan: 7 days x 4 units at DS:A8A1 + manager * 0x1C (HOK state 0x070C for manager 0), one byte
// per unit with the training kind in the order of the icon bar (manual p. 31):
//   0 recovery (sauna / masseur, paid), 1 free time, 2 penalty shooting, 3 strength, 4 tactics,
//   5 practice match, 6 shooting, 7 skating, 8 stick handling.
// 146e:0653 sets the default: on game level 0 free time everywhere; otherwise kinds 2..8 in
// turn (3 skipped the first two times) with free time on Wednesday / Sunday afternoons and the
// pairs at DS:48EB (Wednesday 1-3, Sunday 1-3, Saturday 2-3) - exactly the plan of START.
//
// The week (146e:0c89, called by the day loop): the balance pays DS:97EE (the weekly recovery
// costs; the monthly expense view counts it 8.5 times), then for every squad player the
// injury countdown (146e:1732), the training (146e:00bc, below) and the morale.
//
// Training, 146e:00bc(manager, record), only for human managers:
//   mood SI = -3 + (morale (+0x0E) - 30) / 3
//   for every day, area k = 0..3 (condition, technique, two nibble values) and unit u:
//     recovery F += 10 (once per day and area), count[kind]++, e = DS:4861[kind][k][position]
//     (position = natural position 0 goalkeeper / 1 defence / 2 forward), acc[k] += e, F -= e
//   per day: F += 2 when both afternoon units are recovery / free time (sum < 3) or the last is
//   recovery; SI -= 3 for the same hard kind twice in a row; F -= 10 for two penalty units
//   F += count[recovery] if any; Wednesday / Sunday: SI -= 6 for two hard morning units, SI -= 20
//   for a hard afternoon unit; SI -= 5, and 5 more for every kind 1..8 never used; level > 3:
//   SI = 0; any recovery: SI += 7 (count capped at 4: 4 + 3 / 2 + 2)
//   for each area: acc = acc * (100 + SI) / 100, minus acc * (exhaustion - 125) * 2 / 100 above
//   125 exhaustion, then step(acc * 100 / DS:48CD[k][position], 0); condition / technique
//   (+0x10/+0x11) change by that step (kept in 1..99), the nibbles of +0x19 (not for
//   goalkeepers) by the steps of areas 2 and 3 (kept in 0..15)
//   exhaustion (+0x13) -= step(F * 100 / DS:48E5[position] * 2, 1), kept in 50..150
//   step(v, 0): v = clamp(v, 80, 120) - 100; |v| < 3 gives 0, else +-1 if rand(0, 100) <= |v|
//   step(v, 1): v = clamp(v, 100, 300) - 200; |v| < 3 gives 0, else +-|v| / 5
// Morale (146e:0c89): with chance 3/4 the morale moves by bits 4-6 of +0x14 (up if bit 7 is
// set, down otherwise), the low nibble of +0x14 counts the weeks of that trend (at 0 a new one is
// drawn by 1000:4cbd, open); morale is kept in 30..68.
// Injuries (146e:1732): while +0x09 bit 1 is set, +0x0D counts down once a week; at 0 the
// injury bits 0-1 are cleared.
#include <array>
#include <cstdint>

#include "formats/binary.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kTrainingKinds = 9, kTrainingDays = 7, kTrainingUnits = 4;
enum TrainingKind : uint8_t {
    kRecovery, kFreeTime, kPenalty, kStrength, kTactics, kPracticeMatch, kShooting, kSkating, kStickHandling
};
using TrainingPlan = std::array<std::array<uint8_t, kTrainingUnits>, kTrainingDays>;

struct TrainingTables {
    std::array<std::array<std::array<int, 3>, 4>, kTrainingKinds> effect{};  // DS:4861 [kind][area][pos]
    std::array<std::array<int, 3>, 4> norm{};                                 // DS:48CD [area][pos]
    std::array<int, 3> recovery_norm{};                                       // DS:48E5
    std::array<std::array<uint8_t, 2>, 8> free_units{};                       // DS:48EB (day, unit)
    int level = 2;                                                            // DS:0ED8
};

TrainingTables load_training_tables(const Bytes& exe);

// 146e:0653
TrainingPlan default_training_plan(const TrainingTables& t);

// The squad record fields the week changes.
struct TrainingPlayer {
    int position = 2;            // natural position 0..2 (FUN_1238_0000)
    int condition = 0, technique = 0;  // +0x10, +0x11
    int nibbles = 0;             // +0x19
    int exhaustion = 90;         // +0x13
    int morale = 50;             // +0x0E
    int trend = 0;               // +0x14
};

// 146e:00bc
void train_week(const TrainingTables& t, const TrainingPlan& plan, TrainingPlayer& p, BorlandRandom& rng);
// 146e:0c89, morale part
void morale_week(TrainingPlayer& p, BorlandRandom& rng);

}  // namespace em
