// SPDX-License-Identifier: GPL-3.0-only
// File reading and the conversion between the game's 8-bit character set and UTF-8 (see binary.hpp).
#include "formats/binary.hpp"

#include <fstream>
#include <iterator>

namespace em {

namespace {
// ASCII-only case folding of a file name (the game's names are ASCII).
std::string fold(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
}  // namespace

std::filesystem::path resolve_case(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || fs::exists(path, ec)) return path;
    // walk from the root (or the current folder for relative paths) and look up every missing component
    fs::path resolved = path.root_path();
    for (const fs::path& part : path.relative_path()) {
        fs::path next = resolved / part;
        if (!fs::exists(next, ec)) {
            const fs::path folder = resolved.empty() ? fs::path(".") : resolved;
            const std::string wanted = fold(part.string());
            bool found = false;
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                if (fold(it->path().filename().string()) == wanted) {
                    next = resolved / it->path().filename();
                    found = true;
                    break;
                }
            }
            if (!found) return path;
        }
        resolved = next;
    }
    return resolved;
}

Bytes read_file(const std::filesystem::path& given) {
    const std::filesystem::path path = resolve_case(given);
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FormatError("cannot open " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

std::string iso646de_to_utf8(const char* s, size_t max_len) {
    std::string out;
    for (size_t i = 0; i < max_len && s[i] != '\0'; i++) {
        switch (s[i]) {
            case '[': out += "Ä"; break;
            case '\\': out += "Ö"; break;
            case ']': out += "Ü"; break;
            case '{': out += "ä"; break;
            case '|': out += "ö"; break;
            case '}': out += "ü"; break;
            case '~': out += "ß"; break;
            // CP437 umlauts, used next to the ISO 646 forms (e.g. "K\x99LNER EC" in KB\23.vga and
            // in savegames); EM.EXE itself contains a table of these same seven characters.
            case '\x8e': out += "Ä"; break;
            case '\x99': out += "Ö"; break;
            case '\x9a': out += "Ü"; break;
            case '\x84': out += "ä"; break;
            case '\x94': out += "ö"; break;
            case '\x81': out += "ü"; break;
            case '\xe1': out += "ß"; break;
            default: out += s[i];
        }
    }
    return out;
}

std::string utf8_to_iso646de(const std::string& s) {
    static const std::pair<const char*, char> map[] = {{"Ä", '['}, {"Ö", '\\'}, {"Ü", ']'}, {"ä", '{'},
                                                       {"ö", '|'}, {"ü", '}'}, {"ß", '~'}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (uint8_t(s[i]) < 0x80) { out += s[i++]; continue; }
        bool hit = false;
        for (auto& [u, c] : map) {
            size_t n = std::char_traits<char>::length(u);
            if (s.compare(i, n, u) == 0) { out += c; i += n; hit = true; break; }
        }
        if (!hit) throw FormatError("text: character not available in the game's character set");
    }
    return out;
}

}  // namespace em
