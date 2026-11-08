// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Frueher, Verein, Spieler, Fairplay, WM (ovr034:2691 dispatch).
#include "ui/classic_app.hpp"
#include "ui/classic_stats_club.hpp"
#include "ui/classic_stats_fairplay.hpp"
#include "ui/classic_stats_history.hpp"
#include "ui/classic_stats_players.hpp"
#include "ui/classic_stats_wm.hpp"

#include <algorithm>

namespace em::ui {

void ClassicApplication::open_statistics(int item){
    if(item<1||item>5){notice("Dieser Bereich ist noch nicht nachgebaut.");return;}
    stats_page_=item;
    if(item==1)stats_scroll_=0;                     // 4495:417F local scroll state
    if(item==2)stats_scroll_=classic_stats_club_years(snapshot())-1; // 4495:2513 starts with the last year
    if(item==3){stats_mode_=2;stats_managers_=false;} // 4080:0000 starts with SCORER / LIGA
    screen_=ClassicScreen::Statistics;
}

void ClassicApplication::draw_statistics(){
    if(stats_page_==99)return;
    if(stats_page_==1)draw_classic_stats_history(canvas_,snapshot(),stats_scroll_);
    else if(stats_page_==2)draw_classic_stats_club(canvas_,snapshot(),stats_scroll_);
    else if(stats_page_==3)draw_classic_stats_players(canvas_,snapshot(),stats_mode_,stats_managers_);
    else if(stats_page_==4)draw_classic_stats_fairplay(canvas_,snapshot());
    else if(stats_page_==5)draw_classic_stats_wm(canvas_,snapshot());
}

void ClassicApplication::click_statistics(int x,int y,bool right){
    if(right||stats_page_==4){screen_=ClassicScreen::Main;return;} // Fairplay: sub_19B08 closes on any press
    if(stats_page_==1){
        stats_scroll_=std::clamp(stats_scroll_+classic_stats_history_scroll(x,y),0,30);
    }else if(stats_page_==2){
        const int years=classic_stats_club_years(snapshot()),arrow=classic_stats_club_arrow(x,y);
        if(years&&arrow==-1&&stats_scroll_<years-1)++stats_scroll_;
        else if(years&&arrow==1&&stats_scroll_>0)--stats_scroll_;
    }else if(stats_page_==3){
        const int button=classic_stats_players_button(x,y);
        if(button>=0&&button<3)stats_mode_=button;
        else if(button==3)stats_managers_=false;
        else if(button==4)stats_managers_=true;
    }else if(stats_page_==5){
        const int team=classic_stats_wm_team(x,y);
        if(team>=0)open_club_info(team,2); // 5396:1ADE stub_ovl13_0095(team,2,0,0)
    }
}

bool ClassicApplication::render_statistics(std::string_view name){
    if(name=="stats-history"||name=="stats-history-scrolled"){
        open_statistics(1);
        if(name=="stats-history-scrolled")stats_scroll_=30;
        return true;
    }
    if(name=="stats-club"){open_statistics(2);return true;}
    if(name=="stats-wm"){open_statistics(5);return true;}
    if(name=="stats-fairplay"){open_statistics(4);return true;}
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
