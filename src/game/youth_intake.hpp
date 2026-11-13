// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/native_roster.hpp"
#include <optional>

// Youth intake at the season change: how many juniors a manager's youth department may promote
// into his squad, and the promotion of one junior itself. Called from the annual season
// rollover in game.cpp after youth_new_season has mixed the age groups.

namespace em {
// Result of a successful youth_intake.
struct YouthIntake {
    uint8_t player=0;      // 1-based global player ID (1..179) that was reused for the junior
    int slot=0;            // squad slot (0..29) the junior was inserted into
    bool shifted=false;    // true when later squad rows moved down one place to make room
};
// The number of intake attempts this season (0..2). Two draws are always made: the attempt
// count 0..2 and a suppression flag 0..1. Juniors only arrive from calendar year 1995 on and
// only when the flag is 0. Both draws happen even before 1995, keeping the random sequence.
int youth_review_attempts(int calendar_year,const ContractRandomDraw&);
// Promotes one junior of `quality` (10..85; the season code passes the quality of youth group 2) into the squad of
// `manager` (0..3). Returns nullopt without any draw when the manager already has 28 or more
// reserved places. Otherwise a random global player ID 1..179 whose owner byte (player record
// +0x1A) is 6 (owned by no manager) is reused, keeping its name and position value; the record
// gets national caps 0, age 18..21, form 45..55 and both skills quality-5..quality+1. He joins
// the manager's club and is inserted with native_insert_player (contract years 0), then owned by
// the manager. His squad row then gets the nibble byte +0x19 (two draws 3..12), a salary of 60%
// of the valued salary (+0x28), the market value (+0x2C) and a contract of 2..3 years (+0x0B).
// Throws FormatError for a quality outside 10..85.
// Mutates the save state only; the caller updates the decoded player/squad models afterwards.
std::optional<YouthIntake> youth_intake(HokFile&,const ContractTables&,int manager,
                                      int quality,const ContractRandomDraw&);
} // namespace em
