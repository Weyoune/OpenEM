// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic advertising page (ui/classic_advertising.cpp): the footer with income and
// expenses, the click areas of the boards, shirt, TV and facility surfaces, the offer browser with its
// sponsor texts, the free-board capacity and pending renewals. The page must never write to the game
// state; the stored income is refreshed by the game core when the page is entered.
//
// Usage: classic_advertising_tests [DATA [REFERENCE]] — without arguments only the synthetic cases run;
// with DATA (folder with EM.EXE) and REFERENCE (folder with MONAT1.HOK) the view of a real save is checked.
//
// Advertising records are 10 bytes: +0 monthly amount (i32), +4 boards, +5 months, +6 flags (0x80 = may be
// cancelled), +7 sponsor id, +8 minimum club image. Offers live at state 0xA52C, contracts at 0xACFC.
#include "ui/classic_advertising.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `value` is false.
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// Writes `value` little-endian as 4 bytes at state offset `at`.
void put32(HokFile& h,size_t at,int32_t value) {
    for(size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(uint32_t(value)>>(8*i));
}
// Writes one 10-byte advertising record (offer or contract) at state offset `at`; the minimum image is 68.
void put_record(HokFile& h,size_t at,int32_t amount,int boards,int months,int flags,int sponsor) {
    put32(h,at,amount); h.state.at(at+4)=uint8_t(boards); h.state.at(at+5)=uint8_t(months);
    h.state.at(at+6)=uint8_t(flags); h.state.at(at+7)=uint8_t(sponsor); h.state.at(at+8)=68;
}
// A minimal save for the page: difficulty byte 2 (0xAEB4), cost factor 100 (0x06B0), one manager, a stadium
// facility level 2 and three board offers in slots 2..4 of the offer table (slot 4 is MONAT1's real offer).
HokFile fixture() {
    HokFile h; h.state.assign(47761,0); h.teams.resize(150);
    h.state.at(0xaeb4)=2; put32(h,0x06b0,100); h.state.at(0x0dc6)=1;
    h.state.at(0x0dcd+0x116)=2; //facility surface9 exists,10 does not
    put_record(h,0xa52c+20,50830,2,12,0,2);
    put_record(h,0xa52c+30,64438,2,12,0,3);
    put_record(h,0xa52c+40,64342,2,24,0,4); //independent MONAT1 raw offer vector
    return h;
}
// The page as it is entered: nothing selected, the footer from the saved difficulty and cost factor, and the
// exact click areas of the seven boards, shirt/TV and facility surfaces.
void entry_and_surfaces() {
    const auto h=fixture(); const auto before=h.state; FinanceTables t;
    t.level=5; t.cost_factor=999; //the saved values must override this cache
    const auto v=classic_advertising_view(h,t);
    require(v.selection.contractSlot==-1 && v.kind==-1 && !v.available && v.detail.empty() && v.controls.empty(),
            "fresh entry has no selected surface, detail or acceptance controls");
    require(v.income==0 && v.expenses==372500 && v.nativeIncome=="%%%%%%0 DM" &&
            v.nativeExpenses=="124.166 DM","saved difficulty/cost override immutable EXE cache and native money width7");
    require(classic_advertising_hit(v,36,29).action==ClassicAdvertisingAction::None &&
            classic_advertising_hit(v,37,29).slot==0 && classic_advertising_hit(v,71,53).slot==0 &&
            classic_advertising_hit(v,72,29).slot==1 && classic_advertising_hit(v,281,53).slot==6 &&
            classic_advertising_hit(v,282,53).action==ClassicAdvertisingAction::None,
            "board hit intervals follow floor((x-37)/35) and strict outer native limits");
    require(classic_advertising_hit(v,3,86).action==ClassicAdvertisingAction::None &&
            classic_advertising_hit(v,4,86).slot==7 && classic_advertising_hit(v,79,161).slot==7 &&
            classic_advertising_hit(v,4,162).slot==8 && classic_advertising_hit(v,79,239).slot==8,
            "shirt/TV split at native y161/162, not icon drawing boundaries");
    require(classic_advertising_hit(v,269,91).slot==9 && classic_advertising_hit(v,303,125).slot==9 &&
            classic_advertising_hit(v,269,126).slot==10 && !classic_advertising_hit(v,269,126).enabled,
            "facility requires exact level2 and has35pixel click row despite36pixel crop spacing");
    require(h.state==before,"entry raw view never writes stored income or changes offers");
}
// The offer browser of a board: it starts at the highest filled offer slot, shows the offer's terms and the
// sponsor's information text from EM.EXE, and the arrow and button click areas.
void offers_and_info() {
    auto h=fixture(); const FinanceTables t;
    const auto v=classic_advertising_view(h,t,{3,-1,true});
    require(v.kind==0 && v.highestOffer==4 && v.selection.offerIndex==4 && v.freeBoards==7 &&
            v.record.amount==64342 && v.record.boards==2 && v.record.months==24 && v.record.sponsor==4,
            "native offer browser starts at highest nonzero slot without generating offers");
    require(v.nativeSponsor=="Amiga Games" && v.canInfo && v.canAccept &&
            v.detail[2].native==": 24 Monate" && v.detail[4].native==": 2" &&
            v.detail[6].native==": 21.447 DM  pro Monat",
            "independent native MONAT1 screenshot text:24months,2boards,21447DM");
    require(classic_advertising_hit(v,258,170).action==ClassicAdvertisingAction::Info &&
            classic_advertising_hit(v,313,180).action==ClassicAdvertisingAction::Info &&
            classic_advertising_hit(v,257,170).action==ClassicAdvertisingAction::None &&
            classic_advertising_hit(v,258,186).action==ClassicAdvertisingAction::Accept,
            "Info and acceptance use strict standard-button interiors");
    require(classic_advertising_hit(v,138,183).action==ClassicAdvertisingAction::PreviousOffer &&
            classic_advertising_hit(v,145,191).action==ClassicAdvertisingAction::PreviousOffer &&
            classic_advertising_hit(v,146,191).action==ClassicAdvertisingAction::None &&
            classic_advertising_hit(v,145,200).action==ClassicAdvertisingAction::NextOffer,
            "offer arrow accepts8x9 area, narrower than10x17 painted widget");
    require(v.info[0].native=="Firmen-Info: " && v.info[0].baseline==100 && v.info[0].color==7 &&
            v.info[1].baseline==113 && v.info[2].baseline==121 && v.info[4].baseline==137 &&
            v.info[1].native=="Jeden Monat neu! Spieletests, Insiderberichte und alles" &&
            v.info[2].native=="rund um den Amiga! Jeden Monat neu mit randvoller" &&
            v.info[3].native=="Coverdisk an Ihrem Kiosk!" && v.info[4].native.empty(),
            "original EXE informational linebreaks and irregular first-line gap retained");
    h.state.at(0xa52c+47)=3; //sponsor differs from selected offer table index
    const auto changed=classic_advertising_view(h,t,{3,4,true});
    require(changed.nativeSponsor=="PC Games" && changed.info[2].native==v.info[2].native,
            "Info uses current offerIndex, while displayed logo/name use raw sponsorID");
    const auto empty=classic_advertising_view(h,t,{3,0,true});
    require(empty.canInfo && !empty.canAccept && empty.detail.back().native=="Es liegt momentan kein Angebot vor...",
            "empty browser slot still has Info but no acceptance");
    const auto tv=classic_advertising_view(h,t,{8,0,true});
    require(tv.maxOffer==4 && !tv.canInfo && tv.nativeSponsor=="ARB",
            "TV uses separate five-entry sponsor name range and noInfo");
}
// Free boards (cancellable contracts count as free), a pending renewal option shown without resolving it,
// and the income computed from the raw team bytes and the occupied squad count.
void capacity_options_and_raw_income() {
    auto h=fixture(); const FinanceTables t;
    for(int i=0;i<6;++i) put_record(h,0xacfc+size_t(i)*10,3000,8,12,0,i);
    auto v=classic_advertising_view(h,t,{6,4,true});
    require(v.freeBoards==1 && !v.canAccept,"two-board offer cannot fit one freeboard");
    h.state.at(0xacfc+6)=128;
    v=classic_advertising_view(h,t,{6,4,true});
    require(v.freeBoards==2 && v.canAccept,"cancellable bit80 qualifies as a native replaceable board");
    put_record(h,0xacfc+7*10,9000,0,0,24,4);
    const auto renewal=classic_advertising_view(h,t,{7,0,false});
    require(renewal.renewalPending && !renewal.canAccept &&
            renewal.detail[2].native==": 0 - 24 Monate" && renewal.detail[3].native=="! Option !",
            "renewal metadata is pure, does not draw RNG or resolve pending option");
    h.state.at(0x17e7+0x18)=50; h.state.at(0x17e7+0x19)=40;
    h.state.at(0x17e7+0x1a)=30; h.state.at(0x17e7+0x1b)=20;
    //One raw occupied player; decoded team cache intentionally left zero.
    h.state.at(0x696d+0x0f)=1;
    const auto raw=classic_advertising_view(h,t);
    require(raw.income==27700,"income uses four raw cached team bytes and native occupied count, not stale decoded team values");
    bool rejected=false;
    try {(void)classic_advertising_view(h,t,{3,10,true});} catch(const FormatError&) {rejected=true;}
    require(rejected,"out-of-atlas offer index is rejected explicitly");
}
// The view of the reference save MONAT1.HOK with the tables of the user's EM.EXE, and a check that drawing the
// page, the offer browser and the info box leaves the state untouched.
void original_files(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto exe=read_file(data/"EM.EXE"); const auto t=load_finance_tables(exe);
    const auto h=decode_hok(read_file(refs/"MONAT1.HOK")); const auto before=h.state;
    const auto initial=classic_advertising_view(h,t);
    require(initial.income==40125 && initial.expenses==1447700 &&
            initial.nativeIncome=="%13.375 DM" && initial.nativeExpenses=="482.566 DM",
            "independent native entry footer vector includes saved difficulty and recalculated income");
    const auto offer=classic_advertising_view(h,t,{3,-1,true});
    require(offer.selection.offerIndex==4 && offer.record.amount==64342 && offer.record.minImage==68 &&
            offer.record.months==24 && offer.record.boards==2,"native original highest-offer raw golden");
    ClassicCanvas canvas(data);
    draw_classic_advertising(canvas,h,t); draw_classic_advertising(canvas,h,t,{3,-1,true});
    draw_classic_advertising_info(canvas,offer);
    require(h.state==before && le32(h.state,0x0f64)==40000,
            "all rendering remains pure; parent explicitly owns stored-income entry refresh");
}
} // namespace
int main(int argc,char** argv) {
    try {
        entry_and_surfaces(); offers_and_info(); capacity_options_and_raw_income();
        if(argc>1) original_files(argv[1],argc>2?std::filesystem::path(argv[2]):std::filesystem::path(argv[1]));
        std::puts("Native classic advertising raw-view tests passed"); return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"Classic advertising: %s\n",e.what()); return 1;}
}
