// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor, portable front end: the contents of the five tabs. Each tab has a work area in the middle
// (tab_work) and a side panel on the right (tab_side):
//   Bilder / Torszenen  zoomable picture canvas with grid and cell borders | palette chooser and colour swatches
//   Paletten            the 256 colours of KB21 or KB247                   | six-bit RGB sliders of the selected colour
//   Mannschaften        club and player names, crest editor                | jersey, crest number, place, strengths, swap
//   Hallenkarten        the rink tile map                                   | selected tile, mirror flag, tile picker
// Painting goes through the document classes exactly as in win_main.cpp; this file only converts mouse positions.
#include <algorithm>
#include <cmath>

#include "editor/sdl_app.hpp"

namespace em::editor::sdl {

// ------------------------------------------------------------------------------------------------ Texture

void Texture::reset() {
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    w_ = h_ = 0;
}

void Texture::upload_argb(SDL_Renderer* r, const uint32_t* px, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!tex_ || w != w_ || h != h_) {
        reset();
        tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!tex_) return;
        SDL_SetTextureScaleMode(tex_, SDL_ScaleModeNearest);  // sharp pixels at every zoom
        w_ = w;
        h_ = h;
    }
    SDL_UpdateTexture(tex_, nullptr, px, w * 4);
}

void Texture::upload(SDL_Renderer* r, const Image& img, const Palette& pal) {
    scratch_.resize(img.pixels.size());
    for (size_t i = 0; i < scratch_.size(); i++) {
        const auto& c = pal.rgb[img.pixels[i]];
        scratch_[i] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
    }
    upload_argb(r, scratch_.data(), img.width, img.height);
}

namespace {

using Tool = PixelEditor::Tool;

constexpr ImU32 kCanvasBg = IM_COL32(58, 60, 64, 255);
constexpr ImU32 kBlack = IM_COL32(0, 0, 0, 255), kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kCellLine = IM_COL32(232, 160, 40, 255);  // tile/sprite borders, map grid
constexpr ImU32 kHint = IM_COL32(190, 190, 195, 255);

ImU32 rgb(const std::array<uint8_t, 3>& c) { return IM_COL32(c[0], c[1], c[2], 255); }

int floordiv(float a, float b) { return int(std::floor(a / b)); }  // rounds towards minus infinity

// Right end of the content region in window coordinates; call at the start of a line.
float right_edge() { return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; }

// Continues the line so that an item of width `w` ends at `edge` (or right after the previous item if that is wider).
void same_line_right(float edge, float w) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), edge - w));
}

// Text centred in the remaining content region of the current window.
void centered_text(const char* s, ImU32 color) {
    const ImVec2 sz = ImGui::CalcTextSize(s), av = ImGui::GetContentRegionAvail(), p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (av.x - sz.x) / 2, p.y + (av.y - sz.y) / 2), color, s);
}

// Lines of a grid with `nx` x `ny` cells of `step` pixels starting at p0; only the visible lines are emitted.
void grid_lines(ImDrawList* dl, ImVec2 p0, int nx, int ny, float step, ImU32 color) {
    const ImVec2 cmin = dl->GetClipRectMin(), cmax = dl->GetClipRectMax();
    const float x1 = p0.x + nx * step, y1 = p0.y + ny * step;
    const int i0 = std::max(0, floordiv(cmin.x - p0.x, step)), i1 = std::min(nx, floordiv(cmax.x - p0.x, step) + 1);
    const int j0 = std::max(0, floordiv(cmin.y - p0.y, step)), j1 = std::min(ny, floordiv(cmax.y - p0.y, step) + 1);
    for (int i = i0; i <= i1; i++) dl->AddLine(ImVec2(std::floor(p0.x + i * step) + 0.5f, p0.y), ImVec2(std::floor(p0.x + i * step) + 0.5f, y1), color);
    for (int j = j0; j <= j1; j++) dl->AddLine(ImVec2(p0.x, std::floor(p0.y + j * step) + 0.5f), ImVec2(x1, std::floor(p0.y + j * step) + 0.5f), color);
}

