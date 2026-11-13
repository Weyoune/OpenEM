// SPDX-License-Identifier: GPL-3.0-only
#include "game/youth_intake.hpp"
#include <bit>

// Youth intake rules (see youth_intake.hpp).

namespace em {
namespace {
// Stores a little-endian i32 at state offset `at`.
void put32(Bytes& s,size_t at,int32_t value){
    for(size_t byte=0;byte<4;++byte)s.at(at+byte)=uint8_t(uint32_t(value)>>(8*byte));
}
}
int youth_review_attempts(int year,const ContractRandomDraw& draw){
    const int attempts=draw(0,2);
    const int suppressed=draw(0,1);
    return year>1994&&!suppressed?attempts:0;
}
std::optional<YouthIntake> youth_intake(HokFile& h,const ContractTables& tables,
    int manager,int quality,const ContractRandomDraw& draw){
    if(quality<10||quality>85)throw FormatError("youth intake: invalid native quality");
    if(native_reserved_players(h,manager)>=28)return std::nullopt;
    // Draw player IDs until one is owned by no manager (+0x1A == 6). Player record of ID n
    // (1-based) at 0x4FC6 + (n-1) * 0x21.
    uint8_t id=0;size_t global=0;
    do{id=uint8_t(draw(1,179));global=0x4fa5+size_t(id)*0x21;}
    while(h.state.at(global+0x1a)!=6);
    // national caps, age, form, both skills; team = the manager's club (manager block +0x14)
    h.state.at(global+0x19)=0;
    h.state.at(global+0x14)=uint8_t(draw(18,21));
    h.state.at(global+0x17)=uint8_t(draw(45,55));
    h.state.at(global+0x16)=uint8_t(draw(quality-5,quality+1));
    h.state.at(global+0x15)=uint8_t(draw(quality-5,quality+1));
    h.state.at(global+0x1d)=h.state.at(0x0dcd+size_t(manager)*0x1af+0x14);
    const auto inserted=native_insert_player(h,tables,draw,id,manager,0);
    h.state.at(global+0x1a)=uint8_t(manager);
    // The new squad row (block 0x696D + manager * 0x618, 0x34 bytes per slot).
    const size_t row=0x696d+size_t(manager)*0x618+size_t(inserted.slot)*0x34;
    const int high=draw(3,12),low=draw(3,12);
    h.state.at(row+0x19)=uint8_t((high<<4)|low);
    const auto player=contract_player(h,manager,inserted.slot);
    const long salary=player_worth(tables,player,Worth::Salary,draw);
    // Juniors are paid 60% of the regular salary (32-bit wrapping product, truncating division).
    const int32_t discounted=std::bit_cast<int32_t>(uint32_t(uint64_t(uint32_t(salary))*60u))/100;
    put32(h.state,row+0x28,discounted);
    put32(h.state,row+0x2c,int32_t(player_worth(tables,player,Worth::Value,draw)));
    h.state.at(row+0x0b)=uint8_t(draw(2,3));
    return YouthIntake{id,inserted.slot,inserted.shifted};
}
} // namespace em
