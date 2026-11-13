// SPDX-License-Identifier: GPL-3.0-only
// Taktik page drawing (see classic_tactics.hpp).
#include "ui/classic_tactics.hpp"
#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
// Texts of the mode controls: 0..1 line-up, 2..7 line change mode, 8..10 effort/offence mode.
constexpr std::array<std::string_view, 11> modes{
    "AUTOM.", "MANUELL", "Automatisch", "Spiel nur mit 2 Reihen",
    "Energiest{rkster Block", "Nach Gegnerreihe", "Minuten-Vorgabe (s.u.)",
    "Voll manuell", "AUTOM.", "SEMI", "MANUELL"};
// The six game situations of the effort/offence tables.
constexpr std::array<std::string_view, 6> situations{
    "Grundwert", "hohe F}hrung", "knappe F}hrung", "Unentschieden",
    "knapper R}ckstand", "hoher R}ckstand"};

// Sunken field frame with fill 4; the corner pixels take the colour found at (left+5, top+5) before
// drawing, as the original samples the old inner pixel first.
void plain_frame(ClassicCanvas& c, int left, int top, int right, int bottom) {
    const auto edge = c.pixels().at(left + 5, top + 5);
    const int x = left + 2, y = top + 3;
    c.rect(x, y, right, bottom, 6);
    c.rect(x - 2, y - 2, right - 2, bottom - 2, 4);
    for (const auto p : {std::pair{x - 2, y - 2}, {right, y}, {right - 2, y - 2},
                         {right, bottom}, {x, bottom}, {x - 2, bottom - 2}})
        c.pixels().at(p.first, p.second) = edge;
    c.pixels().at(right - 2, bottom - 2) = 0;
    c.line(x - 1, y - 2, right - 3, y - 2, 1);
    c.line(x - 2, y - 1, x - 2, bottom - 3, 1);
    c.line(right - 2, y - 1, right - 2, bottom - 3, 29);
    c.line(x - 1, bottom - 2, right - 3, bottom - 2, 29);
}

// A two-digit value field with its up/down arrows; disabled values are drawn in colour 3.
void number_field(ClassicCanvas& c, int x, int y, int value, bool enabled) {
    plain_frame(c, x, y, x + 23, y + 14);
    c.blit(4, x + 25, y, 20, 0, 10, 8);
    c.blit(4, x + 25, y + 8, 20, 10, 10, 7);
    std::string text = std::to_string(value);
    if (text.size() < 2) text.insert(0, 2 - text.size(), '%');
    c.text_native(mini, x + 7, y + 9, text, enabled ? 1 : 3);
}
} // namespace

void draw_classic_tactics(ClassicCanvas& c, const HokFile& h, int preset, int manager) {
    if (manager < 0 || manager >= kHokManagers || preset < 0 || preset > 3)
        throw FormatError("classic tactics: invalid manager or preset");
    const size_t m = size_t(manager);
    const size_t block = 0x0DCD + m * 0x1AF;
    const size_t team = 0x17E7 + size_t(h.state.at(block + 0x14)) * 61;
    // Selected mode of each control (save state, one entry per manager): line-up (0x078A, u16 per manager,
    // stored inverted), line change (0x01F9), effort (0x01FF), offence (0x01F3).
    const std::array<int, 4> selected{
        1 - h.state.at(0x078A + 2 * m), h.state.at(0x01F9 + m),
        h.state.at(0x01FF + m), h.state.at(0x01F3 + m)};
    constexpr std::array<int, 4> count{2, 6, 3, 3}, start{0, 2, 8, 8};
    constexpr std::array<int, 4> field_x{71, 13, 152, 248}, field_y{42, 116, 80, 80};
    constexpr std::array<int, 4> extra{0, 57, 4, 4}, width{37, 94, 41, 41};
    constexpr std::array<int, 4> icon_x{13, 41, 152, 248}, icon_y{39, 90, 54, 54};
    for (int i = 0; i < 4; ++i)
        if (selected[size_t(i)] < 0 || selected[size_t(i)] >= count[size_t(i)])
            throw FormatError("classic tactics: invalid native control mode");

    c.background(22);
    c.frame(9, 33, 5, 2);
    c.frame(9, 84, 5, 7);
    c.frame(145, 21, 7, 10);
    c.blit(9, 246, 225, 0, 25, 72, 15);
    for (int i = 0; i < 4; ++i) {
        c.blit(9, icon_x[size_t(i)], icon_y[size_t(i)], i * 57, 0, 57, 25);
        c.text_native(mini, 47, 154 + 16 * i, "REIHE " + std::to_string(i + 1), 7, 0, 88);
    }
    for (int i = 0; i < 4; ++i) {
        const int x = field_x[size_t(i)], y = field_y[size_t(i)];
        plain_frame(c, x, y, x + extra[size_t(i)] + 42, y + 18);
        c.blit(4, x + extra[size_t(i)] + 44, y + 1, 20, 0, 10, 8);
        c.blit(4, x + extra[size_t(i)] + 44, y + 10, 20, 9, 10, 8);
        c.text_native(mini, x + 3, y + 11,
                      modes[size_t(start[size_t(i)] + selected[size_t(i)])], 30, -1,
                      x + 3 + width[size_t(i)]);
    }
    // Minute quotas of lines 1..4 for defence and attack (0x028B, 8 bytes per manager), editable only in the
    // line change mode "Minuten-Vorgabe".
    for (int row = 0; row < 4; ++row)
        for (int kind = 0; kind < 2; ++kind)
            number_field(c, 13 + 76 * kind, 145 + 16 * row,
                         h.state.at(0x028B + m * 8 + size_t(kind * 4 + row)), selected[1] == 4);
    for (int row = 0; row < 6; ++row) {
        // Effort per situation (team +0x27..+0x2B, "Grundwert" from the saved base value at +0x2D, which the
        // original copies over the current value +0x26 when the page is entered) and offence (+0x35..+0x3A);
        // editable only in the SEMI mode of the respective control.
        const int effort = h.state.at(team + (row == 0 ? 0x2D : size_t(0x26 + row)));
        const int offence = h.state.at(team + size_t(0x35 + row));
        number_field(c, 152, 102 + 16 * row, effort, selected[2] == 1);
        number_field(c, 271, 102 + 16 * row, offence, selected[3] == 1);
    }
    for (int p = 0; p < 4; ++p) {
        const int x = 182 + 25 * p;
        plain_frame(c, x, 35, x + 23, 49);
        c.text_native(mini, x + 2, 43, std::to_string(p + 1), p == preset ? 30 : 29, 2, x + 23);
    }
    c.text_native(mini, 17, 142, "Abwehr", 7);
    c.text_native(mini, 95, 142, "Sturm", 7);
    for (int row = 0; row < 6; ++row)
        c.text_native(mini, 186, 111 + 16 * row, situations[size_t(row)], 30, 0, 270);
}
} // namespace em::ui
