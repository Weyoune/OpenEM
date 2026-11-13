// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

// Classic club-information window: content model and renderer.
namespace em::ui {

// One text line of the window (game encoding), drawn with the Mini font.
struct ClassicClubInfoText {
    int x = 98, baseline = 0, right = -1;  // position; right >= 0 centres between x and right
    uint8_t color = 30;                    // palette index
    std::string native;                    // text in the game's 8-bit encoding
};
// One segment of the league-position curve.
struct ClassicClubInfoLine {
    int x1, y1, x2, y2;
    uint8_t color;
};
// Everything the window shows, computed from the save state.
struct ClassicClubInfo {
    std::string native_name, native_caption;  // title (team name) and caption line (position, strength)
    int crest = 143, reporting_mode = 0;       // crest picture id; reporting mode 0..3 (button icon)
    bool schedule = false, has_results = false; // "Spielplan" button shown; faces/statistics available
    std::array<uint8_t,8> faces{};             // last eight results: 0 none, 1 defeat, 2 draw, 3 win
    uint8_t summary_face = 2;                  // overall trend face (1..3)
    std::vector<ClassicClubInfoText> rows;     // statistics text lines
    std::vector<ClassicClubInfoLine> graph;    // axes and position curve
    std::vector<ClassicClubInfoText> graph_labels; // final-position label of the curve
};

// Builds the club-information window for team (0..149) as the game shows it
// when entered from "Stärken" in the ordinary menu. mode selects the
// statistics scope: 0 home, 1 away, 2 total. Reads only the save state
// (including the active manager at 0x0788); it changes nothing, uses no
// random numbers and does not touch the reporting option. Throws
// FormatError for invalid arguments or a truncated/inconsistent state.
ClassicClubInfo classic_club_info(const HokFile& h, int team,int mode=2);
// Computes the window with classic_club_info() and draws it over the
// current screen.
void draw_classic_club_info(ClassicCanvas& canvas, const HokFile& h, int team,int mode=2);

} // namespace em::ui
