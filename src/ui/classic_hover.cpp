// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_hover.hpp"

#include <algorithm>

namespace em::ui {
namespace {
constexpr size_t kDgroup = 0x16dd0, kSquads = 0x696d, kSquadStride = 0x618;
constexpr size_t kRecord = 52, kPlayers = 0x4fc6, kPlayerRecord = 33;
struct Layout { uint32_t mask; int right; };

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

std::string name(const uint8_t* p) {
    size_t length = 0;
    while (length < 18 && p[length]) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

std::string uppercase(std::string text) {
    //645B:0C8E: ASCII letters plus three CP437 aliases. ISO646 braces
    //remain unchanged; a Unicode uppercase operation would differ.
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

ClassicRosterHelp::ClassicRosterHelp(const Bytes& exe) : exe_(exe) {
    if (exe_.size() < kDgroup + 0x50c7 || le16(exe_, 0) != 0x5a4d)
        throw FormatError("classic hover: incompatible EM.EXE layout");
    for (size_t field = 0; field < widths_.size(); ++field) {
        widths_[field] = int(int8_t(exe_[kDgroup + 0x39f + field]));
        if (widths_[field] <= 0)
            throw FormatError("classic hover: invalid native field width");
    }
    // Cache all normal branches; pointer() still handles the original
    // unguarded trend/status-table arithmetic with bounded EXE reads.
    for (int p = 0x984; p <= 0x9d4; p += 4) strings_.emplace(p, pointer(p));
    for (int p = 0xa00; p < 0xa70; p += 4) strings_.emplace(p, pointer(p));
    for (int p : {0x960, 0x964, 0xb80, 0xb84, 0xb88, 0xb8c, 0xb90, 0xb94,
                  0xd34, 0xd38, 0xd3c, 0xd40, 0xd4c, 0x11bd, 0x11c1, 0xc48})
        strings_.emplace(p, pointer(p));
}

std::string ClassicRosterHelp::literal(int offset) const {
    if (offset < 0 || size_t(offset) > exe_.size() - kDgroup)
        throw FormatError("classic hover: invalid literal offset");
    const size_t at = kDgroup + size_t(offset);
    auto end = std::find(exe_.begin() + std::ptrdiff_t(at), exe_.end(), uint8_t(0));
    if (end == exe_.end()) throw FormatError("classic hover: unterminated original text");
    return std::string(exe_.begin() + std::ptrdiff_t(at), end);
}

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
    // The native helper does not reject a gap's ID0. Its one-based far
    // pointer then refers to the preceding33 bytes; keep that bounded read.
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
