// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "ui/classic_graph.hpp"
#include "ui/classic_status.hpp"
#include "ui/classic_totals.hpp"
#include "ui/classic_roster.hpp"
#include "ui/classic_bank.hpp"
#include "ui/classic_lineup.hpp"
#include "ui/classic_tactics.hpp"
#include "ui/classic_transfer.hpp"
#include "ui/classic_opponent.hpp"
#include "ui/classic_strengths.hpp"
#include "ui/classic_team_schedule.hpp"
#include "ui/classic_euro.hpp"
#include "ui/classic_wm.hpp"
#include "ui/classic_league_fixtures.hpp"
#include "ui/classic_playoff.hpp"
#include "ui/classic_cup.hpp"
#include "ui/classic_settings.hpp"
#include <algorithm>
#include <cstdio>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow,bold=ClassicFontId::NewBold;
std::string padded(int value,int width){std::string result=std::to_string(value);if(int(result.size())<width)result.insert(0,size_t(width-int(result.size())),'%');return result;}
}
void ClassicApplication::draw_lineup(){draw_classic_lineup(canvas_,snapshot(),0,selected_line_,lineup_cells_?&*lineup_cells_:nullptr,lineup_cells_?&lineup_six_:nullptr);}
void ClassicApplication::open_strengths(int league){
    action([](Game& game){game.enter_strengths();return ActionResult{};});
    strengths_league_=league;strengths_mode_=0;begin_page_working(ClassicScreen::Strengths);
}
void ClassicApplication::begin_page_working(ClassicScreen screen){
    menu_working_.reset();menu_date_dirty_=team_schedule_open_=false;menu_pairing_league_=-1;club_info_.reset();club_pairings_.reset();screen_=screen;
    menu_working_=std::make_unique<HokFile>(clone_hok(snapshot()));menu_working_revision_=session_.revision();
}
void ClassicApplication::draw_strengths(){draw_classic_strengths(canvas_,snapshot(),strengths_league_,strengths_mode_);}
void ClassicApplication::open_euro(int page){
    if(screen_!=ClassicScreen::EuroCup||!menu_working_)begin_page_working(ClassicScreen::EuroCup);
    euro_page_=page;(euro_from_fixtures_?euro_fixture_page_:euro_table_page_)=page; // separate DS:5126 / DS:5123 memories
    (void)snapshot();
    const Bytes date(menu_working_->state.begin()+0x8E21,menu_working_->state.begin()+0x8E2D);
    prepare_classic_euro_working(*menu_working_,calendar_,page);
    menu_date_dirty_=menu_date_dirty_||!std::equal(date.begin(),date.end(),menu_working_->state.begin()+0x8E21);
}
void ClassicApplication::draw_euro(){draw_classic_euro(canvas_,snapshot(),calendar_,euro_page_);}
void ClassicApplication::open_wm(int page,int round){
    if(screen_!=ClassicScreen::WorldChampionship||!menu_working_)begin_page_working(ClassicScreen::WorldChampionship);
    if(page==11||page==12)wm_table_page_=page;
    else if(page==8||page==9){wm_fixture_page_=page;wm_previous_fixtures_=page;}
    else if(page==10)wm_fixture_page_=page;
    wm_page_=page;wm_round_=round;(void)snapshot();
    const Bytes date(menu_working_->state.begin()+0x8E21,menu_working_->state.begin()+0x8E2D);
    prepare_classic_wm_working(*menu_working_,calendar_,page);
    menu_date_dirty_=menu_date_dirty_||!std::equal(date.begin(),date.end(),menu_working_->state.begin()+0x8E21);
}
void ClassicApplication::draw_wm(){draw_classic_wm(canvas_,snapshot(),calendar_,wm_page_,wm_round_);}
void ClassicApplication::open_league_fixtures(int league,int round,int width){
    if(screen_!=ClassicScreen::LeagueFixtures||!menu_working_)begin_page_working(ClassicScreen::LeagueFixtures);
    (void)snapshot();
    const auto view=classic_league_fixtures(*menu_working_,schedule_,calendar_,league,round,width);
    prepare_classic_league_fixtures_working(*menu_working_,schedule_,calendar_,league,view.round,width);
    league_page_=7;fixture_league_=league;fixture_round_=view.round;fixture_width_=width;fixture_next_width_=view.outgoing_numeric_width;
    menu_date_dirty_=true;menu_pairing_league_=league;
    //15B3 does not redraw its cached rows after a nested club-info refresh.
    fixture_backdrop_=std::make_unique<HokFile>(clone_hok(*menu_working_));
}
void ClassicApplication::draw_league_fixtures(){
    draw_classic_league_fixtures(canvas_,*fixture_backdrop_,schedule_,calendar_,fixture_league_,fixture_round_,fixture_width_);
}
void ClassicApplication::open_league_competition(int league){
    if(screen_==ClassicScreen::LeagueFixtures&&menu_working_)
        finish_classic_league_fixtures_working(*menu_working_,schedule_,fixture_league_);
    else begin_page_working(ClassicScreen::LeagueCompetition);
    screen_=ClassicScreen::LeagueCompetition;league_page_=1;fixture_league_=league;menu_pairing_league_=-1;
    (void)snapshot();const Bytes date(menu_working_->state.begin()+0x8e21,menu_working_->state.begin()+0x8e2d);
    if(league==0)prepare_classic_playoff_working(*menu_working_,calendar_);
    else prepare_classic_promotion_working(*menu_working_,calendar_,league-1);
    menu_date_dirty_=menu_date_dirty_||!std::equal(date.begin(),date.end(),menu_working_->state.begin()+0x8e21);
    fixture_backdrop_=std::make_unique<HokFile>(clone_hok(*menu_working_));
}
void ClassicApplication::draw_league_competition(){
    if(fixture_league_==0)draw_classic_playoff(canvas_,*fixture_backdrop_,calendar_);
    else draw_classic_promotion(canvas_,*fixture_backdrop_,calendar_,fixture_league_-1);
}
void ClassicApplication::open_cup(){
    begin_page_working(ClassicScreen::Cup);
    const auto cup=classic_cup(*menu_working_,calendar_);
    if(cup.preview_date){
        const auto& date=*cup.preview_date;
        const uint32_t fields[]={uint32_t(date.day),uint32_t(date.month-1),uint32_t(date.year)};
        for(size_t field=0;field<3;field++)for(size_t byte=0;byte<4;byte++)
            menu_working_->state.at(0x8E21+field*4+byte)=uint8_t(fields[field]>>(byte*8));
        menu_working_->date.day=date.day;menu_working_->date.month=date.month;menu_working_->date.year=date.year;
        menu_date_dirty_=true;
    }
}
void ClassicApplication::draw_cup(){draw_classic_cup(canvas_,snapshot(),calendar_);}
void ClassicApplication::open_club_info(int team,int mode){
    if(screen_==ClassicScreen::LeagueFixtures){
        club_pairings_.emplace();const auto& h=snapshot();std::copy_n(h.state.begin()+0x8dfd,36,club_pairings_->begin());
    }
    action([&](Game& game){game.enter_club_info(team);return ActionResult{};});
    club_info_=team;club_info_mode_=mode;
}
void ClassicApplication::close_club_info(){
    if(club_pairings_&&menu_working_){
        (void)snapshot();std::copy(club_pairings_->begin(),club_pairings_->end(),menu_working_->state.begin()+0x8dfd);
        menu_working_=std::make_unique<HokFile>(decode_hok(encode_hok(*menu_working_)));
        menu_pairing_league_=fixture_league_;
    }
    club_pairings_.reset();club_info_.reset();
}
void ClassicApplication::open_team_schedule(int half){
    if(!club_info_)return;
    try{
        (void)snapshot();
        const auto preview=classic_team_schedule(*menu_working_,schedule_,calendar_,*club_info_,half);
        if(!preview.available)return;
        const Bytes date(menu_working_->state.begin()+0x8E21,menu_working_->state.begin()+0x8E2D);
        prepare_classic_team_schedule_working(*menu_working_,schedule_,calendar_,*club_info_,preview.half);
        menu_date_dirty_=menu_date_dirty_||!std::equal(date.begin(),date.end(),menu_working_->state.begin()+0x8E21);
        menu_pairing_league_=preview.league;team_schedule_half_=preview.half;team_schedule_open_=true;
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::finish_team_schedule(bool close_club){
    if(team_schedule_open_&&club_info_&&menu_working_){
        finish_classic_team_schedule_working(*menu_working_,schedule_,*club_info_);
        menu_pairing_league_=-1;team_schedule_open_=false;
    }
    if(close_club)close_club_info();
}
void ClassicApplication::finish_page(){
    finish_team_schedule(true);
    if(screen_==ClassicScreen::LeagueFixtures&&menu_working_)
        finish_classic_league_fixtures_working(*menu_working_,schedule_,fixture_league_);
    if(menu_working_)normalize_classic_team_schedule_main_return(*menu_working_);
    menu_working_.reset();menu_date_dirty_=false;menu_pairing_league_=-1;screen_=ClassicScreen::Main;
}
void ClassicApplication::draw_tactics(){
    if(!tactics_plan_)open_tactics();
    if(!tactics_view_)tactics_view_=std::make_unique<HokFile>(preview_tactics(snapshot(),*tactics_plan_));
    draw_classic_tactics(canvas_,*tactics_view_,tactics_plan_->preset);
}

void ClassicApplication::bevel(int x1,int y1,int x2,int y2,int edge,int fill){
    if(edge!=255){
        canvas_.rect(x1,y1,x2,y2,6);canvas_.rect(x1-2,y1-2,x2-2,y2-2,uint8_t(fill));
        for(const auto p:{std::pair{x1-2,y1-2},{x2,y1},{x2-2,y1-2},{x2,y2},{x1,y2},{x1-2,y2-2}})canvas_.rect(p.first,p.second,p.first,p.second,uint8_t(edge));
        canvas_.rect(x2-2,y2-2,x2-2,y2-2,0);
    }
    canvas_.line(x1-1,y1-2,x2-3,y1-2,1);canvas_.line(x1-2,y1-1,x1-2,y2-3,1);
    canvas_.line(x2-2,y1-1,x2-2,y2-3,29);canvas_.line(x1-1,y2-2,x2-3,y2-2,29);
}

std::pair<int,int> ClassicApplication::opponent() const{
    if(!session_.loaded()||!view_)return {-1,0};
    const auto selected=classic_opponent(*view_,calendar_,session_.game().national_coach()>=0?5:0);
    return {selected.has_match()?int(selected.team):-1,int(selected.side)};
}
std::string ClassicApplication::recent_results(const HokFile& h,int team) const{
    if(team<0||team>=64)return {};
    std::string result;
    for(int group=0;group<2;group++){
        int count=0;
        for(int round=4;round>=0&&count<2;round--){
            const auto value=h.state.at(0x8F88+size_t(team)*10+size_t(round)*2+size_t(1-group));
            if(value==255)continue;
            if(!result.empty())result+=",";
            if(team<52)result+=group==0?"A ":"H ";
            result+=std::to_string(value&15)+":"+std::to_string(value>>4);count++;
        }
    }
    if(result.empty())result=" Noch keine";
    if(canvas_.font().measure(mini,result)>85){const auto comma=result.find(',');if(comma!=std::string::npos)result.erase(0,comma+1);}
    return result;
}
void ClassicApplication::draw_main(){
    canvas_.background();canvas_.blit(20,0,0);
    if(!session_.loaded())return;
    const auto& h=snapshot();const auto& g=session_.game();const int own=g.human_team();
    canvas_.rect(2,22,317,74,2);
    canvas_.line(2,22,317,22,7);canvas_.line(1,23,1,74,7);
    canvas_.line(2,75,317,75,3);canvas_.line(318,23,318,74,3);
    canvas_.line(3,76,319,76,29);canvas_.line(319,24,319,75,29);
    bevel(6,35,107,74,1,2);
    canvas_.blit(3,111,48,0,231,27,11);canvas_.blit(3,111,61,27,231,27,11);
    const auto [other,away]=opponent();
    const auto event=calendar_.event(le16(h.state,0xB953));
    int event_type=0;for(int bit=0;bit<8;bit++)if(event&(1<<bit))event_type=bit+1;
    constexpr int icons[]={0,0,0,0,1,2,3,5,1};const int icon=event==kEventSectionEnd?4:icons[std::clamp(event_type,0,8)];
    canvas_.blit(3,111,35,78+14*icon,231,14,11);
    if(other>=0&&other<52)canvas_.blit(3,126,35,66-12*away,231,12,11);
    const int face=h.state.at(0x01EE)%12;
    canvas_.blit(0,233,34,41*(face%3)+1,41*(face/3)+1,38,38);
    const int crest=80+(h.teams.at(size_t(own)).raw[0x34]&63);canvas_.blit(crest,275,34,0,0,38,38);
    bevel(234,35,274,75,255,0);bevel(276,35,316,75,255,0);
    const auto status=classic_status(h,g.budget());
    for(int i=0;i<6;i++)canvas_.blit(3,226,33+7*i,196,219+6*int(status[size_t(i)]),6,6);
    const auto d=session_.date();
    constexpr const char* weekdays[]={"Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"};
    constexpr const char* months[]={"Januar","Februar","März","April","Mai","Juni","Juli","August","Sept.","Okt.","Nov.","Dez."};
    const std::string date=std::string(weekdays[std::clamp(d.weekday,0,6)])+", "+std::to_string(d.day)+". "+months[std::clamp(d.month-1,0,11)]+padded(d.year,5);
    canvas_.text(mini,6,29,date,24,0);
    canvas_.text(mini,108,29,h.manager,24,0,229);canvas_.text(mini,232,29,h.teams[size_t(own)].name,24,0,313);
    canvas_.text(mini,138,42,other>=0?h.teams.at(size_t(other)).name:event==kEventSectionEnd?"Saisonende":"Spielfrei",7,0,230);
    canvas_.text(mini,138,55,recent_results(h,other),7,0,230);canvas_.text(mini,138,68,money(g.budget().balance),7,0,230);
    draw_classic_ranking_graph(canvas_,h,own,other<0?255:other,calendar_.league_day(g.season_day()+1));
    for(int row=0;row<2;row++)for(int col=0;col<4;col++){
        canvas_.frame(8+77*col,80+60*row,3,3);canvas_.rect(12+77*col,84+60*row,75+77*col,132+60*row,30);
        canvas_.blit(3,15+77*col,87+60*row,1+59*col,1+43*row,58,42);
    }
    if(category_>=0){
        canvas_.outline(12+77*(category_%4),84+60*(category_/4),75+77*(category_%4),132+60*(category_/4),24);
        constexpr int offsets[]={0,6,11,17,17,17,24,0},counts[]={6,5,6,6,6,4,3,0};
        for(int item=0;item<6;item++){
            canvas_.frame(3+53*item,201,2,2);canvas_.rect(7+53*item,205,46+53*item,234,30);
            if(item>=counts[category_])canvas_.blit(3,8+53*item,206,118,203,38,28);
            else{const int index=offsets[category_]+item;int sx=1+39*(index%6),sy=87+29*(index/6);if((category_==3||category_==4)&&item>2)sx+=39;canvas_.blit(3,8+53*item,206,sx,sy,38,28);}
        }
    }
}

void ClassicApplication::draw_files(){
    canvas_.background();canvas_.frame(33,48,11,7);canvas_.frame(24,182,12,3);
    // The chooser is drawn from native tiles and fonts, not a host file dialog.
    canvas_.frame(92,4,6,2);
    canvas_.text(bold,92,29,save_mode_?"Speichern":"Laden",7,1,234);
    for(int col=0;col<3;col++){
        canvas_.rect(47+80*col,54,121+80*col,168,4);
        canvas_.outline(42+80*col,54,46+80*col,166,6);canvas_.rect(43+80*col,55,45+80*col,165,3);
        canvas_.line(42+80*col,54,46+80*col,54,1);canvas_.line(42+80*col,54,42+80*col,166,1);
        for(int row=0;row<16;row++){
            const size_t index=file_page_+size_t(col*16+row);if(index>=files_.size())continue;
            const auto& e=files_[index];canvas_.text(mini,48+80*col,60+7*row,e.name,e.directory?6:e.save?24:1);
            if(int(index)==file_selection_)canvas_.rect(43+80*col,55+7*row,45+80*col,60+7*row,24);
        }
    }
    canvas_.blit(4,150,169,20,0,9,8);canvas_.blit(4,174,169,20,9,9,8);
    canvas_.rect(88,187,290,193,4);canvas_.rect(88,197,290,203,4);
    canvas_.text(mini,35,192,"VERZEICHNIS:",1);canvas_.text_native(mini,88,192,virtual_file_path(file_directory_),1);
    canvas_.text(mini,35,202,"FILENAME  :",1);canvas_.text(mini,88,202,filename_,1);
    if(filename_focus_){const int x=88+canvas_.font().measure(mini,filename_);canvas_.rect(x,198,x+2,202,1);}
    constexpr const char* buttons[]={"A:","B:","C:","D:","E:","F:","G:","LÖSCHEN"};
    for(int i=0;i<8;i++){
        const int x=53+59*(i%4),y=207+13*(i/4);canvas_.blit(4,x,y,17,179,57,12);canvas_.text(mini,x,y+7,buttons[i],29,1,x+59);
    }
}

void ClassicApplication::draw_table(){
    canvas_.background();canvas_.frame(4,38,13,8);
    canvas_.rect(10,47,308,57,4);canvas_.rect(10,67,308,152,4);
    canvas_.line(12,66,308,66,1);canvas_.line(12,153,308,153,1);
    canvas_.text_native(mini,10,64,"PL. TD      VEREIN                          BILANZ            S-U-N     PUNKTE     TORE       TORDIFF.",1);
    auto shown=session_.game().league(division_);shown.ranking=table_order_;
    if(table_view_!=1){const int remove=table_view_==0?1:0;for(auto&row:shown.rows){row.games[remove]=row.wins[remove]=row.losses[remove]=row.goals_for[remove]=row.goals_against[remove]=0;}sort_league(shown);}
    table_order_=shown.ranking;
    constexpr const char* views[]={"Heim","Gesamt","Auswärts"};
    const int md=std::max(1,session_.game().next_matchday(division_)-1);
    canvas_.text(flow,5,54,std::string("   TABELLE ")+views[table_view_]+" "+std::to_string(md)+".Spieltag",10,28,313);
    const auto& h=snapshot();
    const int promotion=division_==0?1:2;
    canvas_.rect(10,68,20,67+promotion*7,15);canvas_.rect(10,138,20,151,18);
    for(size_t rank=0;rank<shown.ranking.size();rank++){
        const int slot=shown.ranking[rank],team=shown.first_team+slot,y=73+7*int(rank);const auto&r=shown.rows[size_t(slot)];
        const int owner=h.teams[size_t(team)].raw[0x16];const uint8_t color=owner>=1&&owner<=4?24:30;
        canvas_.text_native(mini,10,y,padded(int(rank)+1,2)+".",8);
        //6668:29CF..2A35, ordinary menu context1: compare against md-3
        //only after the second played matchday. The sprite selector is1/2/3.
        int trend=2;
        const int next=session_.game().next_matchday(division_);
        if(int(int8_t(uint8_t(next)))>2){
            const int previous=fever_curve_position(h.table.at(size_t(team)),uint8_t(next-3));
            if(previous>int(rank))trend=3;
            else if(previous<int(rank))trend=1;
        }
        //47BF indexes DS537A/537E directly by the one-based selector.
        constexpr int trend_x[]={0,19,25,25},trend_y[]={0,36,41,36};
        canvas_.blit(33,24,y-4,trend_x[trend],trend_y[trend],6,5,true,192);
        canvas_.text(mini,33,y,h.teams[size_t(team)].name,color);canvas_.text_native(mini,117,y,r.form,color);
        const int gf=r.total(r.goals_for),ga=r.total(r.goals_against),diff=gf-ga;
        const std::string stats=padded(r.total(r.wins),2)+" "+padded(r.draws(0)+r.draws(1),2)+" "+padded(r.total(r.losses),2)+"    "+padded(r.points_plus(),2)+":"+padded(r.points_minus(),2)+"   "+padded(gf,3)+":"+padded(ga,3)+"      "+(diff<0?"-":diff>0?"+":"`")+padded(std::abs(diff),3);
        canvas_.text_native(mini,166,y,stats,color);
    }
    for(int view=0;view<3;view++){
        const int x=30+102*view;
        const auto corner=canvas_.pixels().at(x,164);
        canvas_.blit(4,x,164,17,179,57,12);
        canvas_.pixels().at(x,164)=corner;canvas_.pixels().at(x+56,164)=corner;
        canvas_.pixels().at(x,175)=corner;canvas_.pixels().at(x+56,175)=corner;
        canvas_.text(mini,x,171,views[view],29,1,x+59);
    }
}

void ClassicApplication::draw_totals(){draw_classic_totals(canvas_,snapshot(),session_.game().budget(),session_.finance_tables());}
void ClassicApplication::draw_bank(){draw_classic_bank(canvas_,snapshot(),session_.game().budget(),bank_party_,bank_selected_,bank_amount_,bank_deposit_,0,mouse_y_>=73&&mouse_y_<=100);}
void ClassicApplication::draw_contracts(){canvas_.background();draw_classic_roster(canvas_,snapshot(),true);}
void ClassicApplication::draw_training(){
    canvas_.background(5);
    for(int day=0;day<7;day++)for(int unit=0;unit<4;unit++){
        const int kind=training_plan_[size_t(day)][size_t(unit)],ys[]={103,125,168,190};
        canvas_.blit(5,14+44*day,ys[unit],26+30*kind,218,29,20,true,192);
    }
    const long cost=training_recovery_cost(session_.training_tables(),training_plan_,session_.game().budget().league);
    canvas_.text(mini,0,41,"Kosten pro Woche:"+money(cost*2),7,6,319);
    canvas_.line(56,238,84,238,2);
    canvas_.line(26+30*training_kind_,238,54+30*training_kind_,238,24);
    struct Average{int count=0,fatigue=0,a=0,b=0;};
    std::array<Average,3> totals{};
    const auto& h=snapshot();
    for(size_t slot=0;slot<h.squad.size();slot++){
        const auto&s=h.squad[slot];
        if(s.player<0)continue;
        const auto&p=h.players.at(size_t(s.player));
        const int role=std::min(p.position/25,2);
        auto&t=totals[size_t(role)];t.count++;t.fatigue+=std::max(s.raw_exhaustion,50)-50;t.a+=s.skill_a;t.b+=s.skill_b;
    }
    constexpr int xs[]={148,157,166},colors[][2]={{17,18},{22,23},{12,15}};
    for(int bar=0;bar<3;bar++){
        int x=xs[bar];
        for(const auto&t:totals){
            if(!t.count)continue;
            const int height=bar==0?(150-t.fatigue/t.count)*36/150:(bar==1?t.a:t.b)/t.count*36/100;
            if(height<=0)continue;
            canvas_.line(x,93,x,94-height,uint8_t(colors[bar][0]));
            canvas_.line(x+1,93,x+1,94-height,uint8_t(colors[bar][1]));x+=2;
        }
    }
}
// ovr030:081D: full-width window (4,92, 13x3), NewBold text centred over x 0..319 at baseline 127 in black and again at 126 in colour 11.
void ClassicApplication::draw_banner(std::string_view text){
    canvas_.background(22);canvas_.frame(4,92,13,3);
    canvas_.text(ClassicFontId::NewBold,0,127,text,0,-1,319);
    canvas_.text(ClassicFontId::NewBold,0,126,text,11,-1,319);
}
void ClassicApplication::open_settings(){hover_kind_.reset();screen_=ClassicScreen::Settings;}
void ClassicApplication::draw_settings(){draw_classic_settings(canvas_,session_.game().settings(),0);}
void ClassicApplication::click_settings(int x,int y,bool right){
    if(right){screen_=ClassicScreen::Main;return;}
    const auto hit=classic_settings_hit(x,y);
    if(hit.widget>=0){
        int value=0;
        action([&](Game& game){value=game.toggle_setting(hit.widget,0);return ActionResult{};});
        if(hit.widget==9&&value==2&&!settings_rink_hint_shown_){
            settings_rink_hint_shown_=true;
            notice("Neuer Spielmodus:\nDas Eisfeld bleibt während des\ngesamten Spieles an. Keine\nTeamveränderungen möglich.");
        }
    }else if(hit.speed>=0)action([&](Game& game){game.set_game_speed(hit.speed);return ActionResult{};});
}
void ClassicApplication::draw_transfer(){draw_classic_transfer(canvas_,purchase_backdrop_?*purchase_backdrop_:snapshot(),session_.game().budget());}
} // namespace em::ui
