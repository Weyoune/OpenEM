// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_cup.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini;
constexpr std::array<int,6> forward{9,10,12,18,34,36}; //DS02C9..02CE
constexpr std::array<int,6> reverse{9,17,33,18,20,36}; //DS02D0..02D5
constexpr std::array<std::string_view,6> headings{
    "BUNDESLIGA GEGEN BUNDESLIGA:","BUNDESLIGA GEGEN ZWEITE LIGA:",
    "BUNDESLIGA GEGEN OBERLIGA:","ZWEITE LIGA GEGEN ZWEITE LIGA:",
    "ZWEITE LIGA GEGEN OBERLIGA:","OBERLIGA GEGEN OBERLIGA:"};
//Original DS0B5C table. Normal Cup mirror0..5, followed by original shared
//competition labels; do not silently replace a mirror by decoded cup_round.
constexpr std::array<std::string_view,12> rounds{
    "","Erste Runde ","Achtelfinale ","Viertelfinale ","Halbfinale ","Endspiel ",
    "DEB-Pokal","Europapokal","Play-Off / Aufstiegsrunden","Weltmeisterschaft","Verletzt.","Gesperrt."};

const uint8_t* bytes(const HokFile& h,size_t at,size_t count) {
    if (at>h.state.size() || count>h.state.size()-at)
        throw FormatError("classic cup: truncated native state");
    return h.state.data()+at;
}
int byte(const HokFile& h,size_t at) { return *bytes(h,at,1); }
int league_flag(int team) { return team<12 ? 1 : team<24 ? 2 : team<=52 ? 4 : 1; }
std::string number(int value) {
    auto text=std::to_string(value);
    if (value>=0 && text.size()<2) text.insert(0,2-text.size(),'%');
    return text;
}
std::string team_name(const HokFile& h,int team) {
    const auto* p=bytes(h,0x17e7+size_t(team)*61,22);
    size_t count=0; while(count<22 && p[count]) ++count;
    return {reinterpret_cast<const char*>(p),count};
}
bool human(const HokFile& h,int team) {
    const int count=byte(h,0x0dc6);
    if (count>6) throw FormatError("classic cup: invalid human manager count");
    for (int manager=0;manager<count;++manager)
        if (byte(h,0x0dcd+size_t(manager)*0x1af+0x14)==team) return true;
    return false;
}
GameDate preview(const HokFile& h,int index) {
    uint32_t day=index ? uint32_t(index) : le32(h.state,0x8e2d);
    if (day==2000) day=0;
    if (day>uint32_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic cup: date outside host range");
    return date_of_season_day(int(day),le16(h.state,0x8e31));
}
std::vector<ClassicCupScore> scores(const HokFile& h,int pair,int game) {
    const bool final_day=le32(h.state,0x8e2d)==218;
    //42B0 decrements an8bit char, then sign-extends it for the loop test.
    const int last=int(static_cast<int8_t>(static_cast<uint8_t>(game-(final_day ? 0 : 1))));
    if (last>2) throw FormatError("classic cup: score history exceeds native three rows");
    std::vector<ClassicCupScore> out;
    for (int j=0;j<=last;++j) {
        const auto* p=bytes(h,0x031c+size_t(j)*32+size_t(pair)*2,2);
        if (p[0]>=100 || p[1]>=100) break;
        int left=p[0],right=p[1],markers=0;
        if (left>29) { left-=30; markers=1; if(left>29) {left-=30; markers=2;} }
        //The right flag overwrites an earlier left flag; this is not max().
        if (right>29) { right-=30; markers=1; if(right>29) {right-=30; markers=2;} }
        out.push_back({187+(3-game)*10+36*j,markers,uint8_t(j%2 ? 30 : 1),number(left)+":"+number(right)});
    }
    return out;
}
} // namespace

