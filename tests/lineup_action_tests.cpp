// SPDX-License-Identifier: GPL-3.0-only
// Independent raw-field expectations from EM.EXE59B5:0384/1553, not an
// alternative implementation of the manual-selection algorithm. No assets needed.
#include "game/lineup.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <utility>

using namespace em;
namespace {
int failures = 0;
#define CHECK(value) do { if (!(value)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #value); \
    ++failures; } } while (0)
constexpr size_t kBase = 0x696d, kStride = 0x618, kSize = 52, kMode = 0x078a;
size_t at(size_t slot, int manager = 0) { return kBase + size_t(manager) * kStride + slot * kSize; }

HokFile fixture(int manager = 0) {
    HokFile h;
    h.state.resize(0x8dfd);
    h.squad.resize(29);
    for (size_t slot = 0; slot < 29; ++slot) {
        const size_t r = at(slot, manager);
        h.state[r + 15] = uint8_t(slot + 1);
        h.state[0x4fc6 + slot * 33 + 24] = 80; //native forward
        h.state[r + 0x1a] = 0x80;
        h.state[r + 0x28] = 0xa9; h.state[r + 0x30] = 0x6b;
        h.squad[slot].player = int(slot);
    }
    h.state[kMode + size_t(manager) * 2] = 4;
    return h;
}

void player(HokFile& h, size_t slot, int position, int member = 0, int line = 0, int manager = 0) {
    const size_t r = at(slot, manager);
    h.state[r + 10] = uint8_t(member);
    h.state[r + 21] = uint8_t(line);
    h.state[0x4fc6 + slot * 33 + 24] = uint8_t(position);
    if (!manager) {
        h.squad[slot].role = member; h.squad[slot].line = line;
        h.squad[slot].status = member ? SquadStatus::Team : SquadStatus::None;
    }
}

void only(const HokFile& h, Bytes expected,
          std::initializer_list<std::pair<size_t, uint8_t>> changes) {
    for (auto change : changes) expected.at(change.first) = change.second;
    CHECK(h.state == expected); //includes salary, insurance, appearance and all unrelated managers
}

void raw_assignment_goldens() {
    //1238:0000 classification is /25, capped at2. These chosen-line bytes
    //come directly from1553, independently of automatic-lineup categories.
    for (const auto example : {std::pair{24, 5}, {25, 2}, {49, 2}, {50, 3}}) {
        auto h = fixture(); player(h, 0, example.first);
        const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, example.second, false);
        const uint8_t expected_line = example.first == 24 ? 0x01 : example.first == 50 ? 0x83 : 0x02;
        CHECK(result.message == ManualLineupMessage::None && result.changed && result.automatic_disabled);
        CHECK(result.assigned_cell == (example.first == 24 ? 4 : example.first == 50 ? 0 : 3));
        only(h, before, {{kMode, 0}, {at(0) + 10, 1}, {at(0) + 21, expected_line}});
        CHECK(h.squad[0].role == 1 && h.squad[0].line == expected_line && h.squad[0].status == SquadStatus::Team);
    }
    //Loans, retirement and incoming-offer flags do not forbid manual lineup.
    auto h = fixture(); h.state[at(0) + 12] = 1; h.state[at(0) + 24] = 0x80; h.state[at(0) + 9] = 0xc0;
    CHECK(manual_lineup(h, 0, 0, 4, false).message == ManualLineupMessage::None);
    CHECK(h.state[at(0) + 21] == 0x84);
}

