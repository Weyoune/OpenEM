// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic world championship pages (ui/classic_wm.cpp): pages 11 and 12 show the standings of
// the two groups of six nations, pages 8 and 9 their fixtures per round, page 10 the knockout round with all ties
// played so far. Checked are the saved table order and raw values, flags and borders, click areas, the preview
// date and the working date written for it, and the pages of real saves.
//
// Usage: classic_wm_tests [DATA [REFERENCE]] — with DATA (EM.EXE and graphics) and REFERENCE (START.HOK and,
// when present, WMCHECK.HOK and WMPHASE.HOK) the real saves are checked as well.
//
// State fields used: 0x03B8 the group schedules (0x24 per group, 6 bytes per round), 0x04D8 the group results
// (same layout), 0x05F8 the table order (6 per group), 0x02BB/0x02EB/0x0628 the table values, 0x8F85 the WM
// phase, 0x8EB1 the knockout pairings, 0x8F60 their results, 0x03B7 the knockout cursor.
#include "ui/classic_wm.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `value` is false.
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// Writes `value` little-endian as 4 bytes at state offset `at`.
void put32(HokFile& h,size_t at,uint32_t value) {
    for(size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(value>>(8*i));
}
// A state on 10.09.1993 with nations 52..57 and 58..63 in the two groups, the flags of START (team record +0x34
// holds the flag picture minus 80), five rounds of schedule and no knockout pairings.
HokFile fixture() {
    HokFile h; h.state.assign(47761,0);
    put32(h,0x8e21,10); put32(h,0x8e25,8); put32(h,0x8e29,1993);
    put32(h,0x8e2d,1); put32(h,0x8e31,1993); h.date={10,9,1993,1};
    // The flag pictures of the twelve WM nations as START holds them.
    constexpr std::array<int,12> badges{134,125,127,142,130,122,138,126,139,131,141,128};
    for(int team=0;team<64;++team) {
        const auto text="NATION"+std::to_string(team);
        std::copy(text.begin(),text.end(),h.state.begin()+0x17e7+size_t(team)*61);
    }
    for(int g=0;g<2;++g) {
        for(int i=0;i<6;++i) {
            h.state.at(0x05f8+size_t(g)*6+size_t(i))=uint8_t(i);
            h.state.at(0x17e7+size_t(52+g*6+i)*61+0x34)=uint8_t(badges[size_t(g*6+i)]-80);
        }
        for(int r=0;r<5;++r) for(int i=0;i<6;++i)
            h.state.at(0x03b8+size_t(g)*0x24+size_t(r)*6+size_t(i))=uint8_t(52+g*6+i);
    }
    std::fill_n(h.state.begin()+0x8eb1,32,255);
    return h;
}
// A calendar with one WM day (event 0x10) on day 217 and the end of season on day 233.
SeasonCalendar calendar() {
    SeasonCalendar c; c.events.assign(234,0); c.events[217]=0x10; c.events[233]=0xff; return c;
}
// Standings pages: rows, flags, number widths, click areas and raw table values.
void table_fields_and_hits() {
    auto h=fixture(); auto c=calendar(); const auto before=h.state;
    const auto v=classic_wm_view(h,c);
    require(v.page==11 && v.group==0 && v.phase==0 && v.native_title=="Gruppe 1",
            "native table frontdoor is page11, group1");
    require(v.table[0].team==52 && v.table[0].badge==134 && v.table[1].badge==125 &&
            v.table[5].badge==122,"flags use original raw crest IDs, not nation-ID arithmetic");
    require(v.table[0].native_rank=="1." && v.table[0].native_stats=="%0:%0 %0:%0" &&
            v.table[0].baseline==57 && v.table[5].baseline==197,
            "NewBold row geometry and explicit width2 statistics");
    require(v.table[0].badge_x==46 && v.table[1].badge_x==102 &&
            v.table[0].color==7 && v.table[1].color==30,"alternating flag placement/foreground");
    require(classic_wm_hit(v,0,42).action==ClassicWmAction::None &&
            classic_wm_hit(v,319,43).team==52 && classic_wm_hit(v,1,69).team==52 &&
            classic_wm_hit(v,319,70).team==53 && classic_wm_hit(v,2,209).team==57 &&
            classic_wm_hit(v,1,210).action==ClassicWmAction::None,
            "standing rows accept any x, exact consecutive native y intervals");
    require(classic_wm_hit(v,237,211).action==ClassicWmAction::Page &&
            classic_wm_hit(v,293,222).page==12 &&
            classic_wm_hit(v,236,211).action==ClassicWmAction::None &&
            classic_wm_hit(v,294,222).action==ClassicWmAction::None,
            "standings manual footer has wider left hit edge than generic button helper");
    const auto wide=classic_wm_view(h,c,11,-1,3);
    require(wide.native_title=="Gruppe %%1" && wide.table[0].native_rank=="%%1." &&
            wide.table[1].native_rank=="2." && wide.buttons[0].native=="Gruppe %%2",
            "caller width inherited by title/footer/first rank, subsequent rank uses reset1");
    require(classic_wm_view(h,c,12).table[0].team==58 &&
            classic_wm_view(h,c,12).buttons[0].hit.page==11,"second standings raw block and navigation");
    require(h.state==before,"table raw views do not change state");
    h.state.at(0x8f85)=7; h.state.at(0x05f8)=4;
    h.state.at(0x0628+4)=255; h.state.at(0x02bb+4)=200; h.state.at(0x02eb+4)=150;
    h.state.at(0x17e7+56*61+0x16)=1;
    const auto raw=classic_wm_view(h,c);
    require(raw.table[0].team==56 && raw.table[0].points==255 && raw.table[0].minus_points==-241 &&
            raw.table[0].native_stats=="255:-241 200:150" && raw.table[0].color==7,
            "saved permutation, unsigned stats, uncapped phase minuspoints and no owner highlight");
}
// Fixture pages: the round shown, raw scores, flag borders of owned nations, click areas and navigation.
void fixture_fields_and_hits() {
    auto h=fixture(); h.state.at(0x8f85)=6;
    constexpr std::array<uint8_t,6> last{56,52,53,54,55,57};
    std::copy(last.begin(),last.end(),h.state.begin()+0x03b8+24);
    h.state.at(0x04d8+24)=255; h.state.at(0x04d8+25)=128;
    h.state.at(0x17e7+52*61+0x16)=6;
    const auto before=h.state; const auto v=classic_wm_view(h,SeasonCalendar{},8);
    require(v.round==4 && v.fixtures[0].home==56 && v.fixtures[0].away==52 &&
            v.fixtures[0].scores==std::array<int,2>{255,128} &&
            v.fixtures[0].native_scores==std::array<std::string,2>{"255","128"},
            "fixture default clamps phase to round5 and displays unsigned bytes without filtering");
    require(v.fixtures[0].home_border==7 && v.fixtures[0].away_border==18 &&
            v.fixtures[2].top==125 && v.native_title=="Gruppe 1   5.Spieltag",
            "flag borders follow nonzero owner and three exact rows");
    require(classic_wm_hit(v,125,40).team==56 && classic_wm_hit(v,126,40).team==52 &&
            classic_wm_hit(v,319,81).team==52 && classic_wm_hit(v,0,82).team==53 &&
            classic_wm_hit(v,319,167).team==57 &&
            classic_wm_hit(v,125,39).action==ClassicWmAction::None,
            "fixture hit y edges and asymmetric x125 split");
    require(classic_wm_hit(v,39,180).round==3 && classic_wm_hit(v,85,218).round==3 &&
            classic_wm_hit(v,86,218).action==ClassicWmAction::None &&
            classic_wm_hit(v,234,180).round==0 && classic_wm_hit(v,280,218).round==0,
            "native arrow hitboxes and local round wrapping");
    require(classic_wm_hit(v,104,180).page==10 && classic_wm_hit(v,152,218).page==10 &&
            classic_wm_hit(v,169,180).page==9 && classic_wm_hit(v,217,218).page==9,
            "Endrunde and other-group footer inclusive boundaries");
    require(classic_wm_view(h,SeasonCalendar{},9,0).fixtures[0].home==58 &&
            classic_wm_view(h,SeasonCalendar{},9,0).buttons[2].hit.page==8,
            "second fixture group has independent native schedule block");
    require(h.state==before,"fixture view does not mutate/sort/book or need calendar");
    h.state.at(0x8f85)=0;
    const auto wide=classic_wm_view(h,SeasonCalendar{},8,0,2,5);
    require(wide.active_manager==5 && wide.native_title=="Gruppe %1   %1.Spieltag" &&
            wide.fixtures[0].native_scores[0]=="%0" && wide.buttons[2].native=="Gruppe %2",
            "first fixture paint exposes caller width and manager5 trophy omission metadata");
}
// Knockout page: placeholder and preview date, the working date, the complete history of ties with headings,
// result decoding and click areas.
void knockout_history_and_date() {
    auto h=fixture(); const auto c=calendar(); const auto original=h.state;
    auto v=classic_wm_view(h,c,10);
    require(!v.available && v.knockout.empty() && v.frame_top==70 && v.frame_rows==5 &&
            v.preview_calendar_index==217 && v.preview_date && v.preview_date->day==14 &&
            v.preview_date->month==4 && v.preview_date->year==1994,
            "early KO placeholder still scans original next WM date and uses native frame geometry");
    require(classic_wm_hit(v,136,201).action==ClassicWmAction::PreviousFixtures &&
            classic_wm_hit(v,184,239).action==ClassicWmAction::PreviousFixtures &&
            classic_wm_hit(v,185,239).action==ClassicWmAction::None,
            "KO footer preserves controller previous fixture page instead of inventing direct page");
    require(h.state==original,"KO preview date remains pure until explicit prepare");
    require(prepare_classic_wm_working(h,c) && le32(h.state,0x8e21)==14 &&
            le32(h.state,0x8e25)==3 && le32(h.state,0x8e29)==1994 &&
            h.date.day==14 && h.date.month==4 && h.date.year==1994 && h.date.day_index==1,
            "explicit prepare commits derived working date and preserves current day counter");
    for(size_t i=0;i<h.state.size();++i) if(h.state[i]!=original[i])
        require(i>=0x8e21 && i<0x8e2d,"only three derived date fields can change");
    const auto prepared=h.state;
    require(!prepare_classic_wm_working(h,SeasonCalendar{},11) && h.state==prepared,
            "standing prepare is a true no-op even without schedule/calendar");
    put32(h,0xb953,210); //cursor is read16, adjacent bytes do not matter here
    require(!prepare_classic_wm_working(h,SeasonCalendar{},10),"calendar threshold209 is inclusive");
    h.state.at(0xb953)=209; h.state.at(0xb954)=0;
    auto mixed=c; mixed.events[209]=0x11;
    require(classic_wm_view(h,mixed,10).preview_calendar_index==209,"combined WM flag is accepted");
    mixed.events[209]=0xff;
    require(classic_wm_view(h,mixed,10).preview_calendar_index==209,"FF satisfies native shifted bit test");
    const auto before_fail=h.state; bool rejected=false;
    try {(void)prepare_classic_wm_working(h,SeasonCalendar{},10);} catch(const FormatError&) {rejected=true;}
    require(rejected && h.state==before_fail,"missing calendar is rejected before any date writes");

    h.state.at(0x8f85)=7; h.state.at(0x03b7)=16; h.state.at(0xb953)=218;
    for(int pair=0;pair<12;++pair) {
        h.state.at(0x8eb1+size_t(pair)*2)=uint8_t(52+pair%6);
        h.state.at(0x8eb1+size_t(pair)*2+1)=uint8_t(58+pair%6);
        h.state.at(0x8f60+size_t(pair)*2)=0; h.state.at(0x8f60+size_t(pair)*2+1)=0;
    }
    h.state.at(0x8f60)=67; h.state.at(0x8f61)=31; //right marker overwrites2 with1
    h.state.at(0x8f62)=100; h.state.at(0x8f63)=0;
    h.state.at(0x17e7+52*61+0x16)=1;
    v=classic_wm_view(h,SeasonCalendar{},10);
    require(v.available && v.pair_count==12 && v.knockout.size()==12 &&
            v.frame_top==2 && v.frame_rows==10 && v.headings.size()==7,
            "WM KO scan counts total history, not only current phase block");
    constexpr std::array<int,7> headings{48,83,104,125,139,153,167};
    for(size_t i=0;i<headings.size();++i) require(v.headings[i].baseline==headings[i],
            "the original's seven placement-heading coordinates");
    constexpr std::array<int,12> rows{55,62,69,76,90,97,111,118,132,146,160,174};
    for(size_t i=0;i<rows.size();++i) require(v.knockout[i].baseline==rows[i],
            "KO flattened tie ordering preserves all twelve historical rows");
    require(v.knockout[0].native_score=="%7:%1" && v.knockout[0].markers==1 &&
            v.knockout[1].native_score.empty() && v.knockout[1].markers==0,
            "30/60 decoding, right override and100 sentinel are KO-only");
    require(classic_wm_hit(v,99,50).team==52 && classic_wm_hit(v,100,55).team==58 &&
            classic_wm_hit(v,99,50).mode==2 && classic_wm_hit(v,100,55).mode==2 &&
            classic_wm_hit(v,99,49).action==ClassicWmAction::None,
            "KO six-pixel hits split99/100 and force national club-info mode2 for both sides");
}
// Expected standings: per rank {team, index in group, points, minus points, goals for, goals against, flag}.
using Rows=std::array<std::array<int,7>,6>;
// Requires the six table rows of `v` to equal `expected`.
void table_vector(const ClassicWmView& v,const Rows& expected) {
    for(size_t i=0;i<6;++i) {
        const auto& r=v.table[i];
        require(std::array<int,7>{r.team,r.index,r.points,r.minus_points,r.goals_for,r.goals_against,r.badge}==expected[i],
                "original HOK differs from the original's WM table");
    }
}
// Standings, fixtures and knockout of START and, when present, WMCHECK (end of the groups) and WMPHASE (semifinals).
void original_files(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto c=load_season_calendar(read_file(data/"EM.EXE"));
    const auto h=decode_hok(read_file(refs/"START.HOK")); const auto before=h.state;
    table_vector(classic_wm_view(h,c,11),{{{52,0,0,0,0,0,134},{53,1,0,0,0,0,125},
        {54,2,0,0,0,0,127},{55,3,0,0,0,0,142},{56,4,0,0,0,0,130},{57,5,0,0,0,0,122}}});
    table_vector(classic_wm_view(h,c,12),{{{58,0,0,0,0,0,138},{59,1,0,0,0,0,126},
        {60,2,0,0,0,0,139},{61,3,0,0,0,0,131},{62,4,0,0,0,0,141},{63,5,0,0,0,0,128}}});
    ClassicCanvas canvas(data);
    for(const int page:{8,9,10,11,12}) draw_classic_wm(canvas,h,c,page);
    require(h.state==before,"painting all WM pages leaves original state entirely unchanged");
    if(std::filesystem::exists(refs/"WMCHECK.HOK")) {
        const auto q=decode_hok(read_file(refs/"WMCHECK.HOK"));
        table_vector(classic_wm_view(q,c,11),{{{52,0,8,2,29,9,134},{56,4,7,3,16,13,130},
            {57,5,5,5,18,14,122},{53,1,4,6,16,21,125},{55,3,4,6,16,27,142},{54,2,2,8,13,24,127}}});
        const auto ko=classic_wm_view(q,c,10);
        require(ko.pair_count==4 && ko.frame_top==70 && ko.frame_rows==5 &&
                ko.knockout[1].home==56 && ko.knockout[1].away==63,
                "original quarterfinal draw vector and frame");
        const auto f=classic_wm_view(q,c,9);
        require(f.round==4 && f.fixtures[0].home==62 && f.fixtures[0].away==58 &&
                f.fixtures[0].scores==std::array<int,2>{6,0},"original last group2 fixture vector");
    }
    if(std::filesystem::exists(refs/"WMPHASE.HOK")) {
        const auto s=decode_hok(read_file(refs/"WMPHASE.HOK"));
        const auto ko=classic_wm_view(s,c,10);
        require(ko.pair_count==8 && ko.frame_top==51 && ko.frame_rows==7 &&
                ko.knockout[1].home==63 && ko.knockout[1].away==56 &&
                ko.knockout[1].native_score=="%5:%4",
                "winner-first saved orientation, historical score and semifinal native layout");
        table_vector(classic_wm_view(s,c,12),{{{62,4,9,3,21,4,141},{61,3,6,6,23,11,131},
            {63,5,6,6,16,15,128},{58,0,6,6,12,20,138},{59,1,3,9,11,20,126},{60,2,0,12,6,19,139}}});
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        table_fields_and_hits(); fixture_fields_and_hits(); knockout_history_and_date();
        if(argc>1) original_files(argv[1],argc>2?std::filesystem::path(argv[2]):std::filesystem::path(argv[1]));
        std::puts("Native classic WM raw-view tests passed"); return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"Classic WM: %s\n",e.what()); return 1;}
}