// 16 x 16 colour swatches of `cell` pixels at the cursor, colour `sel` framed (white inside, black around).
// Returns the clicked colour or -1; `hovered` (optional) receives the colour under the mouse or -1.
int swatches(const char* id, const Palette& pal, int sel, float cell, int* hovered = nullptr) {
    cell = std::max(2.0f, std::floor(cell));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(cell * 16, cell * 16));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 256; i++) {
        const ImVec2 a(p.x + float(i % 16) * cell, p.y + float(i / 16) * cell);
        dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), rgb(pal.rgb[size_t(i)]));
    }
    if (sel >= 0 && sel < 256) {
        const ImVec2 a(p.x + float(sel % 16) * cell, p.y + float(sel / 16) * cell), b(a.x + cell, a.y + cell);
        dl->AddRect(a, b, kWhite);
        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), kBlack);
        dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), kBlack);
    }
    int h = -1;
    if (ImGui::IsItemHovered()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int cx = floordiv(m.x - p.x, cell), cy = floordiv(m.y - p.y, cell);
        if (cx >= 0 && cy >= 0 && cx < 16 && cy < 16) h = cy * 16 + cx;
    }
    if (hovered) *hovered = h;
    return ImGui::IsItemClicked(ImGuiMouseButton_Left) ? h : -1;
}

// A colour sample of `size` pixels at the cursor with a black frame; the cursor moves past it on the same line.
void sample(ImU32 color, ImVec2 size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), color);
    ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), kBlack);
}

// Geometry of a scrollable zoomed picture inside the current child window: the picture is centred while it fits,
// otherwise it starts at a margin and scrolls. Submits one invisible button covering the scrollable area (left and
// right mouse button), so ImGui::IsItem*() afterwards refer to the canvas.
struct Canvas {
    ImVec2 p0;          // screen position of picture pixel (0, 0)
    float zoom;         // screen pixels per picture pixel
    bool hovered, active;
    int ix, iy;         // picture pixel under the mouse (may be outside)
    bool inside;        // (ix, iy) lies on the picture
};

Canvas canvas_area(int w, int h, float zoom, float margin) {
    Canvas c{};
    const ImVec2 view = ImGui::GetContentRegionAvail();
    const float iw = float(w) * zoom, ih = float(h) * zoom, tw = iw + 2 * margin, th = ih + 2 * margin;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float ox = tw <= view.x ? std::floor((view.x - iw) / 2) : margin, oy = th <= view.y ? std::floor((view.y - ih) / 2) : margin;
    ImGui::InvisibleButton("##canvas", ImVec2(std::max(tw, view.x), std::max(th, view.y)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    c.hovered = ImGui::IsItemHovered();
    c.active = ImGui::IsItemActive();
    c.p0 = ImVec2(origin.x + ox, origin.y + oy);
    c.zoom = zoom;
    const ImVec2 m = ImGui::GetIO().MousePos;
    c.ix = floordiv(m.x - c.p0.x, zoom);
    c.iy = floordiv(m.y - c.p0.y, zoom);
    c.inside = c.ix >= 0 && c.iy >= 0 && c.ix < w && c.iy < h;
    return c;
}

// Mouse wheel with Ctrl over the hovered canvas: +1 / -1 zoom steps (0 otherwise).
int ctrl_wheel(const Canvas& c) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!c.hovered || !io.KeyCtrl || io.MouseWheel == 0) return 0;
    return io.MouseWheel > 0 ? 1 : -1;
}

// Begins a scrollable child for a canvas; the wheel scrolls it unless Ctrl is held (Ctrl+wheel zooms).
void begin_canvas_child(const char* id, ImVec2 size, ImU32 bg) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar | (ImGui::GetIO().KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0));
}

void end_canvas_child() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// Feeds a canvas's mouse actions to a PixelEditor: left button draws with the current tool (one stroke from press to
// release), right button picks the colour under the mouse.
void pixel_strokes(PixelEditor& e, const Canvas& c) {
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (c.inside) e.stroke_begin(c.ix, c.iy);
    } else if (c.active && e.drawing() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        e.stroke_move(c.ix, c.iy);
    }
    if (ImGui::IsItemDeactivated()) e.stroke_end();
    if (c.hovered && c.inside && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) e.set_color(e.pick(c.ix, c.iy));
}

