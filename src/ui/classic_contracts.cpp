// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "ui/classic_roster.hpp"
#include <algorithm>

namespace em::ui {
namespace { constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow; }

void ClassicApplication::plain_frame(int left,int top,int right,int bottom,uint8_t fill){
    const auto edge=canvas_.pixels().at(left+5,top+5);
    bevel(left+2,top+3,right,bottom,edge,fill);
}

void ClassicApplication::button(int x,int y,std::string_view label){
    const auto tl=canvas_.pixels().at(x,y),tr=canvas_.pixels().at(x+56,y);
    const auto bl=canvas_.pixels().at(x,y+11),br=canvas_.pixels().at(x+56,y+11);
    canvas_.blit(4,x,y,17,179,57,12);
    if(screen_==ClassicScreen::Stadium){
        canvas_.line(x+1,y+1,x+54,y+1,2);canvas_.line(x+1,y+2,x+1,y+9,2);
    }
    canvas_.pixels().at(x,y)=tl;canvas_.pixels().at(x+56,y)=tr;
    canvas_.pixels().at(x,y+11)=bl;canvas_.pixels().at(x+56,y+11)=br;
    canvas_.text(mini,x,y+7,label,29,1,x+59);
}

void ClassicApplication::open_contract(int slot){
    roster_rules_=false;
    const auto& h=snapshot();
    const int m=active_manager();const auto& squad=h.squad_of(m);
    if(slot<0||slot>=29||size_t(slot)>=squad.size()||squad[size_t(slot)].player<0)return;
    const size_t at=squad_block(m)+size_t(slot)*52;
    const auto& player=h.players.at(size_t(squad[size_t(slot)].player));
    if(h.state.at(at+0x0C)){notice(player.name+" ist leider von\n"+h.teams.at(size_t(player.team)).name+"\nnur ausgeliehen.");return;}
    if(h.state.at(at+0x18)&0x80){notice(player.name+" wird sich mit\nAblauf des Vertrages\nzur Ruhe setzen.");return;}
    ContractEditor editor;
    editor.slot=slot;editor.player=squad[size_t(slot)].player;editor.offset=slot<12?80:0;
    editor.years=h.state.at(at+0x0B);editor.salary=int32_t(le32(h.state,at+0x28));
    const long premium=int32_t(le32(h.state,at+0x30));
    editor.insured=editor.original_insured=premium!=0;
    BorlandRandom unused;
    editor.premium=premium?premium:player_worth(session_.contract_tables(),contract_player(h,m,slot),Worth::Salary,unused)/2;
    contract_editor_=std::move(editor);
}

void ClassicApplication::finish_numeric(bool cancel){
    auto& editor=*contract_editor_;
    const int field=editor.field;editor.field=-1;
    if(cancel||editor.digits.empty())return;
    const long value=std::stol(editor.digits);
    if(!value)return; // native empty/zero restores the previous field
    if(field==1){editor.salary=value*3;editor.salary_edited=true;return;}
    editor.years=int(value);
    editor.salary_edited=false;
    if(editor.season){  // ovr035:481B..49F2: the demand is written into the record and never goes below its salary
        BorlandRandom unused;
        const auto& h=snapshot();
        const long demand=season_change_demand(session_.contract_tables(),h,0,contract_player(h,0,editor.slot),editor.years,season_luck_,unused);
        editor.salary=editor.record_salary=std::max(demand,editor.record_salary);
        return;
    }
    // The saved formats do not include the transient native daily dialog mood.
    // Use the same neutral mood as the existing season-review quote, without
    // consuming the simulation RNG. See the frontend limitations document.
    BorlandRandom unused;
    const int league=session_.game().budget().league;
    editor.salary=renewal_demand(session_.contract_tables(),contract_player(snapshot(),0,editor.slot),
        editor.years,100,session_.game().next_matchday(league),44,session_.game().season_day()+1,unused);
}

void ClassicApplication::submit_contract(){
    if(purchase_draft_){submit_purchase();return;}
    const auto editor=*contract_editor_;
    const std::string name=snapshot().players.at(size_t(editor.player)).name;
    if(editor.years>4){contract_editor_.reset();notice("SOOOO lange\nverpflichtet\nsich kein\nEishockey-Spieler !");return;}
    try{
        const auto result=session_.apply([&](Game& game){
            const auto result=game.negotiate_contract(size_t(editor.slot),editor.years,editor.salary);
            if(result==ContractResult::Signed){
                if(editor.insured!=editor.original_insured&&!game.set_player_insurance(size_t(editor.slot),editor.insured))
                    throw FormatError("Versicherung kann derzeit nicht geändert werden.");
                return ActionResult{};
            }
            switch(result){
                case ContractResult::ShorteningNotConfirmed:return ActionResult{"So dumm\nist "+name+"\nleider nicht...",false};
                case ContractResult::Blocked:return ActionResult{name+" ist im Moment\nnicht verhandlungs-\nbereit.",false};
                case ContractResult::PlayerRefused:return ActionResult{name+" ist nicht an\nIhrem Angebot\ninteressiert.",false};
                default:return ActionResult{"Dieser Vertrag kann derzeit nicht geändert werden.",false};
            }
        });
        contract_editor_.reset();invalidate();
        if(!result.message.empty())notice(result.message);
    }catch(const std::exception& error){notice(error.what());}
}

void ClassicApplication::draw_contract_editor(){
    const auto& e=*contract_editor_;const auto& p=snapshot().players.at(size_t(e.player));const int s=e.offset;
    plain_frame(2,s+28,168,s+97,4);
    canvas_.text(flow,6,s+40,"Vertragsverhandlung",10,28,163);
    canvas_.line(6,s+43,163,s+43,7);
    canvas_.text(mini,2,s+51,p.name+", "+std::to_string(p.age)+" Jahre alt",7,0,158);
    constexpr const char* labels[]={"Vertragsdauer","Gehalt / Monat","Versicherung"};
    for(int row=0;row<3;row++){canvas_.text(mini,7,s+60+8*row,labels[row],7);canvas_.text(mini,76,s+60+8*row,":",7);}
    canvas_.text(mini,81,s+60,std::to_string(e.years)+(e.years==1?" Jahr":" Jahre"),7);
    canvas_.text(mini,81,s+68,money(e.salary),7);
    canvas_.text(mini,e.insured?79:81,s+76,e.insured?money(e.premium):"keine",7);
    button(11,s+80,"Mach' ich!");button(90,s+80,"Abbruch");
    if(e.field>=0){
        const int y=s+(e.field==0?61:69);
        canvas_.rect(79,y-5,153,y+1,4);
        canvas_.font().draw(canvas_.pixels(),mini,81,y,e.digits,1,true,4);
        const int x=83+canvas_.font().measure(mini,e.digits);canvas_.rect(x,y-4,x+2,y,1);
    }
}
} // namespace em::ui

