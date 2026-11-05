// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include <array>
#include <cstdint>

namespace em {
class BorlandRandom;

struct StadiumTables {
    std::array<int32_t, 11> cost{};
    std::array<std::array<int, 2>, 11> weeks{};
    std::array<int, 9> max_level{};
};
StadiumTables load_stadium_tables(const Bytes& exe);

struct StadiumTarget {
    int item = -1, target = 0, base = 0, maximum = 0;
    int32_t nominal_cost = 0;
    int capacity = 0; // capacity preview for item9/10; zero for facilities
    bool available = false, affordable = false;
};

// 56D1:0F1A..1298. target=-1 opens the native initial base+1 quote;
// other targets are arrow candidates, checked as signed bytes. Facilities use
// their current level as base; capacities use base0 and additional500-place chunks.
// available includes pending/max/target gates, but does not include affordability
// or the process-local refusal gate. No mutation/RNG/model-cache dependency.
StadiumTarget stadium_target(const HokFile& h, const StadiumTables& tables,
                             int item, int target, int manager = 0);

// 56D1:13FF..1421: quote - low32(quote*discount)/100u, modulo2^32.
int32_t stadium_discounted_cost(int32_t quote, int discount);

// 56D1:1D75..1DA8. Always consumes eleven draws, including for zero gates.
void stadium_daily_refusal_reset(std::array<std::array<uint8_t, 11>, 4>& gates,
                                 int manager, BorlandRandom& rng);
} // namespace em
