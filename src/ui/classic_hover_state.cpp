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
    if(!kind){hover_kind_.reset();roster_hover_={};roster_rules_=false;transfer_hover_={};pool_rules_=pool_help_visible_=false;pool_trail_=0;return;}
    hover_kind_=kind;
    roster_hover_=classic_roster_hover(snapshot(),squad_manager(),*kind,mouse_x_,mouse_y_,roster_help_);
    roster_rules_=roster_hover_.row>=0;
    if(*kind==ClassicRosterKind::Transfer){
        const auto pool=classic_transfer_hover(snapshot(),active_manager(),mouse_x_,mouse_y_,transfer_help_);
        pool_rules_=pool.row>=0;
        if(pool.row>=0){transfer_hover_=pool;pool_help_visible_=true;pool_trail_|=uint16_t(1u<<pool.row);}
        else if(roster_hover_.row>=0)pool_help_visible_=false;
    }else{transfer_hover_={};pool_rules_=pool_help_visible_=false;}
}
void ClassicApplication::draw_roster_hover(){
    const auto kind=roster_kind();
    if(kind&&hover_kind_==kind)draw_classic_roster_hover(canvas_,*kind,roster_hover_,roster_rules_);
    // Leaving a pool row repaints both of its rules in the separator colour 31 over 180..314, one pixel wider on
    // each side than the drawn separators (DOSBox loan-fee.rgb): the rows the mouse crossed keep that trace
    // until the lists are redrawn.
    if(kind==ClassicRosterKind::Transfer)
        for(int row=0;row<12;row++)
            if((pool_trail_>>row)&1){canvas_.line(180,36+16*row,314,36+16*row,31);canvas_.line(180,52+16*row,314,52+16*row,31);}
    if(kind==ClassicRosterKind::Transfer&&pool_help_visible_)draw_classic_transfer_hover(canvas_,transfer_hover_,pool_rules_);
}
} // namespace em::ui
