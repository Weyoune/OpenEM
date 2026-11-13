// SPDX-License-Identifier: GPL-3.0-only
#include "game/youth_progress.hpp"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstdio>

// Youth team development per matchday and per season (see youth_progress.hpp).

namespace em {
namespace {
// Quality a youth team needs for rank 0 in youth league 0 / 1 / 2 (the original's constant
// table); each rank further down lowers the expectation by one.
constexpr std::array<int,3> league_quality{85,70,57};
// Signed 32-bit product that wraps on overflow, like the original's 32-bit multiply.
int32_t low_product(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(uint64_t(uint32_t(a))*uint32_t(b)));}
int sign(int32_t n){return (n>0)-(n<0);}
// Truncates `n` to a signed byte (as the original stored it) and clamps it to low..high.
int byte_clamp(int n,int low,int high){const auto b=uint8_t(n);return std::clamp(b<128?int(b):int(b)-256,low,high);}
// State offset of the youth block of `manager` (0..3), bounds-checked.
size_t block(const Bytes& state,int manager){
    if(manager<0||manager>=4)throw FormatError("youth progress: invalid manager");
    const size_t at=0x06b8+size_t(manager)*0x15;
    if(at+0x15>state.size())throw FormatError("youth progress: truncated native state");
    return at;
}
}
void youth_fixture_day(Bytes& state,const FinanceTables& finance,int first_matchday,int manager,const YouthRandomDraw& random){
    const size_t at=block(state,manager);
    if(first_matchday<0||first_matchday>44||finance.money_num<=0||finance.money_den<=0)
        throw FormatError("youth progress: invalid matchday or currency");
    // A manager without youth teams (all three qualities 0): the original draws nothing for him.
    if(!state[at]&&!state[at+1]&&!state[at+2]&&!std::getenv("EM_OLD_YOUTHZERO"))return;
    for(size_t i=0;i<3;++i)if(!state[at+i]||state[at+6+i]>=3)
        throw FormatError("youth progress: invalid native quality or league");
    // A reversed range such as rand(1,0) is passed on unchanged; the random function itself
    // swaps the bounds.
    const auto draw=[&](int lo,int hi){return random(lo,hi);};
    // Tempo table: maximum rank stride per matchday, starting at 4 and decaying randomly.
    std::array<uint8_t,45> tempo{};int counter=4;
    for(size_t j=0;j<44;++j){tempo[j]=uint8_t(counter);if(counter&&draw(0,8)==0)--counter;}
    // In the original, entry 44 lies just past the 44-entry table and reads the counter
    // variable stored next to it, i.e. the final counter value.
    tempo[44]=uint8_t(counter);
    if(std::getenv("EM_DEBUG_YOUTH")){std::fprintf(stderr,"youth md %d tempo",first_matchday);for(size_t j=0;j<45;++j)std::fprintf(stderr," %d",tempo[j]);std::fprintf(stderr,"\n");}
    for(size_t i=0;i<3;++i){
        const int quality=state[at+i],rank=state[at+3+i],league=state[at+6+i];
        const auto raw=std::bit_cast<int32_t>(le32(state,at+9+i*4));
        const int32_t display=low_product(int32_t(int64_t(raw)/finance.money_num),int32_t(finance.money_den));
        const int baseline=uint8_t(league_quality[size_t(league)]-rank);
        const int32_t percent=low_product(100,display)/(10000<<i);
        const int32_t target=40+(percent>>1); // arithmetic shift: rounds negative odd values down, as the original
        const int32_t ratio=low_product(100,target)/quality;
        int direction=(ratio>100)-(ratio<100);
        if(draw(0,4)==0)direction=draw(0,2)-1;
        if(std::abs(int64_t(ratio)-100)<=draw(0,30))direction=0;
        const int new_quality=byte_clamp(quality+(draw(0,2)==0?direction:0),10,85);
        state[at+i]=uint8_t(new_quality);
        const int difference=new_quality-baseline;
        direction=sign(difference);
        if(draw(0,4)==0)direction=draw(0,2)-1;
        if(draw(0,3)!=0&&draw(1,18)>2*std::abs(difference))direction=0;
        const int stride=draw(1,tempo[size_t(first_matchday)]); // drawn even when direction is 0
        state[at+3+i]=uint8_t(byte_clamp(rank-stride*direction,0,11));
    }
}
void youth_new_season(Bytes& state,int manager){
    const size_t at=block(state,manager);
    for(int i=2;i>=0;--i){
        const size_t row=size_t(i);const int old_rank=state[at+3+row];
        int league=state[at+6+row],rank=5;
        if(old_rank<2&&league>0){--league;rank=old_rank+10;}
        if(old_rank>10&&league<2){++league;rank=old_rank-10;}
        if(i>0)state[at+row]=uint8_t((2*state[at+row]+state[at+row-1])/3);
        state[at+3+row]=uint8_t(rank);state[at+6+row]=uint8_t(league);
    }
}
} // namespace em
