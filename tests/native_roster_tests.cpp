// SPDX-License-Identifier: GPL-3.0-only
// Tests for the raw squad-roster operations of src/game/native_roster.cpp: inserting a player
// bought by the computer side into club block 0 (native_insert_cpu_purchase) and removing the
// row again (native_remove_purchase_row). Every case compares the whole save state and the
// random generator state against hand-computed golden values, including the original game's
// quirks (scratch slot 29 keeps stale bytes, the reservation count only looks at the first
// <occupied count> rows of other blocks).
#include "game/native_roster.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

using namespace em;
namespace {
// These are independent synthetic fixtures with hand-computed arithmetic goldens; no EXE or
// reference save is required.
// Squad blocks: first block at `squad`, `block` bytes per manager, `record` (52) bytes per row.
constexpr size_t squad = 0x696D, record = 0x34, block = 0x618;
// State offset of row `slot` in the squad block of `manager` (4 = transfer pool, 5 = national).
size_t row(int slot, int manager = 0) { return squad + size_t(manager) * block + size_t(slot) * record; }
// State offset of the 33-byte player-table record of 1-based player `id` (table at 0x4FC6, so
// id 0 lands one record before it). Fields used: +0x14 age, +0x15..+0x17 skills, +0x18
// position value, +0x1A owner kind (4 = computer club), +0x1B/+0x1C goals/games,
// +0x1D team, +0x1E index of the linked manager message.
size_t player(int id) { return 0x4FA5 + size_t(id) * 0x21; }
// Throws `message` as a test failure unless `condition`.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Copies one 52-byte squad record from offset `source` of `from` to offset `at` of `to`.
void copy_record(Bytes& to, size_t at, const Bytes& from, size_t source) {
    std::copy_n(from.begin() + long(source), record, to.begin() + long(at));
}
// Minimal contract tables: the salary weights put everything (100) on the first criterion,
// salary bonus per league class 2400/1800/990 DM, game level 0, and team-class limits
// 12/24/35 (team 20, the player's old club, is therefore second class).
ContractTables tables() {
    ContractTables t;
    t.weights[1] = {100, 0, 0, 0, 0, 0};
    t.league_bonus = {2400, 1800, 990};
    t.level = 0;
    t.class_limit = {12, 24, 35};
    return t;
}
// One human manager (count at 0x0DC6) whose club (manager block +0x14) is team 4. Squad
// block 0 holds a goalkeeper (id 1), a defender (id 2) and a forward (id 3) in rows 0..2; the
// other rows carry filler bytes 0x60 + slot with a zero player ID. Player 4 (age 27, skills
// 60/40/55, defender, old club 20) sits in the transfer pool and is the player being bought.
HokFile fixture() {
    HokFile h;
    h.state.assign(0xBB00, 0);
    h.state.at(0x0DC6) = 1;
    h.state.at(0x0DCD + 0x14) = 4;
    // Deliberately preserve nonzero bytes in unused records and in scratch slot 29.
    for (int slot = 0; slot < 30; ++slot) {
        std::fill_n(h.state.begin() + long(row(slot)), record, uint8_t(0x60 + slot));
        h.state.at(row(slot) + 0x0F) = 0;
    }
    for (int id = 0; id <= 199; ++id) h.state.at(player(id) + 0x1A) = 4;
    h.state.at(row(0) + 0x0F) = 1; h.state.at(player(1) + 0x18) = 0;
    h.state.at(row(1) + 0x0F) = 2; h.state.at(player(2) + 0x18) = 30;
    h.state.at(row(2) + 0x0F) = 3; h.state.at(player(3) + 0x18) = 75;
    const size_t p = player(4);
    h.state.at(p + 0x14) = 27;
    h.state.at(p + 0x15) = 60; h.state.at(p + 0x16) = 40; h.state.at(p + 0x17) = 55;
    h.state.at(p + 0x18) = 30; h.state.at(p + 0x1B) = 7; h.state.at(p + 0x1C) = 9;
    h.state.at(p + 0x1D) = 20; // old second-division club; buyer 4 is first division
    h.state.at(row(0, 4) + 0x0F) = 4;
    return h;
}
// The fresh squad record expected for player 4 when the generator starts at seed 1.
// Salary: p = (60 + 40) / 2 = 50; 50^3 / 1400 = 89; 89 * 85 * 10 / 3 = 25216;
// (25216 + old-club bonus 1800) / 300 * 300 = 27000 = 0x6978 (+0x28, i32).
// The five raw generator outputs 346, 130, 10982, 1090, 11656 of seed 1 map to exhaustion 98
// (+0x13), experience 41 (+0x0E), trend 11, direction 2 and high bit 0 (packed into +0x14 as 43).
// Other fields: +0x03 goals, +0x06 games, +0x0B contract years, +0x0F id, +0x10..+0x12 skills.
std::array<uint8_t, 52> seed1_record() {
    std::array<uint8_t, 52> r{};
    r[0x03] = 7; r[0x06] = 9; r[0x0B] = 1; r[0x0E] = 41; r[0x0F] = 4;
    r[0x10] = 60; r[0x11] = 40; r[0x12] = 55; r[0x13] = 98; r[0x14] = 43;
    r[0x28] = 0x78; r[0x29] = 0x69;
    return r;
}
// Asserts that squad row `slot` of block 0 equals `expected` in all 52 bytes.
void expect_record(const HokFile& h, int slot, const std::array<uint8_t, 52>& expected) {
    require(std::equal(expected.begin(), expected.end(), h.state.begin() + long(row(slot))),
            "fresh record must match every one of 52 raw bytes");
}

// Insertion of a defender between the existing defender and forward (rows 2..28 shift down to
// 3..29) and its removal again: only the mapped squad bytes and the player's team change, and
// the removal leaves the stale copy in scratch slot 29 apart from its cleared player ID.
void insertion_and_cleanup_goldens() {
    auto h = fixture(); const Bytes before = h.state;
    BorlandRandom rng(1);
    require(native_insert_cpu_purchase(h, tables(), rng, 4) == 2,
            "same-class defender inserted after existing defender, before forward");
    require(rng.state() == 2911408402u, "insertion consumes exactly five seed 1 draws");
    expect_record(h, 2, seed1_record());
    Bytes expected = before;
    // Precomputed fixture mapping: rows 0, 1 stay; old rows 2..28 become new rows 3..29.
    for (int destination = 3; destination <= 29; ++destination)
        copy_record(expected, row(destination), before, row(destination - 1));
    const auto incoming = seed1_record();
    std::copy(incoming.begin(), incoming.end(), expected.begin() + long(row(2)));
    expected.at(player(4) + 0x1D) = 4;
    require(h.state == expected,
            "insertion changes only mapped squad bytes and global team; owner/pool/cash stay");
    require(le32(h.state, row(2) + 0x28) == 27000 &&
            h.state.at(player(4) + 0x1A) == 4,
            "salary uses old team class and owner remains CPU");

    native_remove_purchase_row(h, 2);
    expected = before;
    expected.at(player(4) + 0x1D) = 4; // team restoration belongs to negotiation caller
    copy_record(expected, row(29), before, row(28));
    expected.at(row(29) + 0x0F) = 0;
    require(h.state == expected, "cleanup restores occupied rows but leaves the scratch-slot residue");
    require(rng.state() == 2911408402u, "cleanup does not consume RNG");
}

// With a hole at row 1 the insertion fills the hole without moving later rows; the removal
// afterwards still shifts the whole tail up (the original has no rollback of the hole fill).
// A second seed checks the five-draw sequence independently.
void hole_and_second_rng_golden() {
    auto h = fixture(); h.state.at(row(1) + 0x0F) = 0;
    const Bytes before = h.state;
    BorlandRandom rng(0x12345678u);
    require(native_insert_cpu_purchase(h, tables(), rng, 4) == 1,
            "sorted scan stops at first hole without packing subsequent occupied rows");
    auto incoming = seed1_record();
    incoming[0x0E] = 55; incoming[0x13] = 97; incoming[0x14] = 156;
    expect_record(h, 1, incoming);
    require(rng.state() == 1237307973u, "second independent five-draw generator seed golden");
    Bytes expected = before;
    std::copy(incoming.begin(), incoming.end(), expected.begin() + long(row(1)));
    expected.at(player(4) + 0x1D) = 4;
    require(h.state == expected, "empty insertion clears only the hole; no tail memmove");
    native_remove_purchase_row(h, 1);
    expected = before;
    for (int destination = 1; destination <= 28; ++destination)
        copy_record(expected, row(destination), before, row(destination + 1));
    expected.at(row(29) + 0x0F) = 0;
    expected.at(player(4) + 0x1D) = 4;
    require(h.state == expected,
            "cleanup still shifts the tail when insertion used a hole; no invented rollback");
}

// Clears block 0 and fills its first `count` rows with forwards (ids 10, 11, ...).
void fill_regular(HokFile& h, int count) {
    for (int slot = 0; slot < 30; ++slot) h.state.at(row(slot) + 0x0F) = 0;
    for (int slot = 0; slot < count; ++slot) {
        const int id = 10 + slot;
        h.state.at(row(slot) + 0x0F) = uint8_t(id);
        h.state.at(player(id) + 0x18) = 75;
    }
}
// Asserts that buying player 4 is refused (slot 127) before any state write or random draw.
void must_refuse(HokFile& h, const char* message) {
    const Bytes before = h.state;
    BorlandRandom rng(1);
    require(native_insert_cpu_purchase(h, tables(), rng, 4) == 127, message);
    require(h.state == before && rng.state() == 1,
            "capacity refusal precedes every write and RNG draw");
}
// The capacity gate: more than 28 reserved places (own regular rows plus own players held in
// the pool or another active human block) refuse the purchase; inactive human blocks, the
// national block and scratch slot 29 do not count, other blocks are only scanned up to their
// occupied count, and a player already at the buyer's club bypasses the gate.
void capacity_and_reservations() {
    {
        auto h = fixture(); fill_regular(h, 29);
        must_refuse(h, "29 regular records fill capacity regardless of their global owner");
    }
    {
        auto h = fixture(); fill_regular(h, 28);
        h.state.at(row(1, 4) + 0x0F) = 60; h.state.at(player(60) + 0x1A) = 0;
        must_refuse(h, "28 own records plus own player reserved in pool reaches 29");
    }
    {
        auto h = fixture(); fill_regular(h, 28);
        h.state.at(0x0DC6) = 2;
        h.state.at(row(0, 1) + 0x0F) = 60; h.state.at(player(60) + 0x1A) = 0;
        must_refuse(h, "own player in another active human squad also reserves a slot");
    }
    {
        auto h = fixture(); fill_regular(h, 28);
        h.state.at(row(0, 1) + 0x0F) = 60; h.state.at(player(60) + 0x1A) = 0;
        h.state.at(row(29) + 0x0F) = 61; // the scratch slot is excluded from the count
        h.state.at(row(0, 5) + 0x0F) = 62; h.state.at(player(62) + 0x1A) = 0;
        BorlandRandom rng(1);
        require(native_insert_cpu_purchase(h, tables(), rng, 4) == 0,
                "inactive human, national manager 5 and scratch slot 29 do not reserve capacity");
    }
    {
        auto h = fixture(); fill_regular(h, 28); h.state.at(0x0DC6) = 2;
        h.state.at(row(1, 1) + 0x0F) = 60; h.state.at(player(60) + 0x1A) = 0;
        // Occupied count 1, so only row 0 is inspected; it holds player ID 0 with owner 4.
        BorlandRandom rng(1);
        require(native_insert_cpu_purchase(h, tables(), rng, 4) == 0,
                "reservation count does not inspect owned row 1 beyond occupied-count prefix 1");
    }
    {
        auto h = fixture(); fill_regular(h, 28);
        h.state.at(row(0, 4) + 0x0F) = 0;
        h.state.at(row(11, 4) + 0x0F) = 60; h.state.at(player(60) + 0x1A) = 0;
        BorlandRandom rng(1);
        require(native_insert_cpu_purchase(h, tables(), rng, 4) == 0,
                "pool also retains the original count-as-prefix quirk at its 29/12 boundary");
    }
    {
        auto h = fixture(); fill_regular(h, 29);
        h.state.at(player(4) + 0x1D) = 4;
        BorlandRandom rng(1);
        require(native_insert_cpu_purchase(h, tables(), rng, 4) == 0 &&
                le32(h.state, row(0) + 0x28) == 27600,
                "already at buyer club bypasses capacity gate and values that old class");
    }
}

// Removal deletes the player's linked manager message (clears the first 3 bytes of the 0x71-byte
// entry at 0xAEB7 + (index - 1) * 0x71, and the link +0x1E) only for link indices 1..24; 0 and 25
// leave both alone. The fourth byte is set in the fixture to show it survives.
// Removing an empty row changes nothing, and removing scratch slot 29 only clears its ID.
void notification_and_removal_bounds() {
    for (int index : {1, 24, 0, 25}) {
        auto h = fixture();
        h.state.at(row(5) + 0x0F) = 4;
        h.state.at(player(4) + 0x1E) = uint8_t(index);
        const size_t entry = index == 24 ? 0xB8DE : 0xAEB7;
        h.state.at(entry) = 11; h.state.at(entry + 1) = 22;
        h.state.at(entry + 2) = 33; h.state.at(entry + 3) = 44;
        const Bytes before = h.state;
        native_remove_purchase_row(h, 5);
        Bytes expected = before;
        for (int destination = 5; destination <= 28; ++destination)
            copy_record(expected, row(destination), before, row(destination + 1));
        expected.at(row(29) + 0x0F) = 0;
        if (index == 1 || index == 24) {
            expected.at(player(4) + 0x1E) = 0;
            expected.at(entry) = expected.at(entry + 1) = expected.at(entry + 2) = 0;
        }
        require(h.state == expected,
                "message deletion clears exactly 3 bytes for 1..24; 0/25 leave it intact");
    }
    {
        auto h = fixture(); const Bytes before = h.state;
        native_remove_purchase_row(h, 5);
        require(h.state == before, "removing an empty row does not even zero the scratch slot 29 ID");
    }
    {
        auto h = fixture(); h.state.at(row(29) + 0x0F) = 4;
        Bytes expected = h.state; expected.at(row(29) + 0x0F) = 0;
        native_remove_purchase_row(h, 29);
        require(h.state == expected, "scratch slot 29 removal zeros the ID only without clearing the other 51 bytes");
    }
}
} // namespace

int main() {
    try {
        insertion_and_cleanup_goldens(); hole_and_second_rng_golden();
        capacity_and_reservations(); notification_and_removal_bounds();
        std::printf("CPU purchase roster raw/RNG goldens passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Native roster: %s\n", e.what());
        return 1;
    }
}
