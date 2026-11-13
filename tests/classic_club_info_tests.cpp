// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic club-info popup (ui/classic_club_info.cpp): the caption with table place and
// strength, home/away balances, recent results and goal averages, winning/losing series, record wins and
// defeats, the head-to-head results against the manager's club, the all-time points, the rank graph of the
// season and the variants for national and foreign teams. The model is pure: it never writes to the state.
//
// Usage: classic_club_info_tests [DATA] — DATA is a folder with START.HOK and MONAT1.HOK (and the graphics
// for drawing); without it only the synthetic cases run.
#include "ui/classic_club_info.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `condition` is false.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// State offset of the team record of `team` (61 bytes each; name at +0, six strength bytes at +0x18, crest and
// reporting mode at +0x34).
constexpr size_t team_record(int team) { return 0x17e7+size_t(team)*61; }
// State offset of the league table row of `team` (80 bytes each): +7 the last eight results as letters,
// +0x19/+0x1A and +0x1D/+0x1E goals, +0x21.. games/wins/losses per side, +0x31 place, +0x4C all-time points.
constexpr size_t table(int team) { return 0x3ba5+size_t(team)*80; }
// Writes a little-endian u16 at state offset `p`.
void put16(HokFile& h, size_t p, unsigned value) {
    h.state.at(p)=uint8_t(value); h.state.at(p+1)=uint8_t(value>>8);
}
// A synthetic state: manager 0 at team 7, a national manager (block 5) at team 52, 150 teams "T<n>" of
// strength 80, empty head-to-head bytes (255) and matchday 1 in every league.
HokFile fixture() {
    HokFile h;
    h.state.assign(0xbb00,0);
    h.state.at(0x068c)=52;
    h.state.at(0x0dcd+0x14)=7;
    h.state.at(0x0dcd+5*0x1af+0x14)=52;
    h.state.at(0x8e2d)=1;
    std::fill_n(h.state.begin()+0x8f88,4*640,uint8_t(255));
    for (int team=0;team<150;++team) {
        const auto name="T"+std::to_string(team);
        std::copy(name.begin(),name.end(),h.state.begin()+team_record(team));
        std::fill_n(h.state.begin()+team_record(team)+0x18,6,uint8_t(80));
    }
    h.matchday[0]=h.matchday[1]=h.matchday[2]=1;
    return h;
}
// The text row drawn at (x, baseline y); throws if there is none.
const ClassicClubInfoText& at(const ClassicClubInfo& info, int x, int y) {
    for (const auto& row:info.rows) if (row.x==x && row.baseline==y) return row;
    throw std::runtime_error("missing native club-info text position");
}
// The first text row whose text starts with `prefix`; throws if there is none.
const ClassicClubInfoText& starts(const ClassicClubInfo& info, const std::string& prefix) {
    for (const auto& row:info.rows) if (row.native.find(prefix)==0) return row;
    throw std::runtime_error("missing native club-info text");
}
// Caption, balances, recent results, goal averages, series and the empty record/head-to-head texts computed
// from raw table bytes.
void raw_layout_and_arithmetic() {
    auto h=fixture();
    const auto p=table(0);
    h.state.at(p+0x31)=10;
    h.state.at(p+0x21)=7; h.state.at(p+0x29)=4; h.state.at(p+0x2d)=2;
    h.state.at(p+0x22)=3; h.state.at(p+0x2a)=1; h.state.at(p+0x2e)=0;
    h.state.at(p+0x19)=29; h.state.at(p+0x1a)=18;
    h.state.at(p+0x1d)=6; h.state.at(p+0x1e)=7;
    const char history[]="sUn-NuS?";
    std::copy_n(history,8,h.state.begin()+p+7);
    auto info=classic_club_info(h,0);
    require(info.native_caption=="11. Platz in der Bundesliga, St{rke: 80",
            "caption has native period,league grammar and strength width1");
    require(at(info,195,109).native=="4 Siege, 1 Unent., 2 Nied." &&
            at(info,195,115).native=="1 Siege, 2 Unent., 0 Nied.",
            "balances derive draws from raw games minus wins/losses");
    require(at(info,98,115).native=="Ausw{trsbilanz:","preserve the original's misspelling");
    require(info.faces==std::array<uint8_t,8>{3,2,1,0,1,2,3,0} && info.summary_face==2,
            "result faces preserve empty history slots and count both result cases");
    require(at(info,195,121).native=="6:6 Punkte","recent points include only6 recognized results");
    require(at(info,195,127).native=="4.7:1.3","goal average truncates tenths from byte sums,not average of averages");
    require(at(info,103,139).color==6 && at(info,103,139).native=="Keine bemerkenswerte Serie vorhanden.",
            "empty streak still advances one6pixel row");
    require(at(info,183,147).native=="Noch keine Eintragung." && at(info,183,165).native=="Noch keine Eintragung.",
            "four zero records retain home placeholder without fabricated H/A suffix");
    require(at(info,98,173).native=="Historische Ergebnisse gegen T7" &&
            at(info,98,179).native=="H:  Noch keine -- A:  Noch keine",
            "empty head-to-head has both exact double spaces");
    require(at(info,98,187).color==6,"zero all-time points is grey at baseline187");
    // Five qualifying series (series table at state 0x9988, three bytes per kind: overall, home, away): only
    // the first four are shown, the overall column 0 is used, numbers have width 2.
    for (int k=0;k<5;++k) h.state.at(0x9988+size_t(k)*3)=uint8_t(k+2);
    h.state.at(0x9988+1)=99;
    info=classic_club_info(h,0);
    require(at(info,98,139).native=="%2 Spiele in Folge gewonnen" &&
            at(info,98,157).native=="%5 Spiele in Folge nicht gewonnen" &&
            at(info,183,165).native=="Noch keine Eintragung." &&
            starts(info,"Historische Ergebnisse").baseline==191,
            "four-series limit and moving record/head-to-head baselines");
    std::fill_n(h.state.begin()+team_record(0)+0x18,6,uint8_t(255));
    require(classic_club_info(h,0).native_caption.find("255")!=std::string::npos,
            "mean of six raw strength bytes has no99 clamp");
}
// Record wins/defeats (state 0xA11C: u16 scores for home and away, opponent ids at 0x9F1C), the head-to-head
// line and the all-time points.
void native_records_and_results() {
    auto h=fixture();
    const size_t r=0xa11c;
    // The original selects the record by comparing only the low byte of each stored score, nibble by nibble.
    // The high byte 0x14 is deliberately larger but must not win the conventional way.
    put16(h,r,0x1401); put16(h,r+8,0x0302);
    h.state.at(0x9f1c)=1; h.state.at(0x9f1c+4)=2;
    require(at(classic_club_info(h,0),183,147).native=="%2:%3 (T2) (A)",
            "record selection compares low bytes 01 vs02,not20:1 vs3:2");
    put16(h,r,0x0500); put16(h,r+8,0);
    require(at(classic_club_info(h,0),183,147).native=="%5:%0 (T1)",
            "both return bytes0 retains nonempty home text and omits H suffix");
    put16(h,r,0x0204); put16(h,r+8,0x0104);
    require(at(classic_club_info(h,0),183,147).native=="%4:%1 (T2) (A)",
            "equal low-byte comparison chooses away and reverses its score bytes");
    put16(h,r+4,0x0800); put16(h,r+12,5);
    require(at(classic_club_info(h,0),183,159).native=="%5 (T0) (A)",
            "scalar record compares low return0 vs5 even though home formatted value8");
    // Head-to-head bytes (state 0x8F88) hold one result per nibble pair; the popup shows HIGH:LOW in ascending
    // rounds, and a gap at round 0 yields a leading comma.
    h.state.at(0x8f88+1)=0x21;
    h.state.at(0x8f88+2)=0x43;
    h.state.at(0x8f88+5)=0x65;
    require(at(classic_club_info(h,0),98,179).native=="H: 2:1,6:5 -- A: ,4:3",
            "head-to-head preserves ascending rounds,side offset and leading comma after missing first round");
    // All-time points are read from table row +0x4C; the preceding u16 penalty field is unrelated.
    put16(h,table(0)+0x4a,900);
    h.state.at(table(0)+0x4c)=123;
    const auto info=classic_club_info(h,0);
    require(at(info,98,187).native=="Punkte in der Ewigen Tabelle: 123" && at(info,98,187).color==1,
            "all-time points is the32bit field+4C,not nearby penalties");
}
// The rank graph (packed ranks per matchday at table row +0x34), its end label, the national-team and foreign
// variants, crest/reporting bits, purity and the rejection of invalid team ids or truncated states.
void graph_and_variants() {
    auto h=fixture();
    h.matchday[0]=3;
    h.state.at(0x3bd9)=0xb0;
    auto info=classic_club_info(h,0);
    require(info.graph.size()==4 && info.graph_labels.size()==1,"single-team graph has two axes plus2 segments");
    const auto& a=info.graph[2]; const auto& b=info.graph[3];
    require(a.x1==126 && a.y1==63 && a.x2==167 && a.y2==63 &&
            b.x1==167 && b.y1==63 && b.x2==209 && b.y2==85,
            "graph has exact translated origin,first shorter segment and packed low/high ranks");
    require(info.graph_labels[0].x==211 && info.graph_labels[0].baseline==85 &&
            info.graph_labels[0].native=="12.","graph end label is beyond curve by2 pixels");
    h.state.at(0x0dcd+0xeb)=1; h.matchday[1]=45;
    info=classic_club_info(h,0);
    require(info.graph_labels[0].x==210 && info.graph_labels[0].baseline==84,
            "rank10..12 nudge uses active manager stored division,not clicked team division");
    h.state.at(0x0788)=5; h.state.at(0x0dcd+5*0x1af+0xeb)=4;
    h.state.at(0x8f84)=3;
    info=classic_club_info(h,0);
    require(info.graph_labels[0].x==210 && info.graph_labels[0].baseline==84,
            "native eight-byte matchday counter array supports national-manager league4 and limit2");
    h.state.at(0x0788)=0;
    h.matchday[0]=46;
    std::fill_n(h.state.begin()+0x3bd9,23,uint8_t(0xbb));
    info=classic_club_info(h,0);
    require(info.graph.size()==47 && info.graph.back().x2==215,
            "source counts45 rounds and promotes step1 to2; no invented44-round clipping");
    h.matchday[0]=128;
    require(classic_club_info(h,0).graph.size()==2,"signed next-matchday128 yields axes only and no pregame caption");
    h.state.at(table(52)+0x21)=2; h.state.at(table(52)+0x22)=3;
    h.state.at(table(52)+0x29)=1; h.state.at(table(52)+0x2a)=2;
    h.state.at(table(52)+0x2d)=1;
    info=classic_club_info(h,52);
    require(info.graph.empty() && !info.schedule && info.has_results && info.native_caption=="St{rke:80",
            "national path keeps faces/box but no ranking axes,schedule or league standing");
    require(at(info,195,109).native=="3 Siege, 1 Unent., 1 Nied." &&
            at(info,98,115).native=="Aus d. letzten Spielen:","national balance sums sides and shifts later rows6px");
    info=classic_club_info(h,64);
    require(info.crest==143 && !info.has_results && info.graph.empty() && info.rows.size()==3 &&
            info.rows[2].native.front()==char(0xe4) && info.rows[2].baseline==138,
            "foreign screen preserves unsupported originalE4 byte and exact three-message branch");
    h.state.at(team_record(0)+0x34)=0x85;
    require(classic_club_info(h,0).crest==85 && classic_club_info(h,0).reporting_mode==2,
            "crest low6bits and camera high2bits are separate");
    const auto before=h.state;
    (void)classic_club_info(h,0);
    require(h.state==before,"pure model does not run the entry cache writes or consume RNG");
    for (int team:{-1,150}) {
        bool rejected=false; try {(void)classic_club_info(h,team);} catch(const FormatError&) {rejected=true;}
        require(rejected,"reject nonnative team IDs");
    }
    h.state.resize(3);
    bool rejected=false; try {(void)classic_club_info(h,0);} catch(const FormatError&) {rejected=true;}
    require(rejected,"truncated raw state is a safe format error");
}
// The popup for real saves: ESV Kaufbeuren in START.HOK (drawn over a filled canvas to check the frame) and
// the manager's club in MONAT1.HOK, overall and per side.
void original_examples(const std::filesystem::path& data) {
    auto h=decode_hok(read_file(data/"START.HOK"));
    const auto before=h.state;
    auto info=classic_club_info(h,4);
    // In START, ESV has overall strength 78 and rank 1; the original shows exactly this caption.
    require(info.native_name=="ESV KAUFBEUREN" &&
            info.native_caption=="1. Platz in der Bundesliga, St{rke: 78" &&
            at(info,195,127).native=="0.0:0.0", "original START ESV fields differ from the original's popup");
    ClassicCanvas c(data); c.rect(0,0,319,239,83);
    draw_classic_club_info(c,h,4);
    require(h.state==before,"drawing never mutates original HOK");
    for (int y=0;y<240;++y) for (int x=0;x<320;++x)
        if (x<93 || x>308 || y<44 || y>233)
            require(c.pixels().at(x,y)==83,"popup preserves all pixels outside native frame");
    require(c.pixels().at(125,62)==6 && c.pixels().at(221,86)==8,
            "native empty graph axes use translated coordinates and colors");
    h=decode_hok(read_file(data/"MONAT1.HOK"));
    const auto home=classic_club_info(h,0,0),away=classic_club_info(h,0,1);
    require(at(home,195,127).native=="4.4:2.0"&&at(away,195,127).native=="2.7:4.0","native home/away goals use separate unsigned raw columns");
    require(at(home,98,133).native=="Serien (Heim):  "&&at(away,98,133).native=="Serien (Ausw{rts):  ","native mode caption bytes");
    info=classic_club_info(h,0);
    // The raw MONAT1 bytes behind these texts: table bytes 0x21/0x22 = 5/4, goals for 22+11, against 10+16;
    // series kind 4 overall 5; strength bytes sum 435 (mean 435/6).
    require(info.native_caption=="3. Platz in der Bundesliga, St{rke: 72" &&
            at(info,195,109).native=="4 Siege, 1 Unent., 0 Nied." &&
            at(info,195,115).native=="1 Siege, 1 Unent., 2 Nied." &&
            at(info,195,121).native=="10:6 Punkte" && at(info,195,127).native=="3.6:2.8" &&
            at(info,98,139).native=="%5 Spiele in Folge nicht verloren",
            "MONAT1 raw arithmetic differs from independently extracted original bytes");
}
// The home (mode 0) and away (mode 1) variants of the popup: series columns, phrases and record wins.
void side_modes(){
    auto h=fixture();
    h.state.at(0x9988+1)=3;h.state.at(0x9988+4*3+2)=2;
    put16(h,0xA11C,0x0207);put16(h,0xA11C+8,0x0207);
    const auto home=classic_club_info(h,0,0),away=classic_club_info(h,0,1);
    require(at(home,98,139).native=="%3 Spiele in Folge zuhause gewonnen","mode0 selects home streak column and native phrase order");
    require(at(away,98,139).native=="%2 Spiele in Folge ausw{rts nicht verloren","mode1 selects away streak column");
    require(at(home,98,147).native=="H|chster H-Sieg"&&at(home,183,147).native=="%2:%7 (T0)","home record is direct, without overall comparison/suffix");
    require(at(away,98,147).native=="H|chster A-Sieg"&&at(away,183,147).native=="%7:%2 (T0)","away record reverses score bytes without overall suffix");
}
} // namespace

int main(int argc,char** argv) {
    try {
        raw_layout_and_arithmetic(); native_records_and_results(); graph_and_variants();side_modes();
        if (argc>1) original_examples(argv[1]);
        std::printf("Native classic club-info tests passed\n"); return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Classic club info: %s\n",e.what()); return 1;
    }
}
