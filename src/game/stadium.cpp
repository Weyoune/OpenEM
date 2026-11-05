// SPDX-License-Identifier: GPL-3.0-only
#include "game/stadium.hpp"
#include "game/scorers.hpp"
#include <bit>

namespace em {
namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;
constexpr size_t kManagerBlock = 0x0DCD, kManagerStride = 0x1AF;
int signed_byte(int value) { return value < 128 ? value : value - 256; }
int signed_word(uint16_t value) { return value < 32768 ? int(value) : int(value) - 65536; }
void manager_check(int manager) {
    if (manager < 0 || manager >= 4) throw FormatError("stadium: invalid human manager");
}
} // namespace

StadiumTables load_stadium_tables(const Bytes& exe) {
    StadiumTables tables;
    for (size_t i = 0; i < tables.cost.size(); ++i) {
        tables.cost[i] = std::bit_cast<int32_t>(le32(exe, kDgroupFile + 0x00C0 + i * 4));
        for (size_t j = 0; j < 2; ++j)
            tables.weeks[i][j] = exe.at(kDgroupFile + 0x00EC + i * 2 + j);
    }
    for (size_t i = 0; i < tables.max_level.size(); ++i)
        tables.max_level[i] = signed_word(uint16_t(le16(exe, kDgroupFile + 0x0102 + i * 2) - 1));
    if (tables.cost[0] != 3600000 || tables.cost[10] != 405000 || tables.max_level[4] != 4)
        throw FormatError("EM.EXE: stadium tables not found (not V1.00?)");
    return tables;
}

StadiumTarget stadium_target(const HokFile& h, const StadiumTables& tables,
                             int item, int target, int manager) {
    manager_check(manager);
    if (item < 0 || item >= 11) throw FormatError("stadium: invalid item");
    if (target < -1 || target > 255) throw FormatError("stadium: target outside native byte");
    const size_t block = kManagerBlock + size_t(manager) * kManagerStride;
    const size_t i = size_t(item);
    StadiumTarget result;
    result.item = item;
    if (item < 9) {
        result.base = h.state.at(block + 0x116 + i * 2);
        result.maximum = tables.max_level[i];
        result.available = h.state.at(block + 0x117 + i * 2) == 0 && result.base < result.maximum;
    } else {
        uint16_t remaining = 25000;
        for (size_t kind = 0; kind < 2; ++kind) {
            const auto planned = le16(h.state, block + 0x135 + kind * 4);
            remaining = uint16_t(remaining - (planned ? planned : le16(h.state, block + 0x133 + kind * 4)));
        }
        result.maximum = signed_word(remaining) / 500; // native CWD/IDIV, truncates toward0
        result.available = le16(h.state, block + 0x135 + (i - 9) * 4) == 0 && result.maximum != 0;
    }
    const bool initial = target == -1;
    result.target = initial ? int(uint8_t(result.base + 1)) : target;
    const int delta = signed_byte(result.target) - signed_byte(result.base);
    // Initial quote1088 is the table price directly; only arrow changes use
    // the signed-byte delta and target upper bound113A..1197.
    const uint32_t quote = uint32_t(tables.cost[i]) * uint32_t(initial ? 1 : delta);
    result.nominal_cost = std::bit_cast<int32_t>(quote);
    result.affordable = quote <= le32(h.state, block + 0x18F); // unsigned high/low CMP
    if (!initial)
        result.available = result.available && delta > 0 && signed_byte(result.target) <= result.maximum;
    if (item >= 9)
        result.capacity = uint16_t(le16(h.state, block + 0x133 + (i - 9) * 4) + uint32_t(delta) * 500u);
    return result;
}

int32_t stadium_discounted_cost(int32_t quote, int discount) {
    const uint32_t bits = uint32_t(quote);
    const uint32_t product = bits * uint32_t(discount); //03CA discards high32 BEFORE division
    return std::bit_cast<int32_t>(bits - product / 100u); //0483 is unsigned
}

void stadium_daily_refusal_reset(std::array<std::array<uint8_t, 11>, 4>& gates,
                                 int manager, BorlandRandom& rng) {
    manager_check(manager);
    for (auto& gate : gates[size_t(manager)])
        if (rng.range(0, 10) == 0) gate = 0;
}
} // namespace em
