// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

namespace em::ui {

struct ClassicClubInfoText {
    int x = 98, baseline = 0, right = -1;
    uint8_t color = 30;
    std::string native;
};
struct ClassicClubInfoLine {
    int x1, y1, x2, y2;
    uint8_t color;
};
struct ClassicClubInfo {
    std::string native_name, native_caption;
    int crest = 143, reporting_mode = 0;
    bool schedule = false, has_results = false;
    std::array<uint8_t,8> faces{};
    uint8_t summary_face = 2;
    std::vector<ClassicClubInfoText> rows;
    std::vector<ClassicClubInfoLine> graph;
    std::vector<ClassicClubInfoText> graph_labels;
};

// Native6668:0095(team,2,0,0), entered from Stärken in the ordinary menu.
// Reads native raw records and active manager state0788. No162C:146C cache
// refresh, reporting-option mutation, RNG, simulation or background restore.
ClassicClubInfo classic_club_info(const HokFile& h, int team,int mode=2);
void draw_classic_club_info(ClassicCanvas& canvas, const HokFile& h, int team,int mode=2);

} // namespace em::ui
