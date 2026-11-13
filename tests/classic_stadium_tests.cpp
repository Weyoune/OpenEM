// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic stadium page (ui/classic_stadium.cpp): the list of the nine facilities and two
// capacities with their current or planned values, the facility pictures drawn over the stadium, the ticket price
// and cash display, the click areas, the construction quote with its target spinner, and the page of real saves.
// The view is pure: it never writes to the state or draws random numbers.
//
// Usage: classic_stadium_tests [DATA [REFERENCE]] — with DATA (EM.EXE and graphics) and REFERENCE (START.HOK and
// SAISEND.HOK) the real saves are checked as well.
//
// Manager block fields used (offsets from `block`): +0xE4 ticket price in DM (10..30), +0x116 facility levels
// (2 bytes per facility), +0x128 weeks until a planned construction is finished, +0x133/+0x135/+0x137 capacities
// (u16), +0x18F cash (i32).
#include "ui/classic_stadium.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `value` is false.
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// State offset of manager 0's block.
constexpr size_t block=0x0dcd;
// Writes a little-endian u16 at state offset `p`.
void put16(HokFile& h,size_t p,unsigned n) {h.state.at(p)=uint8_t(n);h.state.at(p+1)=uint8_t(n>>8);}
// Writes a little-endian u32 at state offset `p`.
void put32(HokFile& h,size_t p,uint32_t n) {for(size_t i=0;i<4;++i) h.state.at(p+i)=uint8_t(n>>(8*i));}
// The stadium tables of the original EM.EXE, written out: the cost of one step per facility/capacity (raw money
// units) and the highest level of each of the nine facilities.
StadiumTables tables() {
    StadiumTables t;t.cost={3600000,1350000,1920000,1110000,990000,3900000,1740000,1560000,1350000,540000,405000};
    t.max_level={2,2,2,2,4,2,3,3,1};return t;
}
// A START-like stadium: ticket 13 DM, the facility levels of START, capacities 3000 and 4500, cash 1.500.000 raw.
HokFile fixture() {
    HokFile h;h.state.assign(47761,0);h.state.at(block+0xe4)=13;
    constexpr std::array<uint8_t,9> levels{2,1,1,0,2,1,1,0,0};
    for(size_t i=0;i<levels.size();++i) h.state.at(block+0x116+i*2)=levels[i];
    put16(h,block+0x133,3000);put16(h,block+0x137,4500);put32(h,block+0x18f,1500000);
    return h;
}
// Texts, coordinates and colours of the list, the facility pictures, the planned mode and the currency variant.
void values_and_overlays() {
    auto h=fixture();const auto before=h.state;const FinanceTables f;const auto t=tables();
    const auto v=classic_stadium_view(h,f,t);
    require(v.native_ticket==": 13 DM" && v.native_cash=="%%%500.000 DM" &&
            v.values[9].native_value==": 3.000" && v.values[10].native_value==": 4.500",
            "native thousands grouping,width10 cash and raw-DM ticket are distinct formatter paths");
    require(v.values[0].native_label=="]berdachung" && v.values[0].native_value==": HALLE" &&
            v.values[2].native_value==": 'FROSTA'" && v.values[3].native_value==": PLASTIKSCHALEN" &&
            v.values[5].native_value==": STRA~E" && v.values[6].native_value==": IMBI~BUDE",
            "original label and level texts preserve DOS umlauts and exact wording");
    require(v.values[8].baseline==233 && v.values[9].label_x==213 &&
            v.values[9].value_x==267 && v.values[9].baseline==177 && v.values[10].baseline==184 &&
            v.values[0].color==31,"native left/right list coordinates and current foreground");
    require(v.overlays.size()==3 && v.overlays[0].item==5 && v.overlays[1].item==6 && v.overlays[2].item==0 &&
            v.overlays[0].x==244 && v.overlays[0].y==50 && v.overlays[0].sx==0 && v.overlays[0].sy==0 &&
            v.overlays[0].width==66 && v.overlays[0].height==118 &&
            v.overlays[2].sx==0 && v.overlays[2].sy==118 && v.overlays[2].x==80 &&
            v.overlays[2].width==143 && v.overlays[2].height==57,
            "START subset preserves5,6,8,0,7 paint order and level2 roof sprite from independent table");
    const auto p=classic_stadium_view(h,f,t,{true});
    require(p.values[0].native_value==": (keine Baut{tigkeiten)" &&
            p.values[9].native_value==": (kein Bau...)" && p.values[0].color==1 &&
            p.overlays.size()==v.overlays.size() && p.overlays[2].sy==118,
            "planned mode substitutes values only, never draws planned rather than current graphics");
    require(h.state==before,"pure list/overlay generation preserves all HOK and RNG bytes");
    h.state.at(block+0x117+2)=2;h.state.at(block+0x128+1)=1;
    put16(h,block+0x135,4000);h.state.at(block+0x128+9)=5;
    const auto planned=classic_stadium_view(h,f,t,{true});
    require(planned.values[1].native_value==": HIGH-TECH (Noch %1 Woche)" &&
            planned.values[9].native_value==": 4.000 (%5)",
            "facilities and capacities have different planned week phrases; singular removes only finaln");
    h.state.at(block+0x116+7*2)=3;
    require(classic_stadium_view(h,f,t).values[7].native_value==": GRo~",
            "original cinema description deliberately contains lowercaseo");
    FinanceTables foreign=f;foreign.money_num=4;
    const auto alternate=classic_stadium_view(h,foreign,t);
    require(alternate.native_ticket==": 13 DM" && alternate.values[9].native_value==": 3,000" &&
            alternate.native_cash=="%%%375,000 DM","currency4 uses commas but never converts stored ticket price");
}
// Click areas of the pictures, list rows, ticket spinner and mode toggle, and the ticket price limits 10..30.
void native_hits() {
    const auto h=fixture();const auto v=classic_stadium_view(h,FinanceTables{},tables());
    require(classic_stadium_hit(v,244,50).item==5 && classic_stadium_hit(v,310,168).item==5 &&
            classic_stadium_hit(v,201,94).item==8 && classic_stadium_hit(v,223,117).item==8 &&
            classic_stadium_hit(v,79,57).item==0 && classic_stadium_hit(v,29,44).item==7,
            "graphic hitboxes use inclusive FIRST footprints, hidden cinema/bowling and original overlap precedence");
    require(classic_stadium_hit(v,0,173).item==0 && classic_stadium_hit(v,184,233).item==8 &&
            classic_stadium_hit(v,185,233).action==ClassicStadiumAction::None &&
            classic_stadium_hit(v,213,172).item==9 && classic_stadium_hit(v,213,178).item==9 &&
            classic_stadium_hit(v,319,179).item==10 && classic_stadium_hit(v,319,185).item==10,
            "strict native label inequalities produce asymmetric exact row edges");
    require(classic_stadium_hit(v,192,189).action==ClassicStadiumAction::TicketIncrease &&
            classic_stadium_hit(v,199,197).action==ClassicStadiumAction::TicketIncrease &&
            classic_stadium_hit(v,202,189).action==ClassicStadiumAction::TicketDecrease &&
            classic_stadium_hit(v,209,197).action==ClassicStadiumAction::TicketDecrease &&
            classic_stadium_hit(v,200,197).action==ClassicStadiumAction::None,
            "spinner hit8x9 differs from10x8 painted arrows and the lower arrow reverses response direction");
    require(classic_stadium_hit(v,192,168,true).action==ClassicStadiumAction::TogglePlanned &&
            classic_stadium_hit(v,192,167,true).action==ClassicStadiumAction::Back,
            "right lower-panel click toggles mode; upper-picture click returns");
    auto cap=clone_hok(h);cap.state.at(block+0xe4)=30;
    require(!classic_stadium_hit(classic_stadium_view(cap,FinanceTables{},tables()),192,189).enabled,
            "ticket upper boundary retains action but disallows byte write");
    cap.state.at(block+0xe4)=10;
    require(!classic_stadium_hit(classic_stadium_view(cap,FinanceTables{},tables()),202,189).enabled,
            "ticket lower boundary retains action but disallows byte write");
}
// The construction quote: target, cost and colour (red when unaffordable), the capacity quote in blocks of 500
// places, the target steps within budget and level limits, and the quote's click areas.
void quotes_and_steps() {
    auto h=fixture();const auto t=tables();const FinanceTables f;const auto before=h.state;
    const auto q=classic_stadium_view(h,f,t,{false,3,3,-1});
    require(q.quote && q.quote->target==1 && q.native_hint=="Sitzkomfort ausbauen" &&
            q.native_quote_target=="SITZE MIT KISSEN" && q.native_quote_cost==" Kosten: 370.000 DM" &&
            q.quote_color==8,"quote nominal currency/description/colors use original raw initial one-step price");
    const auto red=classic_stadium_view(h,f,t,{false,2,2,-1});
    require(red.quote && !red.quote->affordable && red.quote_color==17 &&
            red.native_quote_cost==" Kosten: 640.000 DM" && red.native_quote_target=="'MEGAFROST'",
            "initial unaffordable quote is painted red, not clamped or silently dismissed");
    const auto seats=classic_stadium_view(h,f,t,{false,9,9,2});
    require(seats.native_quote_target=="4.000" && seats.native_quote_cost==" Kosten: 360.000 DM",
            "seat quote renders current+500*additionalBlocks, not target as absolute capacity");
    require(classic_stadium_target_step(h,t,9,1,-1)==2 &&
            classic_stadium_target_step(h,t,9,2,-1)==2 &&
            classic_stadium_target_step(h,t,9,1,1)==1 &&
            classic_stadium_target_step(h,t,9,2,1)==1,
            "target step accepts affordable increase/decrease and restores last valid at budget/base boundary");
    require(classic_stadium_hit(q,192,210).action==ClassicStadiumAction::TargetIncrease &&
            classic_stadium_hit(q,209,218).action==ClassicStadiumAction::TargetDecrease &&
            classic_stadium_hit(q,213,208).action==ClassicStadiumAction::QuoteConfirm &&
            classic_stadium_hit(q,315,218).action==ClassicStadiumAction::QuoteConfirm &&
            classic_stadium_hit(q,212,208).action==ClassicStadiumAction::QuoteCancel &&
            classic_stadium_hit(q,0,0).action==ClassicStadiumAction::QuoteCancel &&
            classic_stadium_hit(q,315,218,true).action==ClassicStadiumAction::QuoteCancel &&
            !classic_stadium_hit(red,213,208).enabled,
            "quote arrows/strict interior/outside dismissal and right cancellation stay separate from planned toggle");
    require(h.state==before,"quotes, target constraints and hit testing consume no RNG or raw writes");
    auto broken=clone_hok(h);broken.state.resize(5);bool rejected=false;
    try {(void)classic_stadium_view(broken,f,t);} catch(const FormatError&) {rejected=true;}
    require(rejected,"truncated raw state is rejected safely");
}
// The page of START.HOK with the user's EM.EXE (values, purity, wallpaper and palette of KB graphic 14) and a quote
// with the negative cash of SAISEND.HOK.
void originals(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto exe=read_file(data/"EM.EXE");const auto t=load_stadium_tables(exe);
    const auto f=load_finance_tables(exe);
    const auto start=decode_hok(read_file(refs/"START.HOK"));const auto before=start.state;
    const auto v=classic_stadium_view(start,f,t);
    require(v.ticket==13 && v.native_cash=="%%%500.000 DM" && v.overlays.size()==3 &&
            v.values[4].native_value==": MITTEL" && v.values[9].native_value==": 3.000",
            "START directly decrypted ticket/cash/levels/capacities differ from the original's page");
    ClassicCanvas canvas(data);draw_classic_stadium(canvas,start,f,t);
    require(start.state==before,"full original page drawing changes no HOK byte");
    const auto& palette=canvas.palette();const auto& source=canvas.asset(14);
    require(canvas.pixels().at(0,0)==source.pixels[0] && canvas.pixels().at(0,168)==0 &&
            canvas.pixels().at(319,239)==4,"native wallpaper, black separator and lower fill extents");
    const auto raw=read_file(data/"KB"/"14.vga");const auto expected=decode_palette(raw,6+le16(raw,4));
    require(palette.rgb==expected.rgb,"page retains KB14 embedded palette after KB8/KB4 blits");
    const auto end=decode_hok(read_file(refs/"SAISEND.HOK"));
    const auto negative=classic_stadium_view(end,f,t,{false,9,9,35});
    require(negative.native_cash=="-253.521 DM" && negative.quote && negative.quote->affordable &&
            negative.native_quote_target=="20.500" && negative.quote_color==8,
            "original negative-cash bitpattern is still unsigned-affordable in native quote, with signed cash display");
}
} // namespace
int main(int argc,char** argv) {
    try {
        values_and_overlays();native_hits();quotes_and_steps();
        if(argc>1) originals(argv[1],argc>2?std::filesystem::path(argv[2]):std::filesystem::path(argv[1]));
        std::puts("Native classic stadium view tests passed");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"Classic stadium: %s\n",e.what());return 1;}
}
