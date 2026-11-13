// SPDX-License-Identifier: GPL-3.0-only
// The animated goal-chance scenes of the match display (see ClassicGoalScene
// in classic_match_view.hpp): a scrolling top-down rink with the puck and up
// to twelve player sprites, driven by the scene scripts in the user's TORE
// folder, plus the score panel below it. Also builds the frame sequence of
// a scene and its optional replays.
#include "ui/classic_match_view.hpp"

#include <algorithm>
#include <map>
#include <memory>

#include "formats/tore.hpp"

namespace em::ui {
namespace {

// Everything a goal scene needs from KB/231..247 plus the parsed scripts,
// read once per data directory and cached for the session.
struct SceneData {
    Bytes tiles_a, tiles_b;                       // rink tile sets KB/231 and KB/232
    std::array<std::unique_ptr<Image>, 5> rinks;  // rink built from map KB/236 (index 0) or KB/242..245, on demand
    std::array<std::vector<Image>, 3> sprites;    // sprite sets KB/233, KB/234, KB/235
    Image panel;                                  // score panel KB/246
    Bytes palette;                                // KB/247: 255 DAC triples
    std::map<std::string, TorScene> scripts;      // TORE scripts by file name
};

// The cached scene data of a data directory, loaded on first use.
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

// The 672 x 1152 rink picture for an arena: arenas 1..4 use map KB/(241 +
// arena), any other value the default map KB/236.
const Image& rink(const std::filesystem::path& data, int arena) {
    SceneData& d = scene_data(data);
    const int index = arena >= 1 && arena <= 4 ? arena : 0;
    auto& r = d.rinks[size_t(index)];
    if (!r) r = std::make_unique<Image>(decode_rink(d.tiles_a, d.tiles_b,
                                                    read_file(data / "KB" / (std::to_string(index ? 241 + index : 236) + ".vga"))));
    return *r;
}

// The parsed scene script TORE/<name>, cached.
const TorScene& script(const std::filesystem::path& data, const std::string& name) {
    SceneData& d = scene_data(data);
    auto it = d.scripts.find(name);
    if (it == d.scripts.end()) it = d.scripts.emplace(name, decode_tor(read_file(data / "TORE" / name))).first;
    return it->second;
}

// Renderer state after n frames. The camera starts at the script position minus (16, 32), clamped at 0; every camera
// move is clamped to 0..0x160 / 0..0x3B8 (the 320 x 200 window inside the 672 x 1152 rink). The puck moves freely, and
// an object moves when its sprite or move word in the frame is not zero; the object type is the frame's sprite.
struct View {
    int cam_x = 0, cam_y = 0, puck_x = 0, puck_y = 0;  // camera (top left of the window) and puck, rink pixels
    std::array<TorObject, kTorObjects> objects{};      // the twelve objects
};

// Applies one camera move d to pos, clamping only in the direction moved.
int clamp_move(int pos, int d, int max) {
    if (d > 0) return std::min(pos + d, max);
    if (d < 0) return std::max(pos + d, 0);
    return pos;
}

// Replays the first n frames of a script from its start state.
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

// Draws one object into the 320 x 200 rink window. An object at (0, 0) is skipped; its top left corner is
// (x - 0x50, y - 0x60) in rink pixels; types 0 and 1 are the built-in 5 x 5 dot and puck bitmaps, 2..0x29 / 0x2A..0x51 /
// 0x52..0x5D the sprites of KB/233 / KB/234 / KB/235, higher types draw nothing. Colour 0 is transparent.
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

constexpr auto kSceneFont = ClassicFontId::Flow;  // the game's font 2, used for the whole panel

// The panel prints numbers with a minimum width of two: at least two characters, padded in front with '%', a blank
// glyph as wide as a digit in the game fonts (native encoding).
std::string two_places(int n) { return (n >= 0 && n < 10 ? "%" : "") + std::to_string(n); }

}  // namespace

// Number of frames of scene script <name>.
int classic_goal_scene_length(const std::filesystem::path& data, const std::string& name) {
    return int(script(data, name).frames.size());
}

// Draws one frame of a goal scene: the rink window (rows 0..199) and the
// score panel below it (names, chance description, minute, goals).
void draw_classic_goal_scene(ClassicCanvas& c, const ClassicGoalScene& s) {
    const std::filesystem::path& data = c.data();
    const SceneData& d = scene_data(data);
    // Palette entries 0..254 from KB/247 (255 stays), then the two shirt
    // colours in entries 0xFE (home) and 0xFD (away). A shirt colour packs
    // 6-bit R in bits 6..11, G in bits 2..7 and B in bits 0..3 (shifted up by 2).
    c.reset_palette();
    for (int i = 0; i < 255 && size_t(i) * 3 + 2 < d.palette.size(); i++)
        c.set_dac(i, d.palette[size_t(i) * 3], d.palette[size_t(i) * 3 + 1], d.palette[size_t(i) * 3 + 2]);
    for (int side = 0; side < 2; side++) {
        const int col = s.colour[size_t(side)];
        c.set_dac(0xFE - side, (col >> 6) & 63, (col >> 2) & 63, uint8_t(col << 2) & 63);
    }
    Image& screen = c.pixels();
    const View v = s.script.empty() ? View{} : play(script(data, s.script), s.frame);
    const Image& r = rink(data, s.arena);
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) screen.at(x, y) = r.pixels[size_t(v.cam_y + y) * size_t(r.width) + size_t(v.cam_x + x)];
    draw_object(screen, d, v, 1, v.puck_x, v.puck_y);  // the puck first, then the 12 objects
    for (const TorObject& o : v.objects) draw_object(screen, d, v, o.sprite, o.x, o.y);
    // the score panel in the split-screen area below the rink (y + 200)
    for (int y = 0; y < kTorPanelHeight; y++)
        for (int x = 0; x < kTorPanelWidth; x++) screen.at(x, 200 + y) = d.panel.pixels[size_t(y) * kTorPanelWidth + size_t(x)];
    constexpr int Y = 200;
    c.rect(9, Y + 6, 0x98, Y + 0x0E, 4);
    c.text(kSceneFont, 9, Y + 0x0D, s.names[0], 7, 0, 0x97);
    c.rect(0xA9, Y + 6, 0x138, Y + 0x0E, 4);
    c.text(kSceneFont, 0xA9, Y + 0x0D, s.names[1], 7, 0, 0x137);
    c.rect(8, Y + 0x1A, 0xBC, Y + 0x22, 4);
    c.text(kSceneFont, 8, Y + 0x21, s.info, 7, 0);
    c.rect(0xE8, Y + 0x1A, 0x12D, Y + 0x22, 4);  // "<minute>.MINUTE"
    c.text_native(kSceneFont, 0xE8, Y + 0x21, two_places(s.minute) + ".MINUTE", 7, 0);
    const int w = c.font().measure(kSceneFont, "00");  // the score box is as wide as "00"
    for (int side = 0; side < 2; side++) {
        const int x0 = side ? 0xA9 : 9, x1 = side ? 0x137 : 0x97;
        const int bx = 0x47 - w / 2 + x0;
        c.rect(bx, Y + 0x10, bx + w, Y + 0x18, 4);
        c.text_native(kSceneFont, x0, Y + 0x17, two_places(s.goals[size_t(side)]), 7, 0, x1);
    }
}

