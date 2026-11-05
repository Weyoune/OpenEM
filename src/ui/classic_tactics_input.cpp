// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"

namespace em::ui {
namespace { bool hit(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;} }

void ClassicApplication::open_tactics(){
    tactics_plan_=load_tactics(snapshot());tactics_view_.reset();screen_=ClassicScreen::Tactics;
}
void ClassicApplication::finish_tactics(ClassicScreen target){
    if(tactics_plan_){
        const auto plan=*tactics_plan_;
        action([&](Game& game){const bool ok=game.set_tactics(plan);return ActionResult{ok?"":"Taktik ist in dieser Phase nicht verfügbar.",ok};});
        tactics_plan_.reset();tactics_view_.reset();
    }
    selected_line_=0;screen_=target;
    if(target==ClassicScreen::Lineup)open_lineup();
}
void ClassicApplication::click_tactics(int x,int y,bool right){
    if(right||y<14){finish_tactics(ClassicScreen::Main);return;}
    if(x>245&&y>224){finish_tactics(ClassicScreen::Lineup);return;}
    if(!tactics_plan_)return;
    auto& plan=*tactics_plan_;
    for(int preset=0;preset<4;preset++)if(hit(x,y,182+25*preset,35,205+25*preset,49)){
        if(select_preset(plan,preset))tactics_view_.reset();return;
    }
    constexpr int mode_x[]={115,114,200,296},mode_y[]={43,117,81,81};
    for(int control=0;control<4;control++)if(hit(x,y,mode_x[control],mode_y[control],mode_x[control]+9,mode_y[control]+17)){
        if(edit_mode(plan,control,y<=mode_y[control]+8?-1:1))tactics_view_.reset();return;
    }
    for(int arrow=4;arrow<24;arrow++){
        const bool quota=arrow<12;
        const int row=(arrow-(quota?4:12))/2,kind=arrow%2;
        const int ax=(quota?38:177)+(quota?76:119)*kind,ay=(quota?146:103)+16*row;
        if(hit(x,y,ax,ay,ax+9,ay+15)){
            if(edit_numeric(plan,arrow,y<=ay+6?1:-1))tactics_view_.reset();return;
        }
    }
}
} // namespace em::ui
