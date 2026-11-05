// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_opponent.hpp"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void bytes(HokFile& h, size_t at, std::initializer_list<uint8_t> values) {
    for (const auto value : values) h.state.at(at++) = value;
}
void owner(HokFile& h, int team, int manager) {
    h.state.at(0x17E7 + size_t(team) * 61 + 0x16) = uint8_t(manager + 1);
}
HokFile fixture() {
    HokFile h;
    h.state.assign(0xBB00, 0);
    h.state.at(0xB953) = 37;
    h.state.at(0x8E2D) = 7; // deliberately different displayed date counter
    for (size_t manager = 0; manager < 6; ++manager)
        h.state.at(0x0DCD + manager * 0x1AF + 0x14) = 4;
    std::fill_n(h.state.begin() + 0x8E51, 128, uint8_t(0xFF));
    h.state.at(0x03B6) = 0xFF; h.state.at(0x03B7) = 0xFF;
    return h;
}
SeasonCalendar calendar(uint8_t event) {
    SeasonCalendar c;
    c.events.assign(300, 0);
    c.events[37] = event;
    return c;
}
void match(const ClassicOpponent& result, int team, int side, const char* message) {
    require(result.has_match() && result.team == team && result.side == side, message);
}
void free(const ClassicOpponent& result, const char* message) {
    require(!result.has_match() && result.team == 0xFF && result.side == 0xFF, message);
}

void league_cursor_and_ff() {
    auto h = fixture(); auto c = calendar(7);
    bytes(h, 0x8DFD, {0, 1, 4, 9, 2, 3, 5, 6, 7, 8, 10, 11});
    const Bytes before = h.state;
    auto result = classic_opponent(h, c);
    match(result, 9, 1, "league participant2 is home and faces adjacent participant3");
    require(result.calendarIndex == 37 && result.calendarEvent == 7 && result.selectedEvent == 7,
            "B953 event cursor is authoritative,not displayed day8E2D");
    require(h.state == before, "opponent lookup is read-only");
    bytes(h, 0x8DFD, {0, 1, 9, 4});
    match(classic_opponent(h, c), 9, 0, "odd league participant is away");
    const size_t manager1 = 0x0DCD + 0x1AF;
    h.state.at(manager1 + 0x14) = 16; h.state.at(manager1 + 0xEB) = 1;
    bytes(h, 0x8E09, {12, 13, 14, 15, 19, 16, 17, 18, 20, 21, 22, 23});
    match(classic_opponent(h, c, 1), 19, 0, "second league raw IDs remain absolute team indices");
    c.events[37] = 1;
    free(classic_opponent(h, c, 1), "event bit for another league cannot select a fixture");
    c.events[37] = 7;
    result = classic_opponent(h, c, 0, true);
    free(result, "native FF menu argument suppresses match selection");
    require(result.calendarEvent == 7 && result.selectedEvent == 0 && !result.seasonEnd,
            "suppressed selection retains independently drawn header event");
    c.events[37] = 0xFF;
    result = classic_opponent(h, c);
    free(result, "section-end calendarFF must bypass league bitmask despite existing pairing");
    require(result.seasonEnd && result.calendarEvent == 0xFF && result.selectedEvent == 0xFF,
            "calendarFF means Saisonende independently of menu argumentFF");
    c.events[37] = 0; c.events[38] = 7;
    free(classic_opponent(h, c), "no search ahead to next playable calendar day");
    h.state.at(0xB953) = 0xFF; c.events[255] = 7;
    result = classic_opponent(h, c);
    require(result.calendarEvent == 7 && result.selectedEvent == 0 && !result.seasonEnd,
            "byte cursorFF follows native suppressed-argument path");
    free(result, "argumentFF is distinct from a calendar eventFF");
}

void world_groups_and_knockout() {
    auto h = fixture(); auto c = calendar(0x10);
    h.state.at(0x8F85) = 3;
    // ASM192B..1940: group1 offset24h plus round3 offset12h gives03EE.
    bytes(h, 0x03EE, {52, 53, 54, 55, 56, 57});
    owner(h, 55, 5);
    match(classic_opponent(h, c, 5), 54, 0, "WM group1 round3 uses owned national team,not club field4");
    free(classic_opponent(h, c, 0), "national owner6 does not match active club manager1");
    h.state.at(0x8F85) = 5; h.state.at(0x03B7) = 4;
    bytes(h, 0x8EB1, {55, 52, 53, 54, 56, 57, 58, 59, 0xFF});
    owner(h, 58, 5);
    match(classic_opponent(h, c, 5), 59, 1, "WM knockout starts at native cursor and skips earlier owned participant");
    h.state.at(0x03B7) = 0xFF;
    free(classic_opponent(h, c, 5), "WM absent knockout cursorFF has no fixture");
    h.state.at(0x03B7) = 0; h.state.at(0x8EB1) = 0xFF;
    free(classic_opponent(h, c, 5), "WM listFF terminates before later stale pairings");
}

