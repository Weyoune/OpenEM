// SPDX-License-Identifier: GPL-3.0-only
// Stärken page (see classic_strengths.hpp).
#include "ui/classic_strengths.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
constexpr std::array<std::string_view, 3> modes{"Gesamt", "Abwehr", "Sturm"};  // mode button labels
constexpr std::array<std::string_view, 5> leagues{
    "Bundesliga", "Zweite Liga", "Oberliga", "", "L{nderteams"};

// First team index of league (national teams start at 52); validates league and mode.
int first_team(int league, int mode) {
    if ((league != 0 && league != 1 && league != 2 && league != 4) || mode < 0 || mode > 2)
        throw FormatError("classic strengths: invalid league or mode");
    return league == 4 ? 52 : league * 12;
}

// Two-digit number.
std::string number(int value) {
    auto text = std::to_string(value);
    // The game pads with a blank digit-width glyph, not an ordinary space.
    if (text.size() < 2) text.insert(0, 2 - text.size(), '%');
    return text;
}

// A mode button at the bottom; its four corner pixels keep the page underneath.
void button(ClassicCanvas& c, int left, std::string_view label) {
    constexpr int y = 212;
    const std::array<uint8_t, 4> corners{
        c.pixels().at(left, y), c.pixels().at(left + 56, y),
        c.pixels().at(left, y + 11), c.pixels().at(left + 56, y + 11)};
    c.blit(4, left, y, 17, 179, 57, 12, true, 192);
    c.pixels().at(left, y) = corners[0];
    c.pixels().at(left + 56, y) = corners[1];
    c.pixels().at(left, y + 11) = corners[2];
    c.pixels().at(left + 56, y + 11) = corners[3];
    c.text_native(mini, left, 219, label, 29, 1, left + 59);
}

// A small result face from KB33 (1 loss, 2 draw, 3 win; 0 draws nothing).
void face(ClassicCanvas& c, int index, int x, int y) {
    if (index == 0) return;
    c.blit(33, x, y, index == 1 ? 19 : 25, index == 2 ? 41 : 36, 6, 5, true, 192);
}
} // namespace

std::array<ClassicStrengthsRow, 12> classic_strengths_rows(const HokFile& h, int league, int mode) {
    const int first = first_team(league, mode);
    const auto& s = h.state;
    const int begin = mode == 2 ? 1 : 0, end = mode == 1 ? 1 : 2;
    const int divisor = end - begin;
    std::array<ClassicStrengthsRow, 12> rows{};
    std::array<int, 12> scores{};
    for (size_t i = 0; i < rows.size(); ++i) {
        auto& row = rows[i];
        row.team = first + int(i);
        const size_t team = 0x17E7 + size_t(row.team) * 61;
        int total = 0;
        for (int stat = 0; stat < 3; ++stat) {
            int sum = 0;
            for (int side = begin; side < end; ++side)
                sum += s.at(team + 0x18 + size_t(2 * stat + side));
            row.values[size_t(stat)] = std::clamp(sum / divisor, 0, 99);
            total += sum;
        }
        row.values[3] = total / 3 / divisor;
        scores[i] = total;
        row.morale = s.at(team + 0x17);
        const int human_count = s.at(0x0DC6);
        for (size_t manager = 0; manager < 6; ++manager)
            if ((int(manager) < human_count || manager == 5) &&
                s.at(0x0DCD + manager * 0x1AF + 0x14) == row.team)
                row.color = 24;
        int summary = 2;
        // form letters of the last eight games in the league table row (+7): s/S win, n/N loss, u/U draw
        for (size_t game = 0; game < row.faces.size(); ++game) {
            switch (s.at(0x3BAC + size_t(row.team) * 80 + game)) {
            case 's': case 'S': row.faces[game] = 3; ++summary; break;
            case 'n': case 'N': row.faces[game] = 1; --summary; break;
            case 'u': case 'U': row.faces[game] = 2; break;
            default: break;
            }
        }
        row.summary_face = uint8_t(std::clamp(summary, 1, 3));
    }
    // The original's immediate exchange sort: later higher scores can reverse equal keys.
    for (size_t i = 0; i + 1 < rows.size(); ++i)
        for (size_t j = i + 1; j < rows.size(); ++j)
            if (scores[i] < scores[j]) {
                std::swap(scores[i], scores[j]);
                std::swap(rows[i], rows[j]);
            }
    return rows;
}

void draw_classic_strengths(ClassicCanvas& c, const HokFile& h, int league, int mode) {
    const auto rows = classic_strengths_rows(h, league, mode);
    c.background(22);
    c.frame(30, 5, 10, 12);
    for (size_t i = 0; i < modes.size(); ++i) button(c, 43 + 80 * int(i), modes[i]);
    // The original's redraw erases the initial column-heading text as well.
    c.rect(36, 9, 262, 211, 4);
    const std::string title = "ST[RKEN " + std::string(leagues[size_t(league)]) + " - " +
                              std::string(modes[size_t(mode)]);
    c.text_native(ClassicFontId::Flow, 61, 19, title, 10, 28, 248);
    c.line(43, 25, 261, 25, 0);
    for (size_t rank = 0; rank < rows.size(); ++rank) {
        const auto& row = rows[rank];
        const int y = int(rank) * 15;
        const size_t team = 0x17E7 + size_t(row.team) * 61;
        std::string name;
        for (size_t i = 0; i < 22 && h.state.at(team + i) != 0; ++i)
            name += char(h.state.at(team + i));
        c.text_native(mini, 45, 34 + y, number(int(rank) + 1) + ". " + name, row.color);
        std::string values;
        for (size_t i = 0; i < row.values.size(); ++i) {
            if (i != 0) values.append(12, ' ');
            values += number(row.values[i]);
        }
        c.text_native(mini, 142, 34 + y, values, row.color);
        c.line(43, 43 + y, 261, 43 + y, 0);
        for (size_t game = 0; game < row.faces.size(); ++game)
            face(c, row.faces[game], 80 + 8 * int(game), 36 + y);
        face(c, row.summary_face, 240, 36 + y);
        const uint8_t color = row.morale > 50 ? 12 : row.morale == 50 ? 24 : 18;
        c.line(250, 37 + y, 204 + row.morale, 37 + y, color);
        c.line(250, 38 + y, 204 + row.morale, 38 + y, color);
    }
}
} // namespace em::ui
