// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/native_roster.hpp"

namespace em {
enum class AnnualPlayerStatus { Missing,OtherStorage,Stayed,Retired,Disabled };
struct AnnualPlayerReview {
    AnnualPlayerStatus status=AnnualPlayerStatus::Missing;
    int slot=-1;
    int32_t insurance_payment=0;
    int notice_back=0;  // the immediate message's date draw rand(0,3): days dated back
};
// 4184:07D9..0AB6, a fresh lookup in the second review pass. Age/years and
// global statistics have already been processed. Only found own rows mutate.
AnnualPlayerReview annual_owned_player(HokFile&,uint8_t player_id,int manager,
                                       const ContractRandomDraw&);
// 1238:13A1, late annual global-only skill pass, after review and summer.
void annual_global_skills(Bytes&,const ContractRandomDraw&);
} // namespace em
