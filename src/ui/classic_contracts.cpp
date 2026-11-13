// SPDX-License-Identifier: GPL-3.0-only
// Classic-mode contract negotiation: the contract dialog for renewing a
// player of the own squad during the season, and the season-change talks
// in which every player whose contract ran out is offered a one-year
// proposal. Also the shared plain frame and text-button helpers.
//
// Squad records are 52 bytes each (see squad_block()); fields used here:
// +0x0B contract years, +0x0C loan flag (non-zero = only borrowed), +0x18
// negotiation-wish counter (bit 7 = retires when the contract ends), +0x28
// monthly salary (i32), +0x30 insurance premium (i32, 0 = not insured).
// The insurance cannot be toggled when bit 7 of +0x2F is set or bits 0..1
// of +0x09 equal 2.
#include "ui/classic_app.hpp"
#include "ui/classic_roster.hpp"
#include <algorithm>

namespace em::ui {
namespace { constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow; }

// Bevelled panel whose edge colour is taken from the pixel already on
// screen at (left+5, top+5), filled with fill.
void ClassicApplication::plain_frame(int left,int top,int right,int bottom,uint8_t fill){
    const auto edge=canvas_.pixels().at(left+5,top+5);
    bevel(left+2,top+3,right,bottom,edge,fill);
}

// 57x12 text button from the KB/4 atlas at (x,y), keeping the four corner
// pixels of the background. On the stadium screen the game additionally
// draws the highlight edge in colour 2.
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

// Opens the contract dialog for squad slot (0..28) of the active manager.
// Borrowed players and players who announced their retirement only get a
// notice. The dialog starts with the current years and salary; the
// insurance premium shown is the stored one, or half the player's salary
// worth if he is not insured. The dialog sits lower (offset 80) for the
// first twelve slots so it does not hide the selected row.
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

// Ends numeric entry into the years (field 0) or salary (field 1) of the
// contract dialog. The salary is typed in DM and stored as value*3 (the
// game's internal money unit). Entering a number of years recomputes the
// player's salary demand for that duration.
void ClassicApplication::finish_numeric(bool cancel){
    auto& editor=*contract_editor_;
    const int field=editor.field;editor.field=-1;
    if(cancel||editor.digits.empty())return;
    const long value=std::stol(editor.digits);
    if(!value)return; // as in the game, an empty or zero entry keeps the previous value
    if(field==1){editor.salary=value*3;editor.salary_edited=true;return;}
    editor.years=int(value);
    editor.salary_edited=false;
    if(editor.season){  // season change: the demand is written into the record and never goes below its salary
        BorlandRandom unused;
        const auto& h=snapshot();
        const long demand=season_change_demand(session_.contract_tables(),h,season_manager(),contract_player(h,season_manager(),editor.slot),editor.years,season_luck_,unused);
        editor.salary=editor.record_salary=std::max(demand,editor.record_salary);
        return;
    }
    // The game's transient daily negotiation mood is not part of any saved
    // format. Use the same neutral mood (100) as the season-review quote,
    // with a throw-away random generator so the simulation's random
    // sequence is not consumed.
    BorlandRandom unused;
    const int league=session_.game().budget().league;
    editor.salary=renewal_demand(session_.contract_tables(),contract_player(snapshot(),active_manager(),editor.slot),
        editor.years,100,session_.game().next_matchday(league),44,session_.game().season_day()+1,unused);
}

// "Mach' ich!" in the in-season contract dialog (or the purchase dialog
// that shares it): more than four years is refused outright; otherwise the
// game negotiates and, on success, applies a changed insurance choice.
// The outcome is shown as a notice.
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

// Draws the contract dialog (duration, monthly salary, insurance and the
// two buttons). While a number is being typed, the field shows the digits
// with a block cursor.
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

// Left click in the contract dialog: the two buttons, or one of the three
// rows (years / salary start numeric entry, insurance toggles unless the
// record forbids it). In season the buttons answer the season talk.
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
        const size_t record=squad_block(e.season?season_manager():active_manager())+size_t(e.slot)*52;
        const auto& h=snapshot();
        if(field==2){
            if((h.state.at(record+0x2F)&0x80)||(h.state.at(record+9)&3)==2)return;
            e.insured=!e.insured;return;
        }
        // a non-zero wish counter blocks talks during the season; the
        // season change clears it for its own dialog
        if(!e.season&&h.state.at(record+0x18)!=0){notice(h.players.at(size_t(e.player)).name+" ist im Moment\nnicht verhandlungs-\nbereit.");return;}
        e.field=field;e.digits.clear();
    }
}

