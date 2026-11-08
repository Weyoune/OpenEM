// SPDX-License-Identifier: GPL-3.0-only
// Talent scout of the transfer page (ovr035:08D5..0A65, logic in src/game/talent.cpp): the
// "Talent" button asks the native question box (24D6A with two buttons), yes pays the stored price.
// The result arrives later as a manager message of the day loop (seg002:0AC5).
#include "ui/classic_app.hpp"

namespace em::ui {
void ClassicApplication::open_talent_question(){
    if(!session_.loaded()||session_.game().talent_search_days()!=0)return;
    roster_rules_=false;talent_question_=true;
}
void ClassicApplication::answer_talent_question(bool yes){
    talent_question_=false;
    if(!yes)return;
    try{
        TalentResult result=TalentResult::Unavailable;
        session_.apply([&](Game& game){result=game.start_talent_search();return ActionResult{{},result==TalentResult::Started};});
        invalidate();
        if(result==TalentResult::InsufficientFunds)notice("Sie haben nicht\ngenug Geld !");
    }catch(const std::exception& e){notice(e.what());}
}
void ClassicApplication::draw_talent_question(){
    constexpr auto mini=ClassicFontId::Mini;
    canvas_.text_native(mini,105,141,"M|chten Sie, da~ Ihr 'Spion'",7,0,210);
    canvas_.text_native(mini,105,149,"f}r Sie auf Talentsuche geht?",7,0,210);
    canvas_.text_native(mini,105,157,"Preis: "+money(session_.game().talent_search_price()),7,0,210);
}
void ClassicApplication::show_manager_news(){
    // The talent result is a message entry (ovr036:01CA); the main menu opens the message field for it
    // (check_new_messages, classic_career.cpp) instead of a dialog. Season-review messages appear at once.
    show_immediate_messages();
}
} // namespace em::ui
