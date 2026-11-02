// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Round-robin schedules for the leagues (12 teams), world championship groups (6) and European
// cup groups (4), as EM.EXE builds them.
//
// The original keeps one hand-made table per group size in DGROUP (file offset 0x16DD0 + DS):
//   DS:03C0  11 strings for 12 teams ("abcdefghijkl", "dafchejglibk", ...)
//   DS:0450   5 strings for 6 teams  ("abcdef", "dafcbe", ...)
//   DS:0474   3 strings for 4 teams  ("abcd", "dabc", "acbd")
// each list ending with an empty string. String r is round r of a single round robin: letters in
// pairs (home, away), 'a' = slot 0. ovl13_14d7 turns a matchday into pairings: while the
// matchday is >= n-1 it subtracts n-1 and toggles home/away, so rounds n-1 .. 2n-3 are the
// return games, and so on for a third and fourth round robin. The pairings land in DS:5D7B
// (league * 12 + 2k), the array saved at HOK state 0x8DFD; slots are the team indices within the
// league (team index - 12 * league). Checked against the saved pairings of START (matchday 1),
// TAG1 (matchday 2) and MONAT1 (matchday 10) and against the saved WM and European cup rounds.
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace em {

struct Pairing {
    int home = 0, away = 0;  // slots within the group
};

struct ScheduleTables {
    std::vector<std::string> teams12, teams6, teams4;
    const std::vector<std::string>& for_size(int n) const;
};

// Reads the three tables from EM.EXE (the original program file).
ScheduleTables load_schedule_tables(const Bytes& exe);

// Pairings of a 0-based matchday for a group of rounds.size() + 1 teams.
std::vector<Pairing> round_robin_pairings(const std::vector<std::string>& rounds, int matchday);

}  // namespace em
