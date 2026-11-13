// SPDX-License-Identifier: GPL-3.0-only
// Input handling of the transfer page: listing and recalling own players, offers on pool players, player
// details and the Talent button.
#include "ui/classic_app.hpp"

namespace em::ui {
// Click on the transfer page. Right click: player details of the row under the pointer, otherwise leave the
// page. Left click on an own player puts him on the transfer list (with the original's refusals), on an own
// list player answers an open offer or recalls him, on another pool player opens the purchase (x <= 250) or
// loan offer.
void ClassicApplication::click_transfer(int x,int y,bool right){
    const auto& h=snapshot();
    const int own=(y-29)/6,pool=(y-36)/16;
    const int m=active_manager();
    const auto market=hok_squad(h,4);
    const bool own_row=x>=0&&x<=155&&own>=0&&size_t(own)<h.squad_of(m).size();
    const bool pool_row=x>175&&pool>=0&&size_t(pool)<market.size()&&market[size_t(pool)].player>=0;
    if(right){
        if(pool_row){open_player(pool,4);return;}
        if(own_row){open_player(own);return;}
        action([](Game& game){game.finish_transfer_menu();return ActionResult{};});screen_=ClassicScreen::Main;return;
    }
    if(y<14){action([](Game& game){game.finish_transfer_menu();return ActionResult{};});screen_=ClassicScreen::Main;return;}
    if(own_row){
        const size_t at=squad_block(m)+size_t(own)*52;
        // A player who retires at the end of the season is refused before every other check.
        if(h.state.at(at+0x18)&0x80){notice("Dieser Spieler hört\nam Saisonende auf.");return;}
        if(h.state.at(at+9)&0x40){open_sale_offer(0,own,transfer_native_si_);return;}
        // Checked in the original's order: three own list players first, then a player not owned by the
        // manager (loan).
        const int own_id=h.state.at(at+15);
        if(own_id&&h.state.at(0x4FC6+size_t(own_id-1)*33+0x1A)!=m){
            int listed=0;
            for(size_t s=0;s<11;++s){const int id=h.state.at(0x81CD+s*52+15);listed+=id&&h.state.at(0x4FC6+size_t(id-1)*33+0x1A)==m;}
            notice(listed>=3?"Schon 3 Spieler auf\ndem Transfermarkt.":"Dieser Spieler ist\nnur ausgeliehen.");return;
        }
        action([&](Game& game){
            const auto result=game.list_transfer_player(size_t(own),false);
            switch(result){
                case TransferMoveResult::Moved:return ActionResult{};
                case TransferMoveResult::OwnLimit:return ActionResult{"Schon 3 Spieler auf\ndem Transfermarkt.",false};
                case TransferMoveResult::PoolFull:return ActionResult{"Schon 11 Mann auf\ndem Transfermarkt.",false};
                // Blocked = retiring (+0x18 bit 7) or an open offer (+9 bit 6); both are handled above.
                case TransferMoveResult::Blocked:return ActionResult{"Dieser Spieler hört\nam Saisonende auf.",false};
                default:return ActionResult{"Dieser Spieler kann derzeit nicht angeboten werden.",false};
            }
        });return;
    }
    if(pool_row){
        const size_t record=0x696D+4*0x618+size_t(pool)*52;
        const int id=h.state.at(record+15);
        const int owner=h.state.at(0x4FC6+size_t(id-1)*33+0x1A);
        if(owner==m){
            if(h.state.at(record+9)&0x80){open_sale_offer(4,pool,int16_t(x));return;}
            action([&](Game& game){const auto result=game.recall_transfer_player(size_t(pool),false);return ActionResult{result==TransferMoveResult::Moved?"":"Dieser Spieler kann derzeit nicht zurückgerufen werden.",result==TransferMoveResult::Moved};});
        }else if(owner<=4&&x<=250)open_purchase(pool);  // owner 0..3: another manager's list player (classic_hotseat.cpp)
        else if(owner<=4)open_loan(pool);
        return;
    }
    if(x>261&&x<318&&y>226&&y<238)open_talent_question();
}
} // namespace em::ui
