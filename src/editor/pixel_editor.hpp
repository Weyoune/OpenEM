// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the pixel editor for the KB\*.vga images: loading, drawing tools, undo/redo, saving.
// No user interface in here (the Windows front end is win_main.cpp), so it can be tested without a window.
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "formats/vga.hpp"

namespace em::editor {

// Editor state for one KB picture at a time, plus the list of all editable pictures of the game directory.
class PixelEditor {
public:
    // How a file is shown and saved: Image = RLE picture with header (optionally with own palette); Tiles / Sprites =
    // packed KB block of raw 16 x 16 / 32 x 32 cells, laid out as a grid sheet; Panel = the goal scene scoreboard (KB246).
    enum class Kind { Image, Tiles, Sprites, Panel };
    // One entry of the file list.
    struct Entry {
        int number;     // n of KB\<n>.vga
        Kind kind;
        int w, h;       // size of the picture shown (sheets: the laid-out sheet)
        bool embedded;  // image carries its own palette
        std::string label;  // list text, "KB<n>" plus a description for the goal scene files
    };
    // Drawing tools. Fill = flood fill of the 4-connected area of equal colour; Picker = take the colour under the mouse.
    enum class Tool { Pencil, Line, Rect, FilledRect, Fill, Picker };

    // Scans <data>/KB/0.vga..255.vga and lists every file that is a picture, a goal scene sheet or the panel.
    // Throws when KB\21.vga (the game palette) cannot be read.
    explicit PixelEditor(const std::filesystem::path& data);

    const std::vector<Entry>& files() const { return files_; }
    // Opens KB\<number>.vga, re-reading the palettes first (they may have been edited in the palette editor).
    // Clears the undo history. Returns false and sets status() when the file is no picture or cannot be read.
    bool load(int number);
    void close();  // drop the open picture (no saving)
    Kind kind() const { return kind_; }
    int cell_size() const { return cell_; }     // tile or sprite edge, 0 for plain images
    int cell_columns() const { return cols_; }  // cells per row of a sheet
    bool save();                                       // writes KB\<n>.vga, first time with a .bak copy
    bool export_bmp(const std::filesystem::path& to);  // 8-bit indexed BMP with the current palette
    // Replaces the picture by 32-bit pixels (0xAARRGGBB, row by row): each pixel takes the nearest colour of the
    // current palette (colours 0..254; transparent pixels become colour 0). A different size is accepted for plain images
    // only when `allow_resize` is set (the undo history is then cleared); sheets and the panel must match.
    // `approximated` receives the number of pixels without an exact palette match.
    bool import_argb(const std::vector<uint32_t>& argb, int w, int h, bool allow_resize, int* approximated = nullptr);
    Kind picture_kind() const { return kind_; }
    // Undo/redo of whole-picture snapshots (at most 64 undo steps).
    void undo();
    void redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    void set_tool(Tool t) { tool_ = t; }
    Tool tool() const { return tool_; }
    void set_color(int c) { color_ = c & 255; }  // drawing colour, palette index 0..255
    int color() const { return color_; }

    // Strokes in image coordinates (the canvas converts the mouse position). stroke_begin starts a stroke inside the
    // picture (the picker only takes the colour, fill finishes at once); stroke_move extends the pencil line or redraws
    // the line/rectangle preview from the start point (clamped to the picture); stroke_end closes the stroke as one
    // undo step, or drops the snapshot when nothing changed.
    void stroke_begin(int x, int y);
    void stroke_move(int x, int y);
    void stroke_end();
    bool drawing() const { return drawing_; }
    int pick(int x, int y) const;  // colour index at a pixel, -1 outside

    bool dirty() const { return dirty_; }                 // unsaved changes exist
    int current() const { return current_; }              // number of the open file, -1 when none
    const Image& image() const { return img_; }           // the picture (sheets: laid out as a grid)
    const Palette& palette() const { return pal_; }       // palette used for display, BMP export and import
    // Images without their own palette are shown with a palette chosen here: KB21 (game), KB247 (goal scenes) or the
    // palette of a full screen (KB5, 14, 16, 151, 195, 197) - the game keeps the palette of the last screen it loaded,
    // e.g. KB8 (stadium parts) is drawn over KB14. The choice does not change what is saved.
    std::vector<int> palette_choices() const;  // the palette sources of the list above that exist on disk
    int palette_source() const { return pal_source_; }  // KB number the display palette comes from
    // Switches the display palette to KB\<number>.vga (21, 247, or a picture with own palette). Refused (false) when
    // the open picture has its own palette or the file holds no palette.
    bool set_palette_source(int number);
    bool has_own_palette() const { return has_embedded_; }
    const std::string& status() const { return status_; }  // last status or error message (German, shown in the UI)
    void set_status(std::string s) { status_ = std::move(s); }
    // Bumped on every change of the picture; front ends redraw when it differs from what they showed
    unsigned revision() const { return revision_; }

private:
    std::filesystem::path kb_;  // <game>/KB
    std::vector<Entry> files_;
    Palette global_pal_, tore_pal_, pal_;  // KB21, KB247 and the palette currently used for display
    bool has_embedded_ = false;
    int pal_source_ = 21;
    Image img_;
    int current_ = -1;
    Kind kind_ = Kind::Image;
    int cell_ = 0, cols_ = 0, count_ = 0;  // sheet layout: cell edge, columns, used cells
    bool dirty_ = false;
    std::string status_;
    unsigned revision_ = 0;

    Tool tool_ = Tool::Pencil;
    int color_ = 1;
    std::vector<std::vector<uint8_t>> undo_, redo_;
    bool drawing_ = false;
    int sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0;  // stroke start and last position
    std::vector<uint8_t> base_;              // pixels at stroke start (the line/rectangle preview redraws on it)
    std::unordered_set<uint32_t> cache_inexact_;  // scratch of import_argb

    void put(int x, int y, int c);  // sets one pixel, ignores positions outside
    void draw_line(int x0, int y0, int x1, int y1, int c);  // Bresenham line, both end points included
    void draw_rect(int x0, int y0, int x1, int y1, int c, bool filled);  // outline or filled, corners in any order
    void flood(int x, int y, int c);  // 4-connected flood fill
    void push_undo();  // snapshot for undo (at most 64), clears redo
};

}  // namespace em::editor
