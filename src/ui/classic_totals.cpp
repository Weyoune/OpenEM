// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_totals.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini, flow = ClassicFontId::Flow;
constexpr std::array<uint8_t, 13> gradient{16,17,18,10,11,8,22,23,24,25,26,27,28};

// 199F:0164 uses the native '%' blank digit, not a normal space. Negative
// numbers are not padded. Group separators count toward the width.
std::string number(int64_t value, int width = 1, bool grouped = false) {
    const bool negative = value < 0;
    std::string result = std::to_string(negative ? -value : value);
    if (grouped)
        for (size_t n = result.size(); n > 3; n -= 3) result.insert(n - 3, ".");
    if (negative) return "-" + result;
    if (int(result.size()) < width) result.insert(0, size_t(width - int(result.size())), '%');
    return result;
}

std::string series_number(int value) { return value ? number(value, 2) : "%-"; }

void heading(ClassicCanvas& canvas, int x, int y, std::string_view original) {
    // Source begins with '!'. The original underline measures length-1
    // bytes from that original pointer, rather than the visible substring.
    canvas.text_native(flow, x, y, original.substr(1), 7, 0);
    const int width = canvas.font().measure_native(flow, original.substr(0, original.size() - 1));
    canvas.line(x, y + 2, x + width + 1, y + 2, 7);
    canvas.line(x + 1, y + 3, x + width - 1, y + 3, 1);
}

int integer_sqrt(int value) {
    if (value < 0) return -1;
    int root = 0;
    while ((root + 1) * (root + 1) <= value) ++root;
    return root;
}

int64_t signed32(const Bytes& state, size_t at) { return int32_t(le32(state, at)); }

std::string per_game(const Bytes& state, int team, int column, bool against) {
    const size_t row = 0x3BA8 + size_t(team) * 80;
    int games = 0, goals = 0;
    const int first = column == 0 ? 0 : column - 1;
    const int last = column == 0 ? 2 : first + 1;
    for (int side = first; side < last; ++side) {
        games += state.at(row + 0x1E + size_t(side));
        goals += state.at(row + (against ? 0x1A : 0x16) + size_t(side));
    }
    const int tenths = games ? goals * 10 / games : 0;
    return "  " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}
}  // namespace

