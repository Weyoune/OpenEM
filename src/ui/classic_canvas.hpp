// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ui/classic_font.hpp"
#include <map>
#include <optional>

namespace em::ui {

class ClassicCanvas {
public:
    explicit ClassicCanvas(const std::filesystem::path& data);
    Image& pixels() { return pixels_; }
    const Image& pixels() const { return pixels_; }
    const Palette& palette() const { return palette_; }
    const ClassicFont& font() const { return font_; }
    const std::filesystem::path& data() const { return data_; }
    const Image& asset(int id);
    void background(int id = 22);
    void reset_palette();
    void set_dac(int index, int r, int g, int b);
    void set_button_edge(uint8_t color) { button_edge_=color; }
    void blit(int id, int x, int y, int sx = 0, int sy = 0, int width = -1, int height = -1, bool transparent = false, uint8_t key = 0);
    void rect(int left, int top, int right, int bottom, uint8_t color);
    void outline(int left, int top, int right, int bottom, uint8_t color);
    void line(int x0, int y0, int x1, int y1, uint8_t color);
    void frame(int x, int y, int columns, int rows);
    void text(ClassicFontId face, int x, int baseline, std::string_view utf8, uint8_t color,
              int shadow = -1, int right = -1);
    void text_native(ClassicFontId face, int x, int baseline, std::string_view native, uint8_t color,
                     int shadow = -1, int right = -1);
    void cursor(int x,int y);
    std::vector<uint32_t> argb() const;
    Bytes bmp_bytes() const;

private:
    std::filesystem::path data_;
    Image pixels_{320,240};
    Palette base_palette_, palette_;
    ClassicFont font_;
    Image cursor_;
    int cursor_dx_=0,cursor_dy_=0;
    std::optional<uint8_t> button_edge_; //native process-local KB4 atlas patch
    std::map<int,Image> assets_;
    std::map<int,Palette> asset_palettes_;
};
}  // namespace em::ui
