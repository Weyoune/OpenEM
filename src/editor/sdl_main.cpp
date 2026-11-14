// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor - portable front end (Linux, macOS; on Windows built as em_editor_sdl next to the Win32 em_editor).
// SDL2 window with Dear ImGui drawn through SDL_Renderer (no OpenGL). The document logic is shared with the Win32
// front end (pixel_editor.cpp, palette_doc.cpp, team_doc.cpp, map_doc.cpp); see sdl_app.hpp for the file layout.
//
//   em_editor [--data <EM folder>] [--file <KB number>] [--tab <0..4>] [--shot out.bmp]
//             [--export-png out.png | --import-png in.png]
// --shot renders a few frames of the window into an off-screen surface, saves the last one as a BMP and exits (for
// headless checks; falls back to SDL's dummy video driver when no display is available). --export-png /
// --import-png together with --file convert one picture without a window (an import is saved at once) and exit with
// the codes of the Win32 editor: 1 no game folder, 2 file not loadable, 3 export failed, 4 import failed, 5 save failed.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "editor/sdl_app.hpp"
#include "editor/sdl_image_io.hpp"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

// Roboto Medium, compiled in by CMakeLists.txt (generated em_editor_font.cpp)
extern const unsigned char em_editor_font[];
extern const unsigned int em_editor_font_size;

namespace {

using namespace em::editor::sdl;
using em::editor::PixelEditor;

// Command-line options (pairs of "--name value", as in the Win32 editor).
struct Options {
    fs::path data;
    int file = -1, tab = -1;
    std::string shot, export_png, import_png;
};

Options parse_options(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--data") o.data = path_from_utf8(v);
        else if (k == "--file") o.file = std::atoi(v.c_str());
        else if (k == "--tab") o.tab = std::atoi(v.c_str());
        else if (k == "--shot") o.shot = v;
        else if (k == "--export-png") o.export_png = v;
        else if (k == "--import-png") o.import_png = v;
    }
    return o;
}

// --file N with --export-png out.png or --import-png in.png (saves): converts one picture without a window.
int convert(const Options& o) {
    const fs::path data = o.data.empty() ? find_game_folder() : o.data;
    if (!is_game_folder(data)) {
        std::fprintf(stderr, "em_editor: Spielordner nicht gefunden (--data <Ordner mit EM.EXE und KB>)\n");
        return 1;
    }
    try {
        PixelEditor e(data);
        if (!e.load(o.file)) return 2;
        if (!o.export_png.empty()) return em::editor::export_png(path_from_utf8(o.export_png), e.image(), e.palette()) ? 0 : 3;
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (!em::editor::load_argb(path_from_utf8(o.import_png), px, w, h) || !e.import_argb(px, w, h, true)) return 4;
        return e.save() ? 0 : 5;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "em_editor: %s\n", ex.what());
        return 1;
    }
}

// The tab and document shown first: --tab, or the goal scene tab for a sheet given with --file; --file opens that
// picture (palette on the palette tab, map on the hall map tab).
void initial_view(App& a, const Options& o) {
    int start = o.tab >= 0 ? std::clamp(o.tab, 0, kTabCount - 1) : kTabPictures;
    for (const auto& f : a.ed->files())
        if (f.number == o.file && f.kind != PixelEditor::Kind::Image && o.tab < 0) start = kTabSheets;
    switch_tab(a, start);
    if (o.file >= 0) {
        if (start == kTabPictures || start == kTabSheets) {
            if (a.ed->load(o.file)) fit_zoom(a);
        } else if (start == kTabPalettes) {
            a.pal->load(o.file);
        } else if (start == kTabMaps) {
            const auto& n = em::editor::MapDoc::numbers();
            if (std::find(n.begin(), n.end(), o.file) != n.end()) open_map(a, o.file);
        } else if (start == kTabTeams && em::editor::TeamDoc::editable(o.file)) {
            a.team_sel = o.file;
            load_team_fields(a);
        }
    }
    a.list_scroll = true;
}

// Light ImGui style resembling the Win32 editor, scaled for the display, and the compiled-in UI font. `fb` is the
// ratio of framebuffer to window pixels (2 on Retina displays): the font is rasterised at that resolution.
void setup_style(float scale, float fb) {
    ImGui::StyleColorsLight();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0;
    st.ChildRounding = 0;
    st.FrameRounding = 2;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    st.AntiAliasedLines = false;  // crisp one-pixel grid lines and selection frames
    st.AntiAliasedLinesUseTex = false;
    st.ScaleAllSizes(scale);
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // static array
    cfg.OversampleH = 2;
    // Latin-1 (German umlauts, sharp s, multiplication sign) plus dashes, ellipsis, minus and the dot of the file list
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2026, 0x2026, 0x2212, 0x2212, 0x25CF, 0x25CF, 0};
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(em_editor_font), int(em_editor_font_size), std::round(15.0f * scale * fb), &cfg, ranges);
    io.FontGlobalScale = 1.0f / fb;
}

