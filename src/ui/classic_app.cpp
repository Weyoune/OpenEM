// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "ui/classic_player.hpp"
#include "ui/classic_strengths.hpp"
#include "ui/classic_club_info.hpp"
#include "ui/classic_team_schedule.hpp"
#include "ui/classic_euro.hpp"
#include "ui/classic_wm.hpp"
#include "ui/classic_league_fixtures.hpp"
#include "ui/classic_playoff.hpp"
#include "ui/classic_cup.hpp"
#include "ui/classic_sound.hpp"
#include "game/checkpoint.hpp"
#include "game/match.hpp"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace fs=std::filesystem;
namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini, flow=ClassicFontId::Flow;
constexpr uint64_t kMatchBannerMs=900;
constexpr uint64_t kChordMs=150;  // a left click on the board middle waits this long for the right button (both buttons)
bool inside(int x,int y,int l,int t,int r,int b) {return x>=l&&x<=r&&y>=t&&y<=b;}
std::string upper(std::string s){for(char& c:s)c=char(std::toupper(static_cast<unsigned char>(c)));return s;}
uint64_t (*ui_clock)()=nullptr;
}
uint64_t classic_ticks(){return ui_clock?ui_clock():SDL_GetTicks64();}
void set_classic_clock(uint64_t (*clock)()){ui_clock=clock;}