void refusals_keep_auto_side_effect() {
    {
        auto h = fixture(); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 0, false);
        CHECK(result.message == ManualLineupMessage::NoLineSelected && !result.changed && result.automatic_disabled);
        only(h, before, {{kMode, 0}});
    }
    for (const int flags : {1, 2}) {
        auto h = fixture(); h.state[at(0) + 9] = uint8_t(flags); h.state[at(0) + 13] = 0;
        const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 0, true);
        CHECK(result.message == (flags == 1 ? ManualLineupMessage::Suspended : ManualLineupMessage::Injured));
        CHECK(!result.changed && result.automatic_disabled);
        only(h, before, {{kMode, 0}}); //list actions reject even with remaining duration0
    }
    for (const auto example : {std::pair{0, 1}, {80, 5}}) {
        auto h = fixture(); player(h, 0, example.first); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, example.second, false);
        CHECK(result.message == ManualLineupMessage::WrongRole && !result.changed && result.automatic_disabled);
        only(h, before, {{kMode, 0}});
    }
    {
        auto h = fixture(); for (size_t i = 1; i <= 22; ++i) player(h, i, 80, 1);
        //0E6D's team-full message precedes "Keine Reihe" for a new member.
        const auto result = manual_lineup(h, 0, 0, 0, false);
        CHECK(result.message == ManualLineupMessage::TeamFull && result.automatic_disabled && !result.changed);
        CHECK(h.state[at(0) + 10] == 0);
    }
    {
        auto h = fixture(); for (size_t i = 1; i <= 23; ++i) player(h, i, 80, 1);
        //Original CMP22/JNZ is equality, not a silently strengthened >=limit.
        CHECK(manual_lineup(h, 0, 0, 0, true).message == ManualLineupMessage::None);
        CHECK(h.state[at(0) + 10] == 1 && h.state[at(0) + 21] == 0);
    }
}

void toggle_and_number_semantics() {
    {
        auto h = fixture(); const Bytes before = h.state;
        auto result = manual_lineup(h, 0, 0, 0, true);
        CHECK(result.changed && result.message == ManualLineupMessage::None);
        only(h, before, {{kMode, 0}, {at(0) + 10, 1}});
        result = manual_lineup(h, 0, 0, 0, true);
        CHECK(result.changed && !result.automatic_disabled);
        only(h, before, {{kMode, 0}});
        CHECK(h.squad[0].role == 0 && h.squad[0].status == SquadStatus::None);
    }
    for (const bool reserve : {false, true}) {
        auto h = fixture(); player(h, 0, 80, 1, 0x83); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, reserve);
        CHECK(result.changed && result.selected_line == 3 && result.assigned_cell == -1);
        only(h, before, {{kMode, 0}, {at(0) + 10, uint8_t(reserve)}, {at(0) + 21, 0}});
        CHECK(h.squad[0].line == 0 && h.squad[0].role == int(reserve));
    }
    {
        auto h = fixture(); player(h, 0, 80, 1, 0x82); h.state[at(0) + 9] = 1;
        const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, ManualLineupAction::RemoveNumber);
        CHECK(result.changed && result.selected_line == 2 && result.message == ManualLineupMessage::None);
        only(h, before, {{kMode, 0}, {at(0) + 10, 0}, {at(0) + 21, 0}});
    }
    {
        auto h = fixture(); player(h, 0, 80, 1, 0x82);
        h.state[at(0) + 9] = 3; h.state[at(0) + 13] = 1;
        const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, ManualLineupAction::RemoveNumber);
        CHECK(result.message == ManualLineupMessage::Suspended && result.automatic_disabled && !result.changed);
        only(h, before, {{kMode, 0}}); //diagram branch says gesperrt for any low status bits
    }
    {
        auto h = fixture(); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, ManualLineupAction::RemoveNumber);
        CHECK(!result.changed && !result.automatic_disabled && result.message == ManualLineupMessage::None);
        only(h, before, {}); //empty/non-visible plates never disable automatic selection
    }
    {
        auto h = fixture(); player(h, 0, 80, 1, 0x81); h.state[at(0) + 9] = 2;
        const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, ManualLineupAction::RemoveNumber);
        CHECK(!result.changed && !result.automatic_disabled); //injured player has no number plate
        only(h, before, {});
    }
    {
        auto h = fixture(); h.state[at(0) + 9] = 3;
        CHECK(manual_lineup(h, 0, 0, 1, false).message == ManualLineupMessage::None);
        CHECK(h.state[at(0) + 21] == 0x81); //list tests exactly status1/2, not any nonzero low bits
    }
}

