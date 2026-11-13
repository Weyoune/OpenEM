// SPDX-License-Identifier: GPL-3.0-only
// The classic DEB-Pokal (German cup) page: the current round's pairings,
// grouped by the leagues of the two teams, with the results of up to three
// games per pairing and the date of the next cup day. classic_cup() builds
// the view from the save state, classic_cup_hit() maps clicks to teams and
// draw_classic_cup_view() renders it.
#include "ui/classic_cup.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini;
// A pairing's league code is league_flag(home) + 8*league_flag(away)
// (1 Bundesliga, 2 Zweite Liga, 4 Oberliga). Group g collects the pairings
// whose code equals forward[g] or reverse[g] (the same league combination
// with home and away swapped), in the order of the headings below.
constexpr std::array<int,6> forward{9,10,12,18,34,36};
constexpr std::array<int,6> reverse{9,17,33,18,20,36};
constexpr std::array<std::string_view,6> headings{
    "BUNDESLIGA GEGEN BUNDESLIGA:","BUNDESLIGA GEGEN ZWEITE LIGA:",
    "BUNDESLIGA GEGEN OBERLIGA:","ZWEITE LIGA GEGEN ZWEITE LIGA:",
    "ZWEITE LIGA GEGEN OBERLIGA:","OBERLIGA GEGEN OBERLIGA:"};
// The game's competition-title table: cup round titles 0..5, followed by
// the shared competition labels. The caption is looked up with the round
// byte stored in the save state; do not silently replace it by a decoded
// cup round.
constexpr std::array<std::string_view,12> rounds{
    "","Erste Runde ","Achtelfinale ","Viertelfinale ","Halbfinale ","Endspiel ",
    "DEB-Pokal","Europapokal","Play-Off / Aufstiegsrunden","Weltmeisterschaft","Verletzt.","Gesperrt."};

const uint8_t* bytes(const HokFile& h,size_t at,size_t count) {
    if (at>h.state.size() || count>h.state.size()-at)
        throw FormatError("classic cup: truncated native state");
    return h.state.data()+at;
}
int byte(const HokFile& h,size_t at) { return *bytes(h,at,1); }
// League bit of a team: 1 Bundesliga (0..11), 2 Zweite Liga (12..23),
// 4 Oberliga (24..52, including index 52 as the game does), else 1.
int league_flag(int team) { return team<12 ? 1 : team<24 ? 2 : team<=52 ? 4 : 1; }
// Two-digit number padded with the '%' digit-wide blank.
std::string number(int value) {
    auto text=std::to_string(value);
    if (value>=0 && text.size()<2) text.insert(0,2-text.size(),'%');
    return text;
}
// Team name (game encoding) from the 61-byte team records at 0x17E7.
std::string team_name(const HokFile& h,int team) {
    const auto* p=bytes(h,0x17e7+size_t(team)*61,22);
    size_t count=0; while(count<22 && p[count]) ++count;
    return {reinterpret_cast<const char*>(p),count};
}
// True if a human manager runs this team. The number of managers is at
// 0x0DC6, each manager record (0x1AF bytes at 0x0DCD) has its team at +0x14.
bool human(const HokFile& h,int team) {
    const int count=byte(h,0x0dc6);
    if (count>6) throw FormatError("classic cup: invalid human manager count");
    for (int manager=0;manager<count;++manager)
        if (byte(h,0x0dcd+size_t(manager)*0x1af+0x14)==team) return true;
    return false;
}
// Date of season day index (or, for index 0, of the current day stored as
// u32 at 0x8E2D; the value 2000 counts as day 0). The season's start year
// is the u16 at 0x8E31.
GameDate preview(const HokFile& h,int index) {
    uint32_t day=index ? uint32_t(index) : le32(h.state,0x8e2d);
    if (day==2000) day=0;
    if (day>uint32_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic cup: date outside host range");
    return date_of_season_day(int(day),le16(h.state,0x8e31));
}
// Results of one pairing so far. game is the number of the current game of
// the round (byte at 0x031B); on day 218 the current game is included as
// well. Each result is two bytes at 0x031C + j*32 + pair*2 (home, away
// goals; 100 or more = not played). A goal value above 29 carries a marker:
// 30 is subtracted once (one dot) or twice (two dots) and the dots are drawn
// after the score. Columns move left with each later game; colours
// alternate between 1 and 30.
std::vector<ClassicCupScore> scores(const HokFile& h,int pair,int game) {
    const bool final_day=le32(h.state,0x8e2d)==218;
    // The game decrements an 8-bit char and sign-extends it for the loop test.
    const int last=int(static_cast<int8_t>(static_cast<uint8_t>(game-(final_day ? 0 : 1))));
    if (last>2) throw FormatError("classic cup: score history exceeds native three rows");
    std::vector<ClassicCupScore> out;
    for (int j=0;j<=last;++j) {
        const auto* p=bytes(h,0x031c+size_t(j)*32+size_t(pair)*2,2);
        if (p[0]>=100 || p[1]>=100) break;
        int left=p[0],right=p[1],markers=0;
        if (left>29) { left-=30; markers=1; if(left>29) {left-=30; markers=2;} }
        // The away marker overwrites an earlier home marker; this is not max().
        if (right>29) { right-=30; markers=1; if(right>29) {right-=30; markers=2;} }
        out.push_back({187+(3-game)*10+36*j,markers,uint8_t(j%2 ? 30 : 1),number(left)+":"+number(right)});
    }
    return out;
}
} // namespace