void european_groups_and_final() {
    auto h = fixture(); auto c = calendar(0x20);
    owner(h, 72, 2);
    bytes(h, 0x03A4, {64, 65, 66, 67, 70, 71, 72, 73});
    // ASM2BE9:kind2 CED8+90h,group1+24h,firstleg round2+0Ch ->0478.
    bytes(h, 0x0478, {70, 71, 72, 73});
    h.state.at(0x8F86) = 2;
    match(classic_opponent(h, c, 2), 73, 1, "Euro group1 first-leg round2,second pairing");
    h.state.at(0x8F86) = 5;
    match(classic_opponent(h, c, 2), 73, 0, "Euro return leg reuses first-leg schedule with flipped venue");
    owner(h, 75, 0);
    bytes(h, 0x03AC, {74, 75, 76, 77});
    bytes(h, 0x049C, {74, 75, 76, 77}); // final kind3 selected localround2
    free(classic_opponent(h, c, 0), "final-stage entrants are ignored before global Euro round6");
    h.state.at(0x8F86) = 8;
    match(classic_opponent(h, c, 0), 74, 0, "Euro final uses raw0490 schedule after subtracting six rounds");
    h.state.at(0x8F86) = 11;
    match(classic_opponent(h, c, 0), 74, 1, "Euro final second-leg venue flips");
    c.events[37] = 0x30;
    free(classic_opponent(h, c, 0), "18F8 special-event comparisons are exact,not independent flag tests");
}

void relegation_schedule_and_days() {
    auto h = fixture(); auto c = calendar(0x40);
    h.state.at(0x03B6) = 0;
    h.state.at(0x8F87) = 1; h.state.at(0x0259) = 2;
    bytes(h, 0x03A0, {10, 11, 12, 13});
    bytes(h, 0x0424, {10, 11, 12, 13});
    owner(h, 13, 1);
    match(classic_opponent(h, c, 1), 12, 1,
          "relegation stage1 game2 selects round3,return leg first round,group1");
    h.state.at(0x0259) = 1;
    free(classic_opponent(h, c, 1), "relegation menu mode0 excludes stage game1");
    h.state.at(0x0259) = 4;
    free(classic_opponent(h, c, 1), "relegation menu mode0 excludes stage game4");
    h.state.at(0x0259) = 2; h.state.at(0x03B6) = 0xFF;
    free(classic_opponent(h, c, 1), "relegation helper rejects native playoff cursorFF");
    h.state.at(0x03B6) = 0; h.state.at(0x8F87) = 0; h.state.at(0x0259) = 0;
    bytes(h, 0x039C, {4, 5, 6, 7}); bytes(h, 0x0400, {4, 5, 6, 7});
    owner(h, 6, 0);
    match(classic_opponent(h, c, 0), 7, 1, "relegation first stage first day uses first-leg venue");
}

void playoff_pending_scores_and_cursor() {
    auto h = fixture(); auto c = calendar(0x40);
    // Previous quarterfinals at0..7,live semifinals at8..11,and FF at12.
    bytes(h, 0x8E91, {10, 11, 12, 13, 14, 15, 16, 17, 10, 12, 14, 16, 0xFF});
    h.state.at(0x03B6) = 8; owner(h, 14, 0);
    match(classic_opponent(h, c), 16, 1, "playoff cursor skips old quarterfinal for same owned team");
    h.state.at(0x0259) = 1;
    free(classic_opponent(h, c), "playoff game>0 requires pending byte");
    h.state.at(0x077D) = 1;
    match(classic_opponent(h, c), 16, 1, "pending flag enables current playoff fixture");
    h.state.at(0x0239) = 100; //0205+42+10,CCDC+i in1B3B
    free(classic_opponent(h, c), "first100 score column excludes finished tie");
    h.state.at(0x0239) = 0; h.state.at(0x0247) = 100; //0205+56+10
    free(classic_opponent(h, c), "second100 score column independently excludes finished tie");
    h.state.at(0x0247) = 0; h.state.at(0x0248) = 100;
    match(classic_opponent(h, c), 16, 1, "sentinel at adjacent participant is not the owned participant's guard");
    bytes(h, 0x8E99, {10, 12, 16, 14, 0xFF});
    h.state.at(0x0248) = 0;
    match(classic_opponent(h, c), 16, 0, "serialized next-venue order determines away side");
    h.state.at(0x03B6) = 12;
    free(classic_opponent(h, c), "FF at current cursor prevents fallback to an older tie");
}

void cup_pending_count_and_owner() {
    auto h = fixture(); auto c = calendar(0x08);
    h.state.at(0x8ED1) = 4; h.state.at(0x077C) = 1;
    bytes(h, 0x8E51, {20, 21, 22, 23, 24, 25});
    owner(h, 21, 3); owner(h, 24, 3);
    match(classic_opponent(h, c, 3), 20, 0, "cup round4 scans exactly two ties,matching manager owner4");
    h.state.at(0x077C) = 0;
    free(classic_opponent(h, c, 3), "cup pending0 excludes decoded-but-finished draw");
    h.state.at(0x077C) = 1; h.state.at(0x035D) = 100;
    free(classic_opponent(h, c, 3), "cup100 sentinel excludes owned participant1");
    h.state.at(0x035D) = 0;
    free(classic_opponent(h, c, 0), "cup matches teamRecord16 ownership,not manager's club field");
    h.state.at(0x8ED1) = 6;
    free(classic_opponent(h, c, 3), "cup source03B1 table has zero ties after final");
}
} // namespace

int main() {
    try {
        league_cursor_and_ff(); world_groups_and_knockout(); european_groups_and_final();
        relegation_schedule_and_days(); playoff_pending_scores_and_cursor(); cup_pending_count_and_owner();
        std::printf("Native classic opponent raw-fixture tests passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Classic opponent: %s\n", e.what());
        return 1;
    }
}
