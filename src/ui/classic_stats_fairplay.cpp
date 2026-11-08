// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_stats_fairplay.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace em::ui {
namespace {
constexpr auto flow = ClassicFontId::Flow, mini = ClassicFontId::Mini;
constexpr size_t kTable = 0x3BA5, kTeams = 0x17E7, kManagers = 0x0DCD;
constexpr std::array<int, 4> kLeagueFirst{0, 12, 24, 0};  // DS:07C0

std::string number(long value, int width) {
    const bool negative = value < 0;
    std::string s = std::to_string(negative ? -value : value);
    if (negative) return "-" + s;
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
int league_first(const HokFile& h) {
    const int manager = h.state.at(0x0788);
    if (manager >= 4) throw FormatError("classic fair play: invalid manager");
    const int league = h.state.at(kManagers + size_t(manager) * 0x1AF + 0xEB);
    if (league > 3) throw FormatError("classic fair play: invalid league");
    return kLeagueFirst[size_t(league)];
}
int minutes(const HokFile& h, int team) { return le16(h.state, kTable + size_t(team) * 0x50 + 0x4A); }
}  // namespace

std::array<int, 12> classic_stats_fairplay_order(const HokFile& h) {
    const int first = league_first(h);
    std::array<int, 12> order{};
    for (int i = 0; i < 12; ++i) order[size_t(i)] = i;
    // 4495:0042: exchange sort, strictly fewer minutes move up.
    for (size_t i = 0; i < 12; ++i)
        for (size_t j = i + 1; j < 12; ++j)
            if (minutes(h, first + order[j]) < minutes(h, first + order[i])) std::swap(order[i], order[j]);
    for (int& team : order) team += first;
    return order;
}

void draw_classic_stats_fairplay(ClassicCanvas& c, const HokFile& h) {
    const auto order = classic_stats_fairplay_order(h);
    c.background(22);
    c.frame(0x1E, 0x18, 11, 7);
    c.line(0x24, 0x2E, 0x11C, 0x2E, 1);
    c.text_native(flow, 0x23, 0x23, "Die Fair-Play-Tabelle der Liga", 10, 28, 0x11D);
    c.text_native(mini, 0x26, 0x2C, "PL.    VEREIN                           GES.STRAFZEIT       PRO SPIEL", 1);
    for (int i = 0; i < 12; ++i) {
        const int team = order[size_t(i)], y = 0x37 + 8 * i;
        const size_t record = kTeams + size_t(team) * 61;
        const uint8_t color = h.state.at(record + 0x16) ? 24 : 30;
        std::string name;
        for (size_t k = 0; k < 22 && h.state.at(record + k); ++k) name += char(h.state.at(record + k));
        c.text_native(mini, 0x26, y, number(i + 1, 2) + ". " + name, color);
        const int penalty = minutes(h, team);
        c.text_native(mini, 0x96, y, number(penalty, 3) + " min.", color);
        int games = h.state.at(kTable + size_t(team) * 0x50 + 0x21) + h.state.at(kTable + size_t(team) * 0x50 + 0x22);
        if (!games) games = 1;
        const long rate = long(penalty) * 100 / games;
        const long fraction = rate % 100;
        c.text_native(mini, 0xCD, y, number(rate / 100, 2) + "." + (fraction < 10 ? "0" : "") + number(fraction, 1) + " min.", color);
    }
}

}  // namespace em::ui
