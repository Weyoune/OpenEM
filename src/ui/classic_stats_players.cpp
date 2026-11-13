// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Spieler page (see classic_stats_players.hpp).
#include "ui/classic_stats_players.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto flow = ClassicFontId::Flow, mini = ClassicFontId::Mini;
// Save state: player table indexed by player id (record id at 0x4FA5 + id * 0x21), team records, manager
// blocks and squads. Player record: +0x18 position (0 = goalkeeper), +0x1B goals (goals against for a
// goalkeeper), +0x1C games, +0x1D team, +0x1F s16 assists (minutes played for a goalkeeper).
constexpr size_t kPlayers = 0x4FA5, kTeams = 0x17E7, kManagers = 0x0DCD, kSquads = 0x696D;
constexpr int kRecord = 0x21;
// First and last team index of each league.
constexpr std::array<std::array<int, 2>, 4> kLeagueTeams{{{0, 11}, {12, 23}, {24, 35}, {36, 42}}};
constexpr std::array<std::string_view, 3> kModes{"TORSCH.", "VORLEGER", "SCORER"};  // mode button labels

// Decimal text padded to width; '%' is the game's blank digit.
std::string number(long value, int width) {
    const bool negative = value < 0;
    std::string s = std::to_string(negative ? -value : value);
    if (negative) return "-" + s;
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
// Text field of at most limit bytes, up to the first NUL.
std::string cstring(const Bytes& state, size_t at, size_t limit) {
    std::string s;
    for (size_t i = 0; i < limit && state.at(at + i); ++i) s += char(state.at(at + i));
    return s;
}
int s16(const Bytes& s, size_t at) { return int16_t(uint16_t(s.at(at) | s.at(at + 1) << 8)); }

// Sort key of the original's exchange sort: goals, assists or both by mode; goalkeepers use the negative
// goals against per 60 minutes, or -99999 below 400 minutes.
long sort_key(const Bytes& s, int player, int mode) {
    const size_t r = kPlayers + size_t(player) * kRecord;
    const int second = s16(s, r + 0x1F);
    if (s.at(r + 0x18) == 0) return second < 400 ? -99999L : -(long(s.at(r + 0x1B)) * 6000 / second);
    long key = 0;
    if (mode != 1) key = s.at(r + 0x1B);
    if (mode != 0) key += second;
    return key;
}
}  // namespace

ClassicStatsPlayersView classic_stats_players_view(const HokFile& h, int mode, bool managers_only) {
    if (mode < 0 || mode > 2) throw FormatError("classic player statistics: invalid mode");
    Bytes s = h.state;  // local copy for the display-time squad goal sync
    if (s.size() < kSquads + 6 * 0x618) throw FormatError("classic player statistics: truncated state");
    const int humans = s.at(0x0DC6), manager = s.at(0x0788);
    if (humans > 4 || manager >= 6) throw FormatError("classic player statistics: invalid managers");
    int last_row = 0;  // the original's reused row variable (see ClassicStatsPlayersView::empty_notice)
    for (int m = 0; m < humans; ++m) {
        int count = 0;
        for (int slot = 0; slot < 30; ++slot) count += s.at(kSquads + size_t(m) * 0x618 + size_t(slot) * 0x34 + 0x0F) != 0;
        last_row = count;
        for (int slot = 0; slot < count; ++slot) {
            const size_t squad = kSquads + size_t(m) * 0x618 + size_t(slot) * 0x34;
            const int id = s.at(squad + 0x0F);
            if (id) s.at(kPlayers + size_t(id) * kRecord + 0x1B) = s.at(squad + 0x03);
        }
    }
    // Exchange sort of players 1..199; equal keys: fewer games moves up.
    std::array<int, 200> order{};
    for (int i = 1; i < 200; ++i) order[size_t(i)] = i;
    for (int i = 1; i < 200; ++i)
        for (int j = i + 1; j < 200; ++j) {
            const long a = sort_key(s, order[size_t(j)], mode), b = sort_key(s, order[size_t(i)], mode);
            const int games_j = s.at(kPlayers + size_t(order[size_t(j)]) * kRecord + 0x1C);
            const int games_i = s.at(kPlayers + size_t(order[size_t(i)]) * kRecord + 0x1C);
            if (b < a || (a == b && games_j < games_i)) std::swap(order[size_t(i)], order[size_t(j)]);
        }

    ClassicStatsPlayersView view;
    view.mode = mode;
    view.managers_only = managers_only;
    view.native_title = "Die besten " + std::string(kModes[size_t(mode)]) + " der " + (managers_only ? "Manager" : "Liga");
    view.native_keeper_title = "Die besten KEEPER der " + std::string(managers_only ? "Manager" : "Liga");
    const int league = s.at(kManagers + size_t(manager) * 0x1AF + 0xEB);
    if (league > 3) throw FormatError("classic player statistics: invalid league");
    std::array<int, 2> ranks{}, baseline{0x36, 0xA1};
    constexpr std::array<size_t, 2> limit{12, 3};
    for (int i = 1; i < 200; ++i) {
        const int id = order[size_t(i)];
        const size_t r = kPlayers + size_t(id) * kRecord;
        const int team = s.at(r + 0x1D);
        if (team < kLeagueTeams[size_t(league)][0] || team > kLeagueTeams[size_t(league)][1]) continue;
        const int second = s16(s, r + 0x1F), goals = s.at(r + 0x1B), games = s.at(r + 0x1C);
        const bool keeper = s.at(r + 0x18) == 0;
        // eligible: goalkeepers with at least 399 minutes, skaters with at least two scorer points
        if (keeper ? second < 0x18F : goals + second < 2) continue;
        const size_t kind = keeper ? 1 : 0;
        last_row = baseline[kind];
        bool managed = false;
        for (int m = 0; m < humans; ++m)
            if (s.at(kManagers + size_t(m) * 0x1AF + 0x14) == team && managers_only) managed = true;
        ++ranks[kind];
        auto& rows = keeper ? view.keepers : view.skaters;
        if (rows.size() >= limit[kind] || (managers_only && !managed)) continue;
        ClassicStatsPlayerRow row;
        row.player = id; row.team = team; row.rank = ranks[kind]; row.baseline = baseline[kind];
        row.color = s.at(kTeams + size_t(team) * 61 + 0x16) ? 24 : 30;
        row.native_name = number(ranks[kind], 2) + ". " + cstring(s, r, 18);
        if (!keeper) {
            row.native_score = number(goals + second, 2) + "(" + number(goals, 2) + "/" + number(second, 2) + ")";
            long rate = long(goals) * 100;
            if (games) rate /= games;
            row.native_rate = number(rate / 100, 1) + "." + (rate % 100 < 10 ? "0" : "") + number(rate % 100, 1);
        } else {
            row.native_score = number(goals, 3);
            const long rate = long(goals) * 6000 / second, whole = rate / 100 * 100;
            row.native_rate = number(second, 4) + "(" + number(rate / 100, 2) + "." + number((rate - whole) / 10, 1) +
                              number((rate - whole) % 10, 1) + ")";
        }
        rows.push_back(row);
        baseline[kind] += 7;
    }
    view.empty_notice = last_row == 0x36;
    return view;
}

