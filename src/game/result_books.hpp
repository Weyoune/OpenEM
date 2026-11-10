// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The bookings ovl13_2f93 makes after every game on top of the league table (state bytes, no other data):
// win/loss/... series of every club, the head-to-head results of the human club and the club records.
#include "formats/binary.hpp"

namespace em {

// Series of one club (state 0x9988 + team * 0x15). `side` 0 = home, 1 = away.
void book_series(Bytes& state, int team, int side, int goals_for, int goals_against);

// ovl13_3855, once per league matchday after all games: the manager record of every series becomes the maximum of
// the current series of his club (state 0x9EC8 + manager * 0x15 = max(.., series of `team`); ovr037:39EB..3A75 loops over
// the managers 0..N-1 with their club +0x14).
void update_series_records(Bytes& state, int team, int manager = 0);

// Head-to-head results of the human club of `manager` (state 0x8F88 + manager * 0x280): the result enters the opponent's row.
void book_head_to_head(Bytes& state, int home, int away, int home_goals, int away_goals, int human_team, int manager = 0);

// Club records of both teams (state 0xA11C, opponents 0x9F1C), ovl13_3e43.
void book_records(Bytes& state, int home, int away, int home_goals, int away_goals);

}  // namespace em