namespace em::ui {
namespace { bool hit(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;} }

void ClassicApplication::click_contract_editor(int x,int y,bool right){
    auto& e=*contract_editor_;
    if(right)return;
    if(e.field>=0)return;
    const int s=e.offset;
    if(hit(x,y,12,s+81,67,s+91)){if(e.season)answer_season_talk(true);else submit_contract();return;}
    if(hit(x,y,91,s+81,146,s+91)){
        if(e.season){answer_season_talk(false);return;}
        if(purchase_draft_){try{cancel_purchase();}catch(const std::exception& error){notice(error.what());}}
        else contract_editor_.reset();return;
    }
    if(x>2&&x<158&&y>s+52&&y<s+78){
        const int field=int(y>s+63)+int(y>s+71);
        const size_t record=squad_block(e.season?0:active_manager())+size_t(e.slot)*52;
        const auto& h=snapshot();
        if(field==2){
            if((h.state.at(record+0x2F)&0x80)||(h.state.at(record+9)&3)==2)return;
            e.insured=!e.insured;return;
        }
        // the season change clears the wish counter for its dialog (ovr026: +0x18 = 0x65, ovl11_41db)
        if(!e.season&&h.state.at(record+0x18)!=0){notice(h.players.at(size_t(e.player)).name+" ist im Moment\nnicht verhandlungs-\nbereit.");return;}
        e.field=field;e.digits.clear();
    }
}

// ovr026 (DOS ovl02_0000 near 116086): the squad list with the club name, the symbol box and, for each player whose
// contract ran out, ovl11_41db with the one-year proposal; "Abbruch" or a refusal lets him go with two notices.
void ClassicApplication::open_season_talk(){
    const auto& q=*session_.contract_question();
    season_view_=std::make_unique<HokFile>(decode_hok(q.save));
    season_luck_=q.luck;
    season_outcomes_seen_=q.outcomes.size();
    if(q.index==0&&q.review&&!season_review_shown_){show_review(*q.review,q.review_messages);season_review_shown_=true;}
    ContractEditor editor;
    const auto& h=*season_view_;
    const size_t at=0x696D+size_t(q.slot)*52;
    editor.slot=q.slot;editor.player=h.squad_of(0).at(size_t(q.slot)).player;editor.offset=q.slot<12?80:0;
    editor.years=1;editor.salary=editor.record_salary=q.monthly_salary;editor.season=true;
    const long premium=int32_t(le32(h.state,at+0x30));
    editor.insured=editor.original_insured=premium!=0;
    BorlandRandom unused;
    editor.premium=premium?premium:player_worth(session_.contract_tables(),contract_player(h,0,q.slot),Worth::Salary,unused)/2;
    contract_editor_=std::move(editor);
}

// The notices of the talks answered since the last dialog. A refusal is shown over the dialog; the player's farewell
// and the transfer fee over the list as it was (ovl12_0bdc runs before ovl09_25d6 removes him).
void ClassicApplication::queue_season_outcomes(const std::vector<Game::ContractOutcome>& outcomes){
    for(size_t k=season_outcomes_seen_;k<outcomes.size();k++){
        const auto& o=outcomes[k];
        if(o.signed_on)continue;
        if(o.refused){pending_notices_.push_back(o.name+"\nist nicht an\nIhrem Angebot\ninteressiert.");season_editor_with_notice_=true;}
        if(o.too_long){pending_notices_.push_back("SOOOO lange\nverpflichtet\nsich kein\nEishockey-Spieler !");season_editor_with_notice_=true;}
        pending_notices_.push_back(o.name+"\nkehrt Ihrem Verein\nden R\xC3\xBC""cken...");
        pending_notices_.push_back("Sie erhalten eine\nAbl\xC3\xB6sesumme von\n"+std::to_string(o.fee/3)+" DM");
    }
    season_outcomes_seen_=outcomes.size();
    if(!pending_notices_.empty()&&notice_.empty()){notice_=pending_notices_.front();pending_notices_.erase(pending_notices_.begin());}
}

void ClassicApplication::answer_season_talk(bool sign){
    const auto e=*contract_editor_;
    // more than four years: the original says "SOOOO lange..." and lets the player go (Game::expire_contracts)
    Game::ContractTerms terms;
    terms.years=sign?e.years:0;
    terms.salary=e.salary_edited?e.salary:0;
    terms.insured=e.insured;
    try{
        pending_match_frames_.clear();
        const bool done=session_.answer_contract(terms);
        advance_first_=false;
        queue_season_outcomes(done?session_.game().contract_outcomes():session_.contract_question()->outcomes);
        if(!season_editor_with_notice_)contract_editor_.reset();
        if(!done){
            if(notice_.empty())open_season_talk();else season_pending_open_=true;  // after the notices (tick)
            return;
        }
        season_outcomes_seen_=0;
        if(notice_.empty())season_view_.reset();  // otherwise the old list stays behind the notices (draw)
        invalidate();show_manager_news();begin_match_view();invalidate();
    }catch(const std::exception& error){season_view_.reset();contract_editor_.reset();notice(error.what());}
}

void ClassicApplication::draw_season_talk(){
    const auto& h=snapshot();
    canvas_.background(22);  // ovl10_2bf6
    const int team=h.state.at(0x0DCD+0x14);
    if(team>=0&&size_t(team)<h.teams.size())canvas_.text(flow,0,10,h.teams.at(size_t(team)).name,0x1E,0,0x13F);
    canvas_.frame(0x109,0xC3,2,2);
    draw_classic_roster(canvas_,h,true,0);
    // 146e:07ff with symbol 10 of KB13: the frame at (0x90,0), the symbol 3 pixels inside it
    canvas_.blit(13,0x112,200,0x90,0,0x1E,0x1B,true,0xC0);
    canvas_.blit(13,0x115,203,(10%6)*0x18,(10/6)*0x15,0x18,0x15,true,0xC0);
    if(contract_editor_&&contract_editor_->season&&(notice_.empty()||season_editor_with_notice_))draw_contract_editor();
}

}  // namespace em::ui

