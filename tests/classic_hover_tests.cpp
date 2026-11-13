// SPDX-License-Identifier: GPL-3.0-only
// Hover help of the roster pages (ui/classic_hover.cpp): which row and column of the Verträge, Aufstellung and
// Transfer lists lies under the mouse, the help line the original shows for it (texts from EM.EXE), and the
// rule lines and footer area the help drawing touches. The expected column boundaries and texts are the
// original game's, including its quirks.
//
// Usage: classic_hover_tests [DATA] — DATA is a folder with EM.EXE and the graphics; without it only the
// rejection of an empty EXE is checked.
#include "ui/classic_hover.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace em;
using namespace em::ui;
namespace {
// Number of failed CHECKs; the test fails when it is not zero at the end.
int failures = 0;
// Reports a failed condition with file and line and counts it, without stopping the test.
#define CHECK(value) do { if (!(value)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #value); \
    ++failures; } } while (0)
// True when `action` throws a std::exception.
template<class F> bool throws(F action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
// State offsets: squad rows of manager 0 (52 bytes each; +9 status, +10 reserve, +12 loan, +13 suspension
// minutes, +14 trend, +15 player id, +0x10..+0x12 condition/technique/form, +0x13 exhaustion, +21 line) and the
// global record of player id 1 (33 bytes each; name at +0, +0x14 contract years, +0x18 player type).
constexpr size_t kSquad = 0x696d, kPlayer = 0x4fc6;

// A state with 30 filled squad rows whose players are all called "K\x99LN" (an umlaut byte of the game's
// character set), trend 50 and one contract year.
HokFile fixture() {
    HokFile h;
    h.state.resize(0x8dfd);
    for (int slot = 0; slot < 30; ++slot) {
        const size_t r = kSquad + size_t(slot) * 52;
        const size_t p = kPlayer + size_t(slot) * 33;
        h.state[r + 15] = uint8_t(slot + 1);
        h.state[r + 0x0e] = 50;
        h.state[r + 0x10] = 9; h.state[r + 0x11] = 8; h.state[r + 0x12] = 7;
        h.state[p + 0x14] = 1; h.state[p + 0x18] = 25;
        const uint8_t native_name[]{'K', 0x99, 'L', 'N'};
        std::copy(std::begin(native_name), std::end(native_name), h.state.begin() + std::ptrdiff_t(p));
    }
    return h;
}

// Column boundaries, row geometry, help texts for each status and the drawing of rule lines and footer.
void original_goldens(const std::filesystem::path& data) {
    const ClassicRosterHelp help(read_file(data / "EM.EXE"));
    auto h = fixture();
    const Bytes before = h.state;
    // Hover result of manager 0's page `kind` at (x, y); y 29 lies in the first row.
    auto at = [&](ClassicRosterKind kind, int x, int y = 29) {
        return classic_roster_hover(h, 0, kind, x, y, help);
    };
    using K = ClassicRosterKind;
    // The first and last x of each help column as the original accepts them on each of the three pages,
    // quirks included. `field` is the help entry the column selects.
    struct Boundary { K kind; int x; int field; };
    const Boundary boundaries[]{
        {K::Contracts,0,1},{K::Contracts,24,1},{K::Contracts,25,2},{K::Contracts,81,2},
        {K::Contracts,82,6},{K::Contracts,95,6},{K::Contracts,96,8},{K::Contracts,107,8},
        {K::Contracts,108,12},{K::Contracts,120,12},{K::Contracts,121,14},{K::Contracts,160,14},
        {K::Contracts,161,15},{K::Contracts,176,15},{K::Contracts,177,16},{K::Contracts,211,16},
        {K::Contracts,212,17},{K::Contracts,249,17},{K::Contracts,250,18},{K::Contracts,255,18},
        {K::Lineup,0,0},{K::Lineup,17,0},{K::Lineup,18,1},{K::Lineup,35,1},
        {K::Lineup,36,2},{K::Lineup,92,2},{K::Lineup,93,6},{K::Lineup,106,6},
        {K::Lineup,107,7},{K::Lineup,142,7},{K::Lineup,143,12},{K::Lineup,155,12},
        {K::Lineup,156,13},{K::Lineup,182,13},{K::Lineup,183,14},{K::Lineup,222,14},
        {K::Lineup,223,15},{K::Lineup,233,15},
        {K::Transfer,0,1},{K::Transfer,24,1},{K::Transfer,25,2},{K::Transfer,81,2},
        {K::Transfer,82,6},{K::Transfer,95,6},{K::Transfer,96,7},{K::Transfer,131,7},
        {K::Transfer,132,12},{K::Transfer,144,12},{K::Transfer,145,15},{K::Transfer,155,15}
    };
    for (auto b : boundaries) {
        const auto hover = at(b.kind, b.x);
        CHECK(hover.row == 0 && hover.field == b.field);
    }
    CHECK(at(K::Contracts,-1).row == -1);
    CHECK(at(K::Contracts,256).row == -1);
    CHECK(at(K::Lineup,234).row == -1);
    CHECK(at(K::Transfer,156).row == -1);
    CHECK(at(K::Contracts,82,23).row == -1);
    CHECK(at(K::Contracts,82,24).row == 0);
    CHECK(at(K::Contracts,82,34).row == 0);
    CHECK(at(K::Contracts,82,35).row == 1);
    CHECK(at(K::Contracts,82,202).row == 28);
    CHECK(at(K::Contracts,82,203).row == -1); //slot29 exists, but is excluded by count scan

    CHECK(at(K::Contracts,250).native_help == "Torwart");
    CHECK(at(K::Contracts,82).native_help == "Spiele gesamt (Liga und Pokal)");
    CHECK(at(K::Contracts,96).native_help == "Durchschnittsst{rke");
    CHECK(at(K::Contracts,212).native_help == "Monatliches Gehalt des Spielers");
    CHECK(at(K::Lineup,107).native_help == "Kondition, Technik, Form (8)");
    const std::string native_name = std::string("K") + static_cast<char>(static_cast<unsigned char>(0x99)) + "LN";
    CHECK(at(K::Contracts,25).native_help == "Name: " + native_name + " (1 Jahre)");
    CHECK(at(K::Contracts,0).native_help == "Art des Spielers: Abwehrspieler");
    h.state[kSquad + 12] = 1;
    CHECK(at(K::Contracts,0).native_help == "Art des Spielers: Abwehrspieler (ausgeliehen)");
    h.state[kSquad + 12] = 0;
    h.state[kSquad + 21] = 0x80;
    CHECK(at(K::Lineup,7).native_help == "R}ckennummer des Spielers (Reihe: 0)");
    h.state[kSquad + 21] = 0;
    CHECK(at(K::Lineup,7).native_help == "R}ckennummer des Spielers");
    CHECK(at(K::Contracts,121).native_help == "Status: H{ngt halt so rum...");
    h.state[kSquad + 10] = 1; h.state[kSquad + 9] = 2;
    CHECK(at(K::Contracts,121).native_help == "Status: RESERVE"); //reserve precedes injury
    h.state[kSquad + 21] = 1; h.state[kSquad + 9] = 0;
    CHECK(at(K::Contracts,121).native_help == "Status: IM TEAM");
    h.state[kSquad + 9] = 1; h.state[kSquad + 13] = 5;
    CHECK(at(K::Contracts,121).native_help == "Status: Noch 5 Minuten Gesperrt.");
    h.state[kSquad + 9] = 2; h.state[kSquad + 0x17] = 0;
    CHECK(at(K::Contracts,121).native_help == "Status: Verletzt.(SCH{DELBRUCH)");
    h.state[kSquad + 9] = 3;
    CHECK(at(K::Contracts,121).native_help == "Status: Weltmeisterschaft");
    h.state[kSquad + 0x13] = 150;
    CHECK(at(K::Contracts,161).native_help == "Tendenz des Spielers: Konstant.");
    CHECK(at(K::Transfer,145).native_help == "Tendenz des Spielers: Konstant.");
    CHECK(at(K::Lineup,223).native_help == "Tendenz des Spielers: Konstant. (Ersch|pfung:100)");
    h.state[kSquad + 0x0e] = 17;
    CHECK(at(K::Contracts,161).native_help == "Tendenz des Spielers: Gesperrt."); //native negative index, no invented clamp

    h.state = before;
    h.state[kSquad + 52 + 15] = 0; //gap counts toward the slot geometry, not occupied count
    CHECK(at(K::Contracts,82,35).row == 1);
    CHECK(at(K::Contracts,82,196).row == 27);
    CHECK(at(K::Contracts,82,197).row == -1);
    h.state = before;
    for (int slot = 0; slot < 13; ++slot) h.state[kSquad + 4 * 0x618 + size_t(slot) * 52 + 15] = 1;
    CHECK(classic_roster_hover(h,4,K::Transfer,82,100,help).row == 11);
    CHECK(classic_roster_hover(h,4,K::Transfer,82,101,help).row == -1);
    CHECK(throws([&]{classic_roster_hover(h,6,K::Contracts,25,29,help);}));
    CHECK(throws([&]{classic_roster_hover(h,0,K(99),25,29,help);}));
    const Bytes unchanged = h.state;
    (void)at(K::Contracts,25);
    CHECK(h.state == unchanged);
    h.state.resize(3);
    CHECK(throws([&]{at(K::Contracts,25);}));

    // Rule endpoints and footer clear bounds are the original's exact coordinates.
    // An empty help string isolates those pixels from text glyph placement.
    ClassicCanvas canvas(data);
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),uint8_t(55));
    draw_classic_roster_hover(canvas,K::Contracts,{1,2,""});
    CHECK(canvas.pixels().at(7,34) == 16 && canvas.pixels().at(258,34) == 16);
    CHECK(canvas.pixels().at(258,40) == 16 && canvas.pixels().at(259,40) == 55);
    CHECK(canvas.pixels().at(7,210) == 4 && canvas.pixels().at(255,218) == 4);
    CHECK(canvas.pixels().at(256,218) == 55 && canvas.pixels().at(7,219) == 55);
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),uint8_t(55));
    draw_classic_roster_hover(canvas,K::Lineup,{1,2,""},false);
    CHECK(canvas.pixels().at(7,34) == 55 && canvas.pixels().at(236,40) == 55);
    CHECK(canvas.pixels().at(233,218) == 4 && canvas.pixels().at(234,218) == 55);
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),uint8_t(55));
    draw_classic_roster_hover(canvas,K::Transfer,{});
    CHECK(canvas.pixels().at(155,218) == 4 && canvas.pixels().at(156,218) == 55);
}
}

int main(int argc, char** argv) {
    try {
        CHECK(throws([]{ClassicRosterHelp help(Bytes{});}));
        if (argc > 1) original_goldens(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr,"Classic hover: unexpected exception: %s\n",e.what());
        return 2;
    }
    return failures ? 1 : 0;
}
