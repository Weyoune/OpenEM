// SPDX-License-Identifier: GPL-3.0-only
// Goal scenes of ovl14_4938 (see ClassicGoalScene in classic_match_view.hpp).
#include "ui/classic_match_view.hpp"

#include <algorithm>
#include <map>
#include <memory>

#include "formats/tore.hpp"

namespace em::ui {
namespace {

// Everything ovl14_4938 loads from KB\231..247 plus the scripts, read once per data directory.
struct SceneData {
    Bytes tiles_a, tiles_b;
    std::array<std::unique_ptr<Image>, 5> rinks;  // map KB236 (index 0) or KB242..245
    std::array<std::vector<Image>, 3> sprites;    // KB233, KB234, KB235
    Image panel;
    Bytes palette;                                // KB247: 255 DAC triples
    std::map<std::string, TorScene> scripts;
};

SceneData& scene_data(const std::filesystem::path& data) {
    static std::map<std::filesystem::path, std::unique_ptr<SceneData>> cache;
    auto& slot = cache[data];
    if (!slot) {
        auto d = std::make_unique<SceneData>();
        const auto kb = [&](int id) { return read_file(data / "KB" / (std::to_string(id) + ".vga")); };
        d->tiles_a = kb(231);
        d->tiles_b = kb(232);
        for (int i = 0; i < 3; i++) d->sprites[size_t(i)] = decode_tor_sprites(kb(233 + i));
        d->panel = decode_tor_panel(kb(246));
        d->palette = kb(247);
        slot = std::move(d);
    }
    return *slot;
}

const Image& rink(const std::filesystem::path& data, int arena) {
    SceneData& d = scene_data(data);
    const int index = arena >= 1 && arena <= 4 ? arena : 0;  // ovl14_4938: entry 5 of the load loop is 236 or 0xF1 + arena
    auto& r = d.rinks[size_t(index)];
    if (!r) r = std::make_unique<Image>(decode_rink(d.tiles_a, d.tiles_b,
                                                    read_file(data / "KB" / (std::to_string(index ? 241 + index : 236) + ".vga"))));
    return *r;
}

const TorScene& script(const std::filesystem::path& data, const std::string& name) {
    SceneData& d = scene_data(data);
    auto it = d.scripts.find(name);
    if (it == d.scripts.end()) it = d.scripts.emplace(name, decode_tor(read_file(data / "TORE" / name))).first;
    return it->second;
}

// Renderer state after n frames: FUN_1bbf_5250 starts the camera at the script position minus (16, 32), clamped at 0;
// FUN_1bbf_4d98 clamps every camera move to 0..0x160 / 0..0x3B8 (the 320 x 200 window inside the 672 x 1152 rink).
// FUN_1bbf_5316 moves the puck freely and an object whose frame word is not zero; the object type is the frame's sprite.
struct View {
    int cam_x = 0, cam_y = 0, puck_x = 0, puck_y = 0;
    std::array<TorObject, kTorObjects> objects{};
};

int clamp_move(int pos, int d, int max) {
    if (d > 0) return std::min(pos + d, max);
    if (d < 0) return std::max(pos + d, 0);
    return pos;
}

View play(const TorScene& s, int n) {
    View v{std::max(0, s.camera_x - 16), std::max(0, s.camera_y - 32), s.puck_x, s.puck_y, s.objects};
    for (int i = 0; i < n && size_t(i) < s.frames.size(); i++) {
        const TorFrame& f = s.frames[size_t(i)];
        const TorMove c = decode_tor_move(f.camera_move), p = decode_tor_move(f.puck_move);
        v.cam_x = clamp_move(v.cam_x, c.dx, 0x160);
        v.cam_y = clamp_move(v.cam_y, c.dy, 0x3B8);
        v.puck_x += p.dx;
        v.puck_y += p.dy;
        for (size_t o = 0; o < kTorObjects; o++) {
            v.objects[o].sprite = f.sprite[o];
            if (f.sprite[o] == 0 && f.move[o] == 0) continue;
            const TorMove m = decode_tor_move(f.move[o]);
            v.objects[o].x += m.dx;
            v.objects[o].y += m.dy;
        }
    }
    return v;
}

// FUN_1bbf_4ecf / FUN_1bbf_4fe7: an object at (0, 0) is skipped; its top left corner is (x - 0x50, y - 0x60) in rink
// pixels; types 0 and 1 are the 5 x 5 bitmaps of the renderer, 2..0x29 / 0x2A..0x51 / 0x52..0x5D the sprites of KB233 /
// KB234 / KB235, higher types draw nothing. Colour 0 is transparent.
void draw_object(Image& screen, const SceneData& d, const View& v, int type, int x, int y) {
    if (x == 0 && y == 0) return;
    const uint8_t* pixels = nullptr;
    int size = 5;
    if (type == 0) pixels = kTorDot.data();
    else if (type == 1) pixels = kTorPuck.data();
    else {
        size = kTorSprite;
        const int set = type < 0x2A ? 0 : type < 0x52 ? 1 : 2;
        const int index = type - (set == 0 ? 2 : set == 1 ? 0x2A : 0x52);
        const auto& sprites = d.sprites[size_t(set)];
        if (type > 0x5D || index < 0 || size_t(index) >= sprites.size()) return;
        pixels = sprites[size_t(index)].pixels.data();
    }
    const int left = x - 0x50 - v.cam_x, top = y - 0x60 - v.cam_y;
    for (int row = 0; row < size; row++)
        for (int col = 0; col < size; col++) {
            const uint8_t c = pixels[row * size + col];
            const int sx = left + col, sy = top + row;
            if (c && sx >= 0 && sx < 320 && sy >= 0 && sy < 200) screen.at(sx, sy) = c;
        }
}

constexpr auto kSceneFont = ClassicFontId::Flow;  // native font 2 (ovl14_504d)

// sub_19CB2 / sub_19B54 with DS:0374 = 2 (set by ovl14_504d / ovl14_5258): at least two characters, padded in front with
// '%', a blank glyph as wide as a digit in the game fonts (native encoding)
std::string two_places(int n) { return (n >= 0 && n < 10 ? "%" : "") + std::to_string(n); }

}  // namespace

int classic_goal_scene_length(const std::filesystem::path& data, const std::string& name) {
    return int(script(data, name).frames.size());
}

void draw_classic_goal_scene(ClassicCanvas& c, const ClassicGoalScene& s) {
    const std::filesystem::path& data = c.data();
    const SceneData& d = scene_data(data);
    // FUN_1bbf_47f0(0, 0xFF, KB247): DAC 0..254; 255 stays; then the shirt colours (ovl14_4938 4D99..4DF6)
    c.reset_palette();
    for (int i = 0; i < 255 && size_t(i) * 3 + 2 < d.palette.size(); i++)
        c.set_dac(i, d.palette[size_t(i) * 3], d.palette[size_t(i) * 3 + 1], d.palette[size_t(i) * 3 + 2]);
    for (int side = 0; side < 2; side++) {
        const int col = s.colour[size_t(side)];
        c.set_dac(0xFE - side, (col >> 6) & 63, (col >> 2) & 63, uint8_t(col << 2) & 63);
    }
    Image& screen = c.pixels();
    const View v = play(script(data, s.script), s.frame);
    const Image& r = rink(data, s.arena);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) screen.at(x, y) = r.pixels[size_t(v.cam_y + y) * size_t(r.width) + size_t(v.cam_x + x)];
    draw_object(screen, d, v, 1, v.puck_x, v.puck_y);  // FUN_1bbf_51eb: the puck first, then the 12 objects
    for (const TorObject& o : v.objects) draw_object(screen, d, v, o.sprite, o.x, o.y);
    // ovl14_504d on the split screen panel (y + 200)
    for (int y = 0; y < kTorPanelHeight; y++)
        for (int x = 0; x < kTorPanelWidth; x++) screen.at(x, 200 + y) = d.panel.pixels[size_t(y) * kTorPanelWidth + size_t(x)];
    constexpr int Y = 200;
    c.rect(9, Y + 6, 0x98, Y + 0x0E, 4);
    c.text(kSceneFont, 9, Y + 0x0D, s.names[0], 7, 0, 0x97);
    c.rect(0xA9, Y + 6, 0x138, Y + 0x0E, 4);
    c.text(kSceneFont, 0xA9, Y + 0x0D, s.names[1], 7, 0, 0x137);
    c.rect(8, Y + 0x1A, 0xBC, Y + 0x22, 4);
    c.text(kSceneFont, 8, Y + 0x21, s.info, 7, 0);
    c.rect(0xE8, Y + 0x1A, 0x12D, Y + 0x22, 4);  // ovl14_5258: "<minute>.MINUTE" (DS:5518)
    c.text_native(kSceneFont, 0xE8, Y + 0x21, two_places(s.minute) + ".MINUTE", 7, 0);
    const int w = c.font().measure(kSceneFont, "00");  // DS:5515
    for (int side = 0; side < 2; side++) {
        const int x0 = side ? 0xA9 : 9, x1 = side ? 0x137 : 0x97;
        const int bx = 0x47 - w / 2 + x0;
        c.rect(bx, Y + 0x10, bx + w, Y + 0x18, 4);
        c.text_native(kSceneFont, x0, Y + 0x17, two_places(s.goals[size_t(side)]), 7, 0, x1);
    }
}

std::vector<ClassicMatchFrame> classic_goal_scene_frames(const std::filesystem::path& data, const ClassicMatchFrame& base,
                                                         ClassicGoalScene scene, int side, bool goal) {
    std::vector<ClassicMatchFrame> out;
    const int n = classic_goal_scene_length(data, scene.script);
    constexpr int kTickMs = 54925 / 1000;  // one BIOS tick
    for (int k = 1; k <= n; k++) {
        ClassicMatchFrame f = base;
        f.message.reset();
        f.flash.reset();
        scene.frame = k;
        f.scene = scene;
        f.hold_ms = kTickMs;
        out.push_back(std::move(f));
    }
    // ovl14_4938 4F49..: the goal is added (ovl14_504d mode 2), then delay(6) = two ticks
    if (goal) scene.goals[size_t(side)]++;
    ClassicMatchFrame f = base;
    f.message.reset();
    f.flash.reset();
    scene.frame = n;
    f.scene = scene;
    f.hold_ms = 6 * 54925 / 3000;
    out.push_back(std::move(f));
    return out;
}

}  // namespace em::ui
