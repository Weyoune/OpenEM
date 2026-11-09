// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Places of the per-manager blocks in the HOK state (docs/MEHRSPIELER_PLAN.md). The original keeps up to four human
// managers (DS:0386 = state 0x0DC6), the bank/pool party 4 and the national coach 5.
#include <cstddef>

namespace em {

constexpr size_t kManagerBlockBase = 0x0DCD;  // DS:965B, manager block (name, team, finances ...)
constexpr size_t kManagerBlockSize = 0x1AF;
constexpr size_t kSquadBlockBase = 0x696D;    // DS:5DC3, 29 squad records of 0x34 bytes per manager
constexpr size_t kSquadBlockSize = 0x618;
constexpr size_t kManagerCount = 0x0DC6;      // DS:0386, number of human managers (1..4)
constexpr int kMaxManagers = 4;

constexpr size_t manager_block(int manager) { return kManagerBlockBase + size_t(manager) * kManagerBlockSize; }
constexpr size_t squad_block(int manager) { return kSquadBlockBase + size_t(manager) * kSquadBlockSize; }

}  // namespace em
