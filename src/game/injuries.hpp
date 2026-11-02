// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Injuries and illnesses of a human manager's squad (EM.EXE). All draws use Borland rand.
//
// Squad record fields (0x34 bytes, see formats/hok.hpp): +0x09 bit 1 = injured (bit 0 is the
// penalty box of ovl06_02d8, whose +0x0D is the penalty length and +0x00 / +0x01 the penalty
// counts), +0x0D = weeks out (0xFF = for good), +0x17 = injury type, +0x0A = role (0 = none).
//
// The injury itself, 1000:3d1a(record, text, kind):
//   - the player's form (player record +0x17) drops by rand(10, 15) if it is above 20
//   - type t = rand(0, 27 - 6 * kind), taken when rand(0, 15) <= weight[t] (DS:01B9), else drawn
//     again; kind 0 (training) allows all 28 types including illnesses, kind 1 (a match) 0-21
//   - weeks = rand(DS:0181[2t], DS:0182[2t]); for the nine types of DS:483B (fractures of
//     ankle, scaphoid, jaw, kneecap, eye, spine, Achilles tendon, lung, kidney) the career ends
//     (weeks = 0xFF) when rand(0, level * 2 + 10 - min_weeks / 4) == 0
//   - +0x09 |= 2, +0x17 = t, +0x0A = 0: the player leaves his line
//   - the text is the type name (far pointer table DS:0A00)
// Training, inside the squad routine 146e:0c89 that the day loop calls twice a week (season
// day % 7 == 0 and (day + 3) % 7 == 0): a player with +0x0D == 0 and +0x09 == 0 is hurt when
//   rand(0, level * 30 + d + 100) == 0, d = max(320 - 2 * (+0x13) + (no match today ? 50 : 0), 40)
// Matches, ovl06_364f from the minute loop ovl06_2522, for a manager's team that has at least
// four players with a role: x = clamp(40 - load + 20, 0, 40) with load = manager block +0xF0
// (DS:974B, set by 146e:304c from line strengths: sum / 5 + 20 in 0..40, 100 = fewer than nine
// players, then no match injuries); a player is hurt when rand(0, x * 20 + level * 200 + 200)
// == 0. ovl06_2385 picks him: up to 150 tries of a random slot; he must have a role and play in
// the line on the ice; he is taken with rand(0, 10) <= 3 if skill 2 < skill 1, else <= 7, a
// goalkeeper only when rand(0, 2) != 0.
// Healing, 146e:1732 (once a week): +0x0D counts down while bit 1 is set (0xFF never); at 0
// bits 0-1 of +0x09 are cleared.
#include <array>
#include <string>

#include "formats/binary.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kInjuryTypes = 28;
constexpr int kCareerOver = 0xFF;

struct InjuryTables {
    std::array<int, kInjuryTypes> weight{};                 // DS:01B9
    std::array<std::array<int, 2>, kInjuryTypes> weeks{};   // DS:0181: min, max
    std::array<int, 9> career_types{};                      // DS:483B
    std::array<std::string, kInjuryTypes> names;            // DS:0A00 (UTF-8)
};

InjuryTables load_injury_tables(const Bytes& exe);

struct Injury {
    int type = 0;
    int weeks = 0;  // kCareerOver = the career ends
};

// 1000:3d1a without the record update: kind 0 = training, 1 = match.
Injury draw_injury(const InjuryTables& t, int kind, int level, BorlandRandom& rng);

// 146e:0c89: does a fit player get hurt in training? `exhaustion_raw` is squad byte +0x13.
bool training_injury(int exhaustion_raw, bool no_match_today, int level, BorlandRandom& rng);

// ovl06_364f, one minute: is a player of the team hurt? `load` = manager block +0xF0.
bool match_injury_minute(int load, int level, BorlandRandom& rng);

}  // namespace em