// ---------------------------------------------------------------------------------------- pictures and sheets

// Picture canvas: the zoomed picture, the pixel grid from zoom 6 up, the tile/sprite cell borders of sheets and a
// frame; without a picture a hint text.
void picture_work(App& a) {
    PixelEditor& e = *a.ed;
    begin_canvas_child("picture", ImGui::GetContentRegionAvail(), kCanvasBg);
    a.canvas = ImGui::GetContentRegionAvail();
    if (e.current() < 0) {
        a.hover_x = -1;
        centered_text("Links ein Bild wählen", kHint);
        end_canvas_child();
        return;
    }
    const Image& img = e.image();
    if (a.fit_pending) {  // largest zoom (1..32) at which the whole picture fits
        const float m = a.px(24);
        a.zoom = std::clamp(std::min(int((a.canvas.x - m) / float(std::max(1, img.width))), int((a.canvas.y - m) / float(std::max(1, img.height)))), 1, 32);
        a.fit_pending = false;
        ImGui::SetScrollX(0);
        ImGui::SetScrollY(0);
    }
    const Canvas c = canvas_area(img.width, img.height, float(a.zoom), a.px(12));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    a.pic_tex.upload(a.renderer, img, e.palette());
    const ImVec2 p1(c.p0.x + float(img.width) * c.zoom, c.p0.y + float(img.height) * c.zoom);
    dl->AddImage(a.pic_tex.id(), c.p0, p1);
    if (a.grid && a.zoom >= 6) grid_lines(dl, c.p0, img.width, img.height, c.zoom, IM_COL32(110, 110, 118, 255));
    if (const int cell = e.cell_size())  // tile or sprite boundaries
        grid_lines(dl, c.p0, (img.width + cell - 1) / cell, (img.height + cell - 1) / cell, float(cell) * c.zoom, kCellLine);
    dl->AddRect(ImVec2(c.p0.x - 1, c.p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), kBlack);
    const bool show = (c.hovered || c.active) && c.inside;
    a.hover_x = show ? c.ix : -1;
    a.hover_y = show ? c.iy : -1;
    a.hover_color = show ? e.pick(c.ix, c.iy) : -1;
    a.hover_cell = show && e.cell_size() ? c.iy / e.cell_size() * e.cell_columns() + c.ix / e.cell_size() : -1;
    pixel_strokes(e, c);
    if (const int d = ctrl_wheel(c)) a.zoom = std::clamp(a.zoom + d, 1, 32);
    end_canvas_child();
}

std::string palette_label(int n) {
    return n == 21 ? "Palette: Spiel (KB21)" : n == 247 ? "Palette: Torszenen (KB247)" : "Palette: wie KB" + std::to_string(n);
}

// Side panel of the picture tabs: display palette chooser, colour swatches (click = drawing colour), the drawing
// colour with its index and RGB values.
void picture_side(App& a) {
    PixelEditor& e = *a.ed;
    const bool own = e.has_own_palette();
    ImGui::BeginDisabled(e.current() < 0 || own);
    ImGui::SetNextItemWidth(-1);
    const std::string preview = own && e.current() >= 0 ? "Palette: im Bild (KB" + std::to_string(e.current()) + ")" : palette_label(e.palette_source());
    if (ImGui::BeginCombo("##palsrc", preview.c_str())) {
        for (int n : a.palette_choices)
            if (ImGui::Selectable(palette_label(n).c_str(), n == e.palette_source())) e.set_palette_source(n);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    const float cell = std::floor(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - a.px(60)) / 16);
    int hov = -1;
    const int clicked = swatches("##swatches", e.palette(), e.color(), cell, &hov);
    if (clicked >= 0) e.set_color(clicked);
    if (hov >= 0) {
        const auto& c = e.palette().rgb[size_t(hov)];
        a.hover_left = "Farbe " + std::to_string(hov) + "   RGB " + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]);
    }
    ImGui::Spacing();
    const auto& c = e.palette().rgb[size_t(e.color())];
    sample(rgb(c), ImVec2(a.px(36), a.px(36)));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Farbe %d", e.color());
    ImGui::Text("RGB %d, %d, %d", c[0], c[1], c[2]);
    ImGui::EndGroup();
}

