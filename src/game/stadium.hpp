// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Stadium construction: the price/time tables from EM.EXE, the quote for building an item to a
// target level (9 facilities plus the two place counts), the discount, and the daily reset of the
// "builder already asked today" gates. The construction dialog itself is Game::begin_stadium_build /
// finish_stadium_build (stadium_actions.cpp).
//
// Manager block fields used: +0x116 + 2i current level of facility i, +0x117 + 2i level under
// construction (0 = none), +0x128 + i weeks until item i is finished, +0x133 / +0x137 u16 current
// places of the two place kinds, +0x135 / +0x139 u16 their new totals under construction (0 = none),
// +0x18F i32 balance.

#include "formats/hok.hpp"
#include <array>
#include <cstdint>

namespace em {
class BorlandRandom;

// Construction tables of EM.EXE, per item 0..10 (0..8 facilities, 9/10 the two place counts).
struct StadiumTables {
    std::array<int32_t, 11> cost{};              // raw price per level (per 500 places for items 9/10)
    std::array<std::array<int, 2>, 11> weeks{};  // construction time range in weeks {min, max}
    std::array<int, 9> max_level{};              // highest level per facility
};
// Reads the tables from the EM.EXE data segment (offsets 0x00C0, 0x00EC, 0x0102); throws
// FormatError if they do not look like V1.00.
StadiumTables load_stadium_tables(const Bytes& exe);

// A construction quote.
struct StadiumTarget {
    int item = -1, target = 0, base = 0, maximum = 0;  // item, requested level, current level, highest level
    int32_t nominal_cost = 0;  // price before discount (raw)
    int capacity = 0; // places after construction for items 9/10; zero for facilities
    bool available = false, affordable = false;
};

// The quote for building `item` to level `target`. target = -1 is the initial quote the dialog
// opens with (base + 1, at the plain table price); other targets are arrow candidates, compared as
// signed bytes and priced at cost * (target - base). Facilities use their current level as base;
// the place counts use base 0 and steps of 500 places, with at most 25000 places in total.
// `available` covers "nothing under construction", the maximum and the target, but not
// affordability (`affordable`: price <= balance, unsigned comparison) or the per-session refusal
// gate. Does not modify anything or draw random numbers. Throws FormatError for invalid arguments.
StadiumTarget stadium_target(const HokFile& h, const StadiumTables& tables,
                             int item, int target, int manager = 0);

// The price after a discount of `discount` percent: quote - low32(quote * discount) / 100 (unsigned),
// modulo 2^32, as the original computes it.
int32_t stadium_discounted_cost(int32_t quote, int discount);

// The daily reset of the refusal gates of `manager`: each of the 11 gates is cleared with chance
// 1/11. Always consumes eleven draws, including for gates that are already clear.
void stadium_daily_refusal_reset(std::array<std::array<uint8_t, 11>, 4>& gates,
                                 int manager, BorlandRandom& rng);
} // namespace em
