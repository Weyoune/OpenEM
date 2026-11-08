// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Frueher, Verein, Spieler, Fairplay, WM (ovr034:2691 dispatch).
#include "ui/classic_app.hpp"
#include "ui/classic_stats_history.hpp"
#include "ui/classic_stats_players.hpp"

#include <algorithm>

namespace em::ui {

void ClassicApplication::open_statistics(int item){
    if(item!=1&&item!=3){notice("Dieser Bereich ist noch nicht nachgebaut.");return;}
    stats_page_=item;
    if(item==1)stats_scroll_=0;                     // 4495:417F local scroll state
    if(item==3){stats_mode_=2;stats_managers_=false;} // 4080:0000 starts with SCORER / LIGA
    screen_=ClassicScreen::Statistics;
}

void ClassicApplication::draw_statistics(){
    if(stats_page_==99)return;
    if(stats_page_==1)draw_classic_stats_history(canvas_,snapshot(),stats_scroll_);
    else if(stats_page_==3)draw_classic_stats_players(canvas_,snapshot(),stats_mode_,stats_managers_);
}

void ClassicApplication::click_statistics(int x,int y,bool right){
    if(right){screen_=ClassicScreen::Main;return;}
    if(stats_page_==1){
        stats_scroll_=std::clamp(stats_scroll_+classic_stats_history_scroll(x,y),0,30);
    }else if(stats_page_==3){
        const int button=classic_stats_players_button(x,y);
        if(button>=0&&button<3)stats_mode_=button;
        else if(button==3)stats_managers_=false;
        else if(button==4)stats_managers_=true;
    }
}

bool ClassicApplication::render_statistics(std::string_view name){
    if(name=="stats-history"||name=="stats-history-scrolled"){
        open_statistics(1);
        if(name=="stats-history-scrolled")stats_scroll_=30;
        return true;
    }
    if(name=="stats-players"||name=="stats-players-goals"||name=="stats-players-assists"||name=="stats-players-managers"){
        open_statistics(3);
        if(name=="stats-players-goals")stats_mode_=0;
        else if(name=="stats-players-assists")stats_mode_=1;
        else if(name=="stats-players-managers")stats_managers_=true;
        return true;
    }
    if(name=="debug-kb20"||name=="debug-kb22"){stats_page_=99;screen_=ClassicScreen::Statistics;canvas_.background();if(name=="debug-kb20")canvas_.blit(20,0,0);return true;}
    if(name=="euro-fixtures1"){euro_from_fixtures_=true;open_euro(euro_fixture_page_);return true;}
    return false;
}

} // namespace em::ui
