// SPDX-License-Identifier: GPL-3.0-only
// Squad list drawing (see classic_roster.hpp). Offsets are into the save state.
#include "ui/classic_roster.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace em::ui {
namespace {
// Squads: 0x618 bytes per manager from 0x696D, 52-byte slots; byte +0x0F is the 1-based player id.
constexpr size_t kSquads = 0x696d, kSquadStride = 0x618, kRecord = 52;
// Player table: 33-byte records, id 1 at 0x4FC6.
constexpr size_t kPlayers = 0x4fc6, kPlayerRecord = 33;
constexpr auto kMini = ClassicFontId::Mini;

// Number formatting without thousands separators, padded to width. The pad glyph '%' (byte 0x25) is a
// five-pixel blank; an ordinary space is only two Mini pixels wide.
std::string number(int value, int width = 1) {
    std::string result = std::to_string(value);
    if (value >= 0 && int(result.size()) < width)
        result.insert(0, size_t(width - int(result.size())), '%');
    return result;
}

const uint8_t* record(const HokFile& h, size_t offset, size_t length) {
    if (offset > h.state.size() || length > h.state.size() - offset)
        throw FormatError("classic roster: truncated native record");
    return h.state.data() + offset;
}

uint32_t word32(const uint8_t* r) {
    return uint32_t(r[0]) | uint32_t(r[1]) << 8 |
           uint32_t(r[2]) << 16 | uint32_t(r[3]) << 24;
}

// Text colour of a row: by role (goalkeeper 30, defender 24, forward 9). Outside the line-up view, slot byte
// +0x18 with bit 0x80 (player retires) gives 16, other non-zero values give 17 (< 100) or 7 on the contracts
// page; on the transfer page flag 0x40 of byte +0x09 gives 22.
uint8_t row_color(const uint8_t* r, int role, ClassicRosterView view) {
    constexpr std::array<uint8_t, 3> colors{30, 24, 9};
    uint8_t color = colors[size_t(role)];
    // The line-up page skips all colour overrides.
    if (view == ClassicRosterView::Lineup) return color;
    const bool contracts = view == ClassicRosterView::Contracts;
    if (r[0x18] & 0x80) color = 16;
    else if (contracts && r[0x18]) color = r[0x18] < 100 ? 17 : 7;
    // The original applies this after the retirement colour, overriding it.
    // (Only the Transfer view reaches here with contracts == false.)
    if (!contracts && (r[0x09] & 0x40)) color = 22;
    return color;
}

// STATUS column: "RESERVE" (+0x0A set and +0x15 & 0x7F zero), "IM TEAM" (+0x0A set), or by bits 0..1 of
// +0x09 "***", "GESP." (suspended) with the count from +0x0D, or "VERL." (injured); those three switch
// color to 16.
std::string status(const uint8_t* r, uint8_t& color) {
    std::string result = " "; // leading blank of every status text
    if (r[0x0a] && !(r[0x15] & 0x7f)) return result + "RESERVE";
    const int state = r[0x09] & 3;
    if (!state) return result + (r[0x0a] ? "IM TEAM" : " ");
    constexpr std::array<const char*, 4> names{"***", "GESP.", "VERL.", " "};
    result += names[size_t(state)];
    color = 16;
    if (state == 1) result += "(" + number(r[0x0d]) + ")";
    return result;
}

// V.DAUER column: contract years (+0x0B) as "n Jahr(e)", "kein V." without contract, "(v)" when insured
// (u32 insurance at +0x30 non-zero).
std::string duration(const uint8_t* r) {
    if (!r[0x0b]) return "kein V.";
    std::string result = number(r[0x0b]) + " Jahr";
    // The original compares the FIRST formatted digit, not the numeric year.
    result += result.front() > '1' ? "e" : "  ";
    if (word32(r + 0x30)) result += "(v)";
    return result;
}
}

void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         bool contracts, int manager) {
    draw_classic_roster(canvas, h, contracts ? ClassicRosterView::Contracts
                                           : ClassicRosterView::Transfer, manager);
}

