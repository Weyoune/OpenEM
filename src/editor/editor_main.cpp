// SPDX-License-Identifier: GPL-3.0-only
// em_editor - asset editor for the original Eishockey Manager data (KB\*.vga).
//
//   em_editor [--data <EM folder>] [--file <KB number>] [--render out.bmp]
// --render draws the first frame to a BMP without opening a window (for checks).
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "editor/editor_ui.hpp"
#include "editor/pixel_editor.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::editor;

namespace {
bool is_game_folder(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && (fs::exists(p / "EM.EXE", ec) || fs::exists(p / "EM.exe", ec) || fs::exists(p / "em.exe", ec)) &&
           (fs::is_directory(p / "KB", ec) || fs::is_directory(p / "kb", ec));
}

// OPENEM_DATA, then next to the program (itself, EM, data), the development layout work/EM above the program, the
// current folder - the same order as openem.
fs::path find_game_folder(const fs::path& exe_dir) {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("OPENEM_DATA")) candidates.push_back(fs::u8path(env));
    for (fs::path up = exe_dir; !up.empty(); up = up.parent_path()) {
        candidates.push_back(up / "work" / "EM");
        if (up == up.parent_path()) break;
    }
    for (const fs::path& base : {exe_dir, fs::current_path()})
        for (const char* sub : {"", "EM", "data"}) candidates.push_back(*sub ? base / sub : base);
    for (const auto& c : candidates)
        if (is_game_folder(c)) return c;
    return {};
}

void render_bmp(const Frame& f, const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kFrameW, kFrameH, 32, SDL_PIXELFORMAT_ARGB8888);
    std::memcpy(s->pixels, f.data(), size_t(kFrameW) * kFrameH * 4);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}
}  // namespace

int main(int argc, char** argv) {
    try {
        fs::path data;
        int file = -1;
        const char* render = nullptr;
        for (int n = 1; n < argc; n++) {
            const std::string o = argv[n];
            if (n + 1 >= argc) throw std::runtime_error("em_editor [--data folder] [--file KB-number] [--render out.bmp]");
            const std::string v = argv[++n];
            if (o == "--data") data = fs::u8path(v);
            else if (o == "--file") file = std::stoi(v);
            else if (o == "--render") render = argv[n];
            else throw std::runtime_error("unknown option " + o);
        }
        if (data.empty()) {
            char* base = SDL_GetBasePath();
            const fs::path exe_dir = base ? fs::u8path(base) : fs::current_path();
            SDL_free(base);
            data = find_game_folder(exe_dir);
        }
        if (!is_game_folder(data))
            throw std::runtime_error("em_editor needs your copy of the original game (the folder with EM.EXE and KB). "
                                     "Start with --data <folder> or set OPENEM_DATA.");
        const Bytes exe = read_file(data / "EM.EXE");
        ui::ClassicFont font(exe);
        Frame frame;
        frame.set_font(&font);
        PixelEditor pixel(data);
        if (file >= 0) pixel.load(file);

        if (render) {
            pixel.draw(frame);
            render_bmp(frame, render);
            return 0;
        }

        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        SDL_Window* win = SDL_CreateWindow("OpenEM Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kFrameW * 2,
                                           kFrameH * 2, SDL_WINDOW_RESIZABLE);
        if (!win) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(win, kFrameW, kFrameH);
        SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(ren, kFrameW, kFrameH);
        SDL_RenderSetIntegerScale(ren, SDL_TRUE);
        SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kFrameW, kFrameH);

        auto logical = [&](int wx, int wy, int& lx, int& ly) {
            float fx, fy;
            SDL_RenderWindowToLogical(ren, wx, wy, &fx, &fy);
            lx = int(fx);
            ly = int(fy);
        };
        for (bool run = true; run;) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                int lx, ly;
                switch (ev.type) {
                    case SDL_QUIT: run = false; break;
                    case SDL_MOUSEBUTTONDOWN:
                        logical(ev.button.x, ev.button.y, lx, ly);
                        pixel.mouse_down(lx, ly, ev.button.button);
                        break;
                    case SDL_MOUSEBUTTONUP:
                        logical(ev.button.x, ev.button.y, lx, ly);
                        pixel.mouse_up(lx, ly, ev.button.button);
                        break;
                    case SDL_MOUSEMOTION:
                        logical(ev.motion.x, ev.motion.y, lx, ly);
                        pixel.mouse_move(lx, ly);
                        break;
                    case SDL_MOUSEWHEEL: {
                        int mx, my;
                        SDL_GetMouseState(&mx, &my);
                        logical(mx, my, lx, ly);
                        pixel.wheel(lx, ly, ev.wheel.y);
                        break;
                    }
                    case SDL_KEYDOWN: {
                        const SDL_Keycode k = ev.key.keysym.sym;
                        const bool ctrl = ev.key.keysym.mod & KMOD_CTRL, shift = ev.key.keysym.mod & KMOD_SHIFT;
                        if (k == SDLK_LEFT) pixel.pan(-4, 0);
                        else if (k == SDLK_RIGHT) pixel.pan(4, 0);
                        else if (k == SDLK_UP) pixel.pan(0, -4);
                        else if (k == SDLK_DOWN) pixel.pan(0, 4);
                        else if (k == SDLK_PLUS || k == SDLK_KP_PLUS || k == SDLK_EQUALS) pixel.key('+', ctrl, shift);
                        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) pixel.key('-', ctrl, shift);
                        else if (k >= 'a' && k <= 'z') pixel.key(char(k), ctrl, shift);
                        break;
                    }
                    default: break;
                }
            }
            pixel.draw(frame);
            SDL_UpdateTexture(tex, nullptr, frame.data(), kFrameW * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
            SDL_Delay(16);
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "em_editor: %s\n", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenEM Editor", e.what(), nullptr);
        return 1;
    }
    return 0;
}
