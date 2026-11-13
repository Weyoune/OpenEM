// SPDX-License-Identifier: GPL-3.0-only
// Transfer list hover help (see classic_transfer_hover.hpp).
#include "ui/classic_transfer_hover.hpp"

#include <algorithm>
#include <utility>

namespace em::ui {
namespace {
// kDgroup: file offset of the game's data segment in EM.EXE (0x1E00-byte MZ header + segment start).
constexpr size_t kDgroup = 0x16dd0, kRecord = 52;
// Save state: transfer pool (squad 4), player table (id 1 first), team records, manager blocks.
constexpr size_t kPool = 0x696d + 4 * 0x618, kPlayers = 0x4fc6;
constexpr size_t kTeams = 0x17e7, kManagers = 0x0dcd;

// Bounds-checked pointer into the save state.
const uint8_t* record(const HokFile& h, size_t at, size_t length) {
    if (at > h.state.size() || length > h.state.size() - at)
        throw FormatError("classic transfer hover: truncated native record");
    return h.state.data() + at;
}

// Text field of capacity bytes at offset at, up to the first NUL.
std::string native_name(const HokFile& h, size_t at, size_t capacity) {
    const uint8_t* p = record(h, at, capacity);
    size_t length = 0;
    while (length < capacity && p[length]) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

// Upper case in code page 437 as the original does for manager names: a..z and the umlauts ü, ä, ö.
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

// The NUL-terminated string a far pointer (offset, segment) at data-segment offset offset points to; the
// file position is 0x1E00 + segment * 16 + offset.
std::string original_text(const Bytes& exe, size_t offset) {
    const size_t at = kDgroup + offset;
    const size_t target = 0x1e00 + size_t(le16(exe, at + 2)) * 16 + le16(exe, at);
    if (target >= exe.size()) throw FormatError("classic transfer hover: invalid original text pointer");
    const auto end = std::find(exe.begin() + std::ptrdiff_t(target), exe.end(), uint8_t(0));
    if (end == exe.end()) throw FormatError("classic transfer hover: unterminated original text");
    return std::string(exe.begin() + std::ptrdiff_t(target), end);
}
}

ClassicTransferHelp::ClassicTransferHelp(const Bytes& exe) {
    if (exe.size() < kDgroup + 0xd80 || le16(exe, 0) != 0x5a4d)
        throw FormatError("classic transfer hover: incompatible EM.EXE layout");
    from_ = original_text(exe, 0xd70);
    country_ = original_text(exe, 0xd7c);
}

ClassicTransferHover classic_transfer_hover(const HokFile& h, int manager,
    int x, int y, const ClassicTransferHelp& help) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("classic transfer hover: invalid manager");
    // The active manager does not enter the original's ownership-label branch.
    // The owner comes directly from the global player's byte +0x1A (4 = a club, else a manager).
    const uint8_t* pool = record(h, kPool, 12 * kRecord);
    int count = 0;
    for (int slot = 0; slot < 12; ++slot)
        if (pool[size_t(slot) * kRecord + 15]) ++count;
    const int64_t row = (int64_t(y) - 36) / 16;
    if (x <= 175 || row < 0 || row >= count) return {};
    const int id = pool[size_t(row) * kRecord + 15];
    // Id 0 in a slot gap reads the record before player 1, as the original's pointer arithmetic does.
    const size_t at = size_t(int64_t(kPlayers) + (id - 1) * int64_t(33));
    const uint8_t* player = record(h, at, 33);
    std::string text = id > 179 ? help.country_ : help.from_;
    if (player[0x1a] == 4)
        text += native_name(h, kTeams + size_t(player[0x1d]) * 61, 22);
    else
        text += uppercase(native_name(h, kManagers + size_t(player[0x1a]) * 0x1af, 20));
    return {int(row), std::move(text)};
}

void draw_classic_transfer_hover(ClassicCanvas& canvas,
    const ClassicTransferHover& hover, bool drawRules, bool drawHelp) {
    if (hover.row < -1 || hover.row >= 12)
        throw FormatError("classic transfer hover: invalid hover row");
    if (hover.row < 0) return;
    if (drawRules) {
        canvas.line(180, 36 + 16 * hover.row, 314, 36 + 16 * hover.row, 16);
        canvas.line(180, 52 + 16 * hover.row, 314, 52 + 16 * hover.row, 16);
    }
    if (drawHelp) {
        canvas.rect(7, 211, 163, 217, 4);
        canvas.text_native(ClassicFontId::Mini, 1, 216, hover.native_help, 7, 0, 168);
    }
}

} // namespace em::ui
