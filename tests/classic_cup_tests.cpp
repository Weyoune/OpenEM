// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic DEB cup page (ui/classic_cup.cpp): the pairings of the current round grouped by
// the leagues of the two clubs, the centred frame, the caption with round name and preview date, the click
// areas that open the club info of the home or away team, the result columns of earlier games of a series,
// and the rejection of invalid states. The view must not change the state; the preview date is a separate
// working copy owned by the controller.
//
// Usage: classic_cup_tests [DATA [REFERENCE]] — with DATA (EM.EXE and graphics) and REFERENCE (MONAT1, START,
// CUPCHECK, CUPPOST, CUPYEAR and SAISEND .HOK) the pages of real saves are checked as well.
//
// State fields used: 0x8E51 the 16 pairings (home, away; 255 = free), 0x8F84 round number shown in the
// caption, 0xB953 calendar cursor, 0x8E21..0x8E2D date (month, day, year, season day as u32), 0x031B number
// of games played in the round and from 0x031C the result history (two bytes per pairing, 32 bytes per game).
#include "ui/classic_cup.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `condition` is false.
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Writes `value` little-endian as 4 bytes at state offset `at`.
void put32(HokFile& h,size_t at,uint32_t value) {
    for (size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(value>>(8*i));
}
// A synthetic state with six pairings, one for each league combination (teams 0..11 Bundesliga, 12..23 second
// division, 24.. Oberliga), the first round, two managers at teams 3 and 12, and teams named "T<n>".
HokFile fixture() {
    HokFile h; h.state.assign(47761,0);
    std::fill_n(h.state.begin()+0x8e51,32,uint8_t(255));
    constexpr std::array<std::array<int,2>,6> draw{{{0,1},{13,2},{24,3},{12,13},{14,25},{26,27}}};
    for (size_t i=0;i<draw.size();++i) {
        h.state.at(0x8e51+i*2)=uint8_t(draw[i][0]);
        h.state.at(0x8e52+i*2)=uint8_t(draw[i][1]);
    }
    h.state.at(0x8f84)=1; h.state.at(0x8ed1)=1;
    h.state.at(0xb953)=1;
    h.state.at(0x8e31)=0xc9; h.state.at(0x8e32)=7;
    put32(h,0x8e2d,1); put32(h,0x8e21,10); put32(h,0x8e25,8); put32(h,0x8e29,1993);
    h.state.at(0x0dc6)=2;
    h.state.at(0x0dcd+0x14)=3;
    h.state.at(0x0dcd+0x1af+0x14)=12;
    for (int team=0;team<150;++team) {
        const auto text="T"+std::to_string(team);
        std::copy(text.begin(),text.end(),h.state.begin()+0x17e7+size_t(team)*61);
    }
    return h;
}
// A season calendar of 233 days with cup days (event bit 8) on calendar days 13 and 41.
SeasonCalendar calendar() {
    SeasonCalendar c; c.events.assign(233,0); c.events[13]=8; c.events[41]=8;
    return c;
}
// The shown row of pairing `index`; throws if the view does not contain it.
const ClassicCupRow& pair(const ClassicCupView& view,int index) {
    for (const auto& group:view.groups) for (const auto& row:group.rows)
        if (row.pair==index) return row;
    throw std::runtime_error("missing native Cup pairing");
}
// Grouping, frame size, headings, highlight colours of the managers' clubs, click areas and the caption.
void groups_frame_and_hitboxes() {
    auto h=fixture(); const auto c=calendar(); const auto before=h.state;
    auto view=classic_cup(h,c);
    require(h.state==before,"raw Cup view does not mutate its source HOK");
    require(view.pair_count==6 && view.frame_top==42 && view.frame_rows==8,
            "six occupied categories produce exact native centered frame");
    constexpr std::array<const char*,6> headings{
        "BUNDESLIGA GEGEN BUNDESLIGA:","BUNDESLIGA GEGEN ZWEITE LIGA:",
        "BUNDESLIGA GEGEN OBERLIGA:","ZWEITE LIGA GEGEN ZWEITE LIGA:",
        "ZWEITE LIGA GEGEN OBERLIGA:","OBERLIGA GEGEN OBERLIGA:"};
    for (size_t i=0;i<6;++i) {
        require(view.groups[i].native_heading==headings[i] && view.groups[i].heading_baseline==88+15*int(i),
                "original uppercase league pair labels and per-group8+7 advance");
        require(view.groups[i].rows.size()==1 && view.groups[i].rows[0].baseline==96+15*int(i),
                "row is8px below its group heading");
    }
    require(pair(view,1).home==13 && pair(view,1).away==2,
            "second-vs-first draw is grouped without swapping displayed home/away");
    require(pair(view,0).home_color==30 && pair(view,0).away_color==1 &&
            pair(view,2).away_color==24 && pair(view,3).home_color==24,
            "highlight uses each active manager club,normal sides30/1");
    h.state.at(0x17e7+0x16)=1;
    require(pair(classic_cup(h,c),0).home_color==30,"team owner alone does not highlight a Cup row");
    const auto& row=pair(view,0);
    require(row.hit_min_y==91 && row.hit_max_y==96,"native six-pixel row hit strip");
    require(classic_cup_hit(view,99,91).team==0 && classic_cup_hit(view,99,91).mode==0 &&
            classic_cup_hit(view,100,96).team==1 && classic_cup_hit(view,100,96).mode==1,
            "X99/100 split invokes correct home0/away1 club info");
    require(classic_cup_hit(view,319,94).team==1 && classic_cup_hit(view,-5,94).team==0,
            "original has no horizontal clamp; score-area click chooses away");
    require(classic_cup_hit(view,50,90).team<0 && classic_cup_hit(view,50,97).team<0 &&
            classic_cup_hit(view,50,view.groups[1].heading_baseline).team<0,
            "baseline-6,gaps and headings have no club click");
    require(view.native_caption=="Erste Runde    22.9." && view.preview_calendar_index==13 &&
            view.preview_date && view.preview_date->day==22 && view.preview_date->month==9,
            "caption uses original round trailing space plus3 literal spaces,day.month.");
    require(view.groups[0].rows[0].scores.empty(),"initial game0 has no displayed scores");
    // The canonical data has not received the preview date; the controller owns a separate working copy for
    // as long as the page is open, as the original does.
    require(le32(h.state,0x8e21)==10 && le32(h.state,0x8e25)==8,"raw view leaves original date intact");
    h.state.at(0x8f84)=2; h.state.at(0x8ed1)=5; h.cup_round=4;
    require(classic_cup(h,c).native_caption=="Achtelfinale    22.9.",
            "caption reads mirror8F84,not8ED1 or decoded cup_round");
    h.state.at(0xb953)=212;
    view=classic_cup(h,SeasonCalendar{});
    require(!view.preview_date && view.native_caption=="Achtelfinale ",
            "cursor beyond211 appends no date and does not inspect empty host calendar");
    h.state.at(0xb953)=1;
    auto ff=c; ff.events[1]=255;
    require(classic_cup(h,ff).preview_calendar_index==1,"Cupbit search includesFF exactly as the original");
}
// Result columns of a series (score bytes above 30/60 carry overtime/penalty flags), the end marker 100, the
// final-day exception and empty league categories.
void scores_flags_and_empty_groups() {
    auto h=fixture(); const auto c=calendar();
    h.state.at(0x031b)=1;
    h.state.at(0x031c)=4; h.state.at(0x031d)=2;
    h.state.at(0x031e)=61; h.state.at(0x031f)=32;
    auto view=classic_cup(h,c);
    const auto& first=pair(view,0).scores;
    require(first.size()==1 && first[0].x==207 && first[0].native=="%4:%2" && first[0].color==1,
            "G1 displays history0 atX207,not live scores or series wins");
    const auto& flag=pair(view,1).scores[0];
    require(flag.native=="%1:%2" && flag.markers==1,
            "right30 flag overwrites earlier left60 flag,not max(flags)");
    h.state.at(0x031e)=99; h.state.at(0x031f)=0;
    require(pair(classic_cup(h,c),1).scores[0].native=="39:%0" &&
            pair(classic_cup(h,c),1).scores[0].markers==2,
            "99 subtracts30 twice only; no invented third flag subtraction");
    h.state.at(0x031b)=2;
    h.state.at(0x031c+32)=63; h.state.at(0x031d+32)=4;
    h.state.at(0x8f00)=88; h.state.at(0x8f01)=99; h.state.at(0x037c)=2;
    view=classic_cup(h,c);
    require(pair(view,0).scores.size()==2 && pair(view,0).scores[0].x==197 &&
            pair(view,0).scores[1].x==233 && pair(view,0).scores[1].color==30 &&
            pair(view,0).scores[1].native=="%3:%4" && pair(view,0).scores[1].markers==2,
            "G2 two history columns197/233; type0phase0 ignores live and win arrays");
    h.state.at(0x031c)=100;
    require(pair(classic_cup(h,c),0).scores.empty(),"100 sentinel stops all later results for that pairing");
    h.state.at(0x031c)=4; h.state.at(0x031d+32)=100;
    require(pair(classic_cup(h,c),0).scores.size()==1,"right100 sentinel also ends history loop");
    h.state.at(0x031d+32)=4;
    put32(h,0x8e2d,218);
    h.state.at(0x031c+64)=5; h.state.at(0x031d+64)=1;
    view=classic_cup(h,c);
    require(pair(view,0).scores.size()==3 && pair(view,0).scores[2].x==269 &&
            pair(view,0).scores[2].color==1 && pair(view,0).scores[2].native=="%5:%1",
            "day218 exception includes current third result without decrement");
    // Only same-division matches: five categories remain and print an empty
    // line, but their extra rows must also enlarge the centering calculation.
    h=fixture(); std::fill_n(h.state.begin()+0x8e51,32,uint8_t(255));
    h.state.at(0x8e51)=0; h.state.at(0x8e52)=1;
    view=classic_cup(h,c);
    require(view.pair_count==1 && view.frame_top==42 && view.frame_rows==8 &&
            view.groups[1].heading_baseline==103 && view.groups[1].empty_baseline==111 &&
            view.groups[5].empty_baseline==171,"one pair+five empty categories keeps all six headings and blank lines");
    require(classic_cup_hit(view,100,111).team<0,"empty category hyphens are not a team row");
    // The original forces a pair count of at least 1, still classifies FF/FF as a pair for the frame height,
    // then skips the invalid left team while painting. Do not recount afterwards.
    std::fill_n(h.state.begin()+0x8e51,32,uint8_t(255));
    view=classic_cup(h,c);
    require(view.pair_count==1 && view.frame_top==42 && view.groups[0].empty_baseline==96,
            "count minimum occurs before empty-group height calculation");
    h=fixture(); h.state.at(0x03b4)=32;
    require(classic_cup(h,c).pair_count==16,"pair scan starts at the stored count (state 0x03B4) rather than always0");
}
// States the view refuses with a FormatError instead of reading undefined data.
void invalid_inputs() {
    auto h=fixture(); const auto c=calendar();
    bool rejected=false;
    try {(void)classic_cup(h,SeasonCalendar{});} catch(const FormatError&) {rejected=true;}
    require(rejected,"missing requested Cup event is explicit,not unbounded host scan");
    h.state.at(0x8e52)=255; rejected=false;
    try {(void)classic_cup(h,c);} catch(const FormatError&) {rejected=true;}
    require(rejected,"incomplete native pairing does not fabricate uninitialized away hit metadata");
    h=fixture(); h.state.at(0x031b)=4; rejected=false;
    try {(void)classic_cup(h,c);} catch(const FormatError&) {rejected=true;}
    require(rejected,"host cannot silently read beyond the native three historical score rows");
    h.state.resize(3); rejected=false;
    try {(void)classic_cup(h,c);} catch(const FormatError&) {rejected=true;}
    require(rejected,"truncated raw HOK is safely rejected");
}
// The page for real saves of several cup situations, checked against the original's screens, and the trophy
// icons cut from KB graphic 36.
void original_goldens(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto c=load_season_calendar(read_file(data/"EM.EXE"));
    auto h=decode_hok(read_file(refs/"MONAT1.HOK")); const auto before=h.state;
    const auto view=classic_cup(h,c);
    require(view.frame_top==32 && view.frame_rows==9 && view.pair_count==8 &&
            view.native_caption=="Achtelfinale    20.10." && view.preview_calendar_index==41,
            "fresh native em023 frame/caption and independent original EXE/calendar bytes");
    constexpr std::array<int,6> group_y{78,100,136,151,166,181};
    for (size_t i=0;i<6;++i) require(view.groups[i].heading_baseline==group_y[i],"MONAT1 six native category baselines");
    constexpr std::array<std::array<int,4>,8> expected{{
        {2,0,86,1},{6,7,93,24},{14,10,108,1},{19,9,115,1},
        {20,4,122,1},{12,1,129,1},{34,11,144,1},{18,17,159,1}}};
    size_t n=0; for (const auto& group:view.groups) for (const auto& row:group.rows) {
        require(std::array<int,4>{row.home,row.away,row.baseline,row.away_color}==expected.at(n++),
                "MONAT1 original raw draw grouping/order/highlight differs from the original's page");
        require(row.scores.empty(),"native fresh MONAT1 em023 has no scores in new round");
    }
    require(n==8 && view.groups[4].empty_baseline==174 && view.groups[5].empty_baseline==189,
            "MONAT1 native empty2v3/3v3 rows");
    ClassicCanvas canvas(data);
    draw_classic_cup(canvas,h,c);
    require(h.state==before,"full page drawing leaves original state and derived date unchanged");
    const auto& atlas=canvas.asset(36);
    for (int row=0;row<22;++row) for (int col=0;col<22;++col) {
        const auto pixel=atlas.pixels[size_t(row+22)*size_t(atlas.width)+size_t(col)];
        require(canvas.pixels().at(13+col,36+row)==pixel && canvas.pixels().at(286+col,36+row)==pixel,
                "both trophy icons use the independent originalKB36(0,22)22x22 crop");
    }
    h=decode_hok(read_file(refs/"START.HOK"));
    auto v=classic_cup(h,c);
    require(v.frame_top==13 && v.frame_rows==11 && v.groups[5].rows[0].baseline==212,
            "START16ties/no empty groups gives13top and11tile rows");
    h=decode_hok(read_file(refs/"CUPCHECK.HOK")); v=classic_cup(h,c);
    const auto& check=pair(v,7);
    require(check.home==7 && check.away==16 && check.home_color==24 && check.baseline==110 &&
            check.scores.size()==1 && check.scores[0].x==207 && check.scores[0].native=="%3:%4",
            "independent CUPCHECK bytes: swapped homeCologne,3:4 atX207");
    h=decode_hok(read_file(refs/"CUPPOST.HOK")); v=classic_cup(h,c);
    const auto& post=pair(v,7);
    require(post.home==16 && post.away==7 && post.away_color==24 && post.scores.size()==2 &&
            post.scores[0].x==197 && post.scores[0].native=="%4:%3" &&
            post.scores[1].x==233 && post.scores[1].native=="%3:%8",
            "CUPPOST preserves raw current orientation for both physically swapped score columns");
    h=decode_hok(read_file(refs/"CUPYEAR.HOK")); v=classic_cup(h,c);
    require(v.frame_top==4 && v.frame_rows==12 && v.groups[5].rows.back().baseline==217,
            "CUPYEAR16ties/two empty groups exercises height cap11 and top4");
    h=decode_hok(read_file(refs/"SAISEND.HOK")); v=classic_cup(h,c);
    require(v.frame_top==42 && v.frame_rows==8 && v.native_caption=="Endspiel " && !v.preview_date,
            "SAISEND cursor216 suppresses future date while retaining final title's trailing space");
}
} // namespace

int main(int argc,char** argv) {
    try {
        groups_frame_and_hitboxes(); scores_flags_and_empty_groups(); invalid_inputs();
        if (argc>1) original_goldens(argv[1],argc>2 ? argv[2] : argv[1]);
        std::printf("Native classic Cup raw tests passed\n"); return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Classic Cup: %s\n",e.what()); return 1;
    }
}