ClassicCupView classic_cup(const HokFile& h,const SeasonCalendar& calendar) {
    ClassicCupView out;
    const auto* draw=bytes(h,0x8e51,32);
    int scan=int(static_cast<int8_t>(byte(h,0x03b4)));
    if (scan<0) throw FormatError("classic cup: invalid negative draw scan offset");
    while(scan<32 && draw[scan]!=255) scan+=2;
    out.pair_count=std::min(16,scan/2);
    if (!out.pair_count) out.pair_count=1; //39EB: minimum before code/height calculation
    std::array<int,16> codes{};
    for (int i=0;i<out.pair_count;++i) codes[size_t(i)]=league_flag(draw[i*2])+8*league_flag(draw[i*2+1]);
    int empty=0;
    for (size_t group=0;group<6;++group) {
        bool found=false;
        for (int i=0;i<out.pair_count;++i)
            if (codes[size_t(i)]==forward[group] || codes[size_t(i)]==reverse[group]) found=true;
        if (!found) ++empty;
    }
    int units=std::min(11,((out.pair_count+1+empty+4)*7+60)/19);
    out.frame_top=(240-(units*19+23))/2; //before the native minimum2 clamp
    units=std::max(2,units);
    out.frame_rows=units+1;
    const int mirror=byte(h,0x8f84);
    if (mirror>=int(rounds.size())) throw FormatError("classic cup: unavailable original round title");
    out.native_caption=std::string(rounds[size_t(mirror)].substr(0,21));
    const int cursor=le16(h.state,0xb953);
    if (cursor<=211) { //original EXE DS4E00[0], not a guessed season-end date
        int index=cursor;
        while (size_t(index)<calendar.events.size() && !((calendar.events[size_t(index)]>>1)&4)) ++index;
        if (size_t(index)>=calendar.events.size())
            throw FormatError("classic cup: missing future Cup calendar event");
        out.preview_calendar_index=index;
        out.preview_date=preview(h,index);
        out.native_caption+="   "+std::to_string(out.preview_date->day)+"."+
                            std::to_string(out.preview_date->month)+".";
    }
    const int game=int(static_cast<int8_t>(byte(h,0x031b)));
    int y=out.frame_top+46;
    for (size_t group=0;group<6;++group) {
        auto& g=out.groups[group];
        g.native_heading=headings[group];
        g.heading_baseline=y; y+=8;
        for (int i=0;i<out.pair_count;++i) {
            if (codes[size_t(i)]!=forward[group] && codes[size_t(i)]!=reverse[group]) continue;
            const int home=draw[i*2],away=draw[i*2+1];
            if (home>=64) continue;
            //A missing away ID leaves uninitialized native click-array bytes.
            //Do not invent deterministic stack-residue behavior for corrupt draws.
            if (away>=64) throw FormatError("classic cup: incomplete native Cup pairing");
            ClassicCupRow row;
            row.pair=i; row.home=home; row.away=away; row.baseline=y;
            row.hit_min_y=y-5; row.hit_max_y=y;
            row.native_home=team_name(h,home); row.native_away=team_name(h,away);
            row.home_color=human(h,home) ? 24 : 30;
            row.away_color=human(h,away) ? 24 : 1;
            row.scores=scores(h,i,game);
            g.rows.push_back(std::move(row)); y+=7;
        }
        if (g.rows.empty()) {g.empty_baseline=y; y+=7;}
    }
    return out;
}

ClassicCupHit classic_cup_hit(const ClassicCupView& view,int x,int y) {
    if (y<=view.frame_top+26) return {};
    for (const auto& group:view.groups) for (const auto& row:group.rows)
        if (y>=row.hit_min_y && y<=row.hit_max_y)
            return {x>99 ? row.away : row.home,x>99 ? 1 : 0,row.pair,row.baseline,row.hit_min_y,row.hit_max_y};
    return {};
}

void draw_classic_cup(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar) {
    const auto view=classic_cup(h,calendar);
    //Native main caller2BF6 restores wallpaper before invoking38F3.
    c.background(22);
    c.frame(5,view.frame_top,13,view.frame_rows);
    //2115:0A79 ->1BBF:351B is opaque; its extra192 argument is ignored.
    c.blit(36,13,view.frame_top+4,0,22,22,22);
    c.blit(36,286,view.frame_top+4,0,22,22,22);
    c.text_native(ClassicFontId::NewBold,0,view.frame_top+19,"DEB-Pokalspiele",7,1,319);
    c.text_native(ClassicFontId::Flow,0,view.frame_top+34,view.native_caption,30,0,319);
    for (const auto& group:view.groups) {
        c.text_native(mini,15,group.heading_baseline,group.native_heading,7,0,183);
        for (const auto& row:group.rows) {
            c.text_native(mini,15,row.baseline,row.native_home,row.home_color,-1,94);
            c.text_native(mini,98,row.baseline,"-",7,-1,100);
            c.text_native(mini,104,row.baseline,row.native_away,row.away_color,-1,183);
            for (const auto& score:row.scores) {
                c.text_native(mini,score.x,row.baseline,score.native,score.color);
                for (int dot=0;dot<score.markers;++dot)
                    c.rect(score.x+22+2*dot,row.baseline,score.x+22+2*dot,row.baseline,score.color);
            }
        }
        if (group.empty_baseline>=0)
            c.text_native(mini,98,group.empty_baseline,"---------",7,-1,100);
    }
}

} // namespace em::ui
