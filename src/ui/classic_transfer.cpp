// SPDX-License-Identifier: GPL-3.0-only
// Transfer page drawing (see classic_transfer.hpp). Offsets are into the save state.
#include "ui/classic_transfer.hpp"
#include "ui/classic_roster.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini, flow = ClassicFontId::Flow;
// Manager blocks and, inside each block, the debt matrix (i32 per party 0..4; 4 = bank).
constexpr size_t kManager = 0x0DCD, kManagerSize = 0x1AF, kDebts = 0x19B;
// Transfer pool (squad 4), 52-byte slots, and the player table (33-byte records, id 1 first).
constexpr size_t kPool = 0x696D + 4 * 0x618, kRecord = 52, kPlayers = 0x4FC6;

// Number with '.' thousands separators; non-negative values padded to width with the blank digit '%'.
std::string number(int64_t value, int width = 2) {
    const bool negative = value < 0;
    std::string text = std::to_string(negative ? -value : value);
    for (size_t n = text.size(); n > 3; n -= 3) text.insert(n - 3, ".");
    if (negative) return "-" + text;
    if (int(text.size()) < width) text.insert(0, size_t(width - int(text.size())), '%');
    return text;
}
// Internal money (3 units = 1 DM) as "<n> DM".
std::string money(int64_t raw) { return number(raw / 3) + " DM"; }

// Sunken frame; when the pixel at (left+5, top+5) is 255 only the rims are drawn, otherwise also the
// shadow, fill 4 and corner pixels in that sampled colour.
void plain_frame(ClassicCanvas& c, int left, int top, int right, int bottom) {
    const auto edge = c.pixels().at(left + 5, top + 5);
    const int x = left + 2, y = top + 3;
    if (edge != 255) {
        c.rect(x, y, right, bottom, 6);
        c.rect(x - 2, y - 2, right - 2, bottom - 2, 4);
        for (const auto p : {std::pair{x - 2, y - 2}, {right, y}, {right - 2, y - 2},
                             {right, bottom}, {x, bottom}, {x - 2, bottom - 2}})
            c.pixels().at(p.first, p.second) = edge;
        c.pixels().at(right - 2, bottom - 2) = 0;
    }
    c.line(x - 1, y - 2, right - 3, y - 2, 1);
    c.line(x - 2, y - 1, x - 2, bottom - 3, 1);
    c.line(right - 2, y - 1, right - 2, bottom - 3, 29);
    c.line(x - 1, bottom - 2, right - 3, bottom - 2, 29);
}

// The "Talent" button at (261,226); its corners keep the page underneath.
void talent_button(ClassicCanvas& c) {
    constexpr int x = 261, y = 226;
    const std::array<uint8_t, 4> corners{c.pixels().at(x, y), c.pixels().at(x + 56, y),
                                       c.pixels().at(x, y + 11), c.pixels().at(x + 56, y + 11)};
    c.blit(4, x, y, 17, 179, 57, 12);
    c.pixels().at(x, y) = corners[0]; c.pixels().at(x + 56, y) = corners[1];
    c.pixels().at(x, y + 11) = corners[2]; c.pixels().at(x + 56, y + 11) = corners[3];
    c.text_native(mini, x, y + 7, "Talent", 29, 1, x + 59);
}

