// SPDX-License-Identifier: GPL-3.0-only
// The classic line-up page ("Mannschaft" -> "Aufst."): the squad list on the
// left and, on the right, the diagram of the four outfield blocks and the
// goalkeeper with the squad numbers placed in them, plus the count of
// players in the team.
#include "ui/classic_lineup.hpp"
#include "ui/classic_roster.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace em::ui {
namespace {
// Save state: squads at 0x696D (0x618 bytes per manager, 52-byte slot
// records), global player records of 33 bytes at 0x4FC6.
constexpr size_t kSquads = 0x696d, kSquadStride = 0x618, kRecord = 52;
constexpr size_t kPlayers = 0x4fc6, kPlayerRecord = 33;
constexpr auto kMini = ClassicFontId::Mini;
// The diagram as the game builds it when the page is opened.
struct LineupDiagram {
    ClassicLineupCells cells{};
    int first_block = 0;  // players assigned to block 1
    int selected = 0;     // players in the team (including reserves)
};

// Number padded to two places with the '%' digit-wide blank.
std::string two_digits(int value) {
    return (value < 10 ? "%" : "") + std::to_string(value);
}

// Bounds-checked pointer into the save state.
const uint8_t* checked_record(const HokFile& h, size_t offset, size_t length) {
    if (offset > h.state.size() || length > h.state.size() - offset)
        throw FormatError("classic lineup: truncated native record");
    return h.state.data() + offset;
}
// Builds the diagram from the squad records. Slot fields used: +0x09 bits
// 0..1 status (1 suspended, 2 injured), +0x0A in the team, +0x0F player id
// (1-based, 0 = empty), +0x15 block 1..5 in bits 0..6 and the forward half
// in bit 7. A player's role comes from global record +0x18 / 25 (0 =
// goalkeeper).
LineupDiagram lineup_diagram(const HokFile& h, int manager) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("classic lineup: invalid manager");
    const size_t base = kSquads + size_t(manager) * kSquadStride;
    (void)checked_record(h, base, kSquadStride);

    // The game counts the occupied records among the first 29 slots (12 for
    // the transfer pool) and then visits slots 0..count-1, so a gap in the
    // squad hides the last players from the diagram.
    const int available = manager == 4 ? 12 : 29;
    int count = 0;
    LineupDiagram result;
    for (int slot = 0; slot < 30; ++slot) {
        const uint8_t* r = h.state.data() + base + size_t(slot) * kRecord;
        if (slot < available) {
            if (r[0x0f]) ++count;
            if (r[0x0a]) ++result.selected;
        }
        if (!r[0x0f]) continue;
        if (r[0x0f] > kHokPlayers)
            throw FormatError("classic lineup: invalid player id");
        (void)checked_record(h, kPlayers + size_t(r[0x0f] - 1) * kPlayerRecord, kPlayerRecord);
    }

    auto& cells = result.cells;
    // The half of the block (cells 0..2 or 3..5) comes from bit 7 of +0x15,
    // independently of the player's natural position. Suspended assigned
    // players remain in the diagram; injured ones do not.
    for (int slot = 0; slot < count; ++slot) {
        const uint8_t* r = h.state.data() + base + size_t(slot) * kRecord;
        const int status = r[0x09] & 3;
        const int line = r[0x15] & 0x7f;
        if (!r[0x0f] || (!r[0x0a] && status != 1) || status == 2 || !line) continue;
        if (line == 1) ++result.first_block;
        const uint8_t* p = h.state.data() + kPlayers + size_t(r[0x0f] - 1) * kPlayerRecord;
        const int role = std::min(int(p[0x18]) / 25, 2);
        if (!role) {
            if (!cells[4][4]) cells[4][4] = slot + 1;
            continue;
        }
        if (line > 5) throw FormatError("classic lineup: invalid native line");
        const int half = (r[0x15] & 0x80) ? 0 : 3;
        for (int cell = half; cell < half + 3; ++cell) {
            int& occupant = cells[size_t(line - 1)][size_t(cell)];
            if (!occupant) { occupant = slot + 1; break; }
        }
    }
    return result;
}
}

ClassicLineupCells classic_lineup_cells(const HokFile& h, int manager) {
    return lineup_diagram(h, manager).cells;
}

void draw_classic_lineup(ClassicCanvas& canvas, const HokFile& h,
                         int manager, int selectedLine, const ClassicLineupCells* override,const bool* six_override) {
    if (selectedLine < 0 || selectedLine > 5)
        throw FormatError("classic lineup: invalid selected line");
    const auto diagram = lineup_diagram(h, manager);
    const auto& cells = override ? *override : diagram.cells;
    // Use the current stored membership for the six-player mode, not merely
    // cell 5: the original can keep a hidden stale cell 5 after going from
    // six to five players.
    const bool six_field_players = six_override?*six_override:diagram.first_block == 6 && !cells[4][4];

    canvas.background(22);
    draw_classic_roster(canvas, h, ClassicRosterView::Lineup, manager);
    constexpr std::array<const char*, 5> labels{"1.", "2.", "3.", "4.", "TW"};
    for (int line = 0; line < 5; ++line) {
        const int top = 15 + 42 * line;
        canvas.frame(246, top, 3, 2);
        canvas.text(kMini, 251, top + 21, labels[size_t(line)],
                    selectedLine == line + 1 ? 16 : 2);
        const auto plate = [&](int x, int y, int occupant) {
            canvas.blit(4, x, y, 0, 179, 17, 11, true, 192);
            if (occupant) canvas.text(kMini, x + 4, y + 7, two_digits(occupant), 7);
        };
        if (line == 4) {
            plate(276, top + 20, cells[4][4]);
        } else {
            for (int cell = 0; cell < 3; ++cell)
                plate(258 + 18 * cell, top + 7, cells[size_t(line)][size_t(cell)]);
            if (line == 0 && six_field_players) {
                for (int cell = 0; cell < 3; ++cell)
                    plate(258 + 18 * cell, top + 20, cells[0][size_t(cell + 3)]);
            } else {
                for (int cell = 0; cell < 2; ++cell)
                    plate(267 + 18 * cell, top + 20, cells[size_t(line)][size_t(cell + 3)]);
            }
        }
    }
    // "Eingesetzt" counts all team members, including reserves, not just the
    // players whose numbers appear in the diagram. No text shadow here.
    canvas.rect(253, 187, 311, 193, 4);
    canvas.text(kMini, 253, 193, "Eingesetzt: " + two_digits(diagram.selected), 16);
    canvas.blit(9, 246, 225, 72, 25, 72, 15, true, 192);
}

} // namespace em::ui
