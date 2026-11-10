// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
namespace em::ui {
void ClassicApplication::open_advertising(){
    action([](Game& game){game.enter_advertising();return ActionResult{};});
    screen_=ClassicScreen::Advertising;advertising_selection_={};advertising_info_=false;advert_renewal_refused_=false;advert_renewal_.reset();
}
void ClassicApplication::draw_advertising(){
    draw_classic_advertising(canvas_,snapshot(),session_.finance_tables(),advertising_selection_,active_manager());
    if(advertising_info_)draw_classic_advertising_info(canvas_,classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager()));
}
void ClassicApplication::update_advertising_hover(){
    if(screen_!=ClassicScreen::Advertising||advertising_selection_.browsingOffers||advertising_info_||advert_renewal_||!notice_.empty())return;
    const auto view=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager());
    const auto hit=classic_advertising_hit(view,mouse_x_,mouse_y_);
    if(hit.action==ClassicAdvertisingAction::Surface){advertising_selection_.contractSlot=hit.slot;advertising_selection_.clearEmpty=false;}
    else if(advertising_selection_.contractSlot>=0){advertising_selection_.contractSlot=-1;advertising_selection_.clearEmpty=true;}
}
void ClassicApplication::resolve_advert_renewal(bool accept){
    if(!advert_renewal_)return;
    const auto draft=*advert_renewal_;advert_renewal_.reset();advert_renewal_refused_=false;
    action([&](Game& game){game.finish_advert_renewal(draft,accept);return ActionResult{};});
}
void ClassicApplication::click_advertising(int x,int y,bool right){
    if(advertising_info_){if(!right)advertising_info_=false;return;}
    if(advert_renewal_){
        if(!right&&y>=167&&y<=177&&((x>=96&&x<=151)||(x>=166&&x<=221)))resolve_advert_renewal(x<160);
        return;
    }
    if(right){
        if(advertising_selection_.browsingOffers){advertising_selection_.browsingOffers=false;advertising_selection_.contractSlot=-1;advertising_selection_.clearEmpty=true;update_advertising_hover();}
        else screen_=ClassicScreen::Main;
        return;
    }
    const auto view=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager());
    const auto hit=classic_advertising_hit(view,x,y);
    if(!hit.enabled){
        if(hit.action==ClassicAdvertisingAction::Surface)advertising_selection_={hit.slot,0,false};
        return;
    }
    if(hit.action==ClassicAdvertisingAction::Surface){
        if(advertising_selection_.browsingOffers&&advertising_selection_.contractSlot==hit.slot)return;
        advertising_selection_.contractSlot=hit.slot;advertising_selection_.browsingOffers=false;advertising_selection_.offerIndex=0;
        const auto selected=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager());
        if(selected.renewalPending){
            AdvertRenewalDraft draft;bool prepared=false;
            action([&](Game& game){prepared=game.begin_advert_renewal(size_t(hit.slot),draft);return ActionResult{};});
            if(prepared){
                advert_renewal_=draft;advert_renewal_name_=selected.nativeSponsor;
                if(!draft.sponsor_stays){advert_renewal_refused_=true;notice("Die Firma\n"+iso646de_to_utf8(advert_renewal_name_.c_str())+"\nwird den Vertrag\nnicht verlängern.");}
            }
        }else{
            advertising_selection_.browsingOffers=true;advertising_selection_.offerIndex=-1;
            advertising_selection_=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager()).selection;
        }
    }else if(hit.action==ClassicAdvertisingAction::PreviousOffer)--advertising_selection_.offerIndex;
    else if(hit.action==ClassicAdvertisingAction::NextOffer)++advertising_selection_.offerIndex;
    else if(hit.action==ClassicAdvertisingAction::Info)advertising_info_=true;
    else if(hit.action==ClassicAdvertisingAction::Accept){
        bool accepted=false;
        action([&](Game& game){accepted=game.accept_sponsor_offer(view.kind,size_t(view.selection.offerIndex));return ActionResult{{},accepted};});
        if(!accepted)return;
        const auto next=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager());
        if(advertising_selection_.contractSlot>=7||next.freeBoards<1){
            advertising_selection_={-1,0,false,true};update_advertising_hover();
        }else{
            advertising_selection_.offerIndex=-1;
            advertising_selection_=classic_advertising_view(snapshot(),session_.finance_tables(),advertising_selection_,active_manager()).selection;
        }
    }
}
} //namespace em::ui
