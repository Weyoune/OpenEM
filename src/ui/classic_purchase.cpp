// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"

namespace em::ui {
void ClassicApplication::open_purchase(int slot){
    const auto& h=snapshot();const size_t record=0x81CD+size_t(slot)*52;
    const auto flags=h.state.at(record+0x1A);
    const int bit=1<<active_manager(),owner=h.state.at(0x4FC6+size_t(h.state.at(record+15)-1)*33+0x1A);
    if((flags&0x80)&&(flags&bit)){notice("Angebot wurde\nbereits abgelehnt.");return;}
    // ovr035:1161..1314..14E8: the manager bit without 0x80 on a computer player ends the click without
    // editor or message (every write sets the bit together with 0x80, ovr035:1303/1ADD, so this needs a
    // hand-made save).
    if((flags&bit)&&owner==4)return;
    roster_rules_=false;
    fee_editor_=FeeEditor{slot,{}};
}
void ClassicApplication::draw_fee_editor(){
    const auto& editor=*fee_editor_;const auto& h=snapshot();
    const auto roster=hok_squad(h,4);const auto& player=h.players.at(size_t(roster.at(size_t(editor.pool_slot)).player));
    plain_frame(2,28,168,73,4);
    canvas_.text(ClassicFontId::Flow,6,40,player.name+(editor.loan?" leihen":" kaufen"),10,28,163);
    canvas_.line(6,43,163,43,7);
    canvas_.text(ClassicFontId::Flow,11,54,"Ihr Angebot:",7);
    canvas_.text(ClassicFontId::Flow,11,66,editor.digits,1);
    const int x=13+canvas_.font().measure(ClassicFontId::Flow,editor.digits);
    canvas_.rect(x,62,x+2,66,1);
}
void ClassicApplication::finish_fee(bool cancel){
    const auto editor=*fee_editor_;fee_editor_.reset();
    if(cancel||editor.digits.empty())return;
    const long fee=std::stol(editor.digits)*3;
    if(!fee)return;
    if(bid_to_manager(editor.pool_slot,fee,editor.loan))return;  // the list player of another manager
    if(editor.loan){finish_loan(editor.pool_slot,fee);return;}
    try{
        auto backing=std::make_unique<HokFile>(clone_hok(snapshot()));
        PurchaseDraft draft;PurchaseResult result;
        session_.apply([&](Game& game){result=game.begin_transfer_purchase(size_t(editor.pool_slot),fee,draft);return ActionResult{{},result==PurchaseResult::Negotiating};});
        invalidate();
        if(result==PurchaseResult::Negotiating){
            purchase_draft_=draft;purchase_backdrop_=std::move(backing);
            open_contract(draft.club_slot);
            if(contract_editor_)contract_editor_->offset=0;  // ovr035:185D: ovl11_41db(record, slot, 0), the panel at the top
            if(!contract_editor_)cancel_purchase();
            return;
        }
        switch(result){
            case PurchaseResult::SellerRefused:notice("Ihr Angebot\nwurde abgelehnt.");break;
            case PurchaseResult::InsufficientFunds:notice("Sie haben nicht\ngenug Geld !");break;
            case PurchaseResult::SquadFull:notice("Schon 29 Mann\nim Team.");break;
            case PurchaseResult::Blocked:notice("Angebot wurde\nbereits abgelehnt.");break;
            default:notice("Dieser Transfer ist derzeit nicht möglich.");break;
        }
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::cancel_purchase(bool feedback){
    if(!purchase_draft_){contract_editor_.reset();return;}
    const auto draft=*purchase_draft_;
    session_.apply([&](Game& game){if(!game.cancel_transfer_purchase(draft))throw FormatError("Die Verhandlung ist nicht mehr gültig.");return ActionResult{};});
    contract_editor_.reset();purchase_draft_.reset();purchase_backdrop_.reset();invalidate();
    if(feedback)notice("Der Transfer\nfindet nicht statt.");
}
void ClassicApplication::submit_purchase(){
    const auto editor=*contract_editor_;const auto draft=*purchase_draft_;
    try{
        PurchaseResult result;long subsidy=0;const ManagerBudget budget=session_.game().budget();
        session_.apply([&](Game& game){
            result=game.finish_transfer_purchase(draft,editor.years,editor.salary,false,false);
            if(result==PurchaseResult::Purchased)subsidy=game.last_purchase_subsidy();
            if(result==PurchaseResult::Purchased&&editor.insured&&!game.set_player_insurance(size_t(draft.club_slot),true))
                throw FormatError("Versicherung kann derzeit nicht geändert werden.");
            return ActionResult{{},result==PurchaseResult::Purchased};
        });
        if(result==PurchaseResult::Invalid){notice("Diese Vertragswerte sind ungültig.");return;}
        contract_editor_.reset();purchase_draft_.reset();invalidate();
        // As after a loan, the sponsor question keeps the page from before the purchase (not checked in DOSBox).
        if(result==PurchaseResult::Purchased&&subsidy>0){backdrop_budget_=budget;open_subsidy_question(subsidy);}
        else purchase_backdrop_.reset();
        if(result!=PurchaseResult::Purchased){
            if(editor.years>4){notice("SOOOO lange\nverpflichtet\nsich kein\nEishockey-Spieler !");pending_notices_.push_back("Der Transfer\nfindet nicht statt.");}
            else notice("Der Transfer\nfindet nicht statt.");
        }
    }catch(const std::exception& e){notice(e.what());}
}
} // namespace em::ui
