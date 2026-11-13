// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ui/classic_font.hpp"
#include <map>
#include <optional>

// ClassicCanvas: the drawing surface of the classic (original-look) UI.
// A 320x240 image of palette indices plus the active 256-colour palette,
// with the primitive operations the original screens are built from
// (background pictures, atlas blits, filled/outlined rectangles, lines,
// framed panels and bitmap text). Screens render into it and the frontend
// converts the result to ARGB or a BMP.
namespace em::ui {

class ClassicCanvas {
public:
    // data: the directory holding the user's original game files (EM.EXE
    // and the KB folder). Throws if the files are missing or incompatible.
    explicit ClassicCanvas(const std::filesystem::path& data);
    Image& pixels() { return pixels_; }
    const Image& pixels() const { return pixels_; }
    const Palette& palette() const { return palette_; }
    const ClassicFont& font() const { return font_; }
    const std::filesystem::path& data() const { return data_; }
    // Returns picture KB/<id>.vga, decoding and caching it on first use. If
    // the file carries its own palette, that palette is cached as well.
    const Image& asset(int id);
    // Clears the screen, selects the picture's own palette (or the base
    // palette if it has none) and draws picture <id> at the origin.
    void background(int id = 22);
    // Restores the base palette from KB/21.vga.
    void reset_palette();
    // Sets one palette entry from 6-bit VGA DAC components (0..63 each;
    // higher bits are masked), expanded to 8 bits like the hardware does.
    void set_dac(int index, int r, int g, int b);
    // Overrides the colour of the highlight edge of the button graphic in
    // the KB/4 atlas (top row y=180, x 18..71, and left column x=18,
    // y 181..188). The game patches this edge in memory when a screen wants
    // a differently coloured button, and the patch stays for the session.
    void set_button_edge(uint8_t color) { button_edge_=color; }
    // Copies the rectangle (sx,sy,width,height) of picture <id> to (x,y).
    // A negative width/height means "to the right/bottom edge of the
    // source". With transparent=true, pixels equal to key are skipped.
    // Everything is clipped against both source and screen.
    void blit(int id, int x, int y, int sx = 0, int sy = 0, int width = -1, int height = -1, bool transparent = false, uint8_t key = 0);
    // Fills the inclusive rectangle [left..right]x[top..bottom], clipped.
    void rect(int left, int top, int right, int bottom, uint8_t color);
    // Draws the four edges of the inclusive rectangle with line().
    void outline(int left, int top, int right, int bottom, uint8_t color);
    // Draws a line with the original game's stepping (see the .cpp).
    void line(int x0, int y0, int x1, int y1, uint8_t color);
    // Draws a framed panel from the 24x19 tiles in the KB/4 atlas:
    // columns x rows tiles (2..14 by 2..13) with corners, edges and filled
    // centre. Throws FormatError on other sizes.
    void frame(int x, int y, int columns, int rows);
    // Draws UTF-8 text with its baseline at y=baseline. color 0 means
    // opaque black. shadow >= 0 adds a one-pixel down-right shadow in that
    // colour. right >= 0 centres the text between x and right.
    void text(ClassicFontId face, int x, int baseline, std::string_view utf8, uint8_t color,
              int shadow = -1, int right = -1);
    // Same as text() for a string already in the game's own 8-bit encoding
    // (e.g. names copied from the savegame).
    void text_native(ClassicFontId face, int x, int baseline, std::string_view native, uint8_t color,
                     int shadow = -1, int right = -1);
    // Draws the mouse cursor with its hot spot at (x,y).
    void cursor(int x,int y);
    // The screen as 0xAARRGGBB pixels, row by row.
    std::vector<uint32_t> argb() const;
    // The screen as a 24-bit bottom-up Windows BMP file.
    Bytes bmp_bytes() const;

private:
    std::filesystem::path data_;              // directory of the original game files
    Image pixels_{320,240};                   // the framebuffer (palette indices)
    Palette base_palette_, palette_;          // KB/21.vga palette and the active one
    ClassicFont font_;                        // fonts read from EM.EXE
    Image cursor_;                            // mouse cursor bitmap (0 = transparent)
    int cursor_dx_=0,cursor_dy_=0;            // cursor hot-spot offset
    std::optional<uint8_t> button_edge_;      // session-wide KB/4 button-edge colour override
    std::map<int,Image> assets_;              // decoded pictures by KB id
    std::map<int,Palette> asset_palettes_;    // palettes embedded in pictures
};
}  // namespace em::ui
