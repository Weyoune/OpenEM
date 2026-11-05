// SPDX-License-Identifier: GPL-3.0-only
#include "game/sale_quote.hpp"
#include "game/native_roster.hpp"
#include <algorithm>
#include <bit>

namespace em {
namespace {
int16_t i16(int n){return std::bit_cast<int16_t>(uint16_t(n));}
int32_t i32(uint32_t n){return std::bit_cast<int32_t>(n);}
int32_t product(int32_t a,int32_t b){return i32(uint32_t(uint64_t(uint32_t(a))*uint32_t(b)));}
size_t row_at(int block,int slot){return 0x696d+size_t(block)*0x618+size_t(slot)*52;}
void put32(Bytes& s,size_t at,int32_t value){for(size_t b=0;b<4;++b)s.at(at+b)=uint8_t(uint32_t(value)>>(8*b));}
int buyer_mean(const Bytes& s,int buyer,int16_t incoming_si){
    //A read past the native team allocation needs guest heap context, not HOK table bytes.
    const auto shifted=std::ptrdiff_t(buyer)*61+incoming_si;
    if(shifted<0||shifted+26>=150*61)throw FormatError("sale: buyer register outside native team data");
    const size_t at=0x17e7+size_t(shifted);
    return (int(s.at(at+24))+s.at(at+26))/2;
}
void adjust_buyer(Bytes& s,int buyer,int strength,int mean,BorlandRandom& rng){
    const size_t team=0x17e7+size_t(buyer)*61;
    //175C reads the same SI-offset pair twice before SI becomes random kind0/1.
    const int delta=std::clamp((strength-mean+10)/3,1,10);
    const size_t kind=size_t(native_sale_range(rng,0,1));
    s.at(team+24+kind)=uint8_t(s.at(team+24+kind)+native_sale_range(rng,delta-1,delta+1));
    s.at(team+26+kind)=uint8_t(s.at(team+26+kind)+native_sale_range(rng,delta-1,delta+1));
}
}
int native_sale_range(BorlandRandom& rng,int low,int high){
    int lo=i16(low),hi=i16(high);if(hi<lo)std::swap(lo,hi);
    const int width=i16(hi+1-lo);
    if(!width)throw FormatError("sale: native random divisor is zero");
    return i16(lo+rng.rand()%width);
}
SaleAmounts native_sale_amounts(int32_t value,bool listed,bool foreign,int years,BorlandRandom& rng){
    int32_t basis=value;
    if(listed){
        const int jitter=native_sale_range(rng,value/20,value/10);
        basis=i32(uint32_t(value)+uint32_t(jitter)-uint32_t(value/15));
    }
    const int factor=listed?native_sale_range(rng,85,115):native_sale_range(rng,foreign?112:87,foreign?155:125);
    SaleAmounts result;
    result.gross=product(product(basis,factor)/30000,300);
    result.severance=listed?product(result.gross/7,years):0;
    result.net=i32(uint32_t(result.gross)-uint32_t(result.severance));
    return result;
}
bool begin_native_sale(const HokFile& h,const ContractTables& tables,BorlandRandom& rng,
    int block,int slot,int16_t incoming_si,SaleQuote& output){
    if((block!=0&&block!=4)||slot<0||slot>=(block==4?11:29))return false;
    const size_t at=row_at(block,slot);const int id=h.state.at(at+15);
    if(!id||id>199||!(h.state.at(at+9)&(block==4?0x80:0x40)))return false;
    if(h.state.at(0x4fa5+size_t(id)*33+26)!=0)return false;
    if(block==0&&(h.state.at(at+0x18)&0x80))return false;
    const int bidder=h.state.at(at+22);if(bidder>=150)return false;
    SaleQuote quote;quote.block=block;quote.slot=slot;quote.id=id;quote.bidder=bidder;
    quote.years=h.state.at(at+11);quote.native_si=incoming_si;
    std::copy_n(h.state.begin()+std::ptrdiff_t(at),52,quote.row.begin());
    const auto player=contract_player(h,block,slot);
    const int32_t value=int32_t(player_worth(tables,player,Worth::Value,rng));
    quote.amounts=native_sale_amounts(value,block==4,bidder>63,quote.years,rng);
    output=quote;return true;
}
bool current_native_sale(const HokFile& h,const SaleQuote& q){
    if((q.block!=0&&q.block!=4)||q.slot<0||q.slot>=(q.block==4?11:29)||q.id<1||q.id>199)return false;
    const size_t at=row_at(q.block,q.slot);
    return q.row[15]==q.id&&q.bidder==q.row[22]&&q.years==q.row[11]&&
        (q.row[9]&(q.block==4?0x80:0x40))&&h.state.at(0x4fa5+size_t(q.id)*33+26)==0&&
        std::equal(q.row.begin(),q.row.end(),h.state.begin()+std::ptrdiff_t(at));
}
bool finish_native_sale(HokFile& h,BorlandRandom& rng,const SaleQuote& q,bool sell,int* actual_buyer){
    if(!current_native_sale(h,q))return false;
    const size_t at=row_at(q.block,q.slot),global=0x4fa5+size_t(q.id)*33;
    int mean=0;
    if(sell){
        const int buyer=q.block==4?q.bidder:h.state.at(row_at(0,q.slot+1)+22);
        if(q.block==4||buyer<64)mean=buyer_mean(h.state,buyer,q.native_si);
    }
    h.state.at(at+9)^=q.block==4?0x80:0x40;
    native_clear_player_notification(h,uint8_t(q.id));
    if(!sell)return true;
    constexpr size_t cash=0x0dcd+0x18f;
    int buyer=q.bidder;
    if(q.block==0){
        put32(h.state,cash,i32(le32(h.state,cash)+uint32_t(q.amounts.net)));
        native_remove_storage_row(h,0,q.slot,29);
        //0F69 reads the old pointer after the shift, including its bidder/skills.
        buyer=h.state.at(at+22);h.state.at(global+29)=uint8_t(buyer);
        const int strength=(int(h.state.at(at+16))+h.state.at(at+17)+h.state.at(at+18))/3;
        if(buyer<64)adjust_buyer(h.state,buyer,strength,mean,rng);
    }else{
        h.state.at(global+29)=uint8_t(buyer);
        const int strength=(int(h.state.at(at+16))+h.state.at(at+17)+h.state.at(at+18))/3;
        adjust_buyer(h.state,buyer,strength,mean,rng);
        put32(h.state,cash,i32(le32(h.state,cash)+uint32_t(q.amounts.net)));
        native_remove_storage_row(h,4,q.slot,10);
    }
    h.state.at(global+26)=6;if(actual_buyer)*actual_buyer=buyer;return true;
}
} // namespace em