void draw_classic_stats_players(ClassicCanvas& c, const HokFile& h, int mode, bool managers_only) {
    const auto view = classic_stats_players_view(h, mode, managers_only);
    c.background(22);
    c.frame(30, 24, 11, 10);
    for (int y = 0x23; y < 200; y += 0x6B) {
        c.line(0x24, y + 11, 0x11C, y + 11, 1);
        c.text_native(flow, 0x23, y, y == 0x23 ? view.native_title : view.native_keeper_title, 10, 28, 0x11D);
        c.text_native(mini, 0x26, y + 9, "PL. SPIELER                 VEREIN", 1);
        // The original compares the row against 0 here, so both captions start at 190.
        c.text_native(mini, 190, y + 9, y == 0x23 ? "       SCORES    TORE/SPIEL" : "GEGENTORE     MIN.(G/SP.)", 1);
    }
    for (const auto* rows : {&view.skaters, &view.keepers})
        for (const auto& row : *rows) {
            const bool keeper = rows == &view.keepers;
            c.text_native(mini, 0x26, row.baseline, row.native_name, row.color);
            c.text_native(mini, 0x72, row.baseline, cstring(h.state, kTeams + size_t(row.team) * 61, 22), row.color);
            c.text_native(mini, 200, row.baseline, row.native_score, row.color);
            c.text_native(mini, keeper ? 0xFC - 15 : 0xFC, row.baseline, row.native_rate, row.color);
        }
    if (view.empty_notice) c.text_native(mini, 0x23, 0x3C, "NOCH KEINE EINTR[GE VORHANDEN...", 7, 1, 0x11D);
    auto button = [&](int x, int y, std::string_view text) {
        const std::array<uint8_t, 4> corners{c.pixels().at(x, y), c.pixels().at(x + 56, y),
                                             c.pixels().at(x, y + 11), c.pixels().at(x + 56, y + 11)};
        c.blit(4, x, y, 17, 179, 57, 12, true, 192);  // the game's standard 57 x 12 button; corners keep the page
        c.pixels().at(x, y) = corners[0]; c.pixels().at(x + 56, y) = corners[1];
        c.pixels().at(x, y + 11) = corners[2]; c.pixels().at(x + 56, y + 11) = corners[3];
        c.text_native(mini, x, y + 7, text, 29, 1, x + 59);
    };
    button(0x36, 0xC3, "LIGA");
    button(0xD4, 0xC3, "MANAGER");
    for (int i = 0; i < 3; ++i) button(0x36 + i * 0x4F, 0xB6, kModes[size_t(i)]);
}

int classic_stats_players_button(int x, int y) {
    for (int i = 0; i < 3; ++i)
        if (x >= 0x36 + i * 0x4F && x <= 0x36 + i * 0x4F + 56 && y >= 0xB6 && y <= 0xB6 + 11) return i;
    if (y >= 0xC3 && y <= 0xC3 + 11) {
        if (x >= 0x36 && x <= 0x36 + 56) return 3;
        if (x >= 0xD4 && x <= 0xD4 + 56) return 4;
    }
    return -1;
}

int classic_stats_players_row(const ClassicStatsPlayersView& view, int, int y) {
    // As in the original: row (y - first + 4) / 7 inside first-5 < y < next free baseline.
    const std::array<const std::vector<ClassicStatsPlayerRow>*, 2> lists{&view.skaters, &view.keepers};
    const std::array<int, 2> first{0x36, 0xA1};
    for (size_t k = 0; k < 2; ++k) {
        const int end = first[k] + int(lists[k]->size()) * 7;
        if (first[k] - 5 < y && y < end) {
            const size_t row = size_t((y - first[k] + 4) / 7);
            if (row < lists[k]->size()) return (*lists[k])[row].player;
        }
    }
    return -1;
}

}  // namespace em::ui
