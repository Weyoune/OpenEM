// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Round-robin schedules for the leagues (12 teams), world championship groups (6) and European
// cup groups (4), as EM.EXE builds them.
//
// The original keeps one hand-made table per group size in its data segment (EM.EXE file offset
// 0x16DD0 + the offset given here):
//   0x03C0  11 strings for 12 teams ("abcdefghijkl", "dafchejglibk", ...)
//   0x0450   5 strings for 6 teams  ("abcdef", "dafcbe", ...)
//   0x0474   3 strings for 4 teams  ("abcd", "dabc", "acbd")
// each list ending with an empty string. String r is round r of a single round robin: letters in
// pairs (home, away), 'a' = slot 0. A matchday is turned into pairings like this: while the
// matchday is >= n-1, subtract n-1 and toggle home/away, so rounds n-1 .. 2n-3 are the
// return games, and so on for a third and fourth round robin. The league pairings are saved at
// HOK state 0x8DFD (league * 12 + 2k); slots are the team indices within the league
// (team index - 12 * league). This matches the pairings, WM and European cup rounds stored in
// the original's saves.
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace em {

// One game of a matchday.
struct Pairing {
    int home = 0, away = 0;  // slots within the group
};

// The three round tables (one string per round of a single round robin).
struct ScheduleTables {
    std::vector<std::string> teams12, teams6, teams4;
    // The table for a group of `n` teams (12, 6 or 4); throws FormatError for other sizes.
    const std::vector<std::string>& for_size(int n) const;
};

// Reads the three tables from EM.EXE (the original program file); throws FormatError unless they
// have 11, 5 and 3 rounds (V1.00).
ScheduleTables load_schedule_tables(const Bytes& exe);

// Pairings of a 0-based matchday for a group of rounds.size() + 1 teams; every further pass through
// the round robin swaps home and away.
std::vector<Pairing> round_robin_pairings(const std::vector<std::string>& rounds, int matchday);

}  // namespace em
