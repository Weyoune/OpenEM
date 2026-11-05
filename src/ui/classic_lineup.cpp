// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_lineup.hpp"
#include "ui/classic_roster.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace em::ui {
namespace {
constexpr size_t kSquads = 0x696d, kSquadStride = 0x618, kRecord = 52;
constexpr size_t kPlayers = 0x4fc6, kPlayerRecord = 33;
constexpr auto kMini = ClassicFontId::Mini;
struct LineupDiagram {
    ClassicLineupCells cells{};
    int first_block = 0;
    int selected = 0;
};

std::string two_digits(int value) {
    return (value < 10 ? "%" : "") + std::to_string(value);
}

const uint8_t* checked_record(const HokFile& h, size_t offset, size_t length) {
    if (offset > h.state.size() || length > h.state.size() - offset)
        throw FormatError("classic lineup: truncated native record");
    return h.state.data() + offset;
}
LineupDiagram lineup_diagram(const HokFile& h, int manager) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("classic lineup: invalid manager");
    const size_t base = kSquads + size_t(manager) * kSquadStride;
    (void)checked_record(h, base, kSquadStride);

    // 645B:0D27 counts the first29 roster records, or12 for the pool manager.
    // The native diagram visits slots0..count-1, retaining any empty gaps.
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
    // 59B5:03FB..0503: chosen half comes from record15 bit80, independently
    // of natural ART. Suspended assigned players remain; injured ones do not.
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
    // Use the current raw membership for the six-player mode, not merely
    // cell5:the original can retain a hidden stale cell5 after6->5 removal.
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
    // 59B5:1D40 counts all team members, including reserves, not just the
    // players whose numbers appear in the diagram. No text shadow here.
    canvas.rect(253, 187, 311, 193, 4);
    canvas.text(kMini, 253, 193, "Eingesetzt: " + two_digits(diagram.selected), 16);
    canvas.blit(9, 246, 225, 72, 25, 72, 15, true, 192);
}

} // namespace em::ui
