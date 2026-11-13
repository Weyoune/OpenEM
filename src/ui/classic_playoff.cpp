// SPDX-License-Identifier: GPL-3.0-only
// Play-off bracket page (see classic_playoff.hpp). All offsets are into the save state.
#include "ui/classic_playoff.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini;
constexpr std::array<int,4> height_extra{1,2,3,3}; // extra frame lines per play-off stage (EM.EXE table)
constexpr std::array<int,3> heading_at{1,5,7}; // 1-based pairing index before which each stage heading appears
constexpr std::array<const char*,3> heading_text{"VIERTELFINALE","HALBFINALE","ENDSPIEL"};
// Bounds-checked pointer into the save state.
const uint8_t* bytes(const HokFile& h,size_t at,size_t count) {
    if(at>h.state.size() || count>h.state.size()-at)
        throw FormatError("classic playoff: truncated native state");
    return h.state.data()+at;
}
int byte(const HokFile& h,size_t at) { return *bytes(h,at,1); }
// Two-digit number, left-padded with the blank digit '%'.
std::string number(int n) {
    auto result=std::to_string(n);
    if(n>=0 && result.size()<2) result.insert(0,2-result.size(),'%');
    return result;
}
// Team name (first 22 bytes of the 61-byte team record at 0x17E7), up to the first NUL.
std::string name(const HokFile& h,int id) {
    const auto* p=bytes(h,0x17e7+size_t(id)*61,22);
    size_t count=0;while(count<22 && p[count]) ++count;
    return {reinterpret_cast<const char*>(p),count};
}
// True if team is the club of one of the human managers (count at 0x0DC6, club at manager block +0x14).
bool human(const HokFile& h,int team) {
    const int count=byte(h,0x0dc6);
    if(count>6) throw FormatError("classic playoff: invalid human manager count");
    for(int m=0;m<count;++m)
        if(byte(h,0x0dcd+size_t(m)*0x1af+0x14)==team) return true;
    return false;
}
// Finds the next play-off day (event bit 0x40) from the calendar cursor 0xB953 on and stores its index, date
// and the " d.m." caption in out. Index 0 means "today" and uses the current season day (u32 at 0x8E2D).
void preview(ClassicPlayoffView& out,const HokFile& h,const SeasonCalendar& calendar) {
    (void)bytes(h,0xb953,2);
    const int cursor=le16(h.state,0xb953);
    if(cursor>209) return; // the original's limit, applied even before the unavailable-bracket branch
    size_t at=size_t(cursor);
    while(at<calendar.events.size() && !((calendar.events[at]>>1)&32)) ++at;
    if(at>=calendar.events.size()) throw FormatError("classic playoff: missing future calendar event");
    if(at>size_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic playoff: calendar date outside host range");
    (void)bytes(h,0x8e2d,6);
    uint32_t day=at?uint32_t(at):le32(h.state,0x8e2d);
    if(day==2000) day=0; // 2000 is a sentinel for day 0 (not a new season-day write)
    if(day>uint32_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic playoff: current day outside host range");
    out.preview_calendar_index=int(at);
    out.preview_date=date_of_season_day(int(day),le16(h.state,0x8e31));
    out.native_caption="   "+std::to_string(out.preview_date->day)+"."+
                       std::to_string(out.preview_date->month)+".";
}
// Played games of pairing pair: results at 0x0205 + game * 14 + pair * 2 (home, away byte; >= 100 ends the
// list). Pairings of completed earlier stages (before the cursor) show all five stored games, the current
// stage the games 0..game-1 played so far.
std::vector<ClassicPlayoffScore> scores(const HokFile& h,int pair,int cursor,int game) {
    const int last=2*pair<cursor?4:int(static_cast<int8_t>(static_cast<uint8_t>(game-1)));
    if(last>4) throw FormatError("classic playoff: current history exceeds five legs");
    std::vector<ClassicPlayoffScore> out;
    for(int j=0;j<=last;++j) {
        const auto* p=bytes(h,0x0205+size_t(j)*14+size_t(pair)*2,2);
        if(p[0]>=100 || p[1]>=100) break;
        int left=p[0],right=p[1],dots=0;
        if(left>29) {left-=30;dots=1;if(left>29) {left-=30;dots=2;}}
        // As in the original, the right marker overwrites the earlier left marker.
        if(right>29) {right-=30;dots=1;if(right>29) {right-=30;dots=2;}}
        out.push_back({187+26*j,dots,uint8_t(j%2?30:1),number(left)+":"+number(right)});
    }
    return out;
}
} // namespace

ClassicPlayoffView classic_playoff_view(const HokFile& h,const SeasonCalendar& calendar) {
    ClassicPlayoffView out;
    out.stage=byte(h,0x8f87); out.game=byte(h,0x0259); out.cursor=byte(h,0x03b6);
    if(out.stage>=int(height_extra.size())) throw FormatError("classic playoff: unavailable native phase table entry");
    out.available=out.cursor!=255;
    int scan=int(static_cast<int8_t>(static_cast<uint8_t>(out.cursor)));
    if(out.available) {
        if(scan<0) throw FormatError("classic playoff: negative native draw offset");
        while(scan<32 && byte(h,0x8e91+size_t(scan))!=255) scan+=2;
    } else scan=0;
    out.pair_count=std::max(1,std::min(16,scan>>1));
    const int metric=std::max(0,out.pair_count+1-6+height_extra[size_t(out.stage)]);
    int units=std::min(11,((metric+4)*7+60)/19);
    out.frame_top=(240-(units*19+23))>>1;
    units=std::max(2,units); if(units>8) out.frame_top=2;
    out.frame_rows=units+1;
    preview(out,h,calendar); //construction precedes hidden-placeholder substitution
    if(!out.available) return out;
    int y=out.frame_top+46;
    for(int p=0;p<out.pair_count;++p) {
        for(size_t k=0;k<heading_at.size();++k) if(p+1==heading_at[k]) {
            out.headings.push_back({y,heading_text[k]}); y+=7;
        }
        const int home=byte(h,0x8e91+size_t(p)*2),away=byte(h,0x8e92+size_t(p)*2);
        if(home>=64) continue;
        // An invalid away ID leaves undefined click targets in the original. Do not
        // invent a deterministic modal target for that malformed pairing.
        if(away>=64) throw FormatError("classic playoff: incomplete native pairing");
        ClassicPlayoffRow row;
        row.pair=p; row.home=home; row.away=away; row.baseline=y;
        row.native_home=name(h,home); row.native_away=name(h,away);
        row.home_color=human(h,home)?24:30; row.away_color=human(h,away)?24:1;
        row.scores=scores(h,p,out.cursor,out.game);
        out.rows.push_back(std::move(row)); y+=7;
    }
    return out;
}

ClassicPlayoffHit classic_playoff_hit(const ClassicPlayoffView& view,int x,int y) {
    if(x>=136 && x<=184 && y>=201 && y<=239)
        return {ClassicPlayoffAction::Liga,-1,-1,-1,136,201,184,239};
    if(y<=view.frame_top+26) return {};
    for(const auto& row:view.rows) if(y>row.baseline-6 && y<=row.baseline) {
        const bool away=x>99;
        return {ClassicPlayoffAction::ClubInfo,away?row.away:row.home,away?1:0,row.pair,
                away?100:0,row.baseline-5,away?319:99,row.baseline};
    }
    return {};
}

void draw_classic_playoff(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar) {
    const auto view=classic_playoff_view(h,calendar);
    c.background(22);
    c.frame(136,201,2,2); c.text_native(mini,160,221,"Liga",7,-1,160);
    c.frame(5,view.frame_top,13,view.frame_rows);
    c.blit(36,13,view.frame_top+4,0,0,22,22);
    c.blit(36,286,view.frame_top+4,0,0,22,22); // drawn opaque
    c.text_native(ClassicFontId::NewBold,0,view.frame_top+19,view.native_title,7,1,319);
    c.text_native(ClassicFontId::Flow,0,view.frame_top+34,view.available?view.native_caption:
                  "Die Paarungen stehen noch nicht fest...",30,0,319);
    for(const auto& heading:view.headings) c.text_native(mini,15,heading.baseline,heading.native,7,0,183);
    for(const auto& row:view.rows) {
        c.text_native(mini,15,row.baseline,row.native_home,row.home_color,-1,94);
        c.text_native(mini,98,row.baseline,"-",7,-1,100);
        c.text_native(mini,104,row.baseline,row.native_away,row.away_color,-1,183);
        for(const auto& score:row.scores) {
            c.text_native(mini,score.x,row.baseline,score.native,score.color);
            for(int d=0;d<score.markers;++d)
                c.rect(score.x+22+2*d,row.baseline,score.x+22+2*d,row.baseline,score.color);
        }
    }
}

bool prepare_classic_playoff_working(HokFile& h,const SeasonCalendar& calendar) {
    const auto view=classic_playoff_view(h,calendar);
    if(!view.preview_date) return false;
    (void)bytes(h,0x8e21,12); //validate all destination bytes before any write
    const auto date=*view.preview_date;
    const auto put=[&](size_t at,uint32_t value) {
        for(size_t i=0;i<4;++i) h.state[at+i]=uint8_t(value>>(8*i));
    };
    put(0x8e21,uint32_t(date.day)); put(0x8e25,uint32_t(date.month-1)); put(0x8e29,uint32_t(date.year));
    h.date={date.day,date.month,date.year,int(le32(h.state,0x8e2d))};
    return true;
}
} // namespace em::ui
