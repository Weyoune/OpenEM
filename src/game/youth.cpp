// SPDX-License-Identifier: GPL-3.0-only
#include "game/youth.hpp"
#include <bit>

namespace em {
namespace {
int32_t multiply(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(uint64_t(uint32_t(a))*uint32_t(b)));}
int32_t divide(int32_t a,int32_t b){
    if(b<=0)throw FormatError("youth funding: invalid currency divisor");
    return int32_t(int64_t(a)/b);
}
}
YouthFundingScale youth_funding_scale(const FinanceTables& t,int kind){
    if(kind<0||kind>=3)throw FormatError("youth funding: invalid kind");
    if(t.money_num<=0||t.money_den<=0||t.money_num>INT32_MAX||t.money_den>INT32_MAX)
        throw FormatError("youth funding: invalid currency");
    return {500<<kind,10000<<kind};
}
int32_t youth_funding_amount(const HokFile& h,int kind,int manager){
    if(kind<0||kind>=3||manager<0||manager>=4)throw FormatError("youth funding: invalid kind or manager");
    const size_t at=0x06c1+size_t(manager)*0x15+size_t(kind)*4;
    if(at+4>h.state.size())throw FormatError("youth funding: truncated native state");
    return std::bit_cast<int32_t>(le32(h.state,at));
}
int32_t youth_funding_target(int32_t raw,const FinanceTables& t,int kind,int x){
    const auto scale=youth_funding_scale(t,kind);
    int32_t value=divide(multiply(raw,int32_t(t.money_den)),int32_t(t.money_num));
    if(x<121){if(!value)return raw;value=std::bit_cast<int32_t>(uint32_t(value)-uint32_t(scale.step));}
    else if(x>190){if(value>=scale.maximum)return raw;value=std::bit_cast<int32_t>(uint32_t(value)+uint32_t(scale.step));}
    else value=(((x-120)*scale.maximum/70)/scale.step)*scale.step;
    return divide(multiply(value,int32_t(t.money_num)),int32_t(t.money_den));
}
int youth_funding_row(int y){
    for(int kind=0;kind<3;++kind)if(y>167+24*kind&&y<178+24*kind)return kind;
    return -1;
}
} // namespace em