// The draw is 16 pairs of team bytes at 0x8E51 (home, away; 255 ends the
// list), scanned from the offset in the byte at 0x03B4. The window height
// follows from the number of pairs plus one line per empty group.
ClassicCupView classic_cup(const HokFile& h,const SeasonCalendar& calendar) {
    ClassicCupView out;
    const auto* draw=bytes(h,0x8e51,32);
    int scan=int(static_cast<int8_t>(byte(h,0x03b4)));
    if (scan<0) throw FormatError("classic cup: invalid negative draw scan offset");
    while(scan<32 && draw[scan]!=255) scan+=2;
    out.pair_count=std::min(16,scan/2);
    if (!out.pair_count) out.pair_count=1; // the game's minimum, applied before the code/height calculation
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
    out.frame_top=(240-(units*19+23))/2; // computed before the game clamps the size to at least 2
    units=std::max(2,units);
    out.frame_rows=units+1;
    const int mirror=byte(h,0x8f84);  // current cup round (title index)
    if (mirror>=int(rounds.size())) throw FormatError("classic cup: unavailable original round title");
    out.native_caption=std::string(rounds[size_t(mirror)].substr(0,21));
    // Calendar position (u16 at 0xB953). Up to day 211 (the limit is the
    // game's own constant, not a guessed season end) the caption adds the
    // date of the next calendar event that has the cup bit (bit 3) set.
    const int cursor=le16(h.state,0xb953);
    if (cursor<=211) {
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
            // In the game a missing away team leaves its click table partly
            // uninitialised. Rather than invent behaviour for such a corrupt
            // draw, reject it.
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

// A click on a pairing row selects the home team (x <= 99) or the away team.
ClassicCupHit classic_cup_hit(const ClassicCupView& view,int x,int y) {
    if (y<=view.frame_top+26) return {};
    for (const auto& group:view.groups) for (const auto& row:group.rows)
        if (y>=row.hit_min_y && y<=row.hit_max_y)
            return {x>99 ? row.away : row.home,x>99 ? 1 : 0,row.pair,row.baseline,row.hit_min_y,row.hit_max_y};
    return {};
}

void draw_classic_cup(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar) {
    draw_classic_cup_view(c,classic_cup(h,calendar));
}
void draw_classic_cup_view(ClassicCanvas& c,const ClassicCupView& view) {
    // The game restores the ice wallpaper before drawing the page.
    c.background(22);
    c.frame(5,view.frame_top,13,view.frame_rows);
    // The two cup icons are copied opaquely: the game passes a
    // transparency key of 192, but the routine it calls ignores it.
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
