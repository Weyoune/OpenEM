// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The bookings the game makes after every game on top of the league table (state bytes, no other data):
// win/loss/... series of every club, the head-to-head results of the human club and the club records.
// These feed the statistics screens ("Totale", opponent info).
#include "formats/binary.hpp"

namespace em {

// Series of one club (state 0x9988 + team * 0x15, teams 0..63; others are ignored). `side` 0 = home, 1 = away.
// Each of the 7 series kinds counts up (capped at 255) in the "all" column and the column of `side` while its
// condition holds, and resets to 0 otherwise.
void book_series(Bytes& state, int team, int side, int goals_for, int goals_against);

// Once per league matchday after all games: the manager's record of every series becomes the maximum of the record
// and the current series of his club (state 0x9EC8 + manager * 0x15 = max(.., series of `team`)). The caller loops
// over the managers 0..N-1 with their clubs.
void update_series_records(Bytes& state, int team, int manager = 0);

// Head-to-head results of the human club of `manager` (state 0x8F88 + manager * 0x280, 64 rows of 10 bytes): the
// result enters the opponent's row as a byte (goals against << 4 | own goals, truncated to 8 bits, not clamped).
// Against clubs (< 52) a row holds 5 results per side (home column 0, away column 1); against national teams and
// later teams all 10 bytes form one list. 0xFF marks a free entry; a full list drops its oldest result.
// Setting the environment variable EM_OLD_H2HCAP restores an older variant that clamps each count to 15.
void book_head_to_head(Bytes& state, int home, int away, int home_goals, int away_goals, int human_team, int manager = 0);

// Club records of both teams (state 0xA11C, opponents 0x9F1C); see result_books.cpp for the record kinds.
void book_records(Bytes& state, int home, int away, int home_goals, int away_goals);

}  // namespace em
