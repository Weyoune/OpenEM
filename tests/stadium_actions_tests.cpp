// SPDX-License-Identifier: GPL-3.0-only
#include "game/stadium.hpp"
#include "game/scorers.hpp"
#include <bit>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace em;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void put16(Bytes& b, size_t at, unsigned value) {
    b.at(at) = uint8_t(value); b.at(at + 1) = uint8_t(value >> 8);
}
void put32(Bytes& b, size_t at, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) b.at(at + i) = uint8_t(value >> (8 * i));
}
constexpr size_t block(int manager) { return 0x0DCD + size_t(manager) * 0x1AF; }
StadiumTables tables() {
    // Independent shipped-EXE table extraction, stadium_ui/data.json.
    return {{{3600000,1350000,1920000,1110000,990000,3900000,1740000,1560000,1350000,540000,405000}},
            {{{20,30},{12,17},{16,25},{6,9},{5,8},{40,60},{17,24},{14,18},{10,15},{12,17},{10,14}}},
            {{2,2,2,2,4,2,3,3,1}}};
}
HokFile fixture(int manager = 1) {
    HokFile h; h.state.assign(0xBB00, 0);
    const auto b = block(manager);
    put32(h.state, b + 0x18F, 1500000);
    h.state.at(b + 0x118) = 1; // Anzeigetafel current1
    put16(h.state, b + 0x133, 3000); put16(h.state, b + 0x137, 4500);
    return h;
}
void independent_raw_targets() {
    auto h = fixture(); const auto before = h.state; const auto t = tables();
    auto v = stadium_target(h, t, 1, -1, 1);
    require(v.item == 1 && v.base == 1 && v.target == 2 && v.maximum == 2 &&
            v.available && v.affordable && v.nominal_cost == 1350000,
            "raw manager1 fixture: next scoreboard level costs1350000");
    v = stadium_target(h, t, 9, 2, 1);
    require(v.base == 0 && v.target == 2 && v.maximum == 35 && v.capacity == 4000 &&
            v.available && v.affordable && v.nominal_cost == 1080000,
            "3000+4500 places leave35 chunks; two seats chunks are1080000 raw");
    v = stadium_target(h, t, 9, 3, 1);
    require(v.available && !v.affordable && v.nominal_cost == 1620000 && v.capacity == 4500,
            "nominal1620000 remains unaffordable despite potential nine-percent discount");
    require(stadium_target(h, t, 10, 3, 1).nominal_cost == 1215000 &&
            stadium_target(h, t, 10, 3, 1).affordable && !stadium_target(h, t, 10, 4, 1).affordable,
            "standing-place third/fourth chunk original money threshold");
    v = stadium_target(h, t, 0, -1, 1);
    require(v.available && !v.affordable && v.target == 1 && v.nominal_cost == 3600000,
            "native initial unaffordable roof quote remains available for red preview");
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
void unsigned_and_capacity_edges() {
    auto h = fixture(); auto t = tables(); const auto b = block(1);
    put32(h.state, b + 0x18F, 1080000);
    require(stadium_target(h, t, 9, 2, 1).affordable, "cash equality accepted by unsigned low-word JBE");
    put32(h.state, b + 0x18F, 1079999);
    require(!stadium_target(h, t, 9, 2, 1).affordable, "one raw unit below nominal is rejected");
    put32(h.state, b + 0x18F, 4294206733u);
    require(stadium_target(h, t, 9, 35, 1).affordable,
            "SAISEND negative signed cash bitpattern still passes unsigned affordability");
    put16(h.state, b + 0x139, 5500);
    auto v = stadium_target(h, t, 9, 2, 1);
    require(v.maximum == 33 && v.available, "other planned capacity replaces4500 with5500; not added twice");
    require(!stadium_target(h, t, 10, -1, 1).available, "selected nonzero capacity plan blocks construction");
    put16(h.state, b + 0x139, 0); put16(h.state, b + 0x133, 20000);
    put16(h.state, b + 0x137, 5000);
    require(!stadium_target(h, t, 9, -1, 1).available, "exact25000 capacity has zero chunks and blocks entry");
    put16(h.state, b + 0x137, 10000);
    v = stadium_target(h, t, 9, -1, 1);
    require(v.maximum == -10 && v.available && !stadium_target(h, t, 9, 1, 1).available,
            "malformed overcapacity: initial native entry tests DI!=0, arrow additionally tests target<=DI");
    put16(h.state, b + 0x133, 40000); put16(h.state, b + 0x137, 40000);
    require(stadium_target(h, t, 9, 21, 1).maximum == 21 && stadium_target(h, t, 9, 21, 1).available,
            "16-bit capacity subtraction wraps25000-80000 to10536 before signed division");
    put16(h.state, b + 0x133, 65000); put16(h.state, b + 0x137, 0);
    require(stadium_target(h, t, 9, 2, 1).capacity == 464, "capacity preview ADD wraps66000 to464");
    h = fixture(); put32(h.state, b + 0x18F, 1200000000); t.cost[9] = 600000000;
    v = stadium_target(h, t, 9, 9, 1);
    require(v.available && v.affordable && v.nominal_cost == 1105032704,
            "03CA nominal multiplication discards high32 before unsigned cash test");
    require(!stadium_target(h, t, 9, 128, 1).available,
            "target128 sign-extends negative and fails the native signed-byte comparison");
}
void discounted_cost_goldens() {
    // stadium_math/data.json: independent execution of original03CA/0483 bytes.
    struct Case {uint32_t quote; int discount; uint32_t cost;};
    constexpr Case cases[] = {{3600000,9,3276000},{27000000,9,24570000},{125,10,113},
        {600000000,9,588949673},{300000000,9,273000000},{0x80000000u,9,2126008812u},
        {0xffffffffu,9,4252017623u},{0xffffffffu,0,0xffffffffu}};
    for (const auto& c : cases)
        require(uint32_t(stadium_discounted_cost(std::bit_cast<int32_t>(c.quote), c.discount)) == c.cost,
                "native wrapped multiplication/unsigned division rebate golden");
}
void daily_reset_goldens() {
    std::array<std::array<uint8_t,11>,4> gates{};
    for (auto& row : gates) row.fill(7);
    auto expected = gates; expected[1][5] = 0;
    BorlandRandom rng(1);
    // Original1D79..1DA8, seed1:5,9,4,1,7,0,6,2,2,7,6.
    stadium_daily_refusal_reset(gates, 1, rng);
    require(gates == expected && rng.state() == 590113780u,
            "eleven fixed draws clear only manager1/item5, retaining every nonzero-result byte");
    expected[3][5] = expected[3][8] = 0;
    stadium_daily_refusal_reset(gates, 3, rng);
    require(gates == expected && rng.state() == 2060562795u,
            "next manager consumes subsequent eleven draws and clears its own items5/8");
    gates = {}; BorlandRandom zero(0);
    stadium_daily_refusal_reset(gates, 0, zero);
    require(gates == std::array<std::array<uint8_t,11>,4>{} && zero.state() == 4187371143u,
            "already-clear gates still consume all11 draws");
    bool rejected = false; const auto seed = zero.state();
    try {stadium_daily_refusal_reset(gates, 4, zero);} catch (const FormatError&) {rejected = true;}
    require(rejected && zero.state() == seed, "host manager bound rejects before consuming RNG");
}
void original_files(const std::filesystem::path& data, const std::filesystem::path& refs) {
    const auto t = load_stadium_tables(read_file(data / "EM.EXE")); const auto golden = tables();
    require(t.cost == golden.cost && t.weeks == golden.weeks && t.max_level == golden.max_level,
            "all11 original EXE costs/durations and9 maximum-level words match independent raw extraction");
    struct File {const char* name; uint32_t cash;};
    for (const auto& f : {File{"START.HOK",1500000u},File{"MONAT1.HOK",1554800u},File{"SAISEND.HOK",4294206733u}}) {
        const auto h = decode_hok(read_file(refs / f.name)); const auto before = h.state;
        require(le32(h.state, block(0) + 0x18F) == f.cash, "independent original save cash fixture");
        const auto v = stadium_target(h, t, 9, 2);
        require(v.maximum == 35 && v.nominal_cost == 1080000 && v.capacity == 4000 && v.affordable,
                "original save3000/4500-place quote vector");
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
