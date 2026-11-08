// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Frueher, Verein, Spieler, Fairplay, WM (ovr034:2691 dispatch).
#include "ui/classic_app.hpp"
#include "ui/classic_stats_players.hpp"

namespace em::ui {

void ClassicApplication::open_statistics(int item){
    if(item<1||item>5){notice("Dieser Bereich ist noch nicht nachgebaut.");return;}
    if(item!=3){notice("Dieser Bereich ist noch nicht nachgebaut.");return;}
    stats_page_=item;
    if(item==3){stats_mode_=2;stats_managers_=false;} // 4080:0000 starts with SCORER / LIGA
    screen_=ClassicScreen::Statistics;
}

void ClassicApplication::draw_statistics(){
    if(stats_page_==3)draw_classic_stats_players(canvas_,snapshot(),stats_mode_,stats_managers_);
}

void ClassicApplication::click_statistics(int x,int y,bool right){
    if(right){screen_=ClassicScreen::Main;return;}
    if(stats_page_==3){
        const int button=classic_stats_players_button(x,y);
        if(button>=0&&button<3)stats_mode_=button;
        else if(button==3)stats_managers_=false;
        else if(button==4)stats_managers_=true;
    }
}

bool ClassicApplication::render_statistics(std::string_view name){
    if(name=="stats-players"||name=="stats-players-goals"||name=="stats-players-assists"||name=="stats-players-managers"){
        open_statistics(3);
        if(name=="stats-players-goals")stats_mode_=0;
        else if(name=="stats-players-assists")stats_mode_=1;
        else if(name=="stats-players-managers")stats_managers_=true;
        return true;
    }
    if(name=="euro-fixtures1"){euro_from_fixtures_=true;open_euro(euro_fixture_page_);return true;}
    return false;
}

} // namespace em::ui