ClassicApplication::ClassicApplication(fs::path data,fs::path saves,const fs::path& initial)
    :data_(std::move(data)),saves_(std::move(saves)),session_(data_),canvas_(data_),roster_help_(read_file(data_/"EM.EXE")),transfer_help_(read_file(data_/"EM.EXE")),calendar_(load_season_calendar(read_file(data_/"EM.EXE"))),schedule_(load_schedule_tables(read_file(data_/"EM.EXE"))) {
    session_.set_match_observer([this](const Game& game,int minute,const std::vector<LiveMatch>& matches){
        if(live_skip_)return;
        pending_match_frames_.push_back(classic_match_frame(game,minute,matches,game.match_attendance(),game.match_sold_out()));
        pending_match_frames_.back().settings_lamp=live_fast_;
        const bool cup_day=!pending_league_day_&&!(pending_event_&(kEventEuroCup|kEventWorldChampionship));
        if(cup_day&&(minute==60||minute==65||minute==70)){  // ovl06_2522 -> ovl07_0eba after the regular time and each overtime part
            ClassicMatchFrame table=pending_match_frames_.back();table.cup_table=minute;table.cup=classic_cup_live(minute,matches);
            pending_match_frames_.push_back(std::move(table));
        }
        if(pending_league_day_&&(minute==20||minute==40||minute==60)){
            // ovl06_2522 -> ovl07_0eba(7, league, minute) per league of the day: the Paarungen page with the running scores
            // after a period (switch Drittelpause, DS:021D + 3 * league) and after minute 60 (Ergebnisse, + 1); not at
            // Stufe 2 of the goal scenes with a board (DS:5A55)
            const GameSettings s=game.settings();
            const bool rink=s.flags[9]==2&&!pending_match_frames_.back().boards.empty();
            for(int league=0;league<3&&!rink;league++){
                std::vector<ClassicCupLive> pairs;
                for(const LiveMatch& m:matches){
                    if(m.home<league*12||m.home>=league*12+12)continue;
                    const MatchProgress p=m.run->progress();
                    ClassicCupLive c;c.home=m.home;c.away=m.away;c.goals[0]=p.score.home;c.goals[1]=p.score.away;pairs.push_back(c);
                }
                if(pairs.empty()||!s.flags[size_t(league*3+(minute==60?1:0))])continue;
                ClassicMatchFrame page=pending_match_frames_.back();page.league_page=league+1;page.league=std::move(pairs);
                pending_match_frames_.push_back(std::move(page));
            }
        }
        if(live_day_)live_minute(matches);
    });
    install_cup_draw_handlers();  // the live DEB cup draw of later rounds (classic_cup_draw.cpp)
    if(!initial.empty())session_.load(initial);
    if(session_.loaded())table_order_=session_.game().league(division_).ranking;
    if(!session_.loaded()){if(session_.can_start_new_game())open_start_menu();else open_files(false);}
}
const HokFile& ClassicApplication::snapshot(){
    if(season_view_)return *season_view_;  // the contract talks of the season change draw the staged day
    if(!view_||view_revision_!=session_.revision()){
        view_=std::make_unique<HokFile>(decode_hok(session_.game().save_bytes()));view_revision_=session_.revision();
    }
    if(menu_working_){
        if(menu_working_revision_!=session_.revision()){
            auto next=std::make_unique<HokFile>(clone_hok(*view_));
            if(menu_date_dirty_)std::copy_n(menu_working_->state.begin()+0x8E21,12,next->state.begin()+0x8E21);
            if(menu_pairing_league_>=0){const size_t at=0x8DFD+size_t(menu_pairing_league_)*12;std::copy_n(menu_working_->state.begin()+at,12,next->state.begin()+at);}
            menu_working_=std::make_unique<HokFile>(decode_hok(encode_hok(*next)));
            menu_working_revision_=session_.revision();
        }
        return *menu_working_;
    }
    return *view_;
}
Bytes ClassicApplication::native_ui_bytes(){return encode_hok(snapshot());}
void ClassicApplication::notice(std::string text){notice_=std::move(text);native_notice_until_.reset();advancing_=false;}
void ClassicApplication::timed_native_notice(std::string text,bool return_main){
    notice(std::move(text));native_notice_return_main_=return_main;
    //199F:08C8 mode75 ->2115:019E divides by3, then waits25 BIOS ticks.
    //PIT1193182Hz /65536 gives a rounded-up wall interval of1374ms.
    native_notice_until_=classic_ticks()+1374;
}
void ClassicApplication::action(const std::function<ActionResult(Game&)>& fn){
    try{const auto result=session_.apply(fn);invalidate();if(!result.message.empty())notice(result.message);}
    catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::autosave(){
    if(!session_.loaded())return;
    for(int n=1;n<1000000;n++){
        char name[24];std::snprintf(name,sizeof name,"AUTO%04d.OEM",n);
        const auto file=saves_/name;
        if(!fs::exists(file)){write_new_save(file,session_.game().checkpoint_bytes());return;}
    }
    throw FormatError("Kein freier Name für die automatische Sicherung.");
}
void ClassicApplication::start_advance(){
    if(!session_.loaded())return;
    if(wm_flag_blocked())return;  // ovr034:2372 (classic_wm_mode.cpp)
    if(hand_over_menu())return;  // hot seat (classic_hotseat.cpp): the next manager's menu before the day
    advancing_=true;advance_first_=true;advanced_days_=0;category_=-1;
}
// The match display (ovl06_2522): the day is computed at once by Session::advance_day, the observed minutes are shown afterwards.
// The original shows them when the human club plays or the display switch of the competition is on (ovl06_161d).
void ClassicApplication::prepare_match_view(bool human_match){
    pending_match_frames_.clear();pending_human_match_=human_match;
    pending_league_day_=session_.loaded()&&calendar_.league_day(session_.game().season_day()+1);
    pending_event_=session_.loaded()?calendar_.event(session_.game().season_day()+1):0;
    if(session_.loaded()&&!pending_league_day_)match_before_=std::make_unique<HokFile>(clone_hok(snapshot()));
}
void ClassicApplication::begin_match_view(){
    auto frames=std::move(pending_match_frames_);pending_match_frames_.clear();
    if(frames.empty()||!classic_match_display_wanted(session_.game(),pending_human_match_,pending_event_))return;
    // FUN_162c_23c4 goal flashes between the minutes, then the board messages of ovl14_0690 (speed < 70; with goal scenes,
    // DS:0226 = flag 9, only the final lines)
    const GameSettings settings=session_.game().settings();
    match_frames_=classic_board_messages(classic_goal_flashes(std::move(frames)),settings.speed,settings.flags[9],canvas_.data());match_frame_=0;replay_key_=false;
    match_frame_until_=classic_ticks()+uint64_t(classic_match_frame_ms(match_frames_[0],session_.game().settings().speed));
}
int ClassicApplication::live_speed() const{return live_fast_?70:session_.game().settings().speed;}
// The frames of the observed minutes not shown yet: the goal flashes, board messages and goal scenes are derived from the
// raw minute frames since live_base_ (a switch of the quick button starts a new base with the new speed / scene setting).
void ClassicApplication::append_live_frames(){
    std::vector<ClassicMatchFrame> raw(pending_match_frames_.begin()+std::ptrdiff_t(live_base_),pending_match_frames_.end());
    auto out=classic_board_messages(classic_goal_flashes(std::move(raw)),live_speed(),live_fast_?0:live_scenes_,canvas_.data());
    const bool first=match_frames_.empty();
    for(size_t i=live_out_;i<out.size();i++)match_frames_.push_back(std::move(out[i]));
    live_out_=out.size();
    if(first&&!match_frames_.empty()){match_frame_=0;match_frame_until_=classic_ticks()+uint64_t(classic_match_frame_ms(match_frames_[0],live_speed()));}
}
// Called by the minute observer while the day is played: show the minute, then hand the clicks of the minute to the
// matches before the next one (ovl06_2522 reads the mouse at the start of every minute, ovl06_3856 writes the team record).
void ClassicApplication::live_minute(const std::vector<LiveMatch>& matches){
    append_live_frames();
    live_wait_=false;
    while(live_day_&&!live_wait_&&!live_skip_&&!finished_&&pump_)pump_();
    if(live_lineup_){live_lineup_=false;for(const LiveMatch& m:matches)const_cast<MatchRun&>(*m.run).take_lineup();}
    if(live_tactics_){  // the tactics screen was left: the running match of the club takes the new controls and tactic bytes
        live_tactics_=false;
        const Game& game=session_.game();
        for(const LiveMatch& m:matches)for(int side=0;side<2;side++){
            const int team=side==0?m.home:m.away;
            if(team==game.human_team()&&team>=0&&size_t(team)<game.save().teams.size())const_cast<MatchRun&>(*m.run).take_tactics(side,game.save().teams[size_t(team)]);
        }
    }
    if(live_idle_){
        // ovl06_2522 took the Einstellungen instead of this minute: no match plays it; speed and goal scenes are taken anew,
        // the quick switch is off (ovl14_15c3(10, 0xE4, 0))
        live_idle_=false;
        for(const LiveMatch& m:matches)const_cast<MatchRun&>(*m.run).idle_next_minute();
        live_scenes_=session_.game().settings().flags[9];live_fast_=false;rebase_live();
    }
    for(const ClassicBoardOrder& o:live_orders_){
        if(o.team<0)continue;
        for(const LiveMatch& m:matches){
            if(m.home!=o.team&&m.away!=o.team)continue;
            // the matches of the day are owned by the running day; the observer gets them read-only by contract, the
            // minute loop of the original writes these bytes of the team record here
            MatchRun& run=const_cast<MatchRun&>(*m.run);
            HokTeam& t=m.home==o.team?run.home_mut():run.away_mut();
            if(o.effort>=0)t.effort_current=o.effort;
            if(o.offence>=0)t.flags=(t.flags&~0x70)|(o.offence<<4);
            for(int k=0;k<2;k++)if(o.line[k]>=0)t.current_line[k]=o.line[k];
        }
    }
    live_orders_.clear();
    g_goal_scenes=live_fast_?0:live_scenes_;  // DS:0226 while the quick switch is on (ovl06_3856)
    g_match_speed=live_speed();  // DS:0262: the quick switch sets 70 (no message draws), the Einstellungen their speed
}
// A new processing base after a change of speed / goal scenes: the frames shown so far stay, the next ones follow the new setting.
void ClassicApplication::rebase_live(){
    live_base_=pending_match_frames_.empty()?0:pending_match_frames_.size()-1;
    std::vector<ClassicMatchFrame> base(pending_match_frames_.begin()+std::ptrdiff_t(live_base_),pending_match_frames_.end());
    live_out_=classic_board_messages(classic_goal_flashes(std::move(base)),live_speed(),live_fast_?0:live_scenes_,canvas_.data()).size();
    for(size_t i=match_frame_;i<match_frames_.size();i++)match_frames_[i].settings_lamp=live_fast_;
}
bool ClassicApplication::live_click(int x,int y,bool right){
    if(right){live_skip_=true;match_frames_.clear();return true;}
    if(live_in_scene_)return true;  // a scene shown inside a minute (live_scene): the board waits for the minute end
    const auto order=classic_board_click(match_frames_[match_frame_],x,y);
    if(!order)return false;
    if(order->quick){live_fast_=!live_fast_;rebase_live();}
    if(order->squad&&order->team==session_.game().human_team()){
        // ovl14_24bc: the lineup of the clicked club with button 1, the tactics with both buttons (ovl06_2522 reads the button
        // state once per minute); a left click waits a moment for the right button (click_both)
        live_centre_until_=classic_ticks()+kChordMs;return true;
    }
    if(order->team>=0&&(order->effort>=0||order->offence>=0||order->line[0]>=0||order->line[1]>=0))live_orders_.push_back(*order);
    return true;
}
// The middle of the own board (ovl14_24bc, mode DS:02D6 = 5): the lineup (button 1) or the tactics (buttons 1 + 2); the
// minutes wait until the screen is left, the running match takes the changes before the next minute (live_minute).
void ClassicApplication::open_live_board_screen(bool tactics){
    live_centre_until_.reset();
    live_screen_=true;live_lineup_=true;live_screen_before_=screen_;
    if(tactics)open_tactics();else open_lineup();
}
// g_scene_replay during a live day (ovl14_1dc7): the minutes so far are shown, then this scene at once; r / w during it plays
// the chance again, and the simulation draws the next scene from the Borland stream like the original. Not at Stufe 2.
int ClassicApplication::live_scene(const SceneCall& call,const GoalScene& drawn,int play){
    if(!live_day_||live_skip_||live_fast_||finished_||!pump_||live_scenes_!=1||live_screen_||pending_match_frames_.empty()||!call.home||!call.away)return -1;
    struct InScene{bool& flag;~InScene(){flag=false;}} in_scene{live_in_scene_};
    live_in_scene_=true;
    append_live_frames();live_wait_=false;
    while(live_day_&&!live_wait_&&!live_skip_&&!finished_&&pump_)pump_();
    if(live_skip_||finished_||match_frames_.empty())return -1;
    const ClassicMatchFrame& last=pending_match_frames_.back();
    const ClassicBoard* board=nullptr;
    for(const ClassicBoard& b:last.boards)if(b.team[0].name==call.home->name&&b.team[1].name==call.away->name)board=&b;
    if(!board)return -1;
    ClassicMatchFrame base=last;base.message.reset();base.flash.reset();base.cup_table=0;base.cup.clear();base.league_page=0;base.league.clear();
    if(call.minute<=70)base.minute=call.minute;
    ClassicGoalScene scene=classic_goal_scene_of(*board,call.minute);
    scene.goals={call.goals[0]%30,call.goals[1]%30};
    scene.script=classic_goal_scene_script(drawn.number,drawn.side,drawn.goal,drawn.variant=='e',drawn.variant=='j');
    scene.play=play;
    std::vector<ClassicMatchFrame> frames;
    ClassicMatchFrame wait=base;wait.hold_ms=30*54925/3000;frames.push_back(std::move(wait));  // delay(30) before the scene
    for(ClassicMatchFrame& f:classic_goal_scene_frames(canvas_.data(),base,scene,drawn.side,drawn.goal))frames.push_back(std::move(f));
    for(ClassicMatchFrame& f:frames){f.live_scene=true;f.settings_lamp=live_fast_;}
    live_replay_=false;
    const size_t first=match_frames_.size();
    for(ClassicMatchFrame& f:frames)match_frames_.push_back(std::move(f));
    match_frame_=first;match_frame_until_=classic_ticks()+uint64_t(classic_match_frame_ms(match_frames_[first],live_speed()));
    live_wait_=false;
    while(live_day_&&!live_wait_&&!live_skip_&&!finished_&&pump_)pump_();
    return live_replay_&&!live_skip_?1:0;
}
void ClassicApplication::click_both(int x,int y){
    if(live_day_&&!live_screen_&&!live_in_scene_&&!match_frames_.empty()&&!match_frames_[match_frame_].waits()){
        const auto order=classic_board_click(match_frames_[match_frame_],x,y);
        if(order&&order->squad&&order->team==session_.game().human_team()){open_live_board_screen(true);return;}
    }
    click(x,y,false);
}
bool ClassicApplication::tick_match_view(){
    if(match_frames_.empty())return false;
    const uint64_t now=classic_ticks();
    if(match_frames_[match_frame_].waits())return true;  // the cup table waits for a click (FUN_199f_0118)
    if(now<match_frame_until_)return true;
    if(match_frames_[match_frame_].after_scene){
        // ovl14_1dc7: after delay(20) a typed 'r' / 'w' plays the chance again (at most three plays); a scene shown at its
        // chance answers the simulation, which draws the replay itself (live_scene)
        if(match_frames_[match_frame_].live_scene)live_replay_=replay_key_;
        else if(replay_key_){
            auto replay=classic_goal_scene_replay(canvas_.data(),match_frames_[match_frame_]);
            match_frames_.insert(match_frames_.begin()+std::ptrdiff_t(match_frame_+1),replay.begin(),replay.end());
        }
        replay_key_=false;
    }
    if(match_frame_+1>=match_frames_.size()){if(live_day_){live_wait_=true;return true;}match_frames_.clear();return false;}
    match_frame_++;
    classic_sound_match_frame(match_frames_[match_frame_-1],match_frames_[match_frame_]);  // goal-scene sound (classic_sound.hpp)
    // the last minute stays a moment (the original waits for the period end, FUN_199f_0118)
    match_frame_until_=now+uint64_t(classic_match_frame_ms(match_frames_[match_frame_],live_speed()))+(match_frame_+1==match_frames_.size()&&!live_day_?1000u:0u);
    return true;
}
void ClassicApplication::tick(){
    if(native_notice_until_){
        if(classic_ticks()<*native_notice_until_)return;
        finish_timed_native_notice();
    }
    if(cup_live_){tick_cup_live();return;}  // the live DEB cup draw (classic_cup_draw.cpp)
    if(in_day_pump_){tick_match_view();return;}  // a question inside advance_day: no further day may start
    if(!game_end_&&session_.loaded()&&session_.game().game_over()){begin_game_end();return;}  // the end year (classic_misc_input.cpp)
    if(live_day_){  // inside the minute observer: only the match display runs
        if(live_centre_until_&&!live_screen_&&!live_in_scene_&&classic_ticks()>=*live_centre_until_)open_live_board_screen(false);
        if(!live_screen_)tick_match_view();
        else if(screen_==ClassicScreen::Main){live_screen_=false;screen_=live_screen_before_;}  // the screen was left
        return;
    }
    if(season_view_&&!session_.contract_question()&&notice_.empty())season_view_.reset();  // talks done, notices read
    if(season_pending_open_&&notice_.empty()&&session_.contract_question()){season_pending_open_=false;open_season_talk();}
    if(!advancing_)update_roster_hover();
    if(!advancing_)tick_stats_hold(classic_ticks());
    if(tick_match_view())return;
    if(post_table_open_&&screen_==ClassicScreen::Table)return;  // the table after the match day waits for a right click
    post_table_open_=false;
    if(post_tables_){
        int l=0;while(!(post_tables_>>l&1))l++;
        post_tables_&=~(1<<l);
        division_=l;table_view_=1;table_order_=session_.game().league(l).ranking;screen_=ClassicScreen::Table;post_table_open_=true;
        return;
    }
    if(!advancing_)check_new_messages();
    if(!advancing_||!notice_.empty()||quit_question_||session_.contract_question())return;
    if(match_banner_until_){
        if(classic_ticks()<*match_banner_until_)return;
        match_banner_until_.reset();
    }
    try{
        const bool match_day=session_.next_day_has_match();
        if(!advance_first_&&match_day){
            advancing_=false;autosave();return;
        }
        // The original announces the own league match with a banner before the day is played.
        if(match_day&&match_banner_day_!=session_.game().season_day()&&calendar_.league_day(session_.game().season_day()+1)){
            match_banner_day_=session_.game().season_day();match_banner_until_=classic_ticks()+kMatchBannerMs;return;
        }
        if(++advanced_days_>370){notice("Kein weiterer eigener Spieltermin gefunden.");return;}
        prepare_match_view(match_day);
        // ovl06_161d end: the leagues of the day whose switch Tabelle (DS:021F + 3 * league) is on show their table
        post_tables_=0;
        if(pending_league_day_){
            const uint8_t leagues=calendar_.event(session_.game().season_day()+1)&kEventLeagueMask;
            const GameSettings s=session_.game().settings();
            for(int l=0;l<3;l++)if((leagues>>l&1)&&s.flags[size_t(l*3+2)])post_tables_|=1<<l;
        }
        // with the frontend's pump the minutes are shown while the day is played (clicks reach the minute loop)
        live_skip_=false;live_fast_=false;live_base_=0;live_out_=0;live_orders_.clear();live_centre_until_.reset();live_tactics_=false;
        live_day_=pump_&&classic_match_display_wanted(session_.game(),match_day,pending_event_);
        if(live_day_){match_frames_.clear();live_scenes_=session_.game().settings().flags[9];}
        bool advanced=false;
        if(live_day_)g_scene_replay=[this](const SceneCall& c,const GoalScene& s,int play){return live_scene(c,s,play);};
        try{advanced=session_.advance_day();}catch(...){live_day_=false;g_scene_replay=nullptr;throw;}
        g_scene_replay=nullptr;
        const bool live=live_day_||live_skip_;live_day_=false;
        if(!advanced&&session_.contract_question()){open_season_talk();return;}  // the contract talks of the season change
        if(advanced){
            advance_first_=false;invalidate();
            if(post_tables_)news_after_tables_=true;else show_manager_news();  // the tables belong to the match day
            if(!live)begin_match_view();
            else{pending_match_frames_.clear();if(live_skip_)match_frames_.clear();else if(!match_frames_.empty())match_frame_until_=classic_ticks()+1000;}
            live_skip_=false;
        }
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::request_exit(){
    if(stadium_build_||native_notice_until_||sale_quote_)return; //native modal phases must finish
    advancing_=false;
    if(screen_==ClassicScreen::Stadium)canvas_.set_button_edge(3);
    advertising_info_=false;if(advert_renewal_)resolve_advert_renewal(false);
    if(menu_working_)finish_page();
    fee_editor_.reset();talent_question_=false;
    if(subsidy_offer_){subsidy_offer_=0;purchase_backdrop_.reset();backdrop_budget_.reset();}
    if(purchase_draft_){try{cancel_purchase(false);}catch(const std::exception& e){notice(e.what());return;}}
    if(screen_==ClassicScreen::Transfer)action([](Game& game){game.finish_transfer_menu();return ActionResult{};});
    if(screen_==ClassicScreen::Tactics)finish_tactics(ClassicScreen::Main);
    if(screen_==ClassicScreen::Training&&training_changed_){
        action([&](Game& g){return ActionResult{{},g.set_training_plan(training_plan_)};});training_changed_=false;
    }
    notice_.clear();pending_notices_.clear();quit_question_=true;
}
void ClassicApplication::route_submenu(int item){
    hover_kind_.reset();roster_hover_={};roster_rules_=false;
    transfer_hover_={};pool_rules_=pool_help_visible_=false;
    switch(category_){
        case 0:if(item==0){screen_=ClassicScreen::Bank;bank_party_=4;bank_amount_=0;bank_selected_=false;return;}if(item==1){open_advertising();return;}if(item==2){open_stadium();return;}if(item==3){screen_=ClassicScreen::Youth;return;}if(item==4){open_new_manager();return;}if(item==5){open_highscore();return;}break;
        case 1:
            if(item==0){open_lineup();return;}
            if(item==1){open_tactics();return;}
            if(item==2){screen_=ClassicScreen::Contracts;return;}
            if(item==3){screen_=ClassicScreen::Transfer;return;}
            if(item==4){screen_=ClassicScreen::Training;training_plan_=session_.game().training_plan();training_changed_=false;training_kind_=0;return;}
            break;
        case 2:if(item==0){screen_=ClassicScreen::Totals;return;}if(item<6){open_statistics(item);return;}break;
        case 3:
            if(item<3){division_=item;table_view_=1;table_order_=session_.game().league(item).ranking;screen_=ClassicScreen::Table;return;}
            if(item==3){open_cup();return;}
            if(item==4){euro_from_fixtures_=false;open_euro(euro_table_page_);return;}
            if(item==5){open_wm(wm_table_page_);return;}
            break;
        case 6:if(item<2){open_files(item==1);return;}if(item==2){open_settings();return;}break;
        case 5:if(item<4){open_strengths(item==3?4:item);return;}break;
        case 4:if(item<3){if(league_page_==7)open_league_fixtures(item);else open_league_competition(item);return;}if(item==3){open_cup();return;}if(item==4){euro_from_fixtures_=true;open_euro(euro_fixture_page_);return;}if(item==5){open_wm(wm_fixture_page_);return;}break;
    }
    notice("Dieser Bereich ist noch nicht nachgebaut.");
}
void ClassicApplication::open_files(bool saving){
    save_mode_=saving;file_directory_=saving?saves_:data_;
    filename_=saving?"SPIEL.OEM":"";filename_focus_=false;file_selection_=-1;file_page_=0;
    file_preview_.reset();screen_=ClassicScreen::Files;refresh_files();
}
std::string ClassicApplication::virtual_file_path(const fs::path& path) const{
    for(const auto& root:{std::pair{data_,'C'},std::pair{saves_,'D'}}){
        const auto relative=path.lexically_normal().lexically_relative(root.first.lexically_normal());
        if(relative.empty()||*relative.begin()=="..")continue;
        std::string result(1,root.second);result+=":\\";
        if(relative!=".")result+=upper(path_text(relative));
        std::replace(result.begin(),result.end(),'/','\\');return result;
    }
    return upper(path_text(path));
}
void ClassicApplication::refresh_files(){
    files_.clear();
    files_.push_back({file_directory_,".",true,false});
    if(file_directory_.has_parent_path())files_.push_back({file_directory_==data_||file_directory_==saves_?file_directory_:file_directory_.parent_path(),"..",true,false});
    std::error_code ec;
    for(fs::directory_iterator it(file_directory_,ec),end;!ec&&it!=end;it.increment(ec)){
        const bool directory=it->is_directory(ec);if(ec)break;
        const std::string name=path_text(it->path().filename()),ext=upper(path_text(it->path().extension()));
        files_.push_back({it->path(),upper(name),directory,ext==".HOK"||ext==".OEM"});
    }
    std::stable_sort(files_.begin(),files_.end(),[](const FileEntry&a,const FileEntry&b){if(a.directory!=b.directory)return a.directory>b.directory;return a.name<b.name;});
    if(file_page_>=files_.size()&&file_page_)file_page_=0;
}
void ClassicApplication::accept_file(){
    try{
        if(filename_.empty())return;
        fs::path file=path_from_text(filename_);
        if(filename_.size()>=2&&filename_[1]==':'&&(filename_[0]=='C'||filename_[0]=='D')){
            std::string tail=filename_.substr(2);while(!tail.empty()&&(tail.front()=='/'||tail.front()=='\\'))tail.erase(0,1);
            file=(filename_[0]=='C'?data_:saves_)/path_from_text(tail);
        }
        if(file.is_relative())file=file_directory_/file;
        if(fs::is_directory(file)){file_directory_=file;filename_.clear();file_page_=0;refresh_files();return;}
        if(!file.has_extension())file+=save_mode_?".OEM":".HOK";
        if(save_mode_)session_.save(file);else{
            if(session_.loaded()&&session_.dirty())autosave();
            session_.load(file);invalidate();close_start_menu();
        }
        //Live load/save returns within the same native menu invocation.
        //The selected category/footer survives; startup loading starts at-1.
        screen_=ClassicScreen::Main;
    }catch(const NativeReloadError& e){timed_native_notice(e.what(),true);}
    catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::finish_timed_native_notice(){
    notice_.clear();native_notice_until_.reset();
    if(native_notice_return_main_)screen_=session_.loaded()?ClassicScreen::Main:ClassicScreen::Files;
    native_notice_return_main_=false;update_stadium_hover();
}
void ClassicApplication::open_player(int slot,int manager){
    if(manager<0)manager=active_manager();  // hot seat: the squad of the manager at the menu
    try{
        const auto squad=hok_squad(snapshot(),manager);
        if(slot<0||size_t(slot)>=squad.size())return;
        const int player=squad[size_t(slot)].player;
        if(player<0)return;
        long quote=0;
        session_.apply([&](Game& game){quote=game.quote_player_value(manager,slot);return ActionResult{{},true};});
        invalidate();roster_rules_=false;player_detail_=PlayerDetail{player,manager,slot,quote};
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::click(int x,int y,bool right){
    if(title_){title_=false;return;}  // ovr034:07D8 any press ends the title screen
    if(cup_live_){click_cup_live(x,y,right);return;}  // the live DEB cup draw (classic_cup_draw.cpp)
    if(game_end_==2){finished_=true;return;}  // "Das wars...": a press leaves the program (ovl00_058a)
    if(!match_frames_.empty()&&!live_screen_){  // live_screen_: a screen opened during the minutes takes the input
        if(live_day_&&!match_frames_[match_frame_].waits()&&live_click(x,y,right))return;
        // a click on the cup table plays on; a right click on the minutes jumps to the next cup table or ends the replay
        size_t next=match_frame_+1;
        if(const int league=match_frames_[match_frame_].league_page){
            // Amiga FUN_0010e62e mode 2 ends with the right button; a left click on a club opens its Vereinsinfo (DOSBox
            // 10.11.2026), which a right click or "Zurück" closes. Only a view here: the running day is not touched.
            if(club_info_){if(right||inside(x,y,104,216,159,226))club_info_.reset();return;}
            if(!right){
                try{
                    const auto hit=classic_league_fixtures_hit(classic_league_fixtures(snapshot(),schedule_,calendar_,league-1),x,y);
                    if(hit.action==ClassicLeagueAction::ClubInfo&&hit.team>=0){club_info_=hit.team;club_info_mode_=hit.mode;}
                }catch(const std::exception&){}
                return;
            }
        }
        if(!match_frames_[match_frame_].waits()){
            if(!right)return;
            while(next<match_frames_.size()&&!match_frames_[next].waits())next++;
        }
        if(next>=match_frames_.size()){
            if(live_day_){match_frames_[match_frame_].cup_table=0;match_frames_[match_frame_].league_page=0;live_wait_=true;return;}  // the next minute is not played yet
            match_frames_.clear();return;
        }
        match_frame_=next;match_frame_until_=classic_ticks()+uint64_t(classic_match_frame_ms(match_frames_[next],session_.game().settings().speed));
        return;
    }
    if(post_table_open_&&screen_==ClassicScreen::Table&&right){  // the table of ovl06_161d ends the match day
        screen_=ClassicScreen::Main;post_table_open_=false;
        if(!post_tables_&&news_after_tables_){news_after_tables_=false;show_manager_news();}
        return;
    }
    if(native_notice_until_)return;
    if(manager_question_){click_manager_question(x,y,right);return;}  // hot seat (classic_hotseat.cpp)
    if(!notice_.empty()){
        if(!right){notice_.clear();if(season_editor_with_notice_){season_editor_with_notice_=false;contract_editor_.reset();}if(advert_renewal_refused_)resolve_advert_renewal(false);if(!pending_notices_.empty()){notice_=pending_notices_.front();pending_notices_.erase(pending_notices_.begin());}}
        return;
    }
    if(sale_quote_){
        if(!right&&inside(x,y,12,84,67,94))finish_sale_offer(false);
        else if(!right&&inside(x,y,91,84,146,94))finish_sale_offer(true);
        return;
    }
    if(talent_question_){
        if(!right&&inside(x,y,96,167,151,177))answer_talent_question(true);
        else if(!right&&inside(x,y,166,167,221,177))answer_talent_question(false);
        return;
    }
    if(quit_question_){
        if(!right&&inside(x,y,96,167,151,177)){
            try{if(session_.loaded()&&session_.dirty())autosave();finished_=true;}
            catch(const std::exception&e){quit_question_=false;notice(e.what());}
        }else if(!right&&inside(x,y,166,167,221,177))quit_question_=false;
        return;
    }
    if(session_.contract_question()){
        // the review of the day first (career_click), then the dialog "Vertragsverhandlung" (classic_contracts.cpp)
        if(career_click(x,y,right))return;
        if(contract_editor_&&contract_editor_->season)click_contract_editor(x,y,right);
        return;
    }
    if(advancing_)return;
    if(career_click(x,y,right))return;
    if(screen_==ClassicScreen::Advertising){click_advertising(x,y,right);return;}
    if(screen_==ClassicScreen::Stadium){click_stadium(x,y,right);return;}
    if(screen_==ClassicScreen::Youth){click_youth(x,y,right);return;}
    if(screen_==ClassicScreen::Settings){click_settings(x,y,right);return;}
    if(screen_==ClassicScreen::Highscore||screen_==ClassicScreen::NewManager){click_misc(x,y,right);return;}
    if(fee_editor_)return;
    if(file_preview_)return;
    if(player_detail_){
        if(!right&&x>168&&x<225&&y>208&&y<220)player_detail_.reset();
        return;
    }
    if(club_info_){
        if(team_schedule_open_){
            if(right||inside(x,y,104,216,159,226)){finish_team_schedule(true);return;}
            if(inside(x,y,174,216,229,226)){finish_team_schedule(false);return;}
            if(inside(x,y,244,216,299,226))open_team_schedule(team_schedule_half_^1);
            return;
        }
        if(right||inside(x,y,104,216,159,226)){close_club_info();return;}
        if(inside(x,y,244,216,299,226)){
            const int team=*club_info_,old=snapshot().state.at(0x17E7+size_t(team)*61+0x34)>>6;
            action([&](Game& game){game.set_team_reporting(team,old==2?0:old+1);return ActionResult{};});
        }else if(inside(x,y,174,216,229,226)&&classic_club_info(snapshot(),*club_info_,club_info_mode_).schedule)open_team_schedule();
        return;
    }
    if(screen_==ClassicScreen::Statistics){click_statistics(x,y,right);return;}
    if(roster_kind())roster_rules_=false;
    if(contract_editor_){click_contract_editor(x,y,right);return;}
    if(screen_==ClassicScreen::Lineup){click_lineup(x,y,right);return;}
    if(screen_==ClassicScreen::Tactics){click_tactics(x,y,right);return;}
    if(screen_==ClassicScreen::Transfer){click_transfer(x,y,right);return;}
    if(screen_==ClassicScreen::WmSquad){click_wm_squad(x,y,right);return;}  // national coach (classic_wm_mode.cpp)
    if(screen_==ClassicScreen::Friendlies){click_friendlies(x,y,right);return;}
    if(screen_==ClassicScreen::LeagueCompetition){
        if(right){finish_page();return;}
        if(fixture_league_==0){
            const auto hit=classic_playoff_hit(classic_playoff_view(*fixture_backdrop_,calendar_),x,y);
            if(hit.action==ClassicPlayoffAction::Liga)open_league_fixtures(fixture_league_);
            else if(hit.action==ClassicPlayoffAction::ClubInfo)open_club_info(hit.team,hit.mode);
        }else{
            const auto view=classic_promotion_view(*fixture_backdrop_,calendar_,fixture_league_-1);
            for(int i=0;i<2;++i)if(inside(x,y,79+114*i,196,127+114*i,234)){open_league_fixtures(fixture_league_);return;}
            if(view.available){
                for(const auto& match:view.fixtures)if(y>match.baseline-5&&y<match.baseline+2){open_club_info(x<=120?match.home:match.away);return;}
                for(const auto& row:view.rows)if(y>row.baseline-8&&y<row.baseline+2){open_club_info(row.team);return;}
            }
        }
        return;
    }
    if(screen_==ClassicScreen::LeagueFixtures){
        if(right){finish_page();return;}
        const auto hit=classic_league_fixtures_hit(classic_league_fixtures(*fixture_backdrop_,schedule_,calendar_,fixture_league_,fixture_round_,fixture_width_),x,y);
        if(hit.action==ClassicLeagueAction::Previous)open_league_fixtures(fixture_league_,fixture_round_==1?44:fixture_round_-1,fixture_next_width_);
        else if(hit.action==ClassicLeagueAction::Next)open_league_fixtures(fixture_league_,fixture_round_==44?1:fixture_round_+1,fixture_next_width_);
        else if(hit.action==ClassicLeagueAction::ClubInfo)open_club_info(hit.team,hit.mode);
        else if(hit.action==ClassicLeagueAction::Competition)open_league_competition(fixture_league_);
        return;
    }
    if(screen_==ClassicScreen::WorldChampionship){
        if(right){finish_page();return;}
        const auto hit=classic_wm_hit(classic_wm_view(snapshot(),calendar_,wm_page_,wm_round_),x,y);
        if(hit.action==ClassicWmAction::ClubInfo)open_club_info(hit.team,hit.mode);
        else if(hit.action==ClassicWmAction::Page)open_wm(hit.page);
        else if(hit.action==ClassicWmAction::Round)open_wm(wm_page_,hit.round);
        else if(hit.action==ClassicWmAction::PreviousFixtures)open_wm(wm_previous_fixtures_);
        return;
    }
    if(screen_==ClassicScreen::EuroCup){
        if(right){finish_page();return;}
        const auto view=classic_euro_view(snapshot(),calendar_,euro_page_);
        for(int button=0;button<2;button++)if(inside(x,y,79+114*button,196,127+114*button,234)){open_euro(view.button_pages[size_t(button)]);return;}
        if(view.available){
            for(const auto& match:view.fixtures)if(y>match.baseline-5&&y<match.baseline+2){open_club_info(x<=120?match.home:match.away);return;}
            for(const auto& row:view.rows)if(y>row.baseline-8&&y<row.baseline+2){open_club_info(row.team);return;}
        }
        return;
    }
    if(screen_==ClassicScreen::Cup){
        if(right){finish_page();return;}
        const auto hit=classic_cup_hit(classic_cup(snapshot(),calendar_),x,y);
        if(hit.team>=0)open_club_info(hit.team,hit.mode);
        return;
    }
    if(screen_==ClassicScreen::Strengths){
        if(right){finish_page();return;}
        for(int mode=0;mode<3;mode++)if(x>43+mode*80&&x<100+mode*80&&y>212&&y<224){strengths_mode_=mode;return;}
        if(y>27&&y<213&&(y-27)/15<12){
            const auto rows=classic_strengths_rows(snapshot(),strengths_league_,strengths_mode_);
            open_club_info(rows[size_t((y-27)/15)].team);
        }
        return;
    }
    if(screen_==ClassicScreen::Main){
        if(right){category_=-1;return;}
        if(inside(x,y,275,34,312,71)){start_advance();return;}
        for(int row=0;row<2;row++)for(int col=0;col<4;col++)if(inside(x,y,8+77*col,80+60*row,79+77*col,136+60*row)){
            const int category=row*4+col;if(category==7)request_exit();else category_=category;return;
        }
        constexpr int counts[]={6,5,6,6,6,4,3,0};
        if(category_>=0)for(int item=0;item<counts[category_];item++)if(inside(x,y,3+53*item,201,50+53*item,238)){if(wm_menu_route(item))return;  // night sky or national page (classic_wm_mode.cpp)
            if(category_==1&&item==3)transfer_native_si_=int16_t(y);route_submenu(item);return;}
        return;
    }
    if(screen_==ClassicScreen::Files){
        if(right){
            if(y>=54&&y<166)for(int col=0;col<3;col++)if(x>=42+80*col&&x<=121+80*col){
                const size_t index=file_page_+size_t(col*16+(y-54)/7);
                if(index<files_.size()&&files_[index].save){
                    try{
                        Bytes bytes=read_file(files_[index].path);
                        if(is_game_checkpoint(bytes))bytes=game_save_hok(read_file(data_/"EM.EXE"),bytes);
                        auto path=virtual_file_path(files_[index].path);if(path.size()>2)path.erase(0,2);
                        file_preview_=FilePreview{decode_classic_preview(bytes),std::move(path)};
                    }catch(const std::exception& e){notice(e.what());}
                    return;
                }
            }
            if(session_.loaded())screen_=ClassicScreen::Main;else if(start_menu_open())screen_=ClassicScreen::NewManager;return;  // ovr027:0EDD: back to the start menu
        }
        if(inside(x,y,88,194,290,204)){filename_focus_=true;filename_.clear();return;}
        if(y>=207&&y<=218){
            if(inside(x,y,171,208,226,217)||inside(x,y,230,208,285,217)){
                file_directory_=x<230?data_:saves_;filename_.clear();filename_focus_=false;file_selection_=-1;file_page_=0;refresh_files();return;
            }
        }
        if(inside(x,y,149,168,160,178)){if(file_page_>=48)file_page_-=48;return;}
        if(inside(x,y,173,168,184,178)){if(file_page_+48<files_.size())file_page_+=48;return;}
        if(y>=54&&y<166)for(int col=0;col<3;col++)if(x>=42+80*col&&x<=121+80*col){
            const size_t index=file_page_+size_t(col*16+(y-54)/7);
            if(index<files_.size()){
                const auto entry=files_[index];const bool again=file_selection_==int(index);file_selection_=int(index);
                if(entry.directory){file_directory_=entry.path;filename_.clear();file_page_=0;refresh_files();}
                else{filename_=path_text(entry.path.filename());filename_focus_=false;if(again&&entry.save)accept_file();} //native: any file fills FILENAME
            }
            return;
        }
        return;
    }
    if(screen_==ClassicScreen::Table){
        if(right){screen_=ClassicScreen::Main;post_table_open_=false;if(!post_tables_&&news_after_tables_){news_after_tables_=false;show_manager_news();}return;}
        for(int v=0;v<3;v++)if(inside(x,y,31+102*v,165,86+102*v,175)){table_view_=v;return;}
        return;
    }
    if(screen_==ClassicScreen::Contracts){
        const int slot=(y-29)/6;
        if(right&&x>34&&x<96&&y>=29&&slot>=0&&size_t(slot)<session_.game().save().squad_of(active_manager()).size()){open_player(slot);return;}
        if(right||y<14){screen_=ClassicScreen::Main;return;}
        if(x>=0&&x<=255&&slot>=0&&size_t(slot)<session_.game().save().squad_of(active_manager()).size())open_contract(slot);
        return;
    }
    if(screen_==ClassicScreen::Bank){
        if(right){screen_=ClassicScreen::Main;return;}
        if(x<106&&y<54){bank_party_=4;bank_selected_=true;bank_amount_=0;return;}
        if(x<47&&y>101){const int company=(y-102)/34;if(company<4){bank_party_=5+company;bank_selected_=true;bank_amount_=0;}return;}
        if(!bank_selected_)return;
        const auto& b=session_.game().budget();
        const auto& h=snapshot();
        const int64_t counter=int32_t(le32(h.state,0x0DCD+4*0x1AF+0x19B+4*size_t(active_manager())));
        const bool stock=bank_party_>=5;
        const auto limit=[&]{
            if(stock){const size_t company=size_t(bank_party_-5);const int64_t price=le16(h.state,0xB0+company*72+70);return bank_deposit_?int64_t(le32(h.state,0x30+company*8+4)):price?std::max<int64_t>(0,b.balance)/price:0;}
            return bank_deposit_?std::max<int64_t>(0,b.balance):std::max<int64_t>(0,std::max<int64_t>(b.bank,6000000-std::max<int64_t>(0,counter)));
        };
        const int step=stock?1:3000;
        if(inside(x,y,4,90,314,98)&&x!=159){
            bank_deposit_=x>159;
            const int width=bank_deposit_?x-160:158-x;
            bank_amount_=std::min(limit(),(limit()/154+1)*width/step*step);return;
        }
        if(y>=74&&y<=87&&(inside(x,y,4,74,34,87)||inside(x,y,284,74,314,87))){
            bank_deposit_=x>=284;
            const int sign=(x<=18||x>=300)?1:-1;
            bank_amount_=std::clamp<int64_t>(bank_amount_+sign*step,0,limit());return;
        }
        if(y>=73&&y<=88&&bank_amount_>0){
            const int64_t amount=bank_amount_;
            action([&](Game& g){
                const bool ok=stock?g.trade_shares(size_t(bank_party_-5),uint32_t(amount),bank_deposit_)==StockResult::Traded:g.bank_transfer(long(bank_deposit_?amount:-amount))==BankResult::Transferred;
                return ActionResult{ok?"":"Dieser Bankauftrag ist nicht möglich.",ok};
            });
            bank_amount_=0;return;
        }
        bank_amount_=0;return;
    }
    if(screen_==ClassicScreen::Training){
        if(right){
            if(training_changed_)action([&](Game& g){const bool ok=g.set_training_plan(training_plan_);return ActionResult{{},ok};});
            screen_=ClassicScreen::Main;return;
        }
        if(inside(x,y,26,215,293,237)){training_kind_=(x-26)/30;return;}
        constexpr int ys[]={103,125,168,190};
        for(int day=0;day<7;day++)for(int unit=0;unit<4;unit++)if(inside(x,y,14+44*day,ys[unit],37+44*day,ys[unit]+18)){
            training_plan_[size_t(day)][size_t(unit)]=uint8_t(training_kind_);training_changed_=true;return;
        }
        return;
    }
    if(right)screen_=ClassicScreen::Main;
}
void ClassicApplication::release(){
    //7348:1188..11B4 restores the chooser on either mouse-button release.
    //The pointer position and an additional button-down do not close it.
    file_preview_.reset();
    stats_hold_.reset();
    career_release();
}
void ClassicApplication::motion(int x,int y){
    mouse_x_=x;mouse_y_=y;
    if(screen_==ClassicScreen::Transfer&&!sale_quote_&&!contract_editor_&&!fee_editor_)transfer_native_si_=int16_t(x);
    update_advertising_hover();
    update_stadium_hover();
    update_roster_hover();
    if(screen_==ClassicScreen::Bank&&bank_selected_){
        if(y<73||y>100)bank_amount_=0;
        else if(x<159)bank_deposit_=false;
        else if(x>159)bank_deposit_=true;
    }
}
void ClassicApplication::key(int key){
    if(title_){title_=false;return;}
    if(cup_live_){if(cup_live_waiting())click_cup_live(0,0,false);return;}  // sub_19B08 also takes a key
    if(game_end_==2){finished_=true;return;}
    // the key buffer read by ovl14_1dc7 after a goal scene
    if(!match_frames_.empty()&&!live_screen_&&(key==SDLK_r||key==SDLK_w)){replay_key_=true;return;}
    if(live_day_&&!live_screen_){  // the day is being played
        // ovl06_2522: the space key opens the Einstellungen at the start of the next minute, which then passes without play
        if(key==SDLK_SPACE&&!match_frames_.empty()&&!live_in_scene_){live_screen_=true;live_idle_=true;live_screen_before_=screen_;open_settings();}
        return;
    }
    if(native_notice_until_||sale_quote_)return;
    if(career_key(key))return;
    if(screen_==ClassicScreen::NewManager){key_misc(key);return;}
    if(screen_==ClassicScreen::Youth)return; //087B is mouse-driven, including Escape
    if(screen_==ClassicScreen::Stadium){if(!notice_.empty())notice_.clear();return;}
    if(advertising_info_||advert_renewal_)return;
    if(player_detail_||file_preview_||club_info_)return;
    if(fee_editor_&&notice_.empty()){
        if(key==SDLK_ESCAPE)finish_fee(true);
        else if(key==SDLK_RETURN||key==SDLK_KP_ENTER)finish_fee(false);
        else if(key==SDLK_BACKSPACE&&!fee_editor_->digits.empty())fee_editor_->digits.pop_back();
        return;
    }
    if(contract_editor_&&notice_.empty()){
        if(contract_editor_->field>=0){
            if(key==SDLK_ESCAPE)finish_numeric(true);
            else if(key==SDLK_RETURN||key==SDLK_KP_ENTER)finish_numeric(false);
            else if(key==SDLK_BACKSPACE&&!contract_editor_->digits.empty())contract_editor_->digits.pop_back();
        }
        return;
    }
    if(key==SDLK_ESCAPE){
        if(!notice_.empty())notice_.clear();else if(quit_question_)quit_question_=false;
        else if(session_.contract_question()){session_.cancel_advance();advancing_=false;contract_editor_.reset();season_view_.reset();season_review_shown_=false;season_outcomes_seen_=0;season_pending_open_=false;season_editor_with_notice_=false;}
        else if(screen_==ClassicScreen::Main)category_=-1;else if(screen_!=ClassicScreen::Training&&screen_!=ClassicScreen::Tactics&&screen_!=ClassicScreen::Lineup&&screen_!=ClassicScreen::Transfer&&screen_!=ClassicScreen::Strengths&&screen_!=ClassicScreen::Cup&&screen_!=ClassicScreen::EuroCup&&screen_!=ClassicScreen::WorldChampionship&&screen_!=ClassicScreen::LeagueFixtures&&screen_!=ClassicScreen::LeagueCompetition&&screen_!=ClassicScreen::Advertising&&session_.loaded())screen_=ClassicScreen::Main;
        return;
    }
    if(screen_==ClassicScreen::Files&&filename_focus_){
        if(key==SDLK_RETURN||key==SDLK_KP_ENTER)accept_file();
        else if(key==SDLK_BACKSPACE&&!filename_.empty())filename_.pop_back();
    }
}
void ClassicApplication::text_input(std::string_view text){
    if(native_notice_until_)return;
    if(screen_==ClassicScreen::NewManager){text_misc(text);return;}
    //Text input may clear diagnostic errors. Native timed notices above and
    //pending construction questions retain their own completion rules.
    if(screen_==ClassicScreen::Stadium){if(!text.empty())notice_.clear();return;}
    if(fee_editor_&&notice_.empty()){
        for(unsigned char c:text)if(c>='0'&&c<='9'&&fee_editor_->digits.size()<7)fee_editor_->digits.push_back(char(c));
        return;
    }
    if(contract_editor_&&contract_editor_->field>=0&&notice_.empty()){
        auto& e=*contract_editor_;const size_t limit=e.field==0?1:6;
        for(unsigned char c:text)if(c>='0'&&c<='9'&&e.digits.size()<limit)e.digits.push_back(char(c));
        return;
    }
    if(screen_==ClassicScreen::Files&&filename_focus_&&notice_.empty()){
        for(unsigned char c:text)if(c>=32&&c<127&&filename_.size()<64)filename_.push_back(char(std::toupper(c)));
    }
}
void ClassicApplication::draw(){
    if(title_){canvas_.background(197);if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);return;}
    if(cup_live_){draw_cup_live();if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);return;}  // classic_cup_draw.cpp
    if(game_end_==2){draw_game_end();if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);return;}
    if(!match_frames_.empty()&&!live_screen_){
        const ClassicMatchFrame& frame=match_frames_[match_frame_];
        if(frame.league_page){
            std::vector<std::array<int,4>> scores;
            for(const ClassicCupLive& c:frame.league)scores.push_back({c.home,c.away,c.goals[0],c.goals[1]});
            draw_classic_league_live(canvas_,snapshot(),schedule_,calendar_,frame.league_page-1,frame.minute,scores);
            if(club_info_)draw_classic_club_info(canvas_,snapshot(),*club_info_,club_info_mode_);
        }
        else if(frame.cup_table&&match_before_)draw_classic_cup_minute(canvas_,*match_before_,calendar_,frame.cup_table,frame.cup);
        else draw_classic_match_view(canvas_,frame);
        if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);
        return;
    }
    if(match_banner_until_){
        draw_banner("Ligaspiel");
        if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);
        return;
    }
    if(season_view_&&career_modal().empty()){  // the contract talks of the season change (classic_contracts.cpp)
        draw_season_talk();draw_dialog();
        if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);
        return;
    }
    switch(screen_){
        case ClassicScreen::Main:draw_main();break;case ClassicScreen::Files:draw_files();break;
        case ClassicScreen::Table:draw_table();break;case ClassicScreen::Totals:draw_totals();break;
        case ClassicScreen::Bank:draw_bank();break;case ClassicScreen::Contracts:draw_contracts();break;
        case ClassicScreen::Training:draw_training();break;case ClassicScreen::Transfer:draw_transfer();break;
        case ClassicScreen::WmSquad:draw_wm_squad();break;case ClassicScreen::Friendlies:draw_friendlies();break;  // classic_wm_mode.cpp
        case ClassicScreen::Lineup:draw_lineup();break;case ClassicScreen::Tactics:draw_tactics();break;
        case ClassicScreen::Strengths:draw_strengths();break;
        case ClassicScreen::Cup:draw_cup();break;case ClassicScreen::EuroCup:draw_euro();break;
        case ClassicScreen::WorldChampionship:draw_wm();break;
        case ClassicScreen::LeagueFixtures:draw_league_fixtures();break;
        case ClassicScreen::LeagueCompetition:draw_league_competition();break;
        case ClassicScreen::Advertising:draw_advertising();break;
        case ClassicScreen::Stadium:draw_stadium();break;
        case ClassicScreen::Youth:draw_classic_youth(canvas_,snapshot(),session_.finance_tables(),active_manager());break;
        case ClassicScreen::Settings:draw_settings();break;
        case ClassicScreen::Statistics:draw_statistics();break;
        case ClassicScreen::Highscore:case ClassicScreen::NewManager:draw_misc();break;
    }
    draw_career();
    draw_roster_hover();
    if(sale_quote_)draw_sale_offer();
    if(fee_editor_)draw_fee_editor();
    if(contract_editor_)draw_contract_editor();
    if(file_preview_)draw_classic_preview(canvas_,file_preview_->header,file_preview_->path);
    if(player_detail_){const auto& p=*player_detail_;draw_classic_player(canvas_,snapshot(),p.player,p.manager,p.slot,p.quote);}
    if(club_info_)draw_classic_club_info(canvas_,snapshot(),*club_info_,club_info_mode_);
    if(team_schedule_open_&&club_info_)draw_classic_team_schedule(canvas_,snapshot(),schedule_,calendar_,*club_info_,team_schedule_half_);
    draw_dialog();
    if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);
}
void ClassicApplication::draw_dialog(){
    if(manager_question_){draw_manager_question();return;}  // hot seat (classic_hotseat.cpp)
    if(notice_.empty()&&!quit_question_&&!talent_question_&&!advancing_&&!advert_renewal_&&!stadium_build_)return;
    if(advancing_&&notice_.empty())return;
    const bool advert_question=advert_renewal_.has_value()&&!advert_renewal_refused_;
    const bool stadium_question=stadium_build_.has_value();
    const bool talent_question=talent_question_&&notice_.empty();
    const bool question=quit_question_||advert_question||stadium_question||talent_question;
    canvas_.frame(75,107,7,4);
    canvas_.text(flow,105,125,session_.loaded()?manager_name(active_manager()):"",7,0,210);  // FUN_00146858: the name of the manager at the menu
    for(const int x:{83,212}){
        canvas_.rect(x,112,x+21,133,question?14:17);canvas_.outline(x+1,113,x+20,132,question?12:16);
        canvas_.text(ClassicFontId::NewBold,x,129,question?"?":"!",0,-1,x+(question?24:23));
    }
    canvas_.line(109,131,207,131,7);canvas_.line(80,163,237,163,0);
    if(quit_question_){
        canvas_.text(mini,105,141,"Sie haben also wirklich",7,0,210);
        canvas_.text(mini,105,149,"etwas besseres zu tun ?",7,0,210);
        canvas_.text(mini,105,157,"Sind Sie sicher ??",7,0,210);
    }else if(talent_question){
        draw_talent_question();
    }else if(stadium_question){
        const auto& draft=*stadium_build_;
        canvas_.text_native(mini,105,141,"M|chten Sie Ihr Stadion ausbauen ?",7,0,210);
        canvas_.text_native(mini,105,149,"Rabatt:"+std::string(draft.discount<10?"%":"")+std::to_string(draft.discount)+"#",7,0,210);
        canvas_.text_native(mini,105,157,"Bauzeit: ca. "+std::string(draft.weeks<10?"%":"")+std::to_string(draft.weeks)+" Wochen",7,0,210);
    }else if(advert_question){
        canvas_.text_native(mini,105,141,"M|chten Sie den Vertrag mit",7,0,210);
        canvas_.text_native(mini,105,149,advert_renewal_name_,7,0,210);
        canvas_.text_native(mini,105,157,"verl{ngern ?",7,0,210);
    }else{
        std::vector<std::string> lines;
        if(notice_.find('\n')!=std::string::npos){
            size_t start=0;
            do{const size_t end=notice_.find('\n',start);lines.push_back(notice_.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}while(start<=notice_.size());
        }else{
            size_t at=0;
            while(at<notice_.size()){
                size_t n=std::min(size_t(30),notice_.size()-at);
                while(n>1&&canvas_.font().measure(mini,std::string_view(notice_).substr(at,n))>105)--n;
                if(at+n<notice_.size()){const auto space=notice_.rfind(' ',at+n);if(space!=std::string::npos&&space>at)n=space-at;}
                lines.push_back(notice_.substr(at,n));at+=n;while(at<notice_.size()&&notice_[at]==' ')at++;
            }
        }
        const int y=lines.size()>3?139:lines.size()<3?145:141,pitch=lines.size()>3?7:8;
        for(size_t i=0;i<std::min(lines.size(),size_t(4));i++)canvas_.text(mini,105,y+int(i)*pitch,lines[i],7,0,210);
        if(lines.size()>=3){button(95,166,"Na gut.");button(165,166,"Na gut.");}
    }
    if(quit_question_||advert_question||stadium_question||talent_question){
        button(95,166,talent_question?"Aber immer!":quit_question_?"Leider ja":advert_question||stadium_question?"Ja.":"Mach' ich!");
        button(165,166,talent_question?"Lieber nicht.":quit_question_?"Kein Gedanke":advert_question||stadium_question?"Nein.":"Lass' ich!");
    }
}
void ClassicApplication::render_screen(std::string_view name){
    if(screen_==ClassicScreen::Stadium)canvas_.set_button_edge(3);
    hover_kind_.reset();roster_hover_={};roster_rules_=false;
    transfer_hover_={};pool_rules_=pool_help_visible_=false;
    if(name=="main")screen_=ClassicScreen::Main;
    else if(name=="youth")screen_=ClassicScreen::Youth;
    else if(name=="match-banner"){match_banner_until_=UINT64_MAX;}
    else if(name=="title")title_=true;
    else if(name=="settings"||name=="settings-rink"||name=="settings-slow"){
        open_settings();
        if(name=="settings-rink")action([](Game& game){game.toggle_setting(9,0);game.toggle_setting(9,0);return ActionResult{};});
        if(name=="settings-slow")action([](Game& game){game.set_game_speed(20);return ActionResult{};});
    }
    else if(name=="stadium"||name=="stadium-planned"||name=="stadium-quote"||name=="stadium-pending"||name=="stadium-built"||name=="stadium-condition"||name=="stadium-condition-pending"){
        open_stadium();stadium_selection_.planned=name!="stadium";
        if(name=="stadium-built")stadium_selection_.numberWidth=1;
        if(name=="stadium-quote"||name=="stadium-pending"||name=="stadium-condition"||name=="stadium-condition-pending"){
            const int item=name=="stadium-condition"||name=="stadium-condition-pending"?4:8;
            stadium_selection_.hover=item;stadium_selection_.quoteItem=item;stadium_selection_.target=-1;
            if(name=="stadium-pending"||name=="stadium-condition-pending"){StadiumBuildDraft draft;action([&](Game& game){if(game.begin_stadium_build(item,-1,draft)==StadiumBuildResult::Prepared)stadium_build_=draft;return ActionResult{};});}
        }
    }
    else if(name=="main-files-return"){screen_=ClassicScreen::Main;category_=6;}
    else if(render_statistics(name)){}
    else if(render_season_talk(name)){}
    else if(name=="table")screen_=ClassicScreen::Table;
    else if(name=="load")open_files(false);else if(name=="save")open_files(true);
    else if(name=="totals")screen_=ClassicScreen::Totals;else if(name=="bank")screen_=ClassicScreen::Bank;
    else if(name=="contracts")screen_=ClassicScreen::Contracts;else if(name=="training"){screen_=ClassicScreen::Training;training_plan_=session_.game().training_plan();}
    else if(name=="contract"){screen_=ClassicScreen::Contracts;open_contract(0);}
    else if(name=="player"){open_lineup();update_roster_hover();open_player(0);}
    else if(name=="strengths"||name=="strengths-defence"||name=="strengths-attack"||name=="strengths-wm"){
        open_strengths(name=="strengths-wm"?4:0);
        strengths_mode_=name=="strengths-defence"?1:name=="strengths-attack"?2:0;
    }
    else if(name=="clubinfo"){open_strengths(0);strengths_mode_=2;open_club_info(4);}
    else if(name=="cup")open_cup();
    else if(name=="cup-away"||name=="cup-home"){
        open_cup();open_club_info(name=="cup-away"?7:6,name=="cup-away"?1:0);
    }
    else if(name=="euro-group1"||name=="euro-group2"||name=="euro-final")open_euro(name=="euro-group1"?3:name=="euro-group2"?4:5);
    else if(name=="wm-group1"||name=="wm-group2")open_wm(name=="wm-group1"?11:12);
    else if(name=="wm-fixtures1"||name=="wm-fixtures2"||name=="wm-fixtures-next")open_wm(name=="wm-fixtures2"?9:8,name=="wm-fixtures-next"?1:-1);
    else if(name=="wm-final")open_wm(10);
    else if(name=="wm-club"){open_wm(9);open_club_info(58);}
    else if(name=="league-fixtures1"||name=="league-fixtures2"||name=="league-fixtures3")open_league_fixtures(name=="league-fixtures1"?0:name=="league-fixtures2"?1:2);
    else if(name=="league-fixtures-next"||name=="league-fixtures-past")open_league_fixtures(0,name=="league-fixtures-next"?11:9);
    else if(name=="league-playoff"||name=="promotion1"||name=="promotion2")open_league_competition(name=="league-playoff"?0:name=="promotion1"?1:2);
    else if(name=="advertising"||name=="advertising-offer"||name=="advertising-info"||name=="advertising-accepted"||name=="advertising-inactive"){
        open_advertising();
        if(name=="advertising-inactive")advertising_selection_={10,0,false};
        else if(name!="advertising"){
            if(name=="advertising-accepted")action([](Game& game){return ActionResult{{},game.accept_sponsor_offer(0,4)};});
            advertising_selection_={3,-1,true};
            advertising_selection_=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager()).selection;
            advertising_info_=name=="advertising-info";
        }
    }
    else if(name=="team-schedule"||name=="team-schedule-second"){
        open_strengths(0);strengths_mode_=2;open_club_info(4);open_team_schedule(name=="team-schedule"?0:1);
    }
    else if(name=="purchase-fee"||name=="purchase-contract"){
        screen_=ClassicScreen::Transfer;update_roster_hover();open_purchase(3);
        if(name=="purchase-contract"&&fee_editor_){fee_editor_->digits="400000";finish_fee(false);}
    }
    else if(name=="loan-fee"){screen_=ClassicScreen::Transfer;update_roster_hover();open_loan(3);}
    else if(name=="talent-question"){screen_=ClassicScreen::Transfer;update_roster_hover();open_talent_question();}
    else if(name.rfind("subsidy-question:",0)==0){screen_=ClassicScreen::Transfer;update_roster_hover();open_subsidy_question(std::stol(std::string(name.substr(17))));}
    else if(name=="lineup")open_lineup();else if(name=="tactics")open_tactics();
    else if(name=="transfer")screen_=ClassicScreen::Transfer;else throw FormatError("Unknown render screen");
    update_roster_hover();draw();
}
} // namespace em::ui

