// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"

namespace em::ui {
void ClassicApplication::click_transfer(int x,int y,bool right){
    const auto& h=snapshot();
    const int own=(y-29)/6,pool=(y-36)/16;
    const auto market=hok_squad(h,4);
    const bool own_row=x>=0&&x<=155&&own>=0&&size_t(own)<h.squad.size();
    const bool pool_row=x>175&&pool>=0&&size_t(pool)<market.size()&&market[size_t(pool)].player>=0;
    if(right){
        if(pool_row){open_player(pool,4);return;}
        if(own_row){open_player(own);return;}
        action([](Game& game){game.finish_transfer_menu();return ActionResult{};});screen_=ClassicScreen::Main;return;
    }
    if(y<14){action([](Game& game){game.finish_transfer_menu();return ActionResult{};});screen_=ClassicScreen::Main;return;}
    if(own_row){
        const size_t at=0x696d+size_t(own)*52;
        if((h.state.at(at+9)&0x40)&&!(h.state.at(at+0x18)&0x80)){open_sale_offer(0,own,transfer_native_si_);return;}
        // ovr035:0FDD..10F6: three own list players first, then a player not owned by the manager (loan).
        const int own_id=h.state.at(at+15);
        if(own_id&&!(h.state.at(at+0x18)&0x80)&&h.state.at(0x4FC6+size_t(own_id-1)*33+0x1A)!=0){
            int listed=0;
            for(size_t s=0;s<11;++s){const int id=h.state.at(0x81CD+s*52+15);listed+=id&&h.state.at(0x4FC6+size_t(id-1)*33+0x1A)==0;}
            notice(listed>=3?"Schon 3 Spieler auf\ndem Transfermarkt.":"Dieser Spieler ist\nnur ausgeliehen.");return;
        }
        action([&](Game& game){
            const auto result=game.list_transfer_player(size_t(own),false);
            switch(result){
                case TransferMoveResult::Moved:return ActionResult{};
                case TransferMoveResult::OwnLimit:return ActionResult{"Schon 3 Spieler auf\ndem Transfermarkt.",false};
                case TransferMoveResult::PoolFull:return ActionResult{"Schon 11 Mann auf\ndem Transfermarkt.",false};
                case TransferMoveResult::Blocked:{
                    const size_t record=0x696D+size_t(own)*52;
                    if(h.state.at(record+0x18)&0x80)return ActionResult{"Dieser Spieler hört\nam Saisonende auf.",false};
                    if(h.state.at(record+0x0C))return ActionResult{"Dieser Spieler ist\nnur ausgeliehen.",false};
                    return ActionResult{"Dieses Angebot ist noch nicht nachgebaut.",false};
                }
                default:return ActionResult{"Dieser Spieler kann derzeit nicht angeboten werden.",false};
            }
        });return;
    }
    if(pool_row){
        const size_t record=0x696D+4*0x618+size_t(pool)*52;
        const int id=h.state.at(record+15);
        const int owner=h.state.at(0x4FC6+size_t(id-1)*33+0x1A);
        if(owner==0){
            if(h.state.at(record+9)&0x80){open_sale_offer(4,pool,int16_t(x));return;}
            action([&](Game& game){const auto result=game.recall_transfer_player(size_t(pool),false);return ActionResult{result==TransferMoveResult::Moved?"":"Dieser Spieler kann derzeit nicht zurückgerufen werden.",result==TransferMoveResult::Moved};});
        }else if(owner==4&&x<=250)open_purchase(pool);
        else if(owner==4)open_loan(pool);
        return;
    }
    if(x>261&&x<318&&y>226&&y<238)open_talent_question();
}
} // namespace em::ui