namespace em::ui {
// Raster tests (tests/classic_contract_season_tests.cpp): the loaded game as the staged day of a season change.
bool ClassicApplication::render_season_talk(std::string_view name){
    const auto field=[&](size_t n){
        size_t at=0;for(size_t k=0;k<n;k++)at=name.find(':',at)+1;
        return std::string(name.substr(at,name.find(':',at)-at));
    };
    if(name.rfind("contract-season:",0)==0){
        const int slot=std::stoi(field(1));
        season_view_=std::make_unique<HokFile>(decode_hok(encode_hok(snapshot())));
        season_luck_=std::stoi(field(2));
        const auto& h=*season_view_;
        const auto& squad=h.squad_of(0);
        if(slot<0||size_t(slot)>=squad.size())throw FormatError("contract-season: no such slot");
        const ContractPlayer cp=contract_player(h,0,slot);
        BorlandRandom unused;  // as Game::expire_contracts: ovl11_3d36(record, 1) * (147 - age) / 100
        const long proposal=long(int32_t(player_worth(session_.contract_tables(),cp,Worth::Salary,unused)*(147-int(h.players.at(size_t(squad[size_t(slot)].player)).age))/100));
        ContractEditor editor;
        editor.slot=slot;editor.player=squad[size_t(slot)].player;editor.offset=slot<12?80:0;
        editor.years=1;editor.salary=editor.record_salary=proposal;editor.season=true;
        const long premium=int32_t(le32(h.state,0x696D+size_t(slot)*52+0x30));
        editor.insured=editor.original_insured=premium!=0;
        editor.premium=premium?premium:player_worth(session_.contract_tables(),cp,Worth::Salary,unused)/2;
        contract_editor_=std::move(editor);screen_=ClassicScreen::Main;
        return true;
    }
    if(name.rfind("season-notice:",0)==0){
        season_editor_with_notice_=field(1)=="1";
        if(!season_editor_with_notice_)contract_editor_.reset();
        std::string text=std::string(name.substr(name.find(':',name.find(':')+1)+1));
        for(size_t at;(at=text.find("\\n"))!=std::string::npos;)text.replace(at,2,"\n");  // a literal backslash-n
        notice_=std::move(text);
        return true;
    }
    return false;
}
}  // namespace em::ui
