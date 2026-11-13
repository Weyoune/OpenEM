// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Frueher, Verein, Spieler, Fairplay, WM: opening, drawing and input of the five statistics
// pages of the classic frontend (the pages themselves are drawn by classic_stats_*.cpp).
#include "ui/classic_app.hpp"
#include "ui/classic_stats_club.hpp"
#include "ui/classic_stats_fairplay.hpp"
#include "ui/classic_stats_history.hpp"
#include "ui/classic_stats_players.hpp"
#include "ui/classic_stats_wm.hpp"

#include <SDL.h>
#include <algorithm>

namespace em::ui {

// Opens statistics page item (1 Frueher, 2 Verein, 3 Spieler, 4 Fairplay, 5 WM) with the original's start state.
void ClassicApplication::open_statistics(int item){
    if(item<1||item>5){notice("Dieser Bereich ist noch nicht nachgebaut.");return;}
    stats_page_=item;
    if(item==1)stats_scroll_=0;                     // Frueher starts at the top of the list
    if(item==2)stats_scroll_=classic_stats_club_years(snapshot())-1; // Verein starts with the last year
    if(item==3){stats_mode_=2;stats_managers_=false;} // Spieler starts with SCORER / LIGA
    screen_=ClassicScreen::Statistics;
}

// Draws the open statistics page.
void ClassicApplication::draw_statistics(){
    if(stats_page_==1)draw_classic_stats_history(canvas_,snapshot(),stats_scroll_);
    else if(stats_page_==2)draw_classic_stats_club(canvas_,snapshot(),stats_scroll_);
    else if(stats_page_==3)draw_classic_stats_players(canvas_,snapshot(),stats_mode_,stats_managers_);
    else if(stats_page_==4)draw_classic_stats_fairplay(canvas_,snapshot());
    else if(stats_page_==5)draw_classic_stats_wm(canvas_,snapshot());
}

// Click on a statistics page. A right click leaves every page, Fairplay closes on any press.
void ClassicApplication::click_statistics(int x,int y,bool right){
    if(right||stats_page_==4){screen_=ClassicScreen::Main;return;} // Fairplay closes on any press
    if(stats_page_==1){
        stats_scroll_=std::clamp(stats_scroll_+classic_stats_history_scroll(x,y),0,30);
        // As in the original, a press on an arrow latches it and keeps the list scrolling while the button is
        // held; a press on the list bands scrolls at the current pointer position.
        const bool arrow=x>=0xA5&&x<=0xA5+8&&y>=0x1E&&y<=0x27+8&&classic_stats_history_scroll(x,y)!=0;
        stats_hold_=StatsHold{arrow,x,y,classic_ticks()+kStatsRepeatMs};
    }else if(stats_page_==2){
        // The upper arrow moves to the next year, the lower one back; the original has no press check here,
        // so a held arrow repeats.
        const int years=classic_stats_club_years(snapshot()),arrow=classic_stats_club_arrow(x,y);
        if(years&&arrow==1&&stats_scroll_<years-1)++stats_scroll_;
        else if(years&&arrow==-1&&stats_scroll_>0)--stats_scroll_;
        if(arrow)stats_hold_=StatsHold{true,x,y,classic_ticks()+kStatsRepeatMs};
    }else if(stats_page_==3){
        const int button=classic_stats_players_button(x,y);
        if(button>=0&&button<3)stats_mode_=button;
        else if(button==3)stats_managers_=false;
        else if(button==4)stats_managers_=true;
        else if(button<0)open_stats_player(x,y);
    }else if(stats_page_==5){
        const int team=classic_stats_wm_team(x,y);
        if(team>=0)open_club_info(team,2); // club info of the nation in mode 2
    }
}

// A press on a listed player searches the squads of the human managers (0..N-1) and of squad 4 (transfer
// pool) for the player record; the first hit opens the player page for that slot with that squad as the
// current manager, afterwards the list is drawn again. Players found nowhere do nothing.
void ClassicApplication::open_stats_player(int x,int y){
    try{
        const auto& h=snapshot();
        const int player=classic_stats_players_row(classic_stats_players_view(h,stats_mode_,stats_managers_),x,y);
        if(player<=0)return;
        const int humans=h.state.at(0x0DC6);
        for(int m=0;m<5;++m){
            if(m>=humans&&m!=4)continue;
            const size_t slots=hok_squad(h,m).size(),base=0x696D+size_t(m)*0x618;  // squad +0x0F = player id
            for(size_t slot=0;slot<slots;++slot)
                if(h.state.at(base+slot*0x34+0x0F)==player){open_player(int(slot),m);return;}
        }
    }catch(const std::exception& e){notice(e.what());}
}

// Held arrow (see click_statistics): the original repeats once per pass of its page loop; OpenEM repeats every
// kStatsRepeatMs (in the original, Frueher scrolls about five rows during a 0.2 s press) until release().
void ClassicApplication::tick_stats_hold(uint64_t now){
    if(!stats_hold_)return;
    if(screen_!=ClassicScreen::Statistics||(stats_page_!=1&&stats_page_!=2)){stats_hold_.reset();return;}
    while(stats_hold_&&now>=stats_hold_->next){
        const auto hold=*stats_hold_;
        const int x=hold.latched?hold.x:mouse_x_,y=hold.latched?hold.y:mouse_y_;
        click_statistics(x,y,false);
        if(stats_hold_)stats_hold_->next=hold.next+kStatsRepeatMs;
        if(stats_hold_){stats_hold_->latched=hold.latched;stats_hold_->x=hold.x;stats_hold_->y=hold.y;}
    }
}

// Opens the statistics page named by a render target name (e.g. "stats-club"); false for an unknown name.
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
    if(name=="euro-fixtures1"){euro_from_fixtures_=true;open_euro(euro_fixture_page_);return true;}
    return false;
}

} // namespace em::ui
