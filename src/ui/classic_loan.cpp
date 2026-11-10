// SPDX-License-Identifier: GPL-3.0-only
// Loan negotiation of the transfer page (ovr035:1118 with the loan flag, logic in src/game/loan.cpp).
// The click on the "(L)" price (x > 250) of a computer list player opens the same fee editor as a
// purchase, titled "NAME leihen". There is no contract dialog: a loan always runs for one season.
#include "ui/classic_app.hpp"

namespace em::ui {
void ClassicApplication::open_loan(int slot){
    const auto& h=snapshot();
    const auto flags=h.state.at(0x81CD+size_t(slot)*52+0x1A);
    if((flags&0x81)==0x81){notice("Angebot wurde\nbereits abgelehnt.");return;}
    if(flags&1)return; // ovr035:1161: the manager bit alone leaves the click without reaction
    roster_rules_=false;
    fee_editor_=FeeEditor{slot,{},true};
}
void ClassicApplication::finish_loan(int slot,long fee){
    try{
        // The sponsor question (seg001:223E) is drawn over the page as it was before the loan: the original
        // redraws the lists only after the question (DOSBox: sponsor-question.rgb).
        auto backing=std::make_unique<HokFile>(clone_hok(snapshot()));const ManagerBudget budget=session_.game().budget();
        const uint16_t trail=pool_trail_;
        PurchaseResult result=PurchaseResult::Invalid;long subsidy=0;
        session_.apply([&](Game& game){
            result=game.loan_transfer_player(size_t(slot),fee,false,false);
            if(result==PurchaseResult::Purchased)subsidy=game.last_purchase_subsidy();
            return ActionResult{{},result==PurchaseResult::Purchased};
        });
        invalidate();
        switch(result){
            case PurchaseResult::Purchased:
                if(subsidy>0){purchase_backdrop_=std::move(backing);backdrop_budget_=budget;pool_trail_=trail;open_subsidy_question(subsidy);}
                break;
            case PurchaseResult::SellerRefused:notice("Ihr Angebot\nwurde abgelehnt.");break;
            case PurchaseResult::InsufficientFunds:notice("Sie haben nicht\ngenug Geld !");break;
            case PurchaseResult::SquadFull:notice("Schon 29 Mann\nim Team.");break;
            case PurchaseResult::Blocked:notice("Angebot wurde\nbereits abgelehnt.");break;
            default:break;
        }
    }catch(const std::exception& e){notice(e.what());}
}
} // namespace em::ui