// ------------------------------------------------------------------------------------------------ palettes

// The palette being edited as a 16 x 16 grid (click selects the colour), the selected colour framed, and its six-bit
// DAC and eight-bit display values.
void palette_work(App& a) {
    ImGui::BeginChild("pgrid", ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders);
    if (a.pal->current() < 0) {
        centered_text("Palette links wählen", ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::EndChild();
        return;
    }
    const ImVec2 av = ImGui::GetContentRegionAvail();
    const float m = a.px(16), text = a.px(50);
    const float size = std::max(32.0f, std::min(av.x - 2 * m, av.y - 2 * m - text));
    const float cell = std::floor(size / 16);
    const Palette p = a.pal->palette();
    ImGui::SetCursorPos(ImVec2(std::floor((av.x - cell * 16) / 2), m));
    const float x0 = ImGui::GetCursorPosX();
    const int clicked = swatches("##pgrid", p, a.pal_sel, cell);
    if (clicked >= 0) a.pal_sel = clicked;
    ImGui::SetCursorPos(ImVec2(x0, ImGui::GetCursorPosY() + a.px(8)));
    const auto& c = p.rgb[size_t(a.pal_sel)];
    const auto six = a.pal->get(a.pal_sel);
    sample(rgb(c), ImVec2(a.px(40), a.px(32)));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + a.px(7));
    ImGui::Text("Farbe %d   DAC %d, %d, %d   (Anzeige %d, %d, %d)", a.pal_sel, six[0], six[1], six[2], c[0], c[1], c[2]);
    ImGui::EndChild();
}

