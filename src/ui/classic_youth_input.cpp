// SPDX-License-Identifier: GPL-3.0-only
// Input handling of the Jugendteams page.
#include "ui/classic_app.hpp"
#include "game/youth.hpp"

namespace em::ui {
// Right click leaves the page; a left click on a funding bar sets that team's funding from the x position
// (the rule is in Game::adjust_youth_funding).
void ClassicApplication::click_youth(int x,int y,bool right){
    if(right){screen_=ClassicScreen::Main;return;}
    const int kind=youth_funding_row(y);
    if(kind>=0)action([&](Game& game){game.adjust_youth_funding(kind,x);return ActionResult{};});
}
} // namespace em::ui
