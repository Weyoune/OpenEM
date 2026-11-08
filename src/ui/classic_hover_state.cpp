// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"

namespace em::ui {
std::optional<ClassicRosterKind> ClassicApplication::roster_kind() const{
    switch(screen_){
        case ClassicScreen::Contracts:return ClassicRosterKind::Contracts;
        case ClassicScreen::Lineup:return ClassicRosterKind::Lineup;
        case ClassicScreen::Transfer:return ClassicRosterKind::Transfer;
        default:return std::nullopt;
    }
}
void ClassicApplication::update_roster_hover(){
    if(!session_.loaded()||player_detail_||contract_editor_||fee_editor_||sale_quote_||!notice_.empty()||quit_question_||talent_question_||file_preview_)return;
    const auto kind=roster_kind();
    if(!kind){hover_kind_.reset();roster_hover_={};roster_rules_=false;transfer_hover_={};pool_rules_=pool_help_visible_=false;return;}
    hover_kind_=kind;
    roster_hover_=classic_roster_hover(snapshot(),0,*kind,mouse_x_,mouse_y_,roster_help_);
    roster_rules_=roster_hover_.row>=0;
    if(*kind==ClassicRosterKind::Transfer){
        const auto pool=classic_transfer_hover(snapshot(),0,mouse_x_,mouse_y_,transfer_help_);
        pool_rules_=pool.row>=0;
        if(pool.row>=0){transfer_hover_=pool;pool_help_visible_=true;}
        else if(roster_hover_.row>=0)pool_help_visible_=false;
    }else{transfer_hover_={};pool_rules_=pool_help_visible_=false;}
}
void ClassicApplication::draw_roster_hover(){
    const auto kind=roster_kind();
    if(kind&&hover_kind_==kind)draw_classic_roster_hover(canvas_,*kind,roster_hover_,roster_rules_);
    if(kind==ClassicRosterKind::Transfer&&pool_help_visible_)draw_classic_transfer_hover(canvas_,transfer_hover_,pool_rules_);
}
} // namespace em::ui
