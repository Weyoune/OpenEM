// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
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
    if(field==1){editor.salary=value*3;return;}
    editor.years=int(value);
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
