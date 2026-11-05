// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_status.hpp"

#include <cstdint>

namespace em::ui {
namespace {
constexpr size_t kSquad = 0x696D, kSquadBlock = 0x618, kRecord = 52;
constexpr size_t kPlayers = 0x4FC6, kPlayerSize = 33;

// ovl12_0d27(manager,0) counts occupied slots; its callers then walk that
// many records from the beginning, relying on the game's packed rosters.
int native_count(const Bytes& state, int manager) {
    int count = 0;
    const int slots = manager == 4 ? 12 : 29;
    const size_t base = kSquad + size_t(manager) * kSquadBlock;
    for (int slot = 0; slot < slots; ++slot)
        count += state.at(base + size_t(slot) * kRecord + 15) != 0;
    return count;
}
}  // namespace

std::array<bool, 6> classic_status(const HokFile& snapshot, const ManagerBudget& budget, int manager) {
    if (manager < 0 || manager > 5 || manager == 4)
        throw FormatError("HOK: invalid header status manager");
    const Bytes& state = snapshot.state;
    std::array<bool, 6> green{true, true, true, true, true, true};

    // 59B5:2AE4..2C07,2C61: selected goalie and exactly five skaters in
    // each of the first three lines. The original does not filter injuries.
    std::array<uint8_t, 4> missing{5, 5, 5, 5};
    bool goalkeeper = false;
    const size_t base = kSquad + size_t(manager) * kSquadBlock;
    for (int slot = 0, count = native_count(state, manager); slot < count; ++slot) {
        const size_t record = base + size_t(slot) * kRecord;
        const int id = state.at(record + 15);
        if (!id) continue;  // native record0 is an empty dummy, absent from HOK's player block
        const size_t player = kPlayers + size_t(id - 1) * kPlayerSize;
        if (state.at(player + 0x1E)) green[1] = false;
        const int line = state.at(record + 0x15) & 0x7F;
        if (!state.at(record + 0x0A) || !line) continue;
        if (!state.at(player + 0x18)) goalkeeper = true;
        else if (line < 4) --missing[size_t(line)];
    }
    for (int slot = 0, count = native_count(state, 4); slot < count; ++slot) {
        const int id = state.at(kSquad + 4 * kSquadBlock + size_t(slot) * kRecord + 15);
        if (!id) continue;
        const size_t player = kPlayers + size_t(id - 1) * kPlayerSize;
        if (state.at(player + 0x1A) == manager && state.at(player + 0x1E)) green[1] = false;
    }
    green[0] = goalkeeper && missing[1] == 0 && missing[2] == 0 && missing[3] == 0;

    // 59B5:2C6B..2C80 forces the national view's financial lights green.
    if (manager == 5) return green;

    // 29E9..2A87: original holdings are four eight-byte records per
    // manager: signed32 cost, unsigned32 quantity. Price history contains
    // 36 unsigned16 entries per company; entry35 is current. Keep the
    // native low32-bit products and sums, then compare signed32 totals.
    bool has_shares = false;
    uint32_t cost = 0, value = 0;
    for (size_t company = 0; company < 4; ++company) {
        const size_t holding = 0x30 + size_t(manager) * 32 + company * 8;
        const uint32_t quantity = le32(state, holding + 4);
        if (!quantity) continue;
        has_shares = true;
        cost += le32(state, holding);
        value += quantity * le16(state, 0xB0 + company * 72 + 70);
    }
    green[5] = int32_t(value) >= int32_t(cost);

    // 2A8B..2AE4: paid recovery training; cash held back despite assets.
    green[3] = budget.open_97ee == 0;
    green[2] = !(budget.balance < 0 && (budget.bank > 0 || has_shares));

    // 2C09..2C5F: slots0..8 always eligible;9/10 only at the exact
    // corresponding stadium level2, not merely any nonzero level.
    for (size_t slot = 0; slot < budget.adverts.size(); ++slot) {
        if (budget.adverts[slot].amount != 0) continue;
        if (slot == 9 && budget.stadium[0] != 2) continue;
        if (slot == 10 && budget.stadium[2] != 2) continue;
        green[4] = false;
    }
    return green;
}

}  // namespace em::ui
