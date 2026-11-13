// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Offsets of the per-manager blocks in the HOK save state. The original keeps up to four human
// managers (count at state 0x0DC6), the bank/pool party 4 and the national coach 5; each has a
// manager block (name, team, finances, stadium ...) and a squad block.
#include <cstddef>

namespace em {

constexpr size_t kManagerBlockBase = 0x0DCD;  // manager block of manager 0 (name, team, finances ...)
constexpr size_t kManagerBlockSize = 0x1AF;   // stride between two manager blocks
constexpr size_t kSquadBlockBase = 0x696D;    // squad block of manager 0: 29 squad records of 0x34 bytes
constexpr size_t kSquadBlockSize = 0x618;     // stride between two squad blocks
constexpr size_t kManagerCount = 0x0DC6;      // u8: number of human managers (1..4)
constexpr int kMaxManagers = 4;               // human managers the game supports

// State offset of the manager block of `manager` (0-based; 4 = bank/pool, 5 = national coach).
constexpr size_t manager_block(int manager) { return kManagerBlockBase + size_t(manager) * kManagerBlockSize; }
// State offset of the squad block of `manager` (0-based).
constexpr size_t squad_block(int manager) { return kSquadBlockBase + size_t(manager) * kSquadBlockSize; }

}  // namespace em
