// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
namespace em::ui {
void ClassicApplication::open_stadium(){
    canvas_.set_button_edge(2); //native0922..095C patches the persistent widget atlas
    screen_=ClassicScreen::Stadium;stadium_selection_={};stadium_next_value_width_=2;stadium_build_.reset();update_stadium_hover();
}
void ClassicApplication::draw_stadium(){
    draw_classic_stadium(canvas_,snapshot(),session_.finance_tables(),session_.game().stadium_tables(),stadium_selection_,active_manager());
}
void ClassicApplication::update_stadium_hover(){
    if(screen_!=ClassicScreen::Stadium||stadium_selection_.quoteItem>=0||stadium_build_||!notice_.empty())return;
    const auto view=classic_stadium_view(snapshot(),session_.finance_tables(),session_.game().stadium_tables(),stadium_selection_,active_manager());
    const auto hit=classic_stadium_hit(view,mouse_x_,mouse_y_);
    stadium_selection_.hover=hit.action==ClassicStadiumAction::Item?hit.item:-1;
}
void ClassicApplication::resolve_stadium_build(bool accept){
    if(!stadium_build_)return;
    const auto draft=*stadium_build_;
    action([&](Game& game){game.finish_stadium_build(draft,accept);return ActionResult{};});
    //645B1156 leaves DS0374=1. A Yes calls04BF immediately with that
    //width, then its Konto formatter restores2. No leaves old pixels intact.
    if(accept){stadium_selection_.numberWidth=stadium_next_value_width_;stadium_next_value_width_=2;}
    stadium_build_.reset();stadium_selection_.quoteItem=-1;stadium_selection_.target=-1;update_stadium_hover();
}
void ClassicApplication::click_stadium(int x,int y,bool right){
    if(stadium_build_){
        if(!right&&y>=167&&y<=177&&((x>=96&&x<=151)||(x>=166&&x<=221)))resolve_stadium_build(x<160);
        return;
    }
    const auto view=classic_stadium_view(snapshot(),session_.finance_tables(),session_.game().stadium_tables(),stadium_selection_,active_manager());
    const auto hit=classic_stadium_hit(view,x,y,right);
    switch(hit.action){
    case ClassicStadiumAction::Back:canvas_.set_button_edge(3);screen_=ClassicScreen::Main;break;
    case ClassicStadiumAction::TogglePlanned:
        stadium_selection_.planned=!stadium_selection_.planned;stadium_selection_.numberWidth=stadium_next_value_width_;stadium_next_value_width_=2;update_stadium_hover();break;
    case ClassicStadiumAction::TicketIncrease:case ClassicStadiumAction::TicketDecrease:
        if(hit.enabled)action([&](Game& game){game.set_ticket_price(view.ticket+(hit.action==ClassicStadiumAction::TicketIncrease?1:-1));return ActionResult{};});
        break;
    case ClassicStadiumAction::Item:{
        if(session_.game().stadium_refusals()[0][size_t(hit.item)]){timed_native_notice("Die Baufirma\nist noch beleidigt...");break;}
        const auto quote=stadium_target(snapshot(),session_.game().stadium_tables(),hit.item,-1);
        if(!quote.available){canvas_.set_button_edge(3);screen_=ClassicScreen::Main;break;}
        stadium_selection_.hover=stadium_selection_.quoteItem=hit.item;stadium_selection_.target=-1;break;
    }
    case ClassicStadiumAction::TargetIncrease:case ClassicStadiumAction::TargetDecrease:
        stadium_selection_.target=classic_stadium_target_step(snapshot(),session_.game().stadium_tables(),hit.item,
            view.quote->target,hit.action==ClassicStadiumAction::TargetIncrease?-1:1,active_manager());break;
    case ClassicStadiumAction::QuoteConfirm:{
        StadiumBuildDraft draft;StadiumBuildResult result=StadiumBuildResult::Invalid;
        action([&](Game& game){result=game.begin_stadium_build(hit.item,stadium_selection_.target,draft);return ActionResult{};});
        if(result==StadiumBuildResult::Prepared){stadium_build_=draft;stadium_next_value_width_=1;}
        else{stadium_selection_.quoteItem=-1;stadium_selection_.target=-1;update_stadium_hover();}
        break;
    }
    case ClassicStadiumAction::QuoteCancel:stadium_selection_.quoteItem=-1;stadium_selection_.target=-1;update_stadium_hover();break;
    default:break;
    }
}
} // namespace em::ui
