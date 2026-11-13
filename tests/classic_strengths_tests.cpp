// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic strengths page "Stärken" (ui/classic_strengths.cpp): the twelve clubs of a league
// sorted by overall, defence or attack strength, with the columns K, T, F (each clamped to 99 for display), the
// overall value, the faces of the last eight results with their summary, morale, and the highlight of the
// managers' clubs; plus the values for START and MONAT1 and the exact pixels of the drawing.
//
// Usage: classic_strengths_tests [DATA] — DATA holds EM.EXE, the graphics, START.HOK and MONAT1.HOK.
#include "ui/classic_strengths.hpp"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <utility>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `condition` is false.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// State offset of the team record `id` (61 bytes; +0x16 owner, +0x17 morale, +0x18..+0x1D the six strength bytes,
// alternately defence (even) and attack (odd)).
size_t team(int id) { return 0x17E7 + size_t(id) * 61; }
// Writes the six strength bytes of team `id`.
void stats(HokFile& h, int id, std::initializer_list<uint8_t> values) {
    size_t p = team(id) + 0x18;
    for (const auto value : values) h.state.at(p++) = value;
}
// A state with one active manager at club 7, a national manager (block 5) at team 52, the other managers at 149,
// and 150 teams named "T" with morale 50 and strength 0.
HokFile fixture() {
    HokFile h;
    h.state.assign(0xBB00, 0);
    h.state.at(0x0DC6) = 1;
    for (size_t m = 0; m < 6; ++m) h.state.at(0x0DCD + m * 0x1AF + 0x14) = 149;
    h.state.at(0x0DCD + 0x14) = 7;
    h.state.at(0x0DCD + 5 * 0x1AF + 0x14) = 52;
    for (int id = 0; id < 150; ++id) {
        h.state.at(team(id)) = 'T';
        h.state.at(team(id) + 0x17) = 50;
    }
    return h;
}
// The row of team `id`; throws if the page does not list it.
const ClassicStrengthsRow& find(const std::array<ClassicStrengthsRow,12>& rows, int id) {
    for (const auto& row : rows) if (row.team == id) return row;
    throw std::runtime_error("missing strength row");
}
// Sort order (the original's exchange sort, including its order of equal keys), column arithmetic and clamps.
void sorting_and_arithmetic() {
    auto h = fixture();
    stats(h, 0, {1,1,1,1,1,1}); stats(h, 1, {1,1,1,1,1,1});
    stats(h, 2, {2,2,2,2,2,2});
    auto rows = classic_strengths_rows(h);
    require(rows[0].team == 2 && rows[1].team == 1 && rows[2].team == 0,
            "exchange sort indirectly reverses equal keys after a later larger key");
    stats(h, 0, {10,50,20,40,30,70}); stats(h, 1, {70,0,20,0,10,0});
    rows = classic_strengths_rows(h);
    require(rows[0].team == 0 && rows[0].values == std::array<int,4>{30,30,50,36},
            "overall divides raw sum220 by3 then2,not rounded column average");
    require(classic_strengths_rows(h,0,1)[0].team == 1, "defence uses even strength bytes");
    require(classic_strengths_rows(h,0,2)[0].team == 0, "attack uses odd strength bytes");
    stats(h, 0, {0,1,0,1,0,4});
    require(find(classic_strengths_rows(h),0).values == std::array<int,4>{0,0,2,1},
            "overall uses raw sums rather than truncated displayed columns");
    stats(h, 0, {255,255,255,255,255,255});
    stats(h, 1, {254,254,254,254,254,254});
    rows = classic_strengths_rows(h);
    require(rows[0].team == 0 && rows[0].values == std::array<int,4>{99,99,99,255},
            "first three values clamp99,but overall and sorting retain full byte range");
}
// Result faces from the last eight results (table row +7 of team 0 at state 0x3BAC: s/S win, u/U draw, n/N loss,
// other bytes no game), the summary face, highlights, league offsets, purity and invalid arguments.
void faces_and_highlights() {
    auto h = fixture();
    const char* history = "sUn-NuS?";
    std::copy_n(history, 8, h.state.begin() + 0x3BAC);
    auto rows = classic_strengths_rows(h);
    require(rows[0].faces == std::array<uint8_t,8>{3,2,1,0,1,2,3,0} && rows[0].summary_face == 2,
            "result faces preserve unknown-result gaps and summarize wins minus losses");
    std::fill_n(h.state.begin() + 0x3BAC, 8, uint8_t('s'));
    require(classic_strengths_rows(h)[0].summary_face == 3, "summary win upper clamp3");
    std::fill_n(h.state.begin() + 0x3BAC, 8, uint8_t('N'));
    require(classic_strengths_rows(h)[0].summary_face == 1, "summary loss lower clamp1");
    std::fill_n(h.state.begin() + 0x3BAC, 8, uint8_t('-'));
    require(classic_strengths_rows(h)[0].summary_face == 2, "no games means neutral summary");
    h.state.at(0x0DCD + 0x1AF + 0x14) = 8;
    h.state.at(team(9) + 0x16) = 1;
    rows = classic_strengths_rows(h);
    require(find(rows,7).color == 24 && find(rows,8).color == 30 && find(rows,9).color == 30,
            "highlight uses active manager club,not team owner or inactive managers");
    h.state.at(0x0DC6) = 2;
    require(find(classic_strengths_rows(h),8).color == 24, "second active manager is highlighted");
    require(find(classic_strengths_rows(h,4),52).color == 24,
            "manager5 is always eligible regardless of human manager count");
    require(classic_strengths_rows(h,1)[0].team == 12 && classic_strengths_rows(h,2)[0].team == 24,
            "league starts use absolute team indices");
    const auto before = h.state;
    (void)classic_strengths_rows(h,4,2);
    require(before == h.state, "page data helper performs no entry cache writes");
    for (const auto args : {std::pair{3,0}, std::pair{5,0}, std::pair{0,-1}, std::pair{0,3}}) {
        bool rejected = false;
        try { (void)classic_strengths_rows(h,args.first,args.second); }
        catch (const FormatError&) { rejected = true; }
        require(rejected,"reject unsupported league or mode instead of silently substituting");
    }
}