// Side panel of the palette tab: the selected colour's six-bit channels as sliders (colour 255 is not stored).
void palette_side(App& a) {
    const bool on = a.pal->current() >= 0 && a.pal_sel < PaletteDoc::kColors;
    ImGui::TextUnformatted(a.pal->current() < 0 ? "Palette links wählen"
                           : a.pal_sel >= PaletteDoc::kColors ? "Farbe 255 wird nicht gespeichert"
                                                              : ("Farbe " + std::to_string(a.pal_sel)).c_str());
    ImGui::Spacing();
    auto c = a.pal->get(a.pal_sel);
    static const char* const names[] = {"Rot", "Grün", "Blau"};
    bool changed = false;
    ImGui::BeginDisabled(!on);
    for (int i = 0; i < 3; i++) {
        ImGui::Spacing();
        ImGui::TextUnformatted(names[i]);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID(i);
        changed |= ImGui::SliderInt("##ch", &c[size_t(i)], 0, 63);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (changed && on) a.pal->set(a.pal_sel, c[0], c[1], c[2]);
}

// ------------------------------------------------------------------------------------------------ teams

// Screen colour of a 4-bit-per-channel jersey colour: each channel times 4 gives a six-bit DAC value, which is widened
// to eight bits the same way as the palettes.
ImU32 jersey_color(int r, int g, int b) {
    auto c = [](int n) { const int v = n * 4; return (v << 2) | (v >> 4); };
    return IM_COL32(c(r), c(g), c(b), 255);
}

// The crest editor under the player names: zoomable crest canvas (pixel grid from 4 screen pixels up), the palette
// with the drawing colour, crest number and zoom, and the buttons pen, fill, undo, zoom and save.
void crest_editor(App& a) {
    PixelEditor& e = *a.crest;
    const ImVec2 av = ImGui::GetContentRegionAvail();
    const float h = std::max(a.px(120), av.y), bw = a.px(150), pal_cell = a.px(8), pal_w = 16 * pal_cell + a.px(4);
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float view_w = std::max(a.px(150), av.x - bw - pal_w - 2 * sp);
    const bool on = e.current() >= 0;
    if (on && a.crest_auto) {  // largest zoom (2..16 in 96-dpi units) at which the crest fits
        const float m = a.px(6);
        const float device = std::min((view_w - 2 * m) / float(std::max(1, e.image().width)), (h - 2 * m) / float(std::max(1, e.image().height)));
        a.crest_zoom = std::clamp(int(device / a.scale), 2, 16);
    }
    begin_canvas_child("crest", ImVec2(view_w, h), ImGui::GetColorU32(ImGuiCol_FrameBg));
    if (!on) {
        centered_text("Europapokal-Vereine haben kein Wappen", ImGui::GetColorU32(ImGuiCol_TextDisabled));
    } else {
        const Image& img = e.image();
        const Canvas c = canvas_area(img.width, img.height, std::floor(a.px(float(a.crest_zoom))), a.px(6));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        a.crest_tex.upload(a.renderer, img, e.palette());
        const ImVec2 p1(c.p0.x + float(img.width) * c.zoom, c.p0.y + float(img.height) * c.zoom);
        dl->AddImage(a.crest_tex.id(), c.p0, p1);
        if (c.zoom >= 4) grid_lines(dl, c.p0, img.width, img.height, c.zoom, IM_COL32(150, 150, 158, 255));
        pixel_strokes(e, c);
        if (const int d = ctrl_wheel(c)) {
            a.crest_auto = false;
            a.crest_zoom = std::clamp(a.crest_zoom + d, 2, 16);
        }
    }
    end_canvas_child();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (on) {
        const int clicked = swatches("##crestpal", e.palette(), e.color(), pal_cell);
        if (clicked >= 0) e.set_color(clicked);
        ImGui::TextDisabled("Wappen %d (KB%d)", e.current() - 80, e.current());
        ImGui::TextDisabled("Zoom %d×%s", a.crest_zoom, e.dirty() ? "  – geändert" : "");
    } else {
        ImGui::Dummy(ImVec2(pal_w, 1));
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!on);
    auto button = [&](const char* label, bool checked, float w) {
        if (checked) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool r = ImGui::Button(label, ImVec2(w, 0));
        if (checked) ImGui::PopStyleColor();
        return r;
    };
    if (button("Stift", e.tool() == Tool::Pencil, bw)) e.set_tool(Tool::Pencil);
    if (button("Füllen", e.tool() == Tool::Fill, bw)) e.set_tool(Tool::Fill);
    ImGui::BeginDisabled(!e.can_undo());
    if (button("Rückgängig", false, bw)) e.undo();
    ImGui::EndDisabled();
    const float half = (bw - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (button("Zoom -", false, half)) {
        a.crest_auto = false;
        a.crest_zoom = std::clamp(a.crest_zoom - 1, 2, 16);
    }
    ImGui::SameLine();
    if (button("Zoom +", false, half)) {
        a.crest_auto = false;
        a.crest_zoom = std::clamp(a.crest_zoom + 1, 2, 16);
    }
    ImGui::Dummy(ImVec2(1, a.px(8)));
    if (button("Wappen speichern", false, bw)) e.save();
    ImGui::EndDisabled();
    ImGui::EndGroup();
}

// Team tab work area: club name, the 25 player names in two columns with foreigner check boxes, the crest editor
// below. Every change goes to the TeamDoc at once (no "apply" step).
void team_work(App& a) {
    TeamDoc& T = *a.team;
    const int t = a.team_sel;
    const bool players = TeamDoc::has_players(t);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Verein");
    ImGui::SameLine(a.px(60));
    ImGui::SetNextItemWidth(a.px(280));
    if (input_upper("##club", a.club.data(), a.club.size(), TeamDoc::kClubLimit)) T.set_club_name(t, a.club.data());
    ImGui::Spacing();
    ImGui::TextUnformatted("Spieler (Ausländer = Haken)");
    ImGui::BeginDisabled(!players);
    if (ImGui::BeginTable("players", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int r = 0; r < 13; r++) {
            ImGui::TableNextRow();
            for (int col = 0; col < 2; col++) {
                const int i = col == 0 ? r : 13 + r;
                if (i >= TeamDoc::kSquad) continue;
                ImGui::TableSetColumnIndex(col);
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - a.px(70));
                if (input_upper("##name", a.names[size_t(i)].data(), a.names[size_t(i)].size(), TeamDoc::kNameLimit)) T.set_player_name(t, i, a.names[size_t(i)].data());
                ImGui::SameLine();
                bool f = players && T.foreign(t, i);
                if (ImGui::Checkbox("Ausl.", &f) && players) T.set_foreign(t, i, f);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    crest_editor(a);
}

// One "label ... number field" row of the team side panel; returns true with the new value in `v` when changed.
bool number_row(App& a, const char* label, const char* id, int& v, int lo, int hi, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    const float w = a.px(100), edge = right_edge();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    same_line_right(edge, w);
    ImGui::SetNextItemWidth(w);
    const bool changed = ImGui::InputInt(id, &v, 1, 5);
    ImGui::EndDisabled();
    v = std::clamp(v, lo, hi);
    return changed && enabled;
}

// Every other editable team of the same kind (with / without player list) can be the swap partner.
bool swap_candidate(const App& a, int t) {
    return TeamDoc::editable(t) && t != a.team_sel && TeamDoc::has_players(t) == TeamDoc::has_players(a.team_sel);
}

// Team side panel: jersey preview, crest number, league place, the three strengths, jersey sliders and team swap.
void team_side(App& a) {
    TeamDoc& T = *a.team;
    const int t = a.team_sel;
    const TeamData& d = T.data(t);
    const auto jer = TeamDoc::jersey_rgb(d);
    {   // jersey colour as the goal scenes show it
        const float x = right_edge() - a.px(56);
        ImGui::SetCursorPosX(x);
        sample(jersey_color(jer[0], jer[1], jer[2]), ImVec2(a.px(54), a.px(54)));
        ImGui::SetCursorPosX(x);
        ImGui::TextDisabled("Trikot");
    }
    ImGui::Spacing();
    int crest = std::max(0, d.crest);
    if (number_row(a, "Wappen (0–63)", "##crest", crest, 0, 63, TeamDoc::has_players(t)) && crest != d.crest) {
        ensure_crest_clean(a, [&a, t, crest] {  // cancelled: the field shows the old number again
            a.team->set_crest(t, crest);
            load_crest(a);
        });
    }
    int place = std::max(0, d.place);
    if (number_row(a, "Platz in der Liga (1–12)", "##place", place, 0, 12, d.place >= 0)) T.set_place(t, place);
    for (int k = 0; k < 3; k++) {
        int v = d.strength[size_t(k)];
        const std::string label = "Stärke " + std::to_string(k + 1), id = "##str" + std::to_string(k);
        if (number_row(a, label.c_str(), id.c_str(), v, 1, 99, true)) T.set_strength(t, k, v);
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Trikotfarbe der Torszenen");
    static const char* const names[] = {"Rot", "Grün", "Blau"};
    auto c = jer;
    bool changed = false;
    for (int i = 0; i < 3; i++) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(names[i]);
        ImGui::SameLine(a.px(50));
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID(i);
        changed |= ImGui::SliderInt("##jersey", &c[size_t(i)], 0, 15);
        ImGui::PopID();
    }
    if (changed) T.set_jersey(t, c[0], c[1], c[2]);
    ImGui::Spacing();
    ImGui::TextUnformatted("Mannschaft tauschen mit");
    if (!swap_candidate(a, a.swap_with)) {
        a.swap_with = -1;
        for (int o = 0; o < TeamDoc::kAllTeams && a.swap_with < 0; o++)
            if (swap_candidate(a, o)) a.swap_with = o;
    }
    auto label = [&](int o) { return std::to_string(o + 1) + "  " + T.club_name(o); };
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - a.px(86));
    if (ImGui::BeginCombo("##swap", a.swap_with >= 0 ? label(a.swap_with).c_str() : "")) {
        for (int o = 0; o < TeamDoc::kAllTeams; o++) {
            if (!swap_candidate(a, o)) continue;
            ImGui::PushID(o);
            if (ImGui::Selectable(label(o).c_str(), o == a.swap_with)) a.swap_with = o;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(a.swap_with < 0);
    if (ImGui::Button("Tauschen", ImVec2(-1, 0))) {
        T.swap_teams(a.team_sel, a.swap_with);
        load_team_fields(a);
    }
    ImGui::EndDisabled();
}

// ------------------------------------------------------------------------------------------------ hall maps

// The rink map: the tile map (texture rebuilt only when the map revision changed), the optional cell grid from zoom 2
// up and the hovered cell. The toolbar tool decides what a left click does: picker takes tile and mirror flag of the
// cell, fill flood-fills, every other tool paints the selected tile (dragging paints one stroke). Right click picks.
void map_work(App& a) {
    MapDoc& m = *a.map;
    begin_canvas_child("map", ImGui::GetContentRegionAvail(), kCanvasBg);
    if (m.current() < 0) {
        a.map_hc = -1;
        centered_text("Links eine Hallenkarte wählen", kHint);
        end_canvas_child();
        return;
    }
    const Image& img = m.rink();
    if (a.rink_rev != m.revision() || a.rink_no != m.current() || !a.rink_tex.valid()) {
        a.rink_tex.upload(a.renderer, img, m.palette());
        a.rink_rev = m.revision();
        a.rink_no = m.current();
    }
    const Canvas c = canvas_area(img.width, img.height, float(a.map_zoom), a.px(12));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(c.p0.x + float(img.width) * c.zoom, c.p0.y + float(img.height) * c.zoom);
    dl->AddImage(a.rink_tex.id(), c.p0, p1);
    const float cell = float(MapDoc::kTile) * c.zoom;
    if (a.grid && a.map_zoom >= 2) grid_lines(dl, c.p0, MapDoc::kCols, MapDoc::kRows, cell, kCellLine);
    dl->AddRect(ImVec2(c.p0.x - 1, c.p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), kBlack);
    const int col = floordiv(ImGui::GetIO().MousePos.x - c.p0.x, cell), row = floordiv(ImGui::GetIO().MousePos.y - c.p0.y, cell);
    const bool in = col >= 0 && row >= 0 && col < MapDoc::kCols && row < MapDoc::kRows;
    a.map_hc = (c.hovered || c.active) && in ? col : -1;
    a.map_hr = (c.hovered || c.active) && in ? row : -1;
    if (a.map_hc >= 0) {
        const ImVec2 h0(c.p0.x + float(col) * cell, c.p0.y + float(row) * cell);
        dl->AddRect(h0, ImVec2(h0.x + cell, h0.y + cell), kWhite);
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && in) {
        const Tool tool = a.ed->tool();
        if (tool == Tool::Picker) {
            a.map_tile = m.tile(col, row);
            a.map_mirror = m.mirrored(col, row);
        } else if (tool == Tool::Fill) {
            m.fill(col, row, a.map_tile, a.map_mirror);
        } else {
            m.stroke_begin();
            a.map_painting = true;
            m.set(col, row, a.map_tile, a.map_mirror);
        }
    } else if (a.map_painting && c.active && in && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m.set(col, row, a.map_tile, a.map_mirror);  // dragging extends the paint stroke
    }
    if (ImGui::IsItemDeactivated()) {
        m.stroke_end();
        a.map_painting = false;
    }
    if (c.hovered && in && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        a.map_tile = m.tile(col, row);
        a.map_mirror = m.mirrored(col, row);
    }
    if (const int d = ctrl_wheel(c)) a.map_zoom = std::clamp(a.map_zoom + d, 1, 4);
    end_canvas_child();
}

// Side panel of the hall maps: selected tile, "mirrored" check box and the tile picker (click selects the tile).
void map_side(App& a) {
    MapDoc& m = *a.map;
    const float edge = right_edge();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Kachel %d%s", a.map_tile, a.map_mirror ? " (gespiegelt)" : "");
    same_line_right(edge, ImGui::CalcTextSize("Gespiegelt").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Checkbox("Gespiegelt", &a.map_mirror);
    ImGui::BeginChild("tiles", ImGui::GetContentRegionAvail(), ImGuiChildFlags_Borders);
    if (m.current() >= 0 && m.tile_count() > 0) {
        if (!a.atlas_tex.valid()) {  // all tiles in one texture, kAtlasCols per row
            const int cols = App::kAtlasCols, rows = (m.tile_count() + cols - 1) / cols, t = MapDoc::kTile;
            std::vector<uint32_t> px(size_t(cols * t) * size_t(rows * t), 0xFF000000u);
            for (int i = 0; i < m.tile_count(); i++) {
                const Image& ti = m.tile_image(i);
                for (int y = 0; y < t; y++)
                    for (int x = 0; x < t; x++) {
                        const auto& c = m.palette().rgb[ti.pixels[size_t(y * t + x)]];
                        px[size_t((i / cols) * t + y) * size_t(cols * t) + size_t((i % cols) * t + x)] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
                    }
            }
            a.atlas_tex.upload_argb(a.renderer, px.data(), cols * t, rows * t);
        }
        const float cell = std::floor(a.px(36)), tile = std::floor((cell - a.px(4)) / MapDoc::kTile) * MapDoc::kTile;
        const int cols = std::max(1, int(ImGui::GetContentRegionAvail().x / cell)), rows = (m.tile_count() + cols - 1) / cols;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tiles", ImVec2(float(cols) * cell, float(rows) * cell));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float aw = float(a.atlas_tex.width()), ah = float(a.atlas_tex.height()), t = float(MapDoc::kTile);
        const float vis0 = ImGui::GetScrollY() - cell, vis1 = ImGui::GetScrollY() + ImGui::GetWindowHeight();
        for (int i = 0; i < m.tile_count(); i++) {
            const float ry = float(i / cols) * cell;
            if (ry < vis0 || ry > vis1) continue;
            const ImVec2 q(p.x + float(i % cols) * cell + a.px(2), p.y + ry + a.px(2));
            const float u = float(i % App::kAtlasCols) * t / aw, v = float(i / App::kAtlasCols) * t / ah;
            dl->AddImage(a.atlas_tex.id(), q, ImVec2(q.x + tile, q.y + tile), ImVec2(u, v), ImVec2(u + t / aw, v + t / ah));
            if (i == a.map_tile) {
                dl->AddRect(ImVec2(q.x - a.px(2), q.y - a.px(2)), ImVec2(q.x + tile + a.px(2), q.y + tile + a.px(2)), kBlack);
                dl->AddRect(ImVec2(q.x - a.px(2) + 1, q.y - a.px(2) + 1), ImVec2(q.x + tile + a.px(2) - 1, q.y + tile + a.px(2) - 1), kWhite);
            }
        }
        if (ImGui::IsItemHovered()) {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const int cx = floordiv(mp.x - p.x, cell), cy = floordiv(mp.y - p.y, cell), i = cy * cols + cx;
            if (cx >= 0 && cx < cols && i >= 0 && i < m.tile_count()) {
                a.hover_left = "Kachel " + std::to_string(i);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) a.map_tile = i;
            }
        }
    }
    ImGui::EndChild();
}

}  // namespace

void fit_zoom(App& a) { a.fit_pending = true; }

void load_crest(App& a) {
    const int t = a.team_sel;
    if (TeamDoc::has_players(t) && a.team->data(t).crest >= 0) a.crest->load(80 + a.team->data(t).crest);
    else a.crest->close();
}

void load_team_fields(App& a) {
    const int t = a.team_sel;
    const bool players = TeamDoc::has_players(t);
    auto copy = [](auto& buf, const std::string& s) {
        const size_t n = std::min(s.size(), buf.size() - 1);
        std::copy_n(s.begin(), n, buf.begin());
        buf[n] = '\0';
    };
    copy(a.club, a.team->club_name(t));
    for (int i = 0; i < TeamDoc::kSquad; i++) copy(a.names[size_t(i)], players ? a.team->player_name(t, i) : std::string());
    load_crest(a);
    a.swap_with = -1;
}

void open_map(App& a, int number) {
    if (a.map->load(number)) a.map_zoom = 1;
    a.rink_no = -1;
}

void tab_work(App& a) {
    switch (a.tab) {
        case kTabPalettes: palette_work(a); break;
        case kTabTeams: team_work(a); break;
        case kTabMaps: map_work(a); break;
        default: picture_work(a); break;
    }
}

void tab_side(App& a) {
    switch (a.tab) {
        case kTabPalettes: palette_side(a); break;
        case kTabTeams: team_side(a); break;
        case kTabMaps: map_side(a); break;
        default: picture_side(a); break;
    }
}

}  // namespace em::editor::sdl
