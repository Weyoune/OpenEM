// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/settings.hpp"
#include "ui/classic_canvas.hpp"

// Einstellungen (settings) page of the classic frontend: drawing and hit testing of its switches and the
// game-speed strip. The values live in GameSettings (game/settings.hpp).

namespace em::ui {
// The original Einstellungen page. Widgets 0..8 are the 3 x 3 grid (Drittelpause, Ergebnisse, Tabelle per league),
// 9 Torszenen, 10 DEB-Pokal, 11 Europapokal, 12 WM-Spiele, 13 Hintergrundgrafik, 14 Scrolling of the manager.
// Result of classic_settings_hit: at most one of the two fields is set.
struct ClassicSettingsHit {
    int widget = -1;  // 0..14
    int speed = -1;   // 0..70 when the penguin row was hit
};
// Free memory shown as "RAM: n Bytes frei." — a fixed value as the original displayed it in a typical
// session (DOS memory is not modelled).
constexpr int kClassicSettingsFreeMemory = 281216;

// Paints the page for the given settings; switch 14 shows the Scrolling value of manager.
void draw_classic_settings(ClassicCanvas& canvas, const GameSettings& settings, int manager);
// Hit test at (x, y): a switch, or the speed strip (eight penguins, 12 pixels apart, speed 0, 10, ..., 70).
ClassicSettingsHit classic_settings_hit(int x, int y);
}  // namespace em::ui