void fail(const char* what) {
    std::fprintf(stderr, "em_editor: %s: %s\n", what, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenEM Editor", (std::string(what) + ": " + SDL_GetError()).c_str(), nullptr);
}

}  // namespace

// Entry point: options, command-line conversion, SDL and ImGui set-up, game folder lookup (start screen when none is
// found), then the main loop (or the --shot frames).
int main(int argc, char* argv[]) {
    const Options o = parse_options(argc, argv);
    if ((!o.export_png.empty() || !o.import_png.empty()) && o.file >= 0) return convert(o);

    App a;
    a.shot = !o.shot.empty();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        if (!a.shot) {
            fail("SDL_Init");
            return 1;
        }
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");  // headless check without a display
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            fail("SDL_Init");
            return 1;
        }
    }
#ifndef __APPLE__  // macOS scales by itself (points vs. Retina pixels, handled through the framebuffer ratio)
    if (!a.shot) {
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0) a.scale = std::clamp(std::round(ddpi / 96.0f * 4) / 4, 1.0f, 3.0f);
    }
#endif
    const int win_w = int(1180 * a.scale), win_h = int(760 * a.scale);
    a.window = SDL_CreateWindow("OpenEM Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (a.shot ? SDL_WINDOW_HIDDEN : 0));
    if (!a.window) {
        fail("SDL_CreateWindow");
        return 1;
    }
    SDL_SetWindowMinimumSize(a.window, int(900 * a.scale), int(600 * a.scale));
    SDL_Surface* shot_surface = nullptr;
    if (a.shot) {  // software rendering into a surface: the same pixels on every system, no display needed
        shot_surface = SDL_CreateRGBSurfaceWithFormat(0, win_w, win_h, 32, SDL_PIXELFORMAT_RGB888);
        a.renderer = shot_surface ? SDL_CreateSoftwareRenderer(shot_surface) : nullptr;
    } else {
        a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!a.renderer) {
        fail("SDL_CreateRenderer");
        return 1;
    }
    float fb = 1;
    if (!a.shot) {
        int ww = 0, wh = 0, rw = 0, rh = 0;
        SDL_GetWindowSize(a.window, &ww, &wh);
        if (SDL_GetRendererOutputSize(a.renderer, &rw, &rh) == 0 && ww > 0) fb = std::clamp(float(rw) / float(ww), 1.0f, 4.0f);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini next to the program
    setup_style(a.scale, fb);
    ImGui_ImplSDL2_InitForSDLRenderer(a.window, a.renderer);
    ImGui_ImplSDLRenderer2_Init(a.renderer);

    // game folder: --data, else OPENEM_DATA, program folder, current folder (each with EM and data subfolders)
    const fs::path data = o.data.empty() ? find_game_folder() : o.data;
    if (!data.empty()) {
        std::string err;
        if (!open_game_folder(a, data, err)) a.start_error = err;
    }

    bool initialized = false;
    int frames = 0, idle = 0;
    constexpr int kShotFrames = 4;  // ImGui needs a few frames until auto-sized windows and the fit zoom settle
    int result = 0;
    while (!a.quit) {
        bool got = false;
        auto handle = [&](const SDL_Event& ev) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            const bool close = ev.type == SDL_QUIT ||
                               (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE && ev.window.windowID == SDL_GetWindowID(a.window));
            if (close) {
                if (!a.ready) a.quit = true;
                else if (a.dialogs.empty()) request_quit(a);
            }
            got = true;
        };
        SDL_Event ev;
        if (!a.shot && idle > 3 && SDL_WaitEventTimeout(&ev, 500)) handle(ev);  // sleep while nothing happens
        while (SDL_PollEvent(&ev)) handle(ev);
        idle = got ? 0 : idle + 1;
        if (a.quit) break;
        if (a.ready && !initialized) {
            initial_view(a, o);
            initialized = true;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (a.ready) draw_frame(a);
        else draw_start_screen(a);
        ImGui::Render();
        const ImGuiIO& io = ImGui::GetIO();
        SDL_RenderSetScale(a.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(a.renderer, 240, 240, 240, 255);
        SDL_RenderClear(a.renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), a.renderer);
        if (a.shot) {
            if (++frames >= kShotFrames) {
                if (SDL_SaveBMP(shot_surface, o.shot.c_str()) != 0) {
                    std::fprintf(stderr, "em_editor: %s\n", SDL_GetError());
                    result = 1;
                }
                break;
            }
        } else {
            SDL_RenderPresent(a.renderer);
        }
    }

    // textures belong to the renderer: free them first
    a.pic_tex.reset();
    a.crest_tex.reset();
    a.rink_tex.reset();
    a.atlas_tex.reset();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(a.renderer);
    if (shot_surface) SDL_FreeSurface(shot_surface);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    return result;
}
