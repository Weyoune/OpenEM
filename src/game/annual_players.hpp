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
// 1238:13A1 (Amiga 0x13620A) part 1, the late annual global-only skill pass
// after review and summer: for IDs 1..199, field +0x15 then +0x16, each via
// 1238:1359 (Amiga 0x1361BE): v += Borland rand(1,7) - 4; clamp(v, 30, 99)
// (14CF:0052). The same routine then moves players between clubs, see
// annual_random_club_changes in annual_distribution.hpp (1238:14AB..159D).
void annual_global_skills(Bytes&,const ContractRandomDraw&);
} // namespace em