void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         ClassicRosterView view, int manager) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("classic roster: invalid manager");
    if (view != ClassicRosterView::Transfer && view != ClassicRosterView::Contracts &&
        view != ClassicRosterView::Lineup)
        throw FormatError("classic roster: invalid view");
    const bool contracts = view == ClassicRosterView::Contracts;
    const bool lineup = view == ClassicRosterView::Lineup;
    const size_t base = kSquads + size_t(manager) * kSquadStride;
    (void)record(h, base, kSquadStride);

    // The contracts page draws as many rows as there are occupied records among slots 0..28 (only 0..11 for
    // squad 4, the transfer pool) — counted, not searched, as in the original. The transfer page always draws
    // slots 0..29.
    int rows = 30;
    if (contracts) {
        rows = 0;
        const int slots = manager == 4 ? 12 : 29;
        for (int slot = 0; slot < slots; ++slot)
            if (h.state[base + size_t(slot) * kRecord + 0x0f]) ++rows;
    }
    // Validate referenced global records before changing the target image.
    for (int slot = 0; slot < rows; ++slot) {
        const uint8_t id = h.state[base + size_t(slot) * kRecord + 0x0f];
        if (!id) continue;
        if (id > kHokPlayers) throw FormatError("classic roster: invalid player id");
        (void)record(h, kPlayers + size_t(id - 1) * kPlayerRecord, kPlayerRecord);
    }

    canvas.frame(1, 14, contracts ? 11 : lineup ? 10 : 7, 11);
    const char* heading = contracts
        ? "ART   NAME                  SP  ST  TO  STATUS      E  V.DAUER       WERT"
        : lineup ? "NR ART   NAME                  SP  STÄRKEN  TO  2m 5m  STATUS      E  "
                 : "ART   NAME                  SP  STÄRKEN  TO  E  ";
    canvas.text(kMini, 7, 23, heading, 7);
    const int rule_right = contracts ? 251 : lineup ? 232 : 152;
    canvas.line(7, 25, rule_right, 25, 7);
    canvas.line(7, 209, rule_right, 209, 7);

    for (int slot = 0; slot < rows; ++slot) {
        const uint8_t* r = h.state.data() + base + size_t(slot) * kRecord;
        if (!r[0x0f]) continue; // an empty slot keeps its vertical gap, as in the original
        const uint8_t* p = h.state.data() + kPlayers + size_t(r[0x0f] - 1) * kPlayerRecord;
        const int role = std::min(int(p[0x18]) / 25, 2); // position byte: 0..24 TOR, 25..49 ABW, 50.. STU
        const uint8_t color = row_color(r, role, view);
        const int baseline = 33 + 6 * slot;
        const auto draw = [&](int x, const std::string& value, uint8_t ink) {
            canvas.text(kMini, x, baseline, value, ink);
        };
        constexpr std::array<const char*, 3> roles{"TOR", "ABW", "STU"};
        // The line-up list starts with NR, which takes 11 pixels.
        const int shift = lineup ? 11 : 0;
        if (lineup && r[0x0a]) draw(7, number(slot + 1, 2), color);
        draw(7 + shift, roles[size_t(role)], r[0x0c] ? 19 : color);
        draw(25 + shift, iso646de_to_utf8(reinterpret_cast<const char*>(p), 18), color);
        draw(82 + shift, number(int(r[6]) + r[7] + r[8], 2), color);
        if (contracts)
            draw(96, number((int(r[0x10]) + r[0x11] + r[0x12]) / 3, 2), color);
        else
            draw(96 + shift, number(r[0x10], 2) + " " + number(r[0x11], 2) + " " + number(r[0x12], 2), color);
        draw(contracts ? 108 : 132 + shift, role == 0 ? "  -" : number(int(r[3]) + r[4] + r[5], 2), color);
        if (lineup) draw(156, number(r[1], 2) + " " + number(r[0], 2), color);
        if (contracts || lineup) {
            uint8_t status_color = color;
            const std::string value = status(r, status_color);
            draw(contracts ? 121 : 183, value, status_color);
        } else if ((r[0x09] & 3) == 2) {
            canvas.line(5, baseline - 4, 5, baseline, 16);
        }
        draw(contracts ? 156 : lineup ? 218 : 140, "  " + number(int(r[0x13]) * 10 / 15, 2),
             r[0x13] > 130 ? 16 : color);
        if (contracts) {
            draw(177, duration(r), color);
            // Monthly salary (i32 at +0x28) in DM (internal units / 3), width 7, without thousands
            // separators (unlike the cash display in the header).
            const int salary = int(static_cast<int32_t>(word32(r + 0x28))) / 3;
            draw(212, number(salary, 7) + " DM", color);
        }
    }
}

} // namespace em::ui