void roles_capacity_and_sixth() {
    {
        auto h = fixture(); player(h, 0, 40); player(h, 1, 40, 1, 1); player(h, 2, 40, 1, 1);
        const auto result = manual_lineup(h, 0, 0, 1, false);
        CHECK(result.message == ManualLineupMessage::None && result.assigned_cell == 0);
        CHECK(h.state[at(0) + 21] == 0x81); //full defence falls back to Sturm
    }
    {
        auto h = fixture(); for (size_t i = 1; i <= 3; ++i) player(h, i, 80, 1, 0x81);
        const auto result = manual_lineup(h, 0, 0, 1, false);
        CHECK(result.message == ManualLineupMessage::None && result.assigned_cell == 3);
        CHECK(h.state[at(0) + 21] == 1); //full Sturm falls back to defence
    }
    auto full = [](int line, bool goalkeeper) {
        auto h = fixture();
        for (size_t i = 1; i <= 3; ++i) player(h, i, 80, 1, 0x80 | line);
        for (size_t i = 4; i <= 5; ++i) player(h, i, 40, 1, line);
        if (goalkeeper) player(h, 6, 0, 1, 1);
        return h;
    };
    {
        auto h = full(1, false); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, 1, false);
        CHECK(result.message == ManualLineupMessage::None && result.assigned_cell == 5);
        only(h, before, {{kMode, 0}, {at(0) + 10, 1}, {at(0) + 21, 1}});
        player(h, 6, 0); //goalkeeper tries to come back onto a six-field-player ice
        const auto blocked = manual_lineup(h, 0, 6, 5, false);
        CHECK(blocked.message == ManualLineupMessage::SixFieldPlayers && !blocked.changed);
    }
    for (const auto example : {std::pair{1, true}, {2, false}}) {
        auto h = full(example.first, example.second); const Bytes before = h.state;
        const auto result = manual_lineup(h, 0, 0, example.first, false);
        CHECK(result.message == ManualLineupMessage::NoSpace && result.automatic_disabled && !result.changed);
        only(h, before, {{kMode, 0}});
    }
}

void isolation_and_invalid_inputs() {
    {
        auto h = fixture(); player(h, 0, 0);
        for(size_t i=1;i<=5;i++)player(h,i,i<4?80:40,1,i<4?0x81:1);
        bool six=true; // native upper-row removal retains this transient flag
        const auto refused=manual_lineup(h,0,0,5,ManualLineupAction::Select,&six);
        CHECK(refused.message==ManualLineupMessage::SixFieldPlayers&&!refused.changed);
        six=false; // leaving/reopening reconstructs it from the five-player raw state
        CHECK(manual_lineup(h,0,0,5,ManualLineupAction::Select,&six).changed);
    }
    {
        auto h = fixture(2); const Bytes before = h.state;
        h.squad[0].role = 7; h.squad[0].line = 4;
        const auto result = manual_lineup(h, 2, 0, 2, false);
        CHECK(result.changed && h.squad[0].role == 7 && h.squad[0].line == 4);
        only(h, before, {{kMode + 4, 0}, {at(0, 2) + 10, 1}, {at(0, 2) + 21, 0x82}});
    }
    {
        auto h = fixture(); h.state[at(0) + 0x1a] = 0;
        std::srand(1902); const int next = std::rand(); std::srand(1902);
        const auto result = manual_lineup(h, 0, 0, 1, false);
        CHECK(result.changed && std::rand() == next); //no C runtime RNG draw
        CHECK(h.squad[0].status == SquadStatus::Reserve); //same1A meaning as hok_squad
        CHECK(h.state[at(0) + 0x1a] == 0); //no live-match appearance booking
    }
    for (int which = 0; which < 4; ++which) {
        auto h = fixture();
        if (which == 3) h.state.resize(100);
        const Bytes before = h.state;
        bool threw = false;
        try { (void)manual_lineup(h, which == 0 ? -1 : 0, which == 1 ? 29 : 0, which == 2 ? 6 : 1, false); }
        catch (const FormatError&) { threw = true; }
        CHECK(threw && h.state == before);
    }
}
}

int main() {
    raw_assignment_goldens();
    refusals_keep_auto_side_effect();
    toggle_and_number_semantics();
    roles_capacity_and_sixth();
    isolation_and_invalid_inputs();
    if (failures) return 1;
    std::puts("manual lineup: native raw actions, refusal order, automatic side effects and RNG isolation passed");
    return 0;
}
