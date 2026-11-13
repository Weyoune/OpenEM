// SPDX-License-Identifier: GPL-3.0-only
// Jugendteams page (see classic_youth.hpp).
#include "ui/classic_youth.hpp"
#include "game/youth.hpp"
#include <bit>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
constexpr std::array<std::string_view, 3> labels{"Sch}ler", "Jugend", "Junioren"};
// Light, fill and dark colour of each youth team's bar and graph.
constexpr std::array<std::array<uint8_t, 3>, 3> colors{{{18,17,16},{15,13,12},{23,22,8}}};
// The low 16 bits as a signed value, as the original's 16-bit coordinates wrap.
int word(uint32_t bits) { const auto v = uint16_t(bits); return v < 32768 ? int(v) : int(v) - 65536; }

// Number, optionally grouped with separator; non-negative values padded to width with the blank digit '%'.
std::string number(int32_t value, int width, char separator, bool grouped) {
    const int64_t magnitude = value < 0 ? -int64_t(value) : value;
    auto text = std::to_string(magnitude);
    if (grouped)
        for (size_t at = text.size(); at > 3; at -= 3) text.insert(at - 3, 1, separator);
    if (value < 0) return "-" + text;
    if (int(text.size()) < width) text.insert(0, size_t(width - int(text.size())), '%');
    return text;
}

// A bevelled bar in the team's colours. The four edge strokes are asymmetric as in the original; their
// corners are not an outline().
void bar(ClassicCanvas& c, int left, int top, int right, int bottom, size_t kind) {
    c.line(left, uint8_t(top), word(uint32_t(right) - 1u), uint8_t(top), colors[kind][0]);
    c.line(left, uint8_t(top), left, uint8_t(bottom), colors[kind][0]);
    c.line(right, uint8_t(top), right, uint8_t(bottom), colors[kind][2]);
    c.line(left + 1, uint8_t(bottom), right, uint8_t(bottom), colors[kind][2]);
    c.rect(left + 1, top + 1, right - 1, bottom - 1, colors[kind][1]);
}
} // namespace

ClassicYouthView classic_youth_view(const HokFile& h, const FinanceTables& finance, int manager) {
    if (manager < 0 || manager >= 4) throw FormatError("classic youth: invalid human manager");
    if (finance.money_num <= 0 || finance.money_den <= 0)
        throw FormatError("classic youth: invalid currency");
    const size_t at = 0x06BB + size_t(manager) * 0x15;
    ClassicYouthView out;
    const char separator = finance.money_num == 4 ? ',' : '.';
    for (size_t i = 0; i < out.teams.size(); ++i) {
        auto& team = out.teams[i];
        team.rank = h.state.at(at + i);
        team.league = h.state.at(at + 3 + i);
        team.raw_funding = youth_funding_amount(h, int(i), manager);
        // As in the original: divide the raw amount by money_num first, then multiply the low 32 bits by
        // money_den. The bar is amount * 70 / (10000 << team) pixels.
        const uint32_t amount = uint32_t(int64_t(team.raw_funding) / finance.money_num) * uint32_t(finance.money_den);
        team.display_funding = std::bit_cast<int32_t>(amount);
        const uint32_t pixels = amount * 70u;
        team.bar_width = int32_t(int64_t(std::bit_cast<int32_t>(pixels)) / (10000 << i));
        team.graph_x = 60 + int(i) * 69;
        team.graph_y = 54 + 24 * int(team.league) + 2 * int(team.rank);
        int caption_y = team.graph_y;
        if (caption_y < 112 && (caption_y < 67 || caption_y > 71) && (caption_y < 91 || caption_y > 95))
            caption_y += 16;
        team.caption_baseline = caption_y - 5;
        team.native_caption = number(team.rank + 1, 1, separator, false) + ".Platz" +
                              number(team.league + 1, 2, separator, false) + ".Liga";
        // The original shows these amounts (e.g. 10000/20000/40000) without thousands separators.
        team.native_amount = number(team.display_funding, 5, separator, false);
    }
    return out;
}

void draw_classic_youth(ClassicCanvas& c, const HokFile& h, const FinanceTables& finance, int manager) {
    const auto view = classic_youth_view(h, finance, manager);
    c.background(22);
    c.frame(31, 21, 10, 6);
    c.frame(104, 156, 4, 4);
    c.text_native(ClassicFontId::NewBold, 31, 40, "Jugendteams", 7, 1, 270);
    for (size_t i = 0; i < view.teams.size(); ++i) {
        const auto& team = view.teams[i];
        const int y = 167 + int(i) * 24;
        c.rect(120, y - 6, 192, y + 11, 4);
        c.outline(120, y, 190, y + 11, 29);
        c.rect(121, y + 1, 189, y + 10, 5);
        c.text_native(mini, 120, y - 2, labels[i], 1);
        c.text_native(mini, 157, y - 2, ":", 1);
        c.text_native(mini, 165, y - 2, team.native_amount, 1);
        bar(c, 110, y + 2, 117, y + 7, i);
        if (team.bar_width > 2)
            bar(c, 121, y + 1, word(uint32_t(team.bar_width) + 119u), y + 10, i);
    }
    for (int i = 0; i < 4; ++i) {
        c.line(56, 53 + 24 * i, 242, 53 + 24 * i, 18);
        if (i < 3) c.line(58, 54 + 24 * i, 242, 54 + 24 * i, 9);
    }
    for (size_t i = 0; i < view.teams.size(); ++i) {
        const auto& team = view.teams[i];
        c.line(team.graph_x, uint8_t(team.graph_y), team.graph_x, 125, colors[i][0]);
        c.blit(33, team.graph_x, team.graph_y - 3, 30 + int(i) * 16, 0, 16, 10);
        c.text_native(mini, team.graph_x + 2, team.caption_baseline, team.native_caption, colors[i][0], 0);
    }
    // Graph axes from the game's coordinate table; they overwrite earlier grid/icon pixels.
    c.line(53,53,53,126,29); c.line(54,52,54,127,5); c.line(55,53,55,126,29);
    c.line(54,126,242,126,29); c.line(53,127,243,127,5); c.line(54,128,242,128,29);
    for (int y = 54; y < 125; y += 2) c.rect(56,y,57,y,29);
}
} // namespace em::ui