// Footer: Kontostand, Schulden, Anlagen (money lent to others and shares at the current price), Saldo; all
// sums in 32-bit unsigned arithmetic as in the original. The Talent button appears while no talent search
// runs (block +0xEC zero).
void footer(ClassicCanvas& c, const HokFile& h, const ManagerBudget& budget, int manager) {
    const Bytes& s = h.state;
    const size_t m = size_t(manager), block = kManager + m * kManagerSize;
    uint32_t debt = 0, assets = 0;
    const int human_count = s.at(0x0DC6);
    for (int party = 0; party < 5; ++party) {
        if (party == manager) continue;
        // Debts count only active human parties or the bank (4), as in the original's debt calculation.
        const uint32_t principal = le32(s, block + kDebts + size_t(party) * 4);
        if ((party < human_count || party == 4) && int32_t(principal) < 0) debt -= principal;
        // The original footer instead scans ALL reciprocal entries, including 4,
        // and includes any negative reciprocal (money lent/savings).
        const uint32_t reciprocal = le32(s, kManager + size_t(party) * kManagerSize + kDebts + m * 4);
        if (int32_t(reciprocal) < 0) assets -= reciprocal;
    }
    for (size_t company = 0; company < 4; ++company) {
        const uint32_t quantity = le32(s, 0x30 + m * 32 + company * 8 + 4);
        assets += quantity * le16(s, 0xB0 + company * 72 + 70);
    }
    const uint32_t cash = uint32_t(budget.balance), saldo = cash + assets - debt;
    const std::array<uint32_t, 4> values{cash, debt, assets, saldo};
    constexpr std::array<const char*, 4> labels{"Kontostand:", "Schulden:", "Anlagen:", "Saldo:"};
    plain_frame(c, 0, 222, 319, 239);
    for (size_t i = 0; i < 4; ++i) {
        const int x = 3 + int(i) * 65;
        const uint8_t color = i == 3 ? 240 : 7; // the original computes 7 - 23, which wraps to 240; not a sign colour
        c.text_native(mini, x, 229, labels[i], color);
        c.text_native(mini, x, 236, money(int32_t(values[i])), color);
    }
    if (s.at(block + 0xEC) == 0) talent_button(c);
}
} // namespace

void draw_classic_transfer(ClassicCanvas& c, const HokFile& h,
                          const ManagerBudget& budget, int manager) {
    if (manager < 0 || manager >= 4) throw FormatError("classic transfer: invalid manager");
    const Bytes& s = h.state;
    int rows = 0;
    for (size_t slot = 0; slot < 12; ++slot)
        if (s.at(kPool + slot * kRecord + 0x0F)) ++rows;
    for (int slot = 0; slot < rows; ++slot) {
        const int id = s.at(kPool + size_t(slot) * kRecord + 0x0F);
        if (id < 1 || id > kHokPlayers) throw FormatError("classic transfer: invalid pool player");
    }

    c.background(22);
    draw_classic_roster(c, h, false, manager);
    c.frame(175, 14, 6, 11);
    c.text_native(flow, 175, 25, "Transferliste", 10, 28, 318);
    c.text_native(mini, 181, 34, "ART   NAME               ST[RKEN    ALTER", 1);
    c.line(180, 36, 313, 36, 31);
    constexpr std::array<uint8_t, 3> colors{30, 24, 9};
    constexpr std::array<const char*, 3> roles{"T", "A", "S"};
    for (int slot = 0; slot < rows; ++slot) {
        const size_t rec = kPool + size_t(slot) * kRecord;
        const size_t player = kPlayers + size_t(s.at(rec + 0x0F) - 1) * 33;
        const int role = std::min(int(s.at(player + 0x18)) / 25, 2);
        uint8_t color = colors[size_t(role)];
        // own list player with an offer (slot +0x09 bit 7): black
        if ((s.at(rec + 0x09) & 0x80) && s.at(player + 0x1A) == manager) color = 0;
        uint8_t role_color = color;
        // Role letter: 16 for players of a national team (team +0x1D in 52..63), 22 for players listed by a
        // manager (owner +0x1A not 4).
        const int country = s.at(player + 0x1D);
        if (country >= 52 && country < 64) role_color = 16;
        if (s.at(player + 0x1A) != 4) role_color = 22;
        const int baseline = 44 + 16 * slot;
        c.text_native(mini, 181, baseline, roles[size_t(role)], role_color);
        std::string name;
        for (size_t i = 0; i < 18 && s.at(player + i); ++i) name += char(s.at(player + i));
        c.text_native(mini, 189, baseline, name, color);
        c.text_native(mini, 252, baseline,
                      number(s.at(rec + 0x10)) + " " + number(s.at(rec + 0x11)) + " " +
                          number(s.at(rec + 0x12)) + " ", color);
        c.text_native(mini, 296, baseline, number(s.at(player + 0x14)) + " J.", color);
        // purchase price (slot +0x2C, top bit masked); the loan costs a third of it
        const uint32_t fee = le32(s, rec + 0x2C) & 0x7FFFFFFFu;
        c.text_native(mini, 181, baseline + 6, money(fee) + " (K)", 1);
        const std::string loan = money(fee / 3) + " (L)";
        const int x = 315 - c.font().measure_native(mini, loan);
        c.text_native(mini, x, baseline + 6, loan, 1);
        c.line(181, baseline + 8, 313, baseline + 8, 31);
    }
    footer(c, h, budget, manager);
}
} // namespace em::ui