// Script name "<n>.<h|a><t|v>[e|j]": n the scene number, h/a the attacking
// side (home/away), t/v goal or miss, e penalty shot, j the "six" variant.
// Penalty shots and six-scenes always start at scene 2 (there are only two
// scenes of each, see TORE/anzahl).
std::string classic_goal_scene_script(int n, int side, bool goal, bool penalty_shot, bool six) {
    if (penalty_shot || six) n = 2;
    std::string name = std::to_string(n) + (side ? ".a" : ".h") + (goal ? "t" : "v");
    if (penalty_shot) name += "e";
    else if (six) name += "j";
    return name;
}

// Frame sequence of one scene on top of the match frame base: one frame per
// script step (one BIOS tick, about 54 ms each), then the final frame with
// the goal counted (held for six delay units of a third tick, ~109 ms), then
// a pause of 20 delay units (~366 ms) that remembers the scene for a replay.
// With rink_stays the pause keeps the rink on screen; otherwise it returns
// to base.
std::vector<ClassicMatchFrame> classic_goal_scene_frames(const std::filesystem::path& data, const ClassicMatchFrame& base,
                                                         ClassicGoalScene scene, int side, bool goal, bool rink_stays) {
    std::vector<ClassicMatchFrame> out;
    const int n = classic_goal_scene_length(data, scene.script);
    scene.side = side;
    scene.goal = goal;
    const auto frame = [&](const ClassicGoalScene& s, int ms) {
        ClassicMatchFrame f = base;
        f.message.reset();
        f.flash.reset();
        f.after_scene.reset();
        f.scene = s;
        f.hold_ms = ms;
        return f;
    };
    for (int k = 1; k <= n; k++) {
        scene.frame = k;
        out.push_back(frame(scene, 54925 / 1000));  // one BIOS tick
    }
    // after the script the goal is added to the panel, then delay(6) = two ticks
    ClassicGoalScene end = scene;
    if (goal) end.goals[size_t(side)]++;
    end.final = true;
    out.push_back(frame(end, 6 * 54925 / 3000));
    // delay(20); during it a typed 'r' / 'w' plays the chance again
    ClassicMatchFrame after = rink_stays ? frame(end, 20 * 54925 / 3000) : base;
    if (!rink_stays) {
        after.message.reset();
        after.flash.reset();
        after.scene.reset();
        after.hold_ms = 20 * 54925 / 3000;
    }
    ClassicGoalScene replay = scene;  // the chance with the score before it
    replay.frame = 0;
    replay.final = false;
    after.after_scene = replay;
    out.push_back(std::move(after));
    return out;
}

// Frames of a replay requested during the pause frame after: the next scene
// of the same kind with the score from before the chance. At most three
// replays (play < 3); returns no frames otherwise.
std::vector<ClassicMatchFrame> classic_goal_scene_replay(const std::filesystem::path& data, const ClassicMatchFrame& after) {
    if (!after.after_scene || after.after_scene->play >= 3) return {};
    ClassicGoalScene s = *after.after_scene;
    s.play++;
    // the next scene number of the same kind (normal scenes 2..11; e / j stay on their two scenes)
    const size_t dot = s.script.find('.');
    const int n = std::stoi(s.script.substr(0, dot));
    const std::string rest = s.script.substr(dot);
    const bool special = rest.size() > 3;
    const int next = special ? 2 + (n - 1) % 2 : 2 + (n - 1) % 10;
    s.script = std::to_string(next) + rest;
    ClassicMatchFrame base = after;
    base.after_scene.reset();
    const bool rink_stays = after.scene.has_value();
    if (rink_stays) base.scene.reset();
    return classic_goal_scene_frames(data, base, s, s.side, s.goal, rink_stays);
}

}  // namespace em::ui