void draw_classic_totals(ClassicCanvas& canvas, const HokFile& snapshot,
                         const ManagerBudget& budget, const FinanceTables& tables, int manager) {
    if (manager < 0 || manager >= 4 || budget.team < 0 || budget.team >= kHokLeagueTeams)
        throw FormatError("HOK: invalid Totale manager/team");
    if (tables.money_num <= 0 || tables.money_den <= 0 || budget.ticket_price <= 0)
        throw FormatError("HOK: invalid Totale currency/ticket price");
    const Bytes& state = snapshot.state;
    const size_t block = 0x0DCD + size_t(manager) * 0x1AF;
    canvas.background(22);
    canvas.frame(12, 0, 8, 10);
    canvas.frame(12, 201, 8, 2);
    canvas.frame(210, 0, 4, 12);

    // DS4C24, thirteen packed12-bit colors, assigned to DS4C3E in reverse.
    constexpr std::array<std::array<int, 3>, 13> dac{{
        {0,48,0},{8,48,0},{16,48,0},{24,48,0},{32,48,0},{40,48,0},{48,48,0},
        {48,40,0},{52,32,0},{52,24,0},{52,16,0},{52,8,0},{52,0,0}
    }};
    for (size_t i = 0; i < dac.size(); ++i)
        canvas.set_dac(gradient[12 - i], dac[i][0], dac[i][1], dac[i][2]);

    heading(canvas, 19, 22, "!Serien");
    heading(canvas, 19, 111, "!}berzahl/Unterzahl");
    heading(canvas, 217, 19, "!Zuschauer");
    heading(canvas, 217, 159, "!Finanzen");
    heading(canvas, 19, 213, "!Einnahmen,Ausgaben / Monat");
    canvas.text_native(flow, 105, 16, "G       H       A", 7, 0);

    constexpr std::array<std::string_view, 9> series_labels{
        "GEWONNEN", "VERLOREN", "UNENTSCHIEDEN", "NICHT GEWONNEN", "NICHT VERLOREN",
        "OHNE GEGENTOR", "OHNE TORERFOLG", "TORE/SPIEL", "GEGENTORE/SPIEL"
    };
    for (size_t row = 0; row < series_labels.size(); ++row) {
        const int y = 33 + int(row) * 8;
        canvas.text_native(mini, 19, y, series_labels[row], 1);
        for (int column = 0; column < 3; ++column) {
            std::string value;
            if (row < 7) {
                const int current = state.at(0x9988 + size_t(budget.team) * 21 + row * 3 + size_t(column));
                const int record = state.at(0x9EC8 + size_t(manager) * 21 + row * 3 + size_t(column));
                value = series_number(current) + "(" + series_number(record) + ")";
            } else value = per_game(state, budget.team, column, row == 8);
            canvas.text_native(mini, 99 + 34 * column, y, value, 30);
        }
    }

    constexpr std::array<std::string_view, 6> power_labels{
        "TORE IN ]BERZAHL", "GEGENTORE IN ]BERZAHL", "TORE IN UNTERZAHL",
        "GEGENTORE IN UNTERZAHL", "]BERZAHL ERFOLG", "UNTERZAHL ERFOLG"
    };
    for (size_t row = 0; row < power_labels.size(); ++row) {
        const int y = 130 + int(row) * 8;
        canvas.text_native(mini, 19, y, power_labels[row], 1);
        int value = le16(state, block + 0x177 + row * 2);
        if (row >= 4) {
            const int denominator = value;
            if (denominator) {
                const int goals = le16(state, block + (row == 4 ? 0x177 : 0x17D));
                // Native IMUL100 discards the high16 bits before division.
                value = uint16_t(goals * 100) / denominator;
                if (row == 5) value = 100 - value;
            } else value = row == 5 ? 100 : 0;
        }
        value = std::clamp(value, 0, 100);
        canvas.text_native(mini, 135, y, number(value, 3) + (row >= 4 ? "#" : ""), 30);
    }

    // Native full monthly sums, including the existing single-player bank
    // formulas. Other active human parties are read from the debt matrix.
    int64_t income = monthly_income(budget, true);
    int64_t expenses = monthly_expenses(tables, budget, true, true);
    int64_t debt = budget.bank < 0 ? -int64_t(budget.bank) : 0;
    const int human_count = state.at(0x0DC6);
    for (int party = 0; party < 4; ++party) {
        if (party == manager) continue;
        const int64_t principal = signed32(state, block + 0x19B + size_t(party) * 4);
        // 56D1:179B checks the current manager against human_count, and
        // reads the other party's reciprocal entry (including inactive
        // parties). Debt/expense helper02E8 instead filters by party.
        const int64_t reciprocal = signed32(state, 0x0DCD + size_t(party) * 0x1AF + 0x19B + size_t(manager) * 4);
        if (manager < human_count && reciprocal < 0) income -= reciprocal * 25 / 1000;
        if (party < human_count && principal < 0) {
            debt -= principal;
            expenses -= principal * 25 / 1000;  // 7027:0843..0872
        }
    }
    const int64_t needed_raw = int32_t(uint32_t((expenses - income) * 12)) / 23;
    const int64_t needed = std::max<int64_t>(0, needed_raw * tables.money_den / tables.money_num / budget.ticket_price);
    // 38BD leaves numeric width3 active through all right-hand values;
    // only the monthly figures switch to8 at3BC9.
    auto money = [&](int64_t raw, int width = 2) {
        return number(raw * tables.money_den / tables.money_num, width, true) + " DM";
    };
    auto label = [&](int y, std::string_view text) { canvas.text_native(mini, 217, y, text, 1); };
    auto value = [&](int y, const std::string& text) { canvas.text_native(mini, 217, y, text, 30); };

    label(30, "SCHNITT/SPIEL");
    value(36, budget.home_games ? number(budget.attendance_total / budget.home_games, 2, true) : "(noch keiner...)");
    label(45, "BEN|TIGTER SCHNITT");
    value(51, number(needed, 2, true));
    label(60, "ZUSCHAUERREKORD: ");
    label(82, "MINUSKULISSE: ");
    auto opponent = [&](int team) {
        if (team < 0 || size_t(team) >= snapshot.teams.size())
            throw FormatError("HOK: invalid attendance record opponent");
        return "(" + snapshot.teams[size_t(team)].name + ")";
    };
    if (budget.attendance_record) {
        value(66, number(budget.attendance_record, 2, true));
        canvas.text(mini, 217, 72, opponent(budget.record_guest), 30);
    } else value(66, "(noch keiner...)");
    if (budget.attendance_min != 99999) {
        value(88, number(budget.attendance_min, 2, true));
        if (budget.attendance_min) canvas.text(mini, 217, 94, opponent(budget.min_guest), 30);
    } else value(88, "(noch keine...)");

    label(170, "KONTOSTAND"); value(176, money(budget.balance));
    label(197, "SCHULDEN GESAMT"); value(203, money(debt));
    canvas.text_native(mini, 19, 224, "EINNAHMEN(SPONSOREN...)", 1);
    canvas.text_native(mini, 19, 231, "AUSGABEN(GEH[LTER...)", 1);
    canvas.text_native(mini, 149, 224, money(income, 8), 30);
    canvas.text_native(mini, 149, 231, money(expenses, 8), 30);

    canvas.outline(217, 105, 299, 145, 6);
    for (int row = 0; row < 13; ++row)
        canvas.rect(218, 106 + row * 3, 298, 108 + row * 3, gradient[size_t(12 - row)]);

    // 3DF2..3F82: plot center is break-even attendance/1000. Preserve
    // the native signed16 variance accumulator and arithmetic truncation.
    const int center = int(needed / 1000);
    int minimum = 128, maximum = -127;
    int16_t squares = 0;
    for (int game = 0; game < budget.home_games; ++game) {
        const int attendance = state.at(block + 0xFE + size_t(game));
        minimum = std::min(minimum, attendance); maximum = std::max(maximum, attendance);
        const int distance = attendance - center;
        squares = int16_t(uint16_t(squares) + uint16_t(distance * distance));
    }
    int spread = std::max(10, integer_sqrt(budget.home_games ? squares / budget.home_games : 0));
    int upper = center - spread > minimum ? 2 * center - minimum : center + spread;
    upper = std::max(upper, maximum);
    spread = std::max(1, std::abs(upper - center));
    for (int game = 0; game < budget.home_games; ++game) {
        const int attendance = state.at(block + 0xFE + size_t(game));
        const int scaled = int16_t((attendance - center) * 19) / spread + 19;
        const int height = std::clamp(scaled, 0, 37);
        canvas.line(227 + 3 * game, 143 - height, 227 + 3 * game, 144, 4);
    }

    canvas.rect(223, 179, 293, 188, 0); canvas.outline(222, 178, 294, 189, 6);
    canvas.rect(223, 206, 293, 215, 0); canvas.outline(222, 205, 294, 216, 6);
    const int64_t full_debt = debt + std::max<int64_t>(0, -int64_t(budget.balance));
    for (int pixel = 0; pixel < 71; ++pixel) {
        if (budget.balance > -2450000 + int64_t(pixel) * 70000)
            canvas.line(223 + pixel, 179, 223 + pixel, 188, gradient[size_t(pixel / 6)]);
        if (full_debt > int64_t(pixel) * 25000)
            canvas.line(223 + pixel, 206, 223 + pixel, 215, gradient[size_t(12 - pixel / 6)]);
    }
}

}  // namespace em::ui