// Season change: the squad list with the club name, the symbol box and, for
// each player whose contract ran out, the contract dialog with the
// one-year proposal; "Abbruch" or a refusal lets him go with two notices.
// Opens the dialog for the game's pending contract question, working on a
// copy of the save state as it was when the question was asked.
void ClassicApplication::open_season_talk(){
    const auto& q=*session_.contract_question();
    season_view_=std::make_unique<HokFile>(decode_hok(q.save));
    season_luck_=q.luck;
    season_outcomes_seen_=q.outcomes.size();
    // the season-review pages of all managers come first, then the talks of manager 0..N-1 with that manager at the seat
    bool any_review=false;for(const auto& r:q.reviews)any_review|=r.has_value();
    if(q.index==0&&any_review&&!season_review_shown_){show_reviews(q.reviews,q.review_messages);season_review_shown_=true;}
    else if(session_.game().managers()>1&&!(career_modal()=="review"||career_modal()=="review-overview"))session_.select_manager(q.manager);
    const int m=q.manager;
    ContractEditor editor;
    const auto& h=*season_view_;
    const size_t at=squad_block(m)+size_t(q.slot)*52;
    editor.slot=q.slot;editor.player=h.squad_of(m).at(size_t(q.slot)).player;editor.offset=q.slot<12?80:0;
    editor.years=1;editor.salary=editor.record_salary=q.monthly_salary;editor.season=true;
    const long premium=int32_t(le32(h.state,at+0x30));
    editor.insured=editor.original_insured=premium!=0;
    BorlandRandom unused;
    editor.premium=premium?premium:player_worth(session_.contract_tables(),contract_player(h,m,q.slot),Worth::Salary,unused)/2;
    contract_editor_=std::move(editor);
}

// The notices of the talks answered since the last dialog. A refusal is shown over the dialog; the player's farewell
// and the transfer fee over the list as it was (the game shows them before it removes the player from the squad).
// The fee is shown in DM (internal money / 3).
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

// Answers the pending season talk: sign=true offers the dialog's years
// (and the edited salary, if any), false lets the player go. Then either
// opens the next talk (after any notices) or, when all talks are done,
// returns to manager 0 and continues with the news and the match view.
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
        if(session_.game().managers()>1)session_.select_manager(0);  // the talks are over: manager 0 at the next menu
        if(notice_.empty())season_view_.reset();  // otherwise the old list stays behind the notices (draw)
        invalidate();show_manager_news();begin_match_view();invalidate();
    }catch(const std::exception& error){season_view_.reset();contract_editor_.reset();notice(error.what());}
}

// Draws the season-change talk screen: ice background, club name, the
// squad list, the symbol box at the bottom right and the dialog on top.
void ClassicApplication::draw_season_talk(){
    const auto& h=snapshot();
    canvas_.background(22);
    const int m=session_.contract_question()?session_.contract_question()->manager:active_manager();
    const int team=h.state.at(manager_block(m)+0x14);
    if(team>=0&&size_t(team)<h.teams.size())canvas_.text(flow,0,10,h.teams.at(size_t(team)).name,0x1E,0,0x13F);
    canvas_.frame(0x109,0xC3,2,2);
    draw_classic_roster(canvas_,h,true,m);
    // symbol 10 of the KB/13 atlas (6 symbols of 24x21 per row) inside its
    // frame taken from (0x90,0) of the same atlas, 3 pixels in
    canvas_.blit(13,0x112,200,0x90,0,0x1E,0x1B,true,0xC0);
    canvas_.blit(13,0x115,203,(10%6)*0x18,(10/6)*0x15,0x18,0x15,true,0xC0);
    if(contract_editor_&&contract_editor_->season&&(notice_.empty()||season_editor_with_notice_))draw_contract_editor();
}

}  // namespace em::ui

namespace em::ui {
// Test hook for the raster tests: stages the loaded game as a day of the
// season change. "contract-season:<slot>:<luck>" opens manager 0's talk
// for that slot with the proposal computed like Game::expire_contracts;
// "season-notice:<0|1>:<text>" shows a notice (1 = over the open dialog;
// "\n" in the text is a literal backslash-n). Returns false for other names.
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
        BorlandRandom unused;  // as Game::expire_contracts: salary worth * (147 - age) / 100
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
