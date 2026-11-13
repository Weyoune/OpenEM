// SPDX-License-Identifier: GPL-3.0-only
// Player detail modal (see classic_player.hpp). Reads the player record and the squad record of the save state.
#include "ui/classic_player.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace em::ui {
namespace {
constexpr auto kMini = ClassicFontId::Mini;
// Save-state layout: player table (33-byte records, id 1 at 0x4FC6) and the squads (52-byte slots, 30 per
// manager, 0x618 bytes per squad, starting at 0x696D).
constexpr size_t kPlayers = 0x4fc6, kPlayerSize = 33, kSquads = 0x696d, kSquadSize = 52;

// Bounds-checked pointer to length bytes at offset at of the save state.
const uint8_t* record(const HokFile& h, size_t at, size_t length) {
    if (at > h.state.size() || length > h.state.size() - at)
        throw FormatError("classic player: truncated native record");
    return h.state.data() + at;
}
unsigned word16(const uint8_t* p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
uint32_t word32(const uint8_t* p) { return uint32_t(word16(p)) | uint32_t(word16(p + 2)) << 16; }

// Decimal text of value, optionally with '.' thousands separators; non-negative values are left-padded to
// width with '%', the font's blank digit-wide glyph.
std::string number(int64_t value, int width, bool grouped = false) {
    std::string result = std::to_string(value);
    if (grouped) {
        const size_t first = value < 0 ? 1 : 0;
        for (int i = int(result.size()) - 3; i > int(first); i -= 3)
            result.insert(size_t(i), ".");
    }
    if (value >= 0 && int(result.size()) < width)
        result.insert(0, size_t(width - int(result.size())), '%');
    return result;
}

// A count of at least two digits; zero is shown as "%-" (blank digit and a dash), as in the original.
std::string counter(int64_t value, bool grouped = false) {
    return value ? number(value, 2, grouped) : "%-";
}

// Deterministic market value of a squad slot r with player record p, in internal units (3 units = 1 DM).
// Weighted score: strength 58, goals 4, games 4, trend (Tendenz) 5, the penalty bytes r[0] * 6 + r[1] 4 and
// youth 25 (each part capped and scaled to its weight), times 5000, minus 50000 above 55000; a flagged (0x80) player counts 160 %, then
// * 10 / 3, a goalkeeper (p[0x18] == 0) * 1.6, rounded down to a multiple of 3000.
// The random branch of the game's valuation is quoted by the caller instead of inventing a seed.
int64_t market_value(const uint8_t* r, const uint8_t* p) {
    if ((r[9] & 0x80) && r[0x16] <= 63)
        throw FormatError("classic player: native random market quote required");
    int64_t value = ((int(r[0x10]) + r[0x11]) / 2) * 58 / 100;
    const int goals = std::min((r[3] * 30 + r[4] * 30 + r[5] * 40) / 100, 20);
    const int games = std::min((int(word16(r + 0x1c)) * 50 + int(word16(r + 0x1e)) * 20 +
                                int(word16(r + 0x20)) * 30) / 100, 200);
    const int bonus = std::clamp(r[0] * 6 + r[1], 0, 20);
    const int youth = std::clamp(45 - int(p[0x14]), 0, 27);
    value += 4 * (goals * 100 / 20) / 100 + 4 * (games * 100 / 200) / 100;
    value += r[0x0e] * 5 / 100 + 4 * (bonus * 100 / 20) / 100 + 25 * (youth * 100 / 27) / 100;
    value *= 5000;
    if (value > 55000) value -= 50000;
    if (r[9] & 0x80) value = value * 160 / 100;
    value = value * 10 / 3;
    if (!p[0x18]) value = value * 16 / 10;
    return value / 3000 * 3000;
}

// One labelled scale of the modal at (x, y): eight coloured bands and a vertical indicator at value
// (clamped to 1..94). The stripes are a scale with an indicator; they are not a progress bar filled up to
// the assessed value.
void meter(ClassicCanvas& canvas, int x, int y, const char* label, int value) {
    constexpr std::array<uint8_t, 8> colors{16, 17, 18, 19, 15, 14, 13, 12};
    canvas.text(kMini, x + 47, y + 1, label, 1, -1, x + 47);
    canvas.rect(x + 1, y + 3, x + 99, y + 13, 1);
    canvas.rect(x + 2, y + 4, x + 98, y + 12, 0);
    for (int band = 0; band < 8; ++band)
        canvas.rect(x + band * 12 + 2, y + 6, x + band * 12 + 14, y + 10, colors[size_t(band)]);
    value = std::clamp(value, 1, 94);
    canvas.line(x + value + 1, y + 4, x + value + 1, y + 12, colors[size_t(value / 12)]);
}

// The "Aha !" button (KB4 sprite at 168,208); its four corner pixels keep the page underneath.
void aha_button(ClassicCanvas& canvas) {
    constexpr int x = 168, y = 208;
    const std::array<uint8_t, 4> corners{canvas.pixels().at(x, y), canvas.pixels().at(x + 56, y),
        canvas.pixels().at(x, y + 11), canvas.pixels().at(x + 56, y + 11)};
    canvas.blit(4, x, y, 17, 179, 57, 12);
    canvas.pixels().at(x, y) = corners[0]; canvas.pixels().at(x + 56, y) = corners[1];
    canvas.pixels().at(x, y + 11) = corners[2]; canvas.pixels().at(x + 56, y + 11) = corners[3];
    canvas.text(kMini, x, y + 7, "Aha !", 29, 1, x + 59);
}
}

void draw_classic_player(ClassicCanvas& canvas, const HokFile& h, int player,
                         int manager, int slot, std::optional<int64_t> rawMarketQuote) {
    if (manager < 0 || manager >= kHokManagers || player < 0 || size_t(player) >= h.players.size())
        throw FormatError("classic player: invalid player/manager");
    int id = 0;
    for (int candidate = 1; candidate <= kHokPlayers; ++candidate)
        if (h.players[size_t(player)].raw == record(h, kPlayers + size_t(candidate - 1) * kPlayerSize, kPlayerSize)) {
            id = candidate; break;
        }
    if (!id) throw FormatError("classic player: player is not a native record");
    const size_t base = kSquads + size_t(manager) * 0x618;
    (void)record(h, base, 0x618);
    if (slot < 0)
        for (int candidate = 0; candidate < 30; ++candidate)
            if (h.state[base + size_t(candidate) * kSquadSize + 15] == id) { slot = candidate; break; }
    if (slot < 0 || slot >= 30 || h.state[base + size_t(slot) * kSquadSize + 15] != id)
        throw FormatError("classic player: player is not in the selected roster slot");
    const uint8_t* r = h.state.data() + base + size_t(slot) * kSquadSize;
    const uint8_t* p = record(h, kPlayers + size_t(id - 1) * kPlayerSize, kPlayerSize);
    const int64_t assessed = rawMarketQuote ? *rawMarketQuote : market_value(r, p);
    const bool own = h.state.at(0x0dcd + size_t(manager) * 0x1af + 0x14) == p[0x1d];
    const int level = h.state.at(0xaeb4); // game level; below 3 the market value of foreign players is hidden
    const auto name = iso646de_to_utf8(reinterpret_cast<const char*>(p), 18);

    canvas.frame(89, 55, 9, 9);
    canvas.rect(100, 61, 291, 99, 19);
    canvas.outline(101, 62, 290, 98, 27);
    canvas.text(ClassicFontId::Flow, 110, 75, name, 16, 9, 250);
    canvas.text(kMini, 113, 94, "Alter:" + number(p[0x14], 1), 16);
    canvas.text(kMini, 110, 85, "Länderspiele:" + number(p[0x19], 1), 16, 9, 250);
    canvas.text(kMini, 207, 94, "Stärke:" + number((int(r[0x10]) + r[0x11] + r[0x12]) / 3, 1), 16);
    canvas.outline(253, 67, 275, 93, 27);
    canvas.outline(254, 68, 274, 92, 26);
    canvas.outline(255, 69, 273, 91, 27);
    const int age_band = std::min(int(uint8_t((int(p[0x14]) - 18) / 5)), 3);
    canvas.blit(10, 256, 70, (3 - age_band) * 17, int(p[0x12]) * 21, 17, 21, true, 192);

    constexpr std::array<const char*, 3> competitions{"Liga", "DEB-Pokal", "Europacup"};
    for (int column = 0; column < 3; ++column) {
        const int x = 118 + 59 * column;
        canvas.text(kMini, x, 110, competitions[size_t(column)], 30, -1, x + 58);
        if (p[0x18] || !column)
            canvas.text(kMini, x + 26, 118,
                counter(r[3 + column]) + "(" + counter(word16(r + 0x22 + 2 * column)) + ")", 1, 0);
        canvas.text(kMini, x + 26, 126,
            counter(r[6 + column]) + "(" + counter(word16(r + 0x1c + 2 * column)) + ")", 1, 0);
    }
    canvas.text(kMini, 96, 118, p[0x18] ? "Tore:" : "GTore:", 1);
    canvas.text(kMini, 96, 126, "Spiele:", 1);
    canvas.text(kMini, 96, 137, "Daten:", 1);

    constexpr std::array<const char*, 6> labels{
        "Kaufpreis", "Marktwert", "Versicherung", "Sperren in Min.", "Gehalt/M.", "Vertrag"};
    std::array<int64_t, 6> values{word32(r + 0x2c) & 0x7fffffffu,
        (!own && level < 3) ? 0 : assessed, int32_t(word32(r + 0x30)),
        r[0] * 5 + r[1] * 2, own ? int32_t(word32(r + 0x28)) : 0, r[0x0b]};
    for (int field = 0; field < 6; ++field) {
        const bool money = field < 3 || field == 4;
        if (money) values[size_t(field)] /= 3;
        std::string value = field == 2 && !values[2] ? "keine" : counter(values[size_t(field)], true);
        if (money && values[size_t(field)]) value += " DM";
        if (field == 5 && values[5]) value += values[5] > 1 ? " Jahre" : " Jahr";
        if (field == 5 && r[0x0c]) value = "GELIEHEN";
        const int x = 118 + 59 * (field % 3), baseline = 137 + 16 * (field / 3);
        canvas.text(kMini, x, baseline, labels[size_t(field)], 30, -1, x + 58);
        canvas.text(kMini, x, baseline + 7, value, 1, -1, x + 58);
    }
    meter(canvas, 96, 172, "Unterzahl", (r[0x19] & 15) * 94 / 15);
    meter(canvas, 197, 172, "Überzahl", (r[0x19] >> 4) * 94 / 15);
    meter(canvas, 96, 191, "Tendenz", (int(r[0x0e]) - 30) * 100 / 38);
    meter(canvas, 197, 191, "Erschöpfung", (100 - int(r[0x13])) * 94 / 100);
    aha_button(canvas);
}

} // namespace em::ui
