// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Daily share market (ovl39_03ca = 5B0C:03CA, native 63DEA): the four companies' prices, trends and the held quantities.
#include "formats/binary.hpp"

namespace em {

class BorlandRandom;

// State bytes (HOK state offsets): 0x1D0 u32[4] target price x100 (DS:01FD), 0x1E6 u8[4] days left of the trend
// (DS:A891), 0x1EA u8[4] trend 0/1/2 (DS:A895), 0x0B0 u16[4][36] price history x3, oldest first (DS:D0F4),
// 0x034 + manager*32 + company*8 u32 held quantity of the four managers (DS:D218).
// One call per day: grows the targets, moves every price towards its target (with random trend, jumps and the 2:1 split
// above 2000 that doubles all held quantities), shifts the history and stores the new price x3.
void market_day(Bytes& state, BorlandRandom& rng);

}  // namespace em
