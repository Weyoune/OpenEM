// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/settings.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Native Einstellungen page (ovr028:2B3C). Widgets 0..8 are the 3 x 3 grid (Drittelpause, Ergebnisse, Tabelle per league),
// 9 Torszenen, 10 DEB-Pokal, 11 Europapokal, 12 WM-Spiele, 13 Hintergrundgrafik, 14 Scrolling of the manager.
struct ClassicSettingsHit {
    int widget = -1;  // 0..14
    int speed = -1;   // 0..70 when the penguin row was hit
};
constexpr int kClassicSettingsFreeMemory = 281216;  // "RAM: n Bytes frei." of the reference session (DOS memory is not modelled)

void draw_classic_settings(ClassicCanvas& canvas, const GameSettings& settings, int manager);
ClassicSettingsHit classic_settings_hit(int x, int y);
}  // namespace em::ui
