// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the hall map editor for KB\236.vga and KB\242..245.vga: the tile maps of the goal scene rink
// (42 columns x 72 rows of 16 x 16 tiles; entry = u16 big endian, tile = entry & 0x7FFF, bit 0x8000 = mirrored top to
// bottom). The tiles come from KB231 and KB232 (500 tiles), the palette is KB247. See src/formats/tore.hpp.
//
// MapDoc owns the decoded tile set, the map entries of the open file and a rendered picture of the whole rink, and it
// keeps an undo/redo history. It has no UI of its own; win_main.cpp draws it and forwards the mouse actions.
#include <filesystem>
#include <string>
#include <vector>

#include "formats/tore.hpp"
#include "formats/vga.hpp"

namespace em::editor {

// One open rink map plus its tile set and edit history.
class MapDoc {
public:
    // Map size in cells, cell size in pixels and the number of tiles in KB231 + KB232.
    static constexpr int kCols = kRinkColumns, kRows = kRinkRows, kTile = kRinkTile, kTiles = 500;
    // `data` is the game directory; the files are read from its KB subdirectory.
    explicit MapDoc(const std::filesystem::path& data);

    static const std::vector<int>& numbers();  // the editable map files: 236, 242, 243, 244, 245
    // Loads the tile set, the palette and map KB\<number>.vga, renders the rink and clears the history.
    // Returns false (and sets status()) when a file is missing or the map is too short.
    bool load(int number);
    void close();  // forgets the open map (current() becomes -1)
    // Writes the map entries back into KB\<n>.vga; bytes after the map are kept. Before the first overwrite the
    // original file is copied to KB\<n>.vga.bak. Returns false and sets status() on failure.
    bool save();
    int current() const { return current_; }             // number of the open map, -1 when none
    bool dirty() const { return dirty_; }                // unsaved changes exist
    unsigned revision() const { return revision_; }      // bumped on every change, lets the view cache its textures
    const std::string& status() const { return status_; }  // last status or error message (German, shown in the UI)

    int tile(int c, int r) const { return map_[index(c, r)] & 0x7FFF; }            // tile number of cell (c, r)
    bool mirrored(int c, int r) const { return (map_[index(c, r)] & 0x8000) != 0; }  // cell drawn upside down

    // Painting: everything between stroke_begin() and stroke_end() is one undo step; a stroke that changed
    // nothing leaves no undo entry.
    void stroke_begin();
    void stroke_end();
    // Sets cell (c, r) to `tile`, optionally mirrored. Out-of-range cells or tiles are ignored. Outside a stroke
    // the change is its own undo step.
    void set(int c, int r, int tile, bool mirror);
    // Flood fill (4-neighbourhood): replaces the connected cells whose entry equals the clicked one, one undo step.
    void fill(int c, int r, int tile, bool mirror);
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    const Image& rink() const { return rink_; }        // the whole map, 672 x 1152 (updated per cell)
    const Image& tile_image(int t) const { return tiles_[size_t(t)]; }  // 16 x 16 pixels of tile t
    int tile_count() const { return int(tiles_.size()); }
    const Palette& palette() const { return palette_; }  // KB247

private:
    std::filesystem::path kb_;                      // <game>/KB
    std::vector<Image> tiles_;                      // all tiles of KB231 followed by KB232
    Palette palette_;
    std::vector<uint16_t> map_;                     // kCols * kRows entries, column by column
    Image rink_;                                    // rendered map
    int current_ = -1;
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;
    std::vector<std::vector<uint16_t>> undo_, redo_;  // whole-map snapshots, at most 64 undo steps
    bool in_stroke_ = false, stroke_changed_ = false;

    static size_t index(int c, int r) { return size_t(c) * kRows + size_t(r); }  // the map runs down each column
    void draw_cell(int c, int r);  // re-renders one cell into rink_
    void snapshot();               // pushes the current map onto the undo stack and clears redo
};

}  // namespace em::editor
