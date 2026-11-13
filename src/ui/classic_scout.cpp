// SPDX-License-Identifier: GPL-3.0-only
// Talent scout of the transfer page (logic in src/game/talent.cpp): the "Talent" button asks the game's
// two-button question box, yes pays the stored price. The result arrives later as a manager message of the
// day loop. The same question box also carries the sponsor's subsidy offer after a purchase or loan.
#include "ui/classic_app.hpp"

namespace em::ui {
// Opens the talent question, unless no game is loaded or a search is already running.
void ClassicApplication::open_talent_question(){
    if(!session_.loaded()||session_.game().talent_search_days()!=0)return;
    roster_rules_=false;subsidy_offer_=0;talent_question_=true;
}
// Sponsor offer after a purchase or loan: the same question box and buttons as the talent question, text
// "Nehmen Sie einen Sponsor-" / "zuschu~ von" / amount + " DM an ?". raw is the amount in internal units.
void ClassicApplication::open_subsidy_question(long raw){
    if(raw<=0)return;
    roster_rules_=false;subsidy_offer_=raw;talent_question_=true;
}
// Answer of the question box: for a subsidy offer, yes books the subsidy (and the saved backdrop is dropped
// either way); for the talent question, yes starts the search or reports missing money.
void ClassicApplication::answer_talent_question(bool yes){
    talent_question_=false;
    if(subsidy_offer_){
        const long raw=subsidy_offer_;subsidy_offer_=0;purchase_backdrop_.reset();backdrop_budget_.reset();invalidate();
        if(yes)action([raw](Game& game){if(!game.accept_purchase_subsidy(raw))throw FormatError("Ungültiger Sponsorzuschuss.");return ActionResult{};});
        return;
    }
    if(!yes)return;
    try{
        TalentResult result=TalentResult::Unavailable;
        session_.apply([&](Game& game){result=game.start_talent_search();return ActionResult{{},result==TalentResult::Started};});
        invalidate();
        if(result==TalentResult::InsufficientFunds)notice("Sie haben nicht\ngenug Geld !");
    }catch(const std::exception& e){notice(e.what());}
}
// The three text lines of the question box (subsidy offer or talent search with its price).
void ClassicApplication::draw_talent_question(){
    constexpr auto mini=ClassicFontId::Mini;
    if(subsidy_offer_){
        canvas_.text_native(mini,105,141,"Nehmen Sie einen Sponsor-",7,0,210);
        canvas_.text_native(mini,105,149,"zuschu~ von",7,0,210);
        canvas_.text_native(mini,105,157,money(subsidy_offer_)+" an ?",7,0,210);
        return;
    }
    canvas_.text_native(mini,105,141,"M|chten Sie, da~ Ihr 'Spion'",7,0,210);
    canvas_.text_native(mini,105,149,"f}r Sie auf Talentsuche geht?",7,0,210);
    canvas_.text_native(mini,105,157,"Preis: "+money(session_.game().talent_search_price()),7,0,210);
}
void ClassicApplication::show_manager_news(){
    // The talent result is a manager message entry; the main menu opens the message field for it
    // (check_new_messages, classic_career.cpp) instead of a dialog. Season-review messages appear at once.
    show_immediate_messages();
}
} // namespace em::ui
