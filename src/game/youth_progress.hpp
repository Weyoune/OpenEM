// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/finances.hpp"
#include <functional>

// Development of a manager's three youth teams. Each manager has a 0x15-byte youth block in the
// save state at 0x06B8 + manager * 0x15:
//   +0x00 u8[3] quality of age group 0..2 (10..85)
//   +0x03 u8[3] table rank of the group in its youth league (0..11)
//   +0x06 u8[3] youth league of the group (0 = top .. 2)
//   +0x09 i32[3] monthly youth grants (youth.hpp)
// Quality follows the grant, the table rank follows the quality; at the season end teams are
// promoted/relegated and the age groups move up.

namespace em {
// Random draw rand(lo, hi), inclusive; the game passes its battle random generator.
using YouthRandomDraw=std::function<int(int,int)>;
// One youth matchday of `manager` (0..3), run before the league games on every calendar day
// that has league fixtures. `first_matchday` (0..44) is the matchday counter of the first
// league; all managers use it. For each age group:
//   - target quality = 40 + (grant in % of the group's reference amount 10000 << i) / 2.
//     The direction is towards the target; with chance 1/5 it is replaced by a random -1/0/+1,
//     and it becomes 0 when target and quality differ by no more than a random 0..30 %. The
//     step is applied with chance 1/3; the quality is clamped to 10..85.
//   - the expected quality of the current rank is (85 / 70 / 57 for league 0 / 1 / 2) - rank.
//     A group better than expected climbs, a worse one falls (again 1/5 random direction, and
//     with chance 3/4 no move when 2 * |difference| < rand(1, 18)). The rank changes by a stride
//     rand(1, tempo) and is clamped to 0..11. The tempo starts at 4 and drops by one with chance
//     1/9 per matchday of a 44-entry table built fresh on every call.
// A manager whose three quality bytes are all 0 (no youth teams) is skipped without draws
// (EM_OLD_YOUTHZERO restores the old behaviour of processing him). Throws FormatError on an
// invalid matchday, currency, quality or league. EM_DEBUG_YOUTH prints the tempo table.
void youth_fixture_day(Bytes&,const FinanceTables&,int first_matchday,int manager,const YouthRandomDraw&);
// Season end for `manager`, groups processed from 2 down to 0: a team ranked 0..1 is promoted
// (rank + 10), one ranked 11 relegated (rank - 10) if a league above/below exists, every other
// team starts at rank 5; groups
// 1 and 2 get quality (2 * own + younger group) / 3. No random draws.
void youth_new_season(Bytes&,int manager);
} // namespace em
