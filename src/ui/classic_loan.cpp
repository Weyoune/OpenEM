// SPDX-License-Identifier: GPL-3.0-only
// Loan negotiation of the transfer page (the game logic is in src/game/loan.cpp).
// The click on the "(L)" price (x > 250) of a computer list player opens the same fee editor as a
// purchase, titled "NAME leihen". There is no contract dialog: a loan always runs for one season.
#include "ui/classic_app.hpp"

namespace em::ui {
// Opens the loan fee editor for transfer-list slot (52-byte records at
// 0x81CD; +0x0F player id, +0x1A refusal flags: bit n = manager n was
// refused, bit 7 = refusal is final). A final refusal shows a notice; a
// manager bit alone on a player of the computer pool (global record
// +0x1A == 4) leaves the click without reaction, as in the game.
void ClassicApplication::open_loan(int slot){
    const auto& h=snapshot();
    const auto flags=h.state.at(0x81CD+size_t(slot)*52+0x1A);
    const int bit=1<<active_manager();  // the refusal bit of the manager (game/purchase.cpp)
    if((flags&0x80)&&(flags&bit)){notice("Angebot wurde\nbereits abgelehnt.");return;}
    if((flags&bit)&&h.state.at(0x4FC6+size_t(h.state.at(0x81CD+size_t(slot)*52+15)-1)*33+0x1A)==4)return; // the manager bit alone leaves the click without reaction
    roster_rules_=false;
    fee_editor_=FeeEditor{slot,{},true};
}
// Offers fee (internal money) for the loan. On success a sponsor subsidy
// question may follow; refusals and errors are shown as notices.
void ClassicApplication::finish_loan(int slot,long fee){
    try{
        // The sponsor question is drawn over the page as it was before the loan: the original
        // redraws the lists only after the question.
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
