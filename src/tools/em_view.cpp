// SPDX-License-Identifier: GPL-3.0-only
// em_view - developer viewer: shows one KB image in an SDL window (scaled x3); Esc or closing
// the window quits.
//
//   em_view <file.vga> [pal.vga]
// The palette is taken from the image itself (320x240 screens carry one after the pixel data,
// at offset 6 + the u16 at offset 4), from [pal.vga] (a palette file or an image with palette)
// or, failing both, a grey ramp.
#include <SDL.h>

#include <cstdio>
#include <string>

#include "formats/vga.hpp"

using namespace em;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: em_view <file.vga> [pal.vga]\n");
        return 2;
    }
    Image img;
    Palette pal;
    try {
        Bytes d = read_file(argv[1]);
        img = decode_vga(d);
        if (classify_vga(d) == VgaKind::ImageWithPalette) {
            pal = decode_palette(d, 6 + le16(d, 4));
        } else if (argc > 2) {
            Bytes p = read_file(argv[2]);
            pal = classify_vga(p) == VgaKind::Palette ? decode_palette(p) : decode_palette(p, 6 + le16(p, 4));
        } else {
            for (int i = 0; i < 256; i++) pal.rgb[i] = {uint8_t(i), uint8_t(i), uint8_t(i)};
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int scale = 3;
    std::string title = std::string("OpenEM view - ") + argv[1];
    SDL_Window* win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       img.width * scale, img.height * scale, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    std::vector<uint32_t> argb(img.pixels.size());
    for (size_t i = 0; i < img.pixels.size(); i++) {
        auto& c = pal.rgb[img.pixels[i]];
        argb[i] = 0xff000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
    }
    SDL_UpdateTexture(tex, nullptr, argb.data(), img.width * 4);

    for (bool run = true; run;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) run = false;
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
