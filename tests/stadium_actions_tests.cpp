// SPDX-License-Identifier: GPL-3.0-only
// Tests for the stadium construction rules of src/game/stadium.cpp: construction quotes
// (stadium_target), the discount arithmetic (stadium_discounted_cost) and the daily reset of
// the "builder already asked" refusal gates. Synthetic save states check the original's 16- and
// 32-bit wrap-arounds and its signed/unsigned comparisons; optionally the tables are read from
// EM.EXE and quotes are taken from original save files.
// Arguments (optional): the game data folder and a folder with START.HOK, MONAT1.HOK, SAISEND.HOK.
#include "game/stadium.hpp"
#include "game/scorers.hpp"
#include <bit>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace em;
namespace {
// Throws `message` as a test failure unless `ok`.
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
// Writes a little-endian u16 / u32 into `b` at `at`.
void put16(Bytes& b, size_t at, unsigned value) {
    b.at(at) = uint8_t(value); b.at(at + 1) = uint8_t(value >> 8);
}
void put32(Bytes& b, size_t at, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) b.at(at + i) = uint8_t(value >> (8 * i));
}
// State offset of the manager block of `manager`.
constexpr size_t block(int manager) { return 0x0DCD + size_t(manager) * 0x1AF; }
// The construction tables of EM.EXE V1.00, written out independently of the loader: raw price
// per level (per 500 places for items 9/10), construction weeks {min, max} and the highest level
// of each facility. original_files() checks the loader against these values.
StadiumTables tables() {
    return {{{3600000,1350000,1920000,1110000,990000,3900000,1740000,1560000,1350000,540000,405000}},
            {{{20,30},{12,17},{16,25},{6,9},{5,8},{40,60},{17,24},{14,18},{10,15},{12,17},{10,14}}},
            {{2,2,2,2,4,2,3,3,1}}};
}
// A state where `manager` has a raw balance of 1,500,000 (= 500,000 DM; +0x18F), scoreboard
// level 1 (facility 1, +0x118)
// and 3000 + 4500 current places (+0x133 / +0x137); nothing is under construction.
HokFile fixture(int manager = 1) {
    HokFile h; h.state.assign(0xBB00, 0);
    const auto b = block(manager);
    put32(h.state, b + 0x18F, 1500000);
    h.state.at(b + 0x118) = 1; // Anzeigetafel (scoreboard) current level 1
    put16(h.state, b + 0x133, 3000); put16(h.state, b + 0x137, 4500);
    return h;
}
// Quotes for facilities and place counts: initial quote, arrow targets, availability gates
// (pending construction, maximum level) and that queries never modify the state.
void independent_raw_targets() {
    auto h = fixture(); const auto before = h.state; const auto t = tables();
    auto v = stadium_target(h, t, 1, -1, 1);
    require(v.item == 1 && v.base == 1 && v.target == 2 && v.maximum == 2 &&
            v.available && v.affordable && v.nominal_cost == 1350000,
            "raw manager 1 fixture: next scoreboard level costs 1350000");
    v = stadium_target(h, t, 9, 2, 1);
    require(v.base == 0 && v.target == 2 && v.maximum == 35 && v.capacity == 4000 &&
            v.available && v.affordable && v.nominal_cost == 1080000,
            "3000+4500 places leave 35 chunks; two seats chunks are 1080000 raw");
    v = stadium_target(h, t, 9, 3, 1);
    require(v.available && !v.affordable && v.nominal_cost == 1620000 && v.capacity == 4500,
            "nominal 1620000 remains unaffordable despite potential nine-percent discount");
    require(stadium_target(h, t, 10, 3, 1).nominal_cost == 1215000 &&
            stadium_target(h, t, 10, 3, 1).affordable && !stadium_target(h, t, 10, 4, 1).affordable,
            "standing-place third/fourth chunk original money threshold");
    v = stadium_target(h, t, 0, -1, 1);
    require(v.available && !v.affordable && v.target == 1 && v.nominal_cost == 3600000,
            "initial unaffordable roof quote remains available for the red preview");
    require(!stadium_target(h, t, 1, 1, 1).available && !stadium_target(h, t, 1, 3, 1).available,
            "arrow candidates must exceed current and stay within level bound");
    require(h.state == before, "all target queries preserve raw source bytes");
    h.state.at(block(1) + 0x119) = 2;
    require(!stadium_target(h, t, 1, -1, 1).available, "nonzero pending level blocks new construction");
    h.state.at(block(1) + 0x119) = 0; h.state.at(block(1) + 0x129) = 20;
    require(stadium_target(h, t, 1, -1, 1).available,
            "stray remaining weeks alone are not the original pending-level gate");
    h.state.at(block(1) + 0x118) = 2;
    require(!stadium_target(h, t, 1, -1, 1).available, "maximum current facility blocks quote entry");
}
// Arithmetic edge cases reproduced from the original: unsigned affordability (a negative balance
// counts as a huge cash amount), the planned capacity replacing the current one, 16-bit
// wrap-around of the place sums, signed division for the chunk count, the 32-bit wrap of the
// price product and the signed-byte comparison of the target level.
void unsigned_and_capacity_edges() {
    auto h = fixture(); auto t = tables(); const auto b = block(1);
    put32(h.state, b + 0x18F, 1080000);
    require(stadium_target(h, t, 9, 2, 1).affordable, "cash equality accepted by the unsigned less-or-equal test");
    put32(h.state, b + 0x18F, 1079999);
    require(!stadium_target(h, t, 9, 2, 1).affordable, "one raw unit below nominal is rejected");
    put32(h.state, b + 0x18F, 4294206733u);
    require(stadium_target(h, t, 9, 35, 1).affordable,
            "SAISEND negative signed cash bitpattern still passes unsigned affordability");
    put16(h.state, b + 0x139, 5500);
    auto v = stadium_target(h, t, 9, 2, 1);
    require(v.maximum == 33 && v.available, "other planned capacity replaces 4500 with 5500; not added twice");
    require(!stadium_target(h, t, 10, -1, 1).available, "selected nonzero capacity plan blocks construction");
    put16(h.state, b + 0x139, 0); put16(h.state, b + 0x133, 20000);
    put16(h.state, b + 0x137, 5000);
    require(!stadium_target(h, t, 9, -1, 1).available, "exact 25000 capacity has zero chunks and blocks entry");
    put16(h.state, b + 0x137, 10000);
    v = stadium_target(h, t, 9, -1, 1);
    require(v.maximum == -10 && v.available && !stadium_target(h, t, 9, 1, 1).available,
            "malformed overcapacity: the initial entry only tests maximum != 0, an arrow target additionally tests target <= maximum");
    put16(h.state, b + 0x133, 40000); put16(h.state, b + 0x137, 40000);
    require(stadium_target(h, t, 9, 21, 1).maximum == 21 && stadium_target(h, t, 9, 21, 1).available,
            "16-bit capacity subtraction wraps 25000-80000 to 10536 before signed division");
    put16(h.state, b + 0x133, 65000); put16(h.state, b + 0x137, 0);
    require(stadium_target(h, t, 9, 2, 1).capacity == 464, "16-bit capacity preview addition wraps 66000 to 464");
    h = fixture(); put32(h.state, b + 0x18F, 1200000000); t.cost[9] = 600000000;
    v = stadium_target(h, t, 9, 9, 1);
    require(v.available && v.affordable && v.nominal_cost == 1105032704,
            "nominal multiplication discards the high 32 bits before the unsigned cash test");
    require(!stadium_target(h, t, 9, 128, 1).available,
            "target 128 sign-extends negative and fails the original signed-byte comparison");
}
// stadium_discounted_cost: quote - low32(quote * discount) / 100, all unsigned modulo 2^32.
// The expected values were computed independently of the implementation.
void discounted_cost_goldens() {
    struct Case {uint32_t quote; int discount; uint32_t cost;};
    constexpr Case cases[] = {{3600000,9,3276000},{27000000,9,24570000},{125,10,113},
        {600000000,9,588949673},{300000000,9,273000000},{0x80000000u,9,2126008812u},
        {0xffffffffu,9,4252017623u},{0xffffffffu,0,0xffffffffu}};
    for (const auto& c : cases)
        require(uint32_t(stadium_discounted_cost(std::bit_cast<int32_t>(c.quote), c.discount)) == c.cost,
                "wrapped multiplication/unsigned division rebate golden");
}
// Daily refusal-gate reset: eleven draws rand(0..10) per manager; a gate is cleared when its
// draw is 0. Other gates keep their (arbitrary nonzero) value.
void daily_reset_goldens() {
    std::array<std::array<uint8_t,11>,4> gates{};
    for (auto& row : gates) row.fill(7);
    auto expected = gates; expected[1][5] = 0;
    BorlandRandom rng(1);
    // From seed 1 the eleven draws are 5, 9, 4, 1, 7, 0, 6, 2, 2, 7, 6: only gate 5 is cleared.
    stadium_daily_refusal_reset(gates, 1, rng);
    require(gates == expected && rng.state() == 590113780u,
            "eleven fixed draws clear only manager 1/item 5, retaining every nonzero-result byte");
    expected[3][5] = expected[3][8] = 0;
    stadium_daily_refusal_reset(gates, 3, rng);
    require(gates == expected && rng.state() == 2060562795u,
            "next manager consumes subsequent eleven draws and clears its own items 5/8");
    gates = {}; BorlandRandom zero(0);
    stadium_daily_refusal_reset(gates, 0, zero);
    require(gates == std::array<std::array<uint8_t,11>,4>{} && zero.state() == 4187371143u,
            "already-clear gates still consume all 11 draws");
    bool rejected = false; const auto seed = zero.state();
    try {stadium_daily_refusal_reset(gates, 4, zero);} catch (const FormatError&) {rejected = true;}
    require(rejected && zero.state() == seed, "host manager bound rejects before consuming RNG");
}
// Checks load_stadium_tables() against tables() and takes the 2-chunk seat quote from three
// original saves of manager 0 (start, after one month, season end with a negative balance).
void original_files(const std::filesystem::path& data, const std::filesystem::path& refs) {
    const auto t = load_stadium_tables(read_file(data / "EM.EXE")); const auto golden = tables();
    require(t.cost == golden.cost && t.weeks == golden.weeks && t.max_level == golden.max_level,
            "all 11 original EXE costs/durations and 9 maximum-level words match independent raw extraction");
    struct File {const char* name; uint32_t cash;};
    for (const auto& f : {File{"START.HOK",1500000u},File{"MONAT1.HOK",1554800u},File{"SAISEND.HOK",4294206733u}}) {
        const auto h = decode_hok(read_file(refs / f.name)); const auto before = h.state;
        require(le32(h.state, block(0) + 0x18F) == f.cash, "independent original save cash fixture");
        const auto v = stadium_target(h, t, 9, 2);
        require(v.maximum == 35 && v.nominal_cost == 1080000 && v.capacity == 4000 && v.affordable,
                "original save 3000/4500-place quote vector");
        require(h.state == before, "query leaves every original decrypted byte unchanged");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        independent_raw_targets(); unsigned_and_capacity_edges(); discounted_cost_goldens(); daily_reset_goldens();
        if (argc > 1) original_files(argv[1], argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path(argv[1]));
        std::puts("Native stadium target/arithmetic/reset tests passed"); return 0;
    } catch (const std::exception& e) {std::fprintf(stderr,"Stadium actions: %s\n",e.what());return 1;}
}