// The pages the original shows for START and MONAT1 in the three sort modes (overall, defence, attack), written
// out as values, not as a raster. Each row is team,K,T,F,overall,morale; the original's tie order is kept exactly.
using Vector = std::array<std::array<int,6>,12>;
constexpr std::array<Vector,3> start{{
    {{{3,92,90,55,79,49},{4,93,94,48,78,49},{7,80,80,52,71,48},{1,83,79,50,70,48},
      {0,76,84,51,70,48},{10,82,81,48,70,51},{5,84,80,46,70,51},{6,79,79,49,69,49},
      {8,73,73,48,65,50},{2,70,69,54,64,48},{9,69,69,53,64,50},{11,69,70,51,63,48}}},
    {{{3,94,94,55,81,49},{4,92,94,47,77,49},{1,86,77,55,72,48},{0,78,86,53,72,48},
      {7,80,80,54,71,48},{10,86,80,46,70,51},{5,82,81,48,70,51},{6,78,80,48,68,49},
      {8,72,75,51,66,50},{9,70,69,55,64,50},{11,70,69,53,64,48},{2,69,69,53,63,48}}},
    {{{4,94,94,49,79,49},{3,90,86,55,77,49},{7,81,80,51,70,48},{10,78,83,51,70,51},
      {5,86,80,45,70,51},{6,81,79,50,70,49},{0,75,83,50,69,48},{1,80,82,45,69,48},
      {2,72,69,55,65,48},{8,75,72,45,64,50},{11,69,72,50,63,48},{9,69,69,52,63,50}}}
}};
constexpr std::array<Vector,3> month{{
    {{{10,96,91,47,78,53},{3,89,89,52,76,53},{4,79,90,54,74,47},{0,82,85,50,72,53},
      {7,80,80,52,71,53},{1,81,75,48,68,53},{5,75,80,49,68,51},{8,76,75,51,67,53},
      {6,71,77,51,66,47},{2,72,76,48,65,47},{9,68,69,53,63,47},{11,70,67,46,61,47}}},
    {{{10,99,88,47,78,53},{3,88,94,49,77,53},{0,85,86,48,73,53},{4,77,88,53,72,47},
      {7,80,81,54,71,53},{1,85,75,45,68,53},{5,71,77,51,66,51},{6,67,79,52,66,47},
      {8,75,75,47,65,53},{9,66,69,55,63,47},{2,70,75,45,63,47},{11,69,65,45,59,47}}},
    {{{10,93,95,48,78,53},{4,82,92,55,76,47},{3,90,84,55,76,53},{0,80,84,52,72,53},
      {7,80,80,51,70,53},{5,80,83,48,70,51},{8,77,75,55,69,53},{1,78,76,52,68,53},
      {2,74,77,52,67,47},{6,75,75,50,66,47},{9,71,69,51,63,47},{11,71,69,47,62,47}}}
}};
// Compares the rows of START.HOK and MONAT1.HOK in all three modes with the tables above.
void reference_values(const std::filesystem::path& data) {
    for (int file = 0; file < 2; ++file) {
        auto h = decode_hok(read_file(data / (file == 0 ? "START.HOK" : "MONAT1.HOK")));
        for (int mode = 0; mode < 3; ++mode) {
            const auto rows = classic_strengths_rows(h,0,mode);
            const auto& expected = (file == 0 ? start : month)[size_t(mode)];
            for (size_t r = 0; r < rows.size(); ++r) {
                const auto& row = rows[r];
                require(std::array<int,6>{row.team,row.values[0],row.values[1],row.values[2],
                                         row.values[3],row.morale} == expected[r],
                        "saved-state rows differ from the original's page");
            }
        }
    }
}
// Exact pixels of the drawn page: number columns, result face crops from KB graphic 33, the morale bar and the
// dividers; drawing must not change the state.
void paint_primitives(const std::filesystem::path& data) {
    ClassicCanvas c(data);
    auto h = fixture(); stats(h,0,{0,1,0,1,0,4});
    h.state.at(team(0) + 0x17) = 51;
    h.state.at(0x3BAC) = 's';
    const auto before = h.state;
    draw_classic_strengths(c,h);
    require(h.state == before,"drawing never synchronizes or mutates the input HOK");
    Image digits(320,240,4);
    c.font().draw_native(digits,ClassicFontId::Mini,142,34,
                        "%0            %0            %2            %1",30);
    for (int y = 29; y <= 35; ++y) for (int x = 142; x < 254; ++x)
        require(c.pixels().at(x,y) == digits.at(x,y),
                "native whole numeric string uses digit-width %padding and mini font without shadow");
    const auto& atlas = c.asset(33);
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 6; ++x) {
        // First result and summary are wins: exactly the original25,36 crop.
        const auto pixel = atlas.pixels[size_t(y + 36) * size_t(atlas.width) + size_t(x + 25)];
        require(c.pixels().at(80+x,36+y) == (pixel == 192 ? 4 : pixel),
                "history uses native6x5 win crop and key192");
        require(c.pixels().at(240+x,36+y) == (pixel == 192 ? 4 : pixel),
                "summary uses independent240 coordinate");
    }
    for (int y : {37,38}) {
        for (int x = 250; x <= 255; ++x)
            require(c.pixels().at(x,y) == 12,"morale51 is inclusive line250..255,index12");
        require(c.pixels().at(256,y) == 4,"morale is neither scaled nor extended");
    }
    require(c.pixels().at(43,25) == 0 && c.pixels().at(261,43) == 0,
            "native divider endpoints use actual palette0");
}
} // namespace

int main(int argc, char** argv) {
    try {
        sorting_and_arithmetic(); faces_and_highlights();
        // CMake may supply the original game folder for asset/reference checks.
        if (argc > 1) { reference_values(argv[1]); paint_primitives(argv[1]); }
        std::printf("Native classic strengths raw-fixture tests passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"Classic strengths: %s\n",e.what());
        return 1;
    }
}
