// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Little helpers for reading the original little-endian data files of EM.EXE
// (16-bit Borland C++ program: all multi-byte values are little-endian, ints are 16 bit).
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace em {

using Bytes = std::vector<uint8_t>;  // raw file or memory contents

// Thrown for unreadable or malformed game files (and for texts that cannot be encoded).
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The existing path that matches path when upper and lower case are ignored, component by component (the original
// game comes from DOS, so a copy may be spelt EM.exe, KB/21.VGA or kb/21.vga; Linux file systems are case sensitive).
// Returns path unchanged when it exists as given or no matching entry is found.
std::filesystem::path resolve_case(const std::filesystem::path& path);

// Reads a whole file (spelling resolved with resolve_case); throws FormatError when it cannot be opened.
Bytes read_file(const std::filesystem::path& path);

// Unsigned little-endian 16/32-bit value at offset o (bounds-checked, throws std::out_of_range).
inline uint16_t le16(const Bytes& b, size_t o) { return uint16_t(b.at(o) | b.at(o + 1) << 8); }
inline uint32_t le32(const Bytes& b, size_t o) { return uint32_t(le16(b, o)) | uint32_t(le16(b, o + 2)) << 16; }

// The game's texts use ISO 646-DE like the later BMH: [ \ ] { | } ~ are Ä Ö Ü ä ö ü ß
// (seen in EM.EXE strings such as "TORE IN ]BERZAHL", "<TASTE DR]CKEN>"). Some names use the
// CP437 umlauts instead (0x8E 0x99 0x9A 0x84 0x94 0x81 0xE1); both are converted.
// Reads up to `max_len` bytes or up to the first zero byte.
std::string iso646de_to_utf8(const char* s, size_t max_len = std::string::npos);
// Inverse: Ä Ö Ü ä ö ü ß -> [ \ ] { | } ~. Throws FormatError for any other non-ASCII character.
std::string utf8_to_iso646de(const std::string& s);

}  // namespace em
