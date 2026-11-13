// SPDX-License-Identifier: GPL-3.0-only
// Footer help of the squad lists (contracts, line-up, transfer pages): when
// the mouse is over a column of a player row, the game prints an
// explanation of that column in the footer, partly with the player's
// values, and marks the row with two rules. The help texts and column
// widths are read from the user's EM.EXE.
#include "ui/classic_hover.hpp"

#include <algorithm>

namespace em::ui {
namespace {
// kDgroup: file offset in EM.EXE of the game's data segment (0x1E00-byte MZ
// header + 0x14FD0), which holds the help texts, the text pointer table and
// the column widths; "data offset" below means relative to it. Save state:
// squads of the
// managers at 0x696D (0x618 bytes each, 52-byte player records), global
// player records of 33 bytes at 0x4FC6 (indexed by 1-based player id).
constexpr size_t kDgroup = 0x16dd0, kSquads = 0x696d, kSquadStride = 0x618;
constexpr size_t kRecord = 52, kPlayers = 0x4fc6, kPlayerRecord = 33;
// Columns shown by a list (bit n = column n present) and its right edge.
struct Layout { uint32_t mask; int right; };

// Column layout of each squad list.
Layout layout(ClassicRosterKind kind) {
    switch (kind) {
    case ClassicRosterKind::Contracts: return {0x3d146, 255};
    case ClassicRosterKind::Lineup: return {0xf0c7, 233};
    case ClassicRosterKind::Transfer: return {0x90c6, 155};
    }
    throw FormatError("classic hover: invalid roster kind");
}

const uint8_t* record(const HokFile& h, size_t at, size_t length) {
    if (at > h.state.size() || length > h.state.size() - at)
        throw FormatError("classic hover: truncated native record");
    return h.state.data() + at;
}

// NUL-terminated name of at most 18 bytes.
std::string name(const uint8_t* p) {
    size_t length = 0;
    while (length < 18 && p[length]) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

// The game's uppercase conversion: ASCII letters plus the three code page
// 437 umlauts ü, ä, ö. The ISO 646 braces used for umlauts elsewhere stay
// unchanged; a Unicode uppercase operation would differ.
std::string uppercase(std::string text) {
    for (char& c : text) {
        uint8_t byte = uint8_t(c);
        if (byte >= 'a' && byte <= 'z') byte -= 32;
        else if (byte == 0x81) byte = 0x9a;
        else if (byte == 0x84) byte = 0x8e;
        else if (byte == 0x94) byte = 0x99;
        c = char(byte);
    }
    return text;
}
}

// Reads the 18 column widths (signed bytes at data offset 0x39F) and caches
// the help texts that are reached through far pointers in the data area.
ClassicRosterHelp::ClassicRosterHelp(const Bytes& exe) : exe_(exe) {
    if (exe_.size() < kDgroup + 0x50c7 || le16(exe_, 0) != 0x5a4d)
        throw FormatError("classic hover: incompatible EM.EXE layout");
    for (size_t field = 0; field < widths_.size(); ++field) {
        widths_[field] = int(int8_t(exe_[kDgroup + 0x39f + field]));
        if (widths_[field] <= 0)
            throw FormatError("classic hover: invalid native field width");
    }
    // Cache all normal branches; pointer() still handles the game's
    // unchecked index arithmetic into the trend/status tables with bounded
    // reads from the executable.
    for (int p = 0x984; p <= 0x9d4; p += 4) strings_.emplace(p, pointer(p));
    for (int p = 0xa00; p < 0xa70; p += 4) strings_.emplace(p, pointer(p));
    for (int p : {0x960, 0x964, 0xb80, 0xb84, 0xb88, 0xb8c, 0xb90, 0xb94,
                  0xd34, 0xd38, 0xd3c, 0xd40, 0xd4c, 0x11bd, 0x11c1, 0xc48})
        strings_.emplace(p, pointer(p));
}

// NUL-terminated string stored directly at data offset `offset`.
std::string ClassicRosterHelp::literal(int offset) const {
    if (offset < 0 || size_t(offset) > exe_.size() - kDgroup)
        throw FormatError("classic hover: invalid literal offset");
    const size_t at = kDgroup + size_t(offset);
    auto end = std::find(exe_.begin() + std::ptrdiff_t(at), exe_.end(), uint8_t(0));
    if (end == exe_.end()) throw FormatError("classic hover: unterminated original text");
    return std::string(exe_.begin() + std::ptrdiff_t(at), end);
}

// String referenced by the far pointer (u16 offset, u16 segment) stored at
// data offset `offset`; the segment is relative to the end of the 0x1E00-byte
// MZ header.
std::string ClassicRosterHelp::pointer(int offset) const {
    const auto cached = strings_.find(offset);
    if (cached != strings_.end()) return cached->second;
    if (offset < 0 || size_t(offset) > exe_.size() - kDgroup - 4)
        throw FormatError("classic hover: invalid text pointer offset");
    const size_t at = kDgroup + size_t(offset);
    const size_t target = 0x1e00 + size_t(le16(exe_, at + 2)) * 16 + le16(exe_, at);
    if (target >= exe_.size()) throw FormatError("classic hover: invalid original text pointer");
    auto end = std::find(exe_.begin() + std::ptrdiff_t(target), exe_.end(), uint8_t(0));
    if (end == exe_.end()) throw FormatError("classic hover: unterminated original text");
    return std::string(exe_.begin() + std::ptrdiff_t(target), end);
}

// Rows are 6 pixels high starting at y = 29; the column is found by adding
// the widths of the list's columns from x = 7. Squad record fields used:
// +0x09 bits 0..1 the status shown in column 14, +0x0A a status flag, +0x0C
// loan flag, +0x0D a count printed for status 1, +0x0E a value graded in
// steps of 13 from 30, +0x0F player id,
// +0x10..+0x12 the three values averaged for column 7, +0x13 (shown as
// *10/15 on the line-up page), +0x15 (bits 0..6 a count shown in column 0),
// +0x17 index into a text table. Global record: name at +0, age at +0x14,
// +0x18 graded in steps of 25.
ClassicRosterHover classic_roster_hover(const HokFile& h, int manager,
    ClassicRosterKind kind, int x, int y, const ClassicRosterHelp& help) {
    const auto geometry = layout(kind);
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("classic hover: invalid manager");
    const size_t base = kSquads + size_t(manager) * kSquadStride;
    const uint8_t* squad = record(h, base, kSquadStride);
    int count = 0;
    for (int slot = 0; slot < (manager == 4 ? 12 : 29); ++slot)
        if (squad[size_t(slot) * kRecord + 15]) ++count;
    // Widen before subtracting to avoid host overflow for arbitrary input.
    const int64_t row = (int64_t(y) - 29) / 6;
    if (x < 0 || x > geometry.right || row < 0 || row >= count) return {};

    int cumulative = 0, field = 0;
    for (; field < 18; ++field) {
        if (!(geometry.mask & (1u << field))) continue;
        cumulative += help.widths_[size_t(field)];
        if (x - 7 < cumulative) break;
    }
    ClassicRosterHover result{int(row), field, {}};
    const uint8_t* r = squad + size_t(row) * kRecord;
    // The game does not reject an empty slot's id 0. Its one-based index
    // then refers to the preceding 33 bytes; keep that read, bounded.
    const auto global = [&] {
        const size_t at = size_t(int64_t(kPlayers) + (int(r[15]) - 1) * int64_t(kPlayerRecord));
        return record(h, at, kPlayerRecord);
    };
    auto& text = result.native_help;
    switch (field) {
    case 0:
        text = help.pointer(0x984);
        if (r[0x15]) text += help.pointer(0x11bd) + std::to_string(r[0x15] & 0x7f) + help.literal(0x50b9);
        break;
    case 1:
        text = help.pointer(0x988) + help.pointer(0x9cc + 4 * std::min(int(global()[0x18]) / 25, 2));
        if (r[0x0c]) text += help.pointer(0xd34);
        break;
    case 2: {
        const uint8_t* p = global();
        text = help.pointer(0x98c) + name(p) + help.literal(0x50bb) +
            std::to_string(p[0x14]) + help.pointer(0xd38);
        break;
    }
    case 14: {
        text = help.pointer(0xd3c);
        if (r[0x0a] && !(r[0x15] & 0x7f)) { text += help.pointer(0x964); break; }
        const int state = r[9] & 3;
        if (!state) { text += help.pointer(r[0x0a] ? 0x960 : 0xd40); break; }
        if (state == 1) text += help.pointer(0xc48) + std::to_string(r[0x0d]) + help.pointer(0x11c1);
        text += help.pointer(0xb84 + 4 * (2 - state));
        if (state == 2)
            text += help.literal(0x50be) + uppercase(help.pointer(0xa00 + 4 * r[0x17])) + help.literal(0x50c0);
        break;
    }
    case 15:
        text = help.pointer(0x9c0) + help.pointer(0xb8c + 4 * ((int(r[0x0e]) - 30) / 13));
        if (geometry.right == 233)
            text += help.pointer(0xd4c) + std::to_string(int(r[0x13]) * 10 / 15) + help.literal(0x50c2);
        break;
    default:
        text = help.pointer(0x984 + 4 * field);
        if (field == 7)
            text += help.literal(0x50c4) + std::to_string((int(r[0x10]) + r[0x11] + r[0x12]) / 3) + help.literal(0x50c6);
        break;
    }
    return result;
}

// Clears the footer (y 210..218, colour 4), prints the help text and, if
// drawRules, the two rules above and below the row in colour 16.
void draw_classic_roster_hover(ClassicCanvas& canvas, ClassicRosterKind kind,
    const ClassicRosterHover& hover, bool drawRules) {
    const auto geometry = layout(kind);
    if (hover.row < -1 || hover.row > 29 || hover.field < -1 || hover.field > 18)
        throw FormatError("classic hover: invalid hover result");
    canvas.rect(7, 210, geometry.right, 218, 4);
    if (hover.row < 0 || hover.field < 0) return;
    canvas.text_native(ClassicFontId::Mini, 7, 216, hover.native_help, 1, 0, geometry.right);
    if (drawRules) {
        canvas.line(7, 28 + 6 * hover.row, geometry.right + 3, 28 + 6 * hover.row, 16);
        canvas.line(7, 34 + 6 * hover.row, geometry.right + 3, 34 + 6 * hover.row, 16);
    }
}

} // namespace em::ui
