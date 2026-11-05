// SPDX-License-Identifier: GPL-3.0-only
#include "game/annual_players.hpp"
#include <algorithm>
#include <bit>

namespace em {
AnnualPlayerReview annual_owned_player(HokFile& h,uint8_t id,int manager,const ContractRandomDraw& draw){
    if(manager<0||manager>=4)throw FormatError("annual players: invalid manager");
    const auto location=native_find_roster_player(h,id);
    if(!location)return {};
    if(location->manager!=manager)return {AnnualPlayerStatus::OtherStorage,location->slot,0};
    const size_t row=0x696d+size_t(manager)*0x618+size_t(location->slot)*0x34;
    const bool disabled=h.state.at(row+0x0d)==0xff;
    int32_t payment=0;
    if(disabled)payment=std::bit_cast<int32_t>(uint32_t(le32(h.state,row+0x30)*60u));
    else {
        // The original draws the age threshold, but an existing row always
        // falls through to the same years/flag gate on either age comparison.
        (void)draw(37,42);
        if(h.state.at(row+0x0b)!=0||!(h.state.at(row+0x18)&0x80))
            return {AnnualPlayerStatus::Stayed,location->slot,0};
    }
    (void)draw(0,3); // immediate notification date, including a full queue
    native_remove_player(h,manager,location->slot);
    const size_t global=0x4fa5+size_t(id)*0x21;
    h.state.at(global+0x1a)=6;
    h.state.at(global+0x14)=uint8_t(draw(18,25));
    const int base=draw(40,92);
    h.state.at(global+0x15)=uint8_t(draw(base-5,base+5));
    h.state.at(global+0x19)=0;
    h.state.at(global+0x16)=uint8_t(draw(base-5,base+5));
    return {disabled?AnnualPlayerStatus::Disabled:AnnualPlayerStatus::Retired,location->slot,payment};
}
void annual_global_skills(Bytes& state,const ContractRandomDraw& draw){
    if(state.size()<0x4fc6+199*0x21)throw FormatError("annual players: truncated globals");
    for(size_t id=1;id<200;++id)for(size_t skill:{size_t(0x15),size_t(0x16)}){
        auto& value=state.at(0x4fa5+id*0x21+skill);
        value=uint8_t(std::clamp(int(value)+draw(1,7)-4,30,99));
    }
}
} // namespace em
