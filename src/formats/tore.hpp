// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Goal scenes ("Torszenen"): TORE\<n>.<side><kind>[variant] scripts and the KB graphics they use.
//
// File name, as the game builds it: "tore\" + n + (".h" home goal | ".a" away goal)
//   + ("t" | "v") + optional "e" / "j" variant. n is rolled from 2..MAXSZENE; TORE\anzahl holds
//   "MAXSZENE:11|2|2" = number of normal scenes and of the e and j variants.
//
// Script layout (header, then one record per animation frame):
//   0x00  "TOR!"
//   0x04  u16be camera x, u16be camera y   (the player clamps x-16 / y-32 at 0)
//   0x08  u16be puck x, u16be puck y
//   0x0C  12 x { u8 (unused), u8 sprite, u16be x, u16be y }   start of the 12 skaters
//   0x54  frames of 28 bytes until the u16 0xAAAA end mark:
//         +0 camera move, +1 puck move, +2 u8 (always 0), +3 sound (0 = none, else sample 1..4)
//         +4 12 x { u8 sprite, u8 move }  (sprite 0 = object not drawn this frame; a zero word
//            leaves the object unchanged)
//   Move byte m: dx = (m >> 3) & 7, dy = m & 7, negated by bit 0x40 (dx) and 0x80 (dy) - so an
//   object moves at most 7 pixels per frame per axis.
// Scene coordinates are those of the portrait rink below (x 0..671, y 0..1151); goals are at the
// top (".h" scenes, camera y near 0) and bottom (".a" scenes, camera y near 870).
//
// Graphics, loaded by the goal scene player as KB\<231 + i>.vga for i = 0..13 (EM.EXE holds a table of their
// unpacked sizes):
//   231, 232  rink tile sets: u16 packed length + RLE (same scheme as images) -> 250 tiles of
//             16 x 16 each (64000 bytes); together tiles 0..499
//   233, 234  skater sprites of the two teams: packed, 40 sprites of 32 x 32 (40960 bytes)
//   235       12 further 32 x 32 sprites (packed, 12288 bytes; goalies/puck, open)
//   236       rink tile map, plain: 42 x 72 u16be (column by column in screen terms, see
//             decode_rink); entry & 0x7FFF = tile, bit 0x8000 = mirrored. 242..245 replace 236
//             for some arenas (scene argument 1..4)
//   237..241  VOC samples (formats/sound.hpp); 246 = scoreboard panel (decode_tor_panel); 247 = palette
// All 16x16 tiles and 32x32 sprites are stored row by row in screen orientation; the original
// renderer unpacks them and writes them to Mode X planes.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/vga.hpp"

namespace em {

constexpr int kTorObjects = 12;    // skaters per scene
constexpr int kRinkTile = 16;      // tile edge in pixels
constexpr int kRinkColumns = 42, kRinkRows = 72;  // portrait: 672 x 1152 pixels
constexpr int kTorSprite = 32;     // sprite edge in pixels

// A decoded move byte, -7..7 pixels per axis.
struct TorMove {
    int dx = 0, dy = 0;
};
TorMove decode_tor_move(uint8_t m);

// One skater: sprite number (0 = not drawn) and position.
struct TorObject {
    int sprite = 0;
    int x = 0, y = 0;
};

// One 28-byte frame record.
struct TorFrame {
    uint8_t camera_move = 0, puck_move = 0;  // move bytes
    uint8_t sound = 0;                       // 0 = none, else sample 1..4 (KB238..241)
    std::array<uint8_t, kTorObjects> sprite{};
    std::array<uint8_t, kTorObjects> move{};
};

// A whole script: start positions and frames.
struct TorScene {
    int camera_x = 0, camera_y = 0;
    int puck_x = 0, puck_y = 0;
    std::array<TorObject, kTorObjects> objects{};
    std::vector<TorFrame> frames;
};

// Parses a TORE script. Throws FormatError without the "TOR!" tag or without the 0xAAAA end mark.
TorScene decode_tor(const Bytes& data);

// Positions after playing frames [0, n): applies all moves as the game's scene player does. Every frame sets each
// object's sprite; a frame whose sprite and move bytes are both 0 leaves that object where it is.
struct TorState {
    int camera_x = 0, camera_y = 0, puck_x = 0, puck_y = 0;
    std::array<TorObject, kTorObjects> objects{};
};
TorState play_tor(const TorScene& scene, size_t n);

// u16 packed length + RLE stream (KB\231..235.vga) -> raw bytes. Same RLE codes as the panel below; decoding
// stops at the 0x80 end marker or at the end of the packed length. Throws when the length exceeds the file.
Bytes unpack_kb_block(const Bytes& data);

// Inverse of unpack_kb_block: u16 packed length (RLE incl. the 0x80 end marker) + stream. Throws when the packed
// stream is longer than 65535 bytes.
Bytes pack_kb_block(const Bytes& raw);
// Inverse of decode_tor_sprites (32 x 32 images, concatenated and packed).
Bytes encode_tor_sprites(const std::vector<Image>& sprites);
// Inverse of decode_tor_panel (320 x 40, plain RLE stream with end marker, no length).
Bytes encode_tor_panel(const Image& panel);

// Rink as one portrait image from the two tile sets (231, 232) and a tile map (236 or 242..245). Cells with a
// tile number beyond the tile sets stay colour 0. Throws when the map is shorter than 42 x 72 entries.
Image decode_rink(const Bytes& tiles_a, const Bytes& tiles_b, const Bytes& map);

// Sprite sheet (233, 234, 235) as a column of 32 x 32 images.
std::vector<Image> decode_tor_sprites(const Bytes& data);

// KB\246.vga: the panel of the goal scene (the bottom 40 lines, shown through the VGA split screen), a plain RLE stream
// without length (c < 0x80 -> c + 1 literal bytes, c > 0x80 -> (c & 0x7F) + 1 copies of the next byte,
// 0x80 ends) of 320 x 40 pixels row by row. Image::complete is false when the stream ends early.
constexpr int kTorPanelWidth = 320, kTorPanelHeight = 40;
Image decode_tor_panel(const Bytes& data);

// The puck (object type 1) and type 0 as the 5 x 5 bitmaps built into the game's renderer, 0 = transparent.
extern const std::array<uint8_t, 25> kTorPuck, kTorDot;

}  // namespace em
