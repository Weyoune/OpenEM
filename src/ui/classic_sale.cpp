// SPDX-License-Identifier: GPL-3.0-only
// Purchase offer of another club for one of the manager's players ("ANGEBOT von ..."): opening the offer box,
// drawing it and the two answers "Behalten" / "Verkaufen". The quote and the sale itself are computed by Game
// (src/game/sale_quote.cpp).
#include "ui/classic_app.hpp"

namespace em::ui {
// Asks Game for an offer on squad slot slot of squad block; native_si is passed through unchanged to
// Game::begin_sale_offer. Opens the box only if Game produced a quote.
void ClassicApplication::open_sale_offer(int block,int slot,int16_t native_si){
    try{
        SaleQuote quote;bool opened=false;
        session_.apply([&](Game& game){opened=game.begin_sale_offer(block,slot,native_si,quote);return ActionResult{{},opened};});
        if(opened){sale_quote_=quote;invalidate();}
    }catch(const std::exception& e){notice(e.what());}
}
// Answers the open offer (sell = "Verkaufen") and closes the box.
void ClassicApplication::finish_sale_offer(bool sell){
    const auto quote=*sale_quote_;
    try{
        session_.apply([&](Game& game){
            if(!game.finish_sale_offer(quote,sell))throw FormatError("Das Angebot ist nicht mehr gültig.");
            return ActionResult{};
        });
        sale_quote_.reset();invalidate();
    }catch(const std::exception& e){notice(e.what());}
}
// Paints the offer box: player name, bidding club, the offered sum and the transfer fee ("ABLÖSESUMME",
// shown only for block 4, otherwise "keine"), and the two buttons.
void ClassicApplication::draw_sale_offer(){
    const auto& q=*sale_quote_;const auto& h=snapshot();
    const auto native_name=[&](size_t at,size_t length){
        std::string s;for(size_t i=0;i<length&&h.state.at(at+i);++i)s+=char(h.state.at(at+i));return s;
    };
    constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow;
    plain_frame(2,28,168,99,4);canvas_.line(6,43,163,43,7);
    canvas_.text_native(flow,6,40,"Manager "+native_name(0x4fa5+size_t(q.id)*33,18),10,28,163);
    canvas_.text_native(mini,7,52,"ANGEBOT von "+native_name(0x17e7+size_t(q.bidder)*61,22)+" :",7);
    canvas_.text(mini,4,60,money(q.amounts.gross),7,0,166);
    canvas_.text_native(mini,7,68,"ABL|SESUMME :",7);
    canvas_.text(mini,4,76,q.block==4?money(q.amounts.severance):"keine",7,0,166);
    button(11,83,"Behalten");button(90,83,"Verkaufen");
}
} // namespace em
