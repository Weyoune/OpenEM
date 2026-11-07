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
#include "game/checkpoint.hpp"
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
bool inside(int x,int y,int l,int t,int r,int b) {return x>=l&&x<=r&&y>=t&&y<=b;}
std::string upper(std::string s){for(char& c:s)c=char(std::toupper(static_cast<unsigned char>(c)));return s;}
}

ClassicApplication::ClassicApplication(fs::path data,fs::path saves,const fs::path& initial)
    :data_(std::move(data)),saves_(std::move(saves)),session_(data_),canvas_(data_),roster_help_(read_file(data_/"EM.EXE")),transfer_help_(read_file(data_/"EM.EXE")),calendar_(load_season_calendar(read_file(data_/"EM.EXE"))),schedule_(load_schedule_tables(read_file(data_/"EM.EXE"))) {
    if(!initial.empty())session_.load(initial);
    if(session_.loaded())table_order_=session_.game().league(division_).ranking;
    if(!session_.loaded())open_files(false);
}
const HokFile& ClassicApplication::snapshot(){
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
    native_notice_until_=SDL_GetTicks64()+1374;
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
    advancing_=true;advance_first_=true;advanced_days_=0;category_=-1;
}
void ClassicApplication::tick(){
    if(native_notice_until_){
        if(SDL_GetTicks64()<*native_notice_until_)return;
        finish_timed_native_notice();
    }
    if(!advancing_)update_roster_hover();
    if(!advancing_||!notice_.empty()||quit_question_||session_.contract_question())return;
    if(match_banner_until_){
        if(SDL_GetTicks64()<*match_banner_until_)return;
        match_banner_until_.reset();
    }
    try{
        const bool match_day=session_.next_day_has_match();
        if(!advance_first_&&match_day){
            advancing_=false;autosave();return;
        }
        // The original announces the own league match with a banner before the day is played.
        if(match_day&&match_banner_day_!=session_.game().season_day()&&calendar_.league_day(session_.game().season_day()+1)){
            match_banner_day_=session_.game().season_day();match_banner_until_=SDL_GetTicks64()+kMatchBannerMs;return;
        }
        if(++advanced_days_>370){notice("Kein weiterer eigener Spieltermin gefunden.");return;}
        if(session_.advance_day()){advance_first_=false;invalidate();}
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::request_exit(){
    if(stadium_build_||native_notice_until_||sale_quote_)return; //native modal phases must finish
    advancing_=false;
    if(screen_==ClassicScreen::Stadium)canvas_.set_button_edge(3);
    advertising_info_=false;if(advert_renewal_)resolve_advert_renewal(false);
    if(menu_working_)finish_page();
    fee_editor_.reset();
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
        case 0:if(item==0){screen_=ClassicScreen::Bank;bank_party_=4;bank_amount_=0;bank_selected_=false;return;}if(item==1){open_advertising();return;}if(item==2){open_stadium();return;}if(item==3){screen_=ClassicScreen::Youth;return;}break;
        case 1:
            if(item==0){open_lineup();return;}
            if(item==1){open_tactics();return;}
            if(item==2){screen_=ClassicScreen::Contracts;return;}
            if(item==3){screen_=ClassicScreen::Transfer;return;}
            if(item==4){screen_=ClassicScreen::Training;training_plan_=session_.game().training_plan();training_changed_=false;training_kind_=0;return;}
            break;
        case 2:if(item==0){screen_=ClassicScreen::Totals;return;}break;
        case 3:
            if(item<3){division_=item;table_view_=1;table_order_=session_.game().league(item).ranking;screen_=ClassicScreen::Table;return;}
            if(item==3){open_cup();return;}
            if(item==4){open_euro(euro_page_);return;}
            if(item==5){open_wm(wm_table_page_);return;}
            break;
        case 6:if(item<2){open_files(item==1);return;}if(item==2){open_settings();return;}break;
        case 5:if(item<4){open_strengths(item==3?4:item);return;}break;
        case 4:if(item<3){if(league_page_==7)open_league_fixtures(item);else open_league_competition(item);return;}if(item==3){open_cup();return;}if(item==5){open_wm(wm_fixture_page_);return;}break;
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
            session_.load(file);invalidate();
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
    if(native_notice_until_)return;
    if(!notice_.empty()){
        if(!right){notice_.clear();if(advert_renewal_refused_)resolve_advert_renewal(false);if(!pending_notices_.empty()){notice_=pending_notices_.front();pending_notices_.erase(pending_notices_.begin());}}
        return;
    }
    if(sale_quote_){
        if(!right&&inside(x,y,12,84,67,94))finish_sale_offer(false);
        else if(!right&&inside(x,y,91,84,146,94))finish_sale_offer(true);
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
        if(!right&&(inside(x,y,96,167,151,177)||inside(x,y,166,167,221,177))){
            try{session_.answer_contract(x<160);invalidate();advance_first_=false;}
            catch(const std::exception&e){notice(e.what());}
        }
        return;
    }
    if(advancing_)return;
    if(screen_==ClassicScreen::Advertising){click_advertising(x,y,right);return;}
    if(screen_==ClassicScreen::Stadium){click_stadium(x,y,right);return;}
    if(screen_==ClassicScreen::Youth){click_youth(x,y,right);return;}
    if(screen_==ClassicScreen::Settings){click_settings(x,y,right);return;}
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
    if(roster_kind())roster_rules_=false;
    if(contract_editor_){
        auto& e=*contract_editor_;
        if(right)return;
        if(e.field>=0)return;
        const int s=e.offset;
        if(inside(x,y,12,s+81,67,s+91)){submit_contract();return;}
        if(inside(x,y,91,s+81,146,s+91)){
            if(purchase_draft_){try{cancel_purchase();}catch(const std::exception& e){notice(e.what());}}
            else contract_editor_.reset();return;
        }
        if(x>2&&x<158&&y>s+52&&y<s+78){
            const int field=int(y>s+63)+int(y>s+71);
            const size_t record=0x696D+size_t(e.slot)*52;
            const auto& h=snapshot();
            if(field==2){
                if((h.state.at(record+0x2F)&0x80)||(h.state.at(record+9)&3)==2)return;
                e.insured=!e.insured;return;
            }
            if(h.state.at(record+0x18)!=0){notice(h.players.at(size_t(e.player)).name+" ist im Moment\nnicht verhandlungs-\nbereit.");return;}
            e.field=field;e.digits.clear();
        }
        return;
    }
    if(screen_==ClassicScreen::Lineup){click_lineup(x,y,right);return;}
    if(screen_==ClassicScreen::Tactics){click_tactics(x,y,right);return;}
    if(screen_==ClassicScreen::Transfer){click_transfer(x,y,right);return;}
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
        if(category_>=0)for(int item=0;item<counts[category_];item++)if(inside(x,y,3+53*item,201,50+53*item,238)){if(category_==1&&item==3)transfer_native_si_=int16_t(y);route_submenu(item);return;}
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
            if(session_.loaded())screen_=ClassicScreen::Main;return;
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
                else if(entry.save){filename_=path_text(entry.path.filename());filename_focus_=false;if(again)accept_file();}
            }
            return;
        }
        return;
    }
    if(screen_==ClassicScreen::Table){
        if(right){screen_=ClassicScreen::Main;return;}
        for(int v=0;v<3;v++)if(inside(x,y,31+102*v,165,86+102*v,175)){table_view_=v;return;}
        return;
    }
    if(screen_==ClassicScreen::Contracts){
        const int slot=(y-29)/6;
        if(right&&x>34&&x<96&&y>=29&&slot>=0&&size_t(slot)<session_.game().save().squad.size()){open_player(slot);return;}
        if(right||y<14){screen_=ClassicScreen::Main;return;}
        if(x>=0&&x<=255&&slot>=0&&size_t(slot)<session_.game().save().squad.size())open_contract(slot);
        return;
    }
    if(screen_==ClassicScreen::Bank){
        if(right){screen_=ClassicScreen::Main;return;}
        if(x<106&&y<54){bank_party_=4;bank_selected_=true;bank_amount_=0;return;}
        if(x<47&&y>101){const int company=(y-102)/34;if(company<4){bank_party_=5+company;bank_selected_=true;bank_amount_=0;}return;}
        if(!bank_selected_)return;
        const auto& b=session_.game().budget();
        const auto& h=snapshot();
        const int64_t counter=int32_t(le32(h.state,0x0DCD+4*0x1AF+0x19B));
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
    if(native_notice_until_||sale_quote_)return;
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
        else if(session_.contract_question()){session_.cancel_advance();advancing_=false;}
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
    if(match_banner_until_){
        draw_banner("Ligaspiel");
        if(mouse_x_>=0&&mouse_y_>=0)canvas_.cursor(mouse_x_,mouse_y_);
        return;
    }
    switch(screen_){
        case ClassicScreen::Main:draw_main();break;case ClassicScreen::Files:draw_files();break;
        case ClassicScreen::Table:draw_table();break;case ClassicScreen::Totals:draw_totals();break;
        case ClassicScreen::Bank:draw_bank();break;case ClassicScreen::Contracts:draw_contracts();break;
        case ClassicScreen::Training:draw_training();break;case ClassicScreen::Transfer:draw_transfer();break;
        case ClassicScreen::Lineup:draw_lineup();break;case ClassicScreen::Tactics:draw_tactics();break;
        case ClassicScreen::Strengths:draw_strengths();break;
        case ClassicScreen::Cup:draw_cup();break;case ClassicScreen::EuroCup:draw_euro();break;
        case ClassicScreen::WorldChampionship:draw_wm();break;
        case ClassicScreen::LeagueFixtures:draw_league_fixtures();break;
        case ClassicScreen::LeagueCompetition:draw_league_competition();break;
        case ClassicScreen::Advertising:draw_advertising();break;
        case ClassicScreen::Stadium:draw_stadium();break;
        case ClassicScreen::Youth:draw_classic_youth(canvas_,snapshot(),session_.finance_tables());break;
        case ClassicScreen::Settings:draw_settings();break;
    }
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
    if(notice_.empty()&&!quit_question_&&!session_.contract_question()&&!advancing_&&!advert_renewal_&&!stadium_build_)return;
    if(advancing_&&notice_.empty()&&!session_.contract_question())return;
    const bool advert_question=advert_renewal_.has_value()&&!advert_renewal_refused_;
    const bool stadium_question=stadium_build_.has_value();
    const bool question=quit_question_||session_.contract_question().has_value()||advert_question||stadium_question;
    canvas_.frame(75,107,7,4);
    canvas_.text(flow,105,125,session_.loaded()?session_.game().save().manager:"",7,0,210);
    for(const int x:{83,212}){
        canvas_.rect(x,112,x+21,133,question?14:17);canvas_.outline(x+1,113,x+20,132,question?12:16);
        canvas_.text(ClassicFontId::NewBold,x,129,question?"?":"!",0,-1,x+(question?24:23));
    }
    canvas_.line(109,131,207,131,7);canvas_.line(80,163,237,163,0);
    if(quit_question_){
        canvas_.text(mini,105,141,"Sie haben also wirklich",7,0,210);
        canvas_.text(mini,105,149,"etwas besseres zu tun ?",7,0,210);
        canvas_.text(mini,105,157,"Sind Sie sicher ??",7,0,210);
    }else if(stadium_question){
        const auto& draft=*stadium_build_;
        canvas_.text_native(mini,105,141,"M|chten Sie Ihr Stadion ausbauen ?",7,0,210);
        canvas_.text_native(mini,105,149,"Rabatt:"+std::string(draft.discount<10?"%":"")+std::to_string(draft.discount)+"#",7,0,210);
        canvas_.text_native(mini,105,157,"Bauzeit: ca. "+std::string(draft.weeks<10?"%":"")+std::to_string(draft.weeks)+" Wochen",7,0,210);
    }else if(advert_question){
        canvas_.text_native(mini,105,141,"M|chten Sie den Vertrag mit",7,0,210);
        canvas_.text_native(mini,105,149,advert_renewal_name_,7,0,210);
        canvas_.text_native(mini,105,157,"verl{ngern ?",7,0,210);
    }else if(session_.contract_question()){
        const auto& q=*session_.contract_question();
        canvas_.text(mini,105,141,q.name,7,0,210);
        canvas_.text(mini,105,149,"Neuer Vertrag für 2 Jahre",7,0,210);
        canvas_.text(mini,105,157,money(q.monthly_salary)+" / Monat",7,0,210);
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
    if(quit_question_||session_.contract_question()||advert_question||stadium_question){
        button(95,166,quit_question_?"Leider ja":advert_question||stadium_question?"Ja.":"Mach' ich!");
        button(165,166,quit_question_?"Kein Gedanke":advert_question||stadium_question?"Nein.":"Lass' ich!");
    }
}
void ClassicApplication::render_screen(std::string_view name){
    if(screen_==ClassicScreen::Stadium)canvas_.set_button_edge(3);
    hover_kind_.reset();roster_hover_={};roster_rules_=false;
    transfer_hover_={};pool_rules_=pool_help_visible_=false;
    if(name=="main")screen_=ClassicScreen::Main;
    else if(name=="youth")screen_=ClassicScreen::Youth;
    else if(name=="match-banner"){match_banner_until_=UINT64_MAX;}
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
            advertising_selection_=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_).selection;
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
    else if(name=="lineup")open_lineup();else if(name=="tactics")open_tactics();
    else if(name=="transfer")screen_=ClassicScreen::Transfer;else throw FormatError("Unknown render screen");
    update_roster_hover();draw();
}
} // namespace em::ui

