// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Frueher page (see classic_stats_history.hpp).
#include "ui/classic_stats_history.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto flow = ClassicFontId::Flow, mini = ClassicFontId::Mini;
// Save state: league table rows (0x50 bytes per team), team records (61 bytes), manager blocks (0x1AF bytes).
constexpr size_t kTable = 0x3BA5, kTeams = 0x17E7, kManagers = 0x0DCD;
// Row labels of the manager's all-time record.
constexpr std::array<std::string_view, 5> kLabels{"PUNKTE", "TORE", "SIEGE", "NIEDERLAGEN", "UNENTSCH."};

// Decimal text, non-negative values padded to width with the blank digit '%'.
std::string number(long value, int width) {
    const bool negative = value < 0;
    std::string s = std::to_string(negative ? -value : value);
    if (negative) return "-" + s;
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
// Team name: first 22 bytes of the team record, up to the first NUL.
std::string team_name(const Bytes& s, int team) {
    std::string out;
    for (size_t i = 0; i < 22 && s.at(kTeams + size_t(team) * 61 + i); ++i) out += char(s.at(kTeams + size_t(team) * 61 + i));
    return out;
}
uint32_t points(const Bytes& s, int team) { return le32(s, kTable + size_t(team) * 0x50 + 0x4C); }
int word(const Bytes& s, size_t at) { return int(le16(s, at)); }
}  // namespace

std::array<int, 42> classic_stats_history_order(const HokFile& h) {
    std::array<int, 42> order{};
    for (int i = 0; i < 42; ++i) order[size_t(i)] = i;
    // Exchange sort as in the original: the inner index starts at the outer one; strictly greater swaps.
    for (size_t i = 0; i < 41; ++i)
        for (size_t j = i; j < 42; ++j)
            if (points(h.state, order[j]) > points(h.state, order[i])) std::swap(order[i], order[j]);
    return order;
}

void draw_classic_stats_history(ClassicCanvas& c, const HokFile& h, int scroll) {
    const Bytes& s = h.state;
    if (scroll < 0 || scroll > 30) throw FormatError("classic history: invalid scroll");
    const int manager = s.at(0x0788), humans = s.at(0x0DC6);
    if (manager >= 4 || humans > 4) throw FormatError("classic history: invalid manager");
    const auto order = classic_stats_history_order(h);
    c.background(22);
    c.frame(0x32, 0x17, 9, 6);
    c.frame(0x32, 0x9A, 9, 4);
    c.text_native(flow, 100, 0x29, "Ewige Tabelle", 10, 28, 100);
    c.line(0x39, 0x33, 0x103, 0x33, 1);
    c.line(0x39, 0xAD, 0x103, 0xAD, 1);
    for (int i = 0; i < 3; ++i) c.blit(36, 0xBD + 24 * i, 0x1D, 0, 22 * i, 22, 22, true, 0);
    c.blit(4, 0xA5, 0x1E, 0x14, 0, 9, 17, true, 0xC0);

    // The 200x314 list buffer, filled with colour 4: place, name, points and the first three table bytes.
    Image list(200, 0x13A, 4);
    int own_rank = 0;
    for (int i = 0; i < 42; ++i) {
        const int team = order[size_t(i)];
        uint8_t color = 30;
        for (int m = 0; m < humans; ++m)
            if (s.at(kManagers + size_t(m) * 0x1AF + 0x14) == team) {
                color = 24;
                if (m == manager) own_rank = i;
            }
        const int y = 6 + 6 * i;
        c.font().draw_native(list, mini, 9, y, number(i + 1, 2) + ". " + team_name(s, team), color);
        c.font().draw_native(list, mini, 0x69, y, number(long(points(s, team) % 100000u), 5), color);
        for (int k = 0; k < 3; ++k)
            c.font().draw_native(list, mini, 0x89 + 24 * k, y, number(s.at(kTable + size_t(team) * 0x50 + size_t(k)), 2), color);
    }

    c.text_native(mini, 0x76, 0xB7, "GESAMT             HEIM              AUSW[RTS", 24);
    const size_t block = kManagers + size_t(manager) * 0x1AF;
    for (int r = 0; r < 5; ++r) {
        c.text_native(mini, 0x3E, 0xC0 + 7 * r, kLabels[size_t(r)], 24);
        // Columns GESAMT (home + away, 16-bit), HEIM, AUSWÄRTS. PUNKTE and TORE show "plus:minus"; the
        // original's switch that would hide the minus points is off, so PUNKTE shows them (+0x147) as well.
        const size_t a = r == 0 ? block + 0x13B : r == 1 ? block + 0x13F : block + 0x14F + size_t(r - 2) * 4;
        const size_t e = r == 0 ? block + 0x147 : block + 0x143;
        for (int k = 0; k < 3; ++k) {
            const int v = k == 0 ? (word(s, a) + word(s, a + 2)) & 0xFFFF : word(s, a + size_t(k - 1) * 2);
            std::string text = number(v, 4);
            if (r < 2) {
                const int w = k == 0 ? (word(s, e) + word(s, e + 2)) & 0xFFFF : word(s, e + size_t(k - 1) * 2);
                text += ":" + number(w, 4);
            }
            c.text_native(mini, (r < 2 ? 0x6E : 0x78) + 0x34 * k, 0xC0 + 7 * r, text, 24);
        }
    }
    const int own = s.at(block + 0x14);
    if (own >= 42) throw FormatError("classic history: invalid manager club");
    const std::string summary = team_name(s, own) + " (" + number(own_rank + 1, 1) + ".)," +
                                number(long(points(s, own) % 100000u), 5) + " P.";
    c.text_native(flow, 0x43, 0xA7, summary, 10, 28, 0xFC);

    // The 200x72 window at (57,58) shows the buffer from row scroll*6+2; colour 0xC0 is transparent.
    const int top = scroll * 6 + 2;
    for (int y = 0; y < 0x48; ++y)
        for (int x = 0; x < 200; ++x) {
            const uint8_t p = list.at(x, top + y);
            if (p != 0xC0) c.pixels().at(0x39 + x, 0x3A + y) = p;
        }
}

int classic_stats_history_scroll(int x, int y) {
    if (x >= 0xA5 && x <= 0xA5 + 8) {
        // The upper arrow scrolls up (-1), the lower one down (+1).
        if (y >= 0x1E && y <= 0x1E + 8) return -1;
        if (y >= 0x27 && y <= 0x27 + 8) return 1;
    }
    if (y > 0x39 && y < 0x44) return 1;
    if (y > 0x75 && y < 0x81) return -1;
    return 0;
}

}  // namespace em::ui
