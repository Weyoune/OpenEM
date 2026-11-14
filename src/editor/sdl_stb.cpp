// SPDX-License-Identifier: GPL-3.0-only
// Compiles the implementations of the single-header libraries stb_image (picture import) and stb_image_write (its
// zlib compressor is used by the indexed PNG writer in sdl_image_io.cpp). Kept in a file of its own so the
// third-party code can be compiled without warnings (see CMakeLists.txt).
#define STBI_NO_STDIO      // files are read through std::filesystem paths (UTF-8 safe on every system)
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
