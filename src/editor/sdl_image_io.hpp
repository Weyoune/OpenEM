// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Picture file import/export of the portable editor front end (sdl_main.cpp): reading PNG, BMP, GIF and JPEG files
// into 32-bit pixels (stb_image) and writing an 8-bit indexed PNG with the palette of the picture. This replaces
// what the Win32 front end does with the Windows Imaging Component.
#include <cstdint>
#include <filesystem>
#include <vector>

#include "formats/vga.hpp"

namespace em::editor {

// Reads any picture stb_image understands (PNG, BMP, GIF - first frame, JPEG, TGA) into 0xAARRGGBB pixels, row by row.
// Fails (false) when the file cannot be read or decoded, or when it is larger than 4096 x 4096.
bool load_argb(const std::filesystem::path& from, std::vector<uint32_t>& out, int& w, int& h);

// Writes the picture as an 8-bit indexed PNG (colour type 3) with all 256 colours of `pal` in the PLTE chunk, the same
// kind of file the Win32 editor writes. False on any failure.
bool export_png(const std::filesystem::path& to, const Image& img, const Palette& pal);

}  // namespace em::editor
