// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_preview.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace em::ui {
namespace {
constexpr size_t kPreviewBytes = 0xb0;
constexpr size_t kRecordStart = 0x10, kRecordBytes = 40, kNameBytes = 19;
// DS09EC has five entries; club managers normally use the first three.
constexpr std::array<const char*, 5> kLeagues{
    "Bundesliga", "Zweite Liga", "Oberliga", "", "L{nderteams"};

std::string native_name(const Bytes& bytes, size_t at) {
    size_t length = 0;
    while (length < kNameBytes && bytes[at + length]) ++length;
    return std::string(reinterpret_cast<const char*>(bytes.data() + at), length);
}

void validate(const ClassicPreview& preview) {
    if (preview.version != "KRON_0.99" || preview.managers.size() > 4)
        throw FormatError("classic preview: invalid version/manager count");
    for (const auto& manager : preview.managers)
        if ((!manager.world_cup && manager.league >= kLeagues.size()) ||
            manager.manager.size() > kNameBytes || manager.club.size() > kNameBytes)
            throw FormatError("classic preview: invalid active manager record");
}
}

ClassicPreview decode_classic_preview(const Bytes& headerOrFile) {
    // The original always reads all four records, including opaque unused
    // bytes. Require their extent, but interpret only the active count.
    if (headerOrFile.size() < kPreviewBytes)
        throw FormatError("classic preview: truncated HOK header");
    if (std::memcmp(headerOrFile.data(), "KRON_0.99", 10) != 0)
        throw FormatError("classic preview: no KRON_0.99 savegame");
    const size_t count = headerOrFile[0x0e];
    if (count > 4) throw FormatError("classic preview: invalid manager count");

    ClassicPreview preview;
    const uint32_t rawYear = le32(headerOrFile, 0x0a);
    const int64_t signedYear = rawYear > 0x7fffffffu ? int64_t(rawYear) - 0x100000000LL : rawYear;
    preview.year = int32_t(signedYear);
    preview.matchday = headerOrFile[0x0f];
    preview.managers.reserve(count);
    for (size_t manager = 0; manager < count; ++manager) {
        const size_t at = kRecordStart + manager * kRecordBytes;
        ClassicPreviewRecord record;
        record.league = headerOrFile[at];
        record.world_cup = headerOrFile[at + 1] != 0;
        record.manager = native_name(headerOrFile, at + 2);
        record.club = native_name(headerOrFile, at + 21);
        preview.managers.push_back(std::move(record));
    }
    validate(preview);
    return preview;
}

void draw_classic_preview(ClassicCanvas& canvas, const ClassicPreview& preview,
                          std::string_view virtualDOSpath) {
    validate(preview); // Do not partially paint malformed manually built data.
    constexpr auto mini = ClassicFontId::Mini;
    std::string title = "SPIELSTAND:";
    title.append(virtualDOSpath.substr(0, virtualDOSpath.find('\0')));
    const std::string metadata = " Jahr " + std::to_string(preview.year) + ", " +
        std::to_string(unsigned(preview.matchday)) + ".Spieltag";

    canvas.frame(71, 71, 8, 3);
    canvas.text_native(mini, 72, 81, title, 7, 0, 261);
    canvas.line(77, 83, 254, 83, 7);
    canvas.text_native(mini, 72, 90, metadata, 1, 0, 261);
    for (size_t manager = 0; manager < preview.managers.size(); ++manager) {
        const auto& record = preview.managers[manager];
        const int baseline = 99 + 7 * int(manager);
        canvas.text_native(mini, 76, baseline, record.manager, 1, 0);
        canvas.text_native(mini, 133, baseline, record.club, 1, 0);
        if (!record.world_cup)
            canvas.text_native(mini, 212, baseline, kLeagues[record.league], 1, 0);
    }
}

} // namespace em::ui
