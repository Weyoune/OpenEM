// SPDX-License-Identifier: GPL-3.0-only
// The classic club-information window (opened e.g. from the "Stärken" page):
// crest, league position curve, recent-result faces, home/away balance,
// goal averages, current series, record results, head-to-head history and
// all-time table points. classic_club_info() computes the content straight
// from the save state; draw_classic_club_info() renders it. All texts are in
// the game's own 8-bit encoding ('{' = ä, '|' = ö, '}' = ü, '%' = padding
// digit space).
#include "ui/classic_club_info.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
// Save-state offsets: 150 team records of 61 bytes (name at +0, 22 bytes;
// strengths at +0x18..+0x1D; crest/reporting byte at +0x34) and the manager
// records of 0x1AF bytes (own team at +0x14, league at +0xEB).
constexpr size_t teams = 0x17e7, managers = 0x0dcd;

// Bounds-checked pointer to count bytes of the save state at offset p.
const uint8_t* bytes(const HokFile& h, size_t p, size_t count) {
    if (p > h.state.size() || count > h.state.size() - p)
        throw FormatError("classic club info: truncated native state");
    return h.state.data() + p;
}
uint8_t byte(const HokFile& h, size_t p) { return *bytes(h,p,1); }
unsigned word(const uint8_t* p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }       // little-endian u16
uint32_t dword(const uint8_t* p) { return uint32_t(word(p)) | uint32_t(word(p+2)) << 16; }  // little-endian u32
// Teams 52..63 are national teams; 64..149 are foreign clubs without statistics.
bool national(int team) { return team >= 52 && team < 64; }
// Team name in the game's encoding (NUL-terminated, at most 22 bytes).
std::string name(const HokFile& h, int team) {
    if (team < 0 || team >= 150) throw FormatError("classic club info: invalid team name");
    const auto* p = bytes(h,teams+size_t(team)*61,22);
    size_t n=0; while (n<22 && p[n]) ++n;
    return {reinterpret_cast<const char*>(p),n};
}
// Decimal number, left-padded to width with '%' (the font's digit-wide
// blank) when non-negative.
std::string number(int64_t n, int width=1) {
    auto s = std::to_string(n);
    if (n>=0 && int(s.size())<width) s.insert(0,size_t(width-int(s.size())),'%');
    return s;
}
void row(ClassicClubInfo& info, int y, std::string text, int x=98, uint8_t color=30, int right=-1) {
    info.rows.push_back({x,y,right,color,std::move(text)});
}

// One record result of a team. kind 0..3 = home records, 4..7 = away
// records (highest win, highest defeat, goals scored, goals conceded). The
// record words are at 0xA11C + team*16 + kind*2 (0 = no entry): for the
// win/defeat kinds the two bytes are the two scores (home records show the
// high byte first, away records the low byte first), for the goal kinds the
// count (whichever byte is non-zero). The opponent team index is at 0x9F1C + team*8 + kind.
// returned is the value the game uses to pick between the home and away
// record in the combined view: only the LOW byte of the record word, a quirk
// reproduced here.
struct Record { std::string text; int returned; };
Record record(const HokFile& h, int team, int kind) {
    const unsigned w=word(bytes(h,0xa11c+size_t(team)*16+size_t(kind)*2,2));
    if (!w) return {"Noch keine Eintragung.",0};
    std::string result;
    if (kind==0 || kind==1 || kind==4 || kind==5) {
        result=number(kind<4 ? w>>8 : w&255,2)+":"+
               number(kind<4 ? w&255 : w>>8,2);
    } else result=number(w>>8 ? w>>8 : w&255,2);
    result+=" ("+name(h,byte(h,0x9f1c+size_t(team)*8+size_t(kind)))+")";
    return {std::move(result),int(w&255)};
}

// League-position curve of a club over the season so far, using the game's
// general table-curve routine in the configuration of this window (single
// team, no opponent curve, no overlap nudge, no league caption and no
// first-matchday message). Positions are packed as nibbles at
// 0x3BD9 + team*80 (two rounds per byte, low nibble first); y = 63 + 2*pos.
// The final position is labelled at the end of the curve.
void graph(ClassicClubInfo& info, const HokFile& h, int team, int manager) {
    if (national(team)) return;
    info.graph.push_back({125,62,125,85,6});
    info.graph.push_back({126,86,221,86,8});
    const int count=int(static_cast<int8_t>(static_cast<uint8_t>(h.matchday[0])))-1;
    if (count<=0) return;
    int step=(8500/count)/100; if (step==1) step=2;
    const auto position=[&](int round) {
        const auto packed=byte(h,0x3bd9+size_t(team)*80+size_t(round/2));
        return int((packed>>((round&1)*4))&15);
    };
    int x=126,y=63+2*position(0),last=0;
    for (int i=0;i<count;++i) {
        last=position(i);
        const int end_x=125+(i+1)*step,end_y=63+2*last;
        info.graph.push_back({x,y,end_x,end_y,24});
        x=end_x; y=end_y;
    }
    const int league=byte(h,managers+size_t(manager)*0x1af+0xeb);
    constexpr std::array<int,6> limits{44,44,44,2,2,2}; // the game's per-league matchday limits
    if (league>=int(limits.size())) throw FormatError("classic club info: invalid graph manager league");
    // The game keeps eight matchday counters, not just HokFile::matchday's
    // three domestic ones; the others are read from the save state at
    // 0x8F80 + league. In a national-manager phase the league can be 4.
    // The label is nudged one pixel up-left when it has two digits and the
    // manager's league is past the limit.
    const int raw_next=league<3 ? h.matchday[league] : byte(h,0x8f80+size_t(league));
    const int next=int(static_cast<int8_t>(static_cast<uint8_t>(raw_next)));
    const bool lower=last+1>9 && next>limits[size_t(league)];
    info.graph_labels.push_back({127+count*step-int(lower),63+2*last-int(lower),-1,24,
                                number(last+1)+"."});
}

// Historical results of the manager's team against the given team: up to
// five home ("H:") and five away ("A:") games, each a packed byte
// (goals for in the high nibble, against in the low nibble, 255 = unused)
// at 0x8F88 + bank*640 + team*10 + 2*game + (1-side). The national-team
// manager (index 5) uses bank 0, and national teams have a single list
// without the H/A prefixes.
std::string head_to_head(const HokFile& h, int team, int manager, bool is_national) {
    const int bank=manager==5 ? 0 : manager;
    std::string result=is_national ? "" : "H: ";
    for (int side=0;side<2;++side) {
        for (int game=0;game<5;++game) {
            const int packed=byte(h,0x8f88+size_t(bank)*640+size_t(team)*10+size_t(2*game+1-side));
            if (packed==255) continue;
            if (game!=0 || (side!=0 && is_national && !result.empty())) result+=",";
            result+=number(packed>>4)+":"+number(packed&15);
        }
        if (!side && !is_national) {
            if (result.size()==3) result+=" Noch keine";
            result+=" -- A: ";
        }
    }
    if (!result.empty() && result.back()==' ') result+=" Noch keine";
    return result;
}

// Draws one of the three 57x12 buttons at the bottom of the window,
// preserving the four corner pixels underneath. Either a text button or,
// with reporting >= 0, the reporting-mode icon (0..2; 3 draws an empty
// button).
void button(ClassicCanvas& c, int x, std::string_view text, int reporting=-1) {
    constexpr int y=215;
    const std::array<uint8_t,4> corner{c.pixels().at(x,y),c.pixels().at(x+56,y),
                                     c.pixels().at(x,y+11),c.pixels().at(x+56,y+11)};
    c.blit(4,x,y,17,179,57,12,true,192);
    c.pixels().at(x,y)=corner[0]; c.pixels().at(x+56,y)=corner[1];
    c.pixels().at(x,y+11)=corner[2]; c.pixels().at(x+56,y+11)=corner[3];
    // The game's button routine ignores the colour its caller asks for and
    // always uses colour 29 with shadow 1.
    if (reporting<0) c.text_native(mini,x,y+7,text,29,1,x+59);
    else if (reporting<3) c.blit(4,x+23,y+1,19,36+11*reporting,12,10,true,192);
}
// Draws a small result face at (x,89): 0 = none, 1 = defeat, 2 = draw,
// 3 = win.
void face(ClassicCanvas& c, int index, int x) {
    if (index) c.blit(33,x,89,index==1 ? 19 : 25,index==2 ? 41 : 36,6,5,true,192);
}
} // namespace

// Field notes for the statistics block at 0x3BA5 + team*80 used below:
// +0x07..+0x0E last eight results ('s' win, 'u' draw, 'n' defeat),
// +0x19/+0x1A goals home/away, +0x1D/+0x1E conceded home/away,
// +0x21/+0x22 games home/away, +0x29/+0x2A wins, +0x2D/+0x2E defeats,
// +0x31 league position (0-based), +0x34.. packed position curve,
// +0x4C all-time table points (i32). Series counters are at
// 0x9988 + team*21 + kind*3 + column (column 0 total, 1 home, 2 away).
ClassicClubInfo classic_club_info(const HokFile& h, int team,int mode) {
    if (team<0 || team>=150||mode<0||mode>2) throw FormatError("classic club info: invalid team or mode");
    ClassicClubInfo info;
    const auto* t=bytes(h,teams+size_t(team)*61,61);
    const int manager=byte(h,0x0788);
    if (manager>=6) throw FormatError("classic club info: invalid active manager");
    const int own=byte(h,managers+size_t(manager)*0x1af+0x14);
    const bool n=team>=52;
    info.native_name=name(h,team);
    info.crest=team<64 ? 80+(t[0x34]&63) : 143;
    info.reporting_mode=(t[0x34]&192)>>6;
    info.schedule=team<52 && dword(bytes(h,0x8e2d,4))!=0;
    int strength=0; for (int i=0;i<6;++i) strength+=t[0x18+i];
    strength/=6;
    // Loading the window's graphics resets the number width to 1, so the
    // width 2 used by the "Stärken" rows behind it is NOT inherited by this
    // caption or the balance lines.
    info.native_caption="St{rke:"+number(strength);
    if (team>=64) {
        row(info,120,"Leider sind aus vielerlei Gr}nden keine",93,1,317);
        row(info,128,"Informationen verf}gbar...",93,1,317);
        row(info,138,std::string(1,char(0xe4))+"hm... der Grund ist der begrenzte Speicher...",93,1,317);
        return info;
    }
    const auto* b=bytes(h,0x3ba5+size_t(team)*80,80);
    if (!n) {
        constexpr std::array<const char*,3> leagues{"Bundesliga","Zweiten Liga","Oberliga"};
        const int league=team<12 ? 0 : team<24 ? 1 : 2;
        info.native_caption=number(int(b[0x31])+1)+". Platz in der "+leagues[size_t(league)]+
                            ", St{rke: "+number(strength);
    }
    graph(info,h,team,manager);
    info.has_results=true;
    int recent_points=0,played=0,summary=2;
    for (size_t i=0;i<8;++i) {
        switch (b[7+i]) {
        case 's': case 'S': info.faces[i]=3; recent_points+=2; ++played; ++summary; break;
        case 'u': case 'U': info.faces[i]=2; ++recent_points; ++played; break;
        case 'n': case 'N': info.faces[i]=1; ++played; --summary; break;
        default: break;
        }
    }
    info.summary_face=uint8_t(std::clamp(summary,1,3));
    const auto balance=[&](int side) {
        int wins=b[0x29+side],losses=b[0x2d+side],games=b[0x21+side];
        if (n) { wins+=b[0x2a]; losses+=b[0x2e]; games+=b[0x22]; }
        return number(wins)+" Siege, "+number(games-wins-losses)+" Unent., "+number(losses)+" Nied.";
    };
    int y=109;
    row(info,y,n ? "Bilanz:" : "Heimbilanz:"); row(info,y,balance(0),195); y+=6;
    if (!n) { row(info,y,"Ausw{trsbilanz:"); row(info,y,balance(1),195); y+=6; }
    row(info,y,"Aus d. letzten Spielen:");
    row(info,y,number(recent_points)+":"+number(played*2-recent_points)+" Punkte",195); y+=6;
    constexpr std::array<const char*,3> scope{"(Heim):  ","(Ausw{rts):  ","(Gesamt):  "};
    row(info,y,"Tore / Spiel "+std::string(scope[size_t(mode)]));
    const int games=mode==2 ? int(b[0x21])+b[0x22] : b[0x21+mode];
    const auto average=[&](int goals) {
        const int tenths=games ? goals*10/games : 0;
        return number(tenths/10)+"."+number(tenths%10);
    };
    const int goals=mode==2?int(b[0x19])+b[0x1a]:b[0x19+mode];
    const int conceded=mode==2?int(b[0x1d])+b[0x1e]:b[0x1d+mode];
    row(info,y,average(goals)+":"+average(conceded),195); y+=6;
    row(info,y,"Serien "+std::string(scope[size_t(mode)])); y+=6;
    constexpr std::array<const char*,7> phrases{"gewonnen","verloren","unentschieden",
        "nicht gewonnen","nicht verloren","ohne Gegentor","ohne Torerfolg"};
    int streaks=0;
    constexpr std::array<int,3> columns{1,2,0};
    constexpr std::array<const char*,3> side{"","zuhause ","ausw{rts "};
    const int column=columns[size_t(mode)];
    for (size_t k=0;k<7 && streaks<4;++k) {
        const int count=byte(h,0x9988+size_t(team)*21+k*3+size_t(column));
        if (count<=1) continue;
        row(info,y,number(count,2)+" Spiele in Folge "+side[size_t(column)]+phrases[k]); y+=6; ++streaks;
    }
    if (!streaks) { row(info,y,"Keine bemerkenswerte Serie vorhanden.",103,6); y+=6; }
    y+=2;
    constexpr std::array<const char*,4> labels{"H|chster Sieg","H|chste Niederl.","Erzielte Tore","Kassierte Tore"};
    constexpr std::array<const char*,4> home_labels{"H|chster H-Sieg","H|chste H-Niederl.","Erzielte H-Tore","Kassierte H-Tore"};
    constexpr std::array<const char*,4> away_labels{"H|chster A-Sieg","H|chste A-Niederl.","Erzielte A-Tore","Kassierte A-Tore"};
    for (int r=0;r<4;++r) {
        const int k=mode==0?r:r+4;
        row(info,y,(mode==0?home_labels:mode==1?away_labels:labels)[size_t(r)]);
        if(mode!=2){row(info,y,record(h,team,k).text,183);y+=6;continue;}
        auto home=record(h,team,k-4),away=record(h,team,k);
        std::string result=home.text;
        if (home.returned || away.returned) {
            const auto compare=[&](int value) { return k<6 ? std::abs((value>>4)-(value&15)) : value; };
            const bool pick_home=compare(home.returned)>compare(away.returned);
            result=pick_home ? std::move(home.text) : std::move(away.text);
            if (!n) result+=pick_home ? " (H)" : " (A)";
        }
        row(info,y,std::move(result),183); y+=6;
    }
    y+=2;
    const int own_nation=byte(h,0x068c);
    if (team!=own_nation && team!=own && (manager!=5 || national(team))) {
        row(info,y,"Historische Ergebnisse gegen "+name(h,n ? own_nation : own)); y+=6;
        row(info,y,head_to_head(h,team,manager,n)); y+=8;
    }
    if (!n) {
        const auto points=static_cast<int32_t>(dword(b+0x4c));
        row(info,y,"Punkte in der Ewigen Tabelle: "+
            (points ? number(points) : "Noch keine..."),98,points ? 1 : 6);
    }
    return info;
}

// Renders the window: 9x10 tile frame at (93,44), crest box, curve panel,
// result faces, text area and the "Reicht mir" / "Spielplan" /
// reporting-mode buttons.
void draw_classic_club_info(ClassicCanvas& c, const HokFile& h, int team,int mode) {
    const auto info=classic_club_info(h,team,mode);
    c.frame(93,44,9,10);
    c.outline(258,52,297,91,4);
    c.blit(info.crest,259,53,0,0,38,38,true,192);
    c.rect(122,58,228,87,1); c.outline(122,58,228,87,3);
    for (const auto& line:info.graph) c.line(line.x1,line.y1,line.x2,line.y2,line.color);
    for (const auto& text:info.graph_labels)
        c.text_native(mini,text.x,text.baseline,text.native,text.color,-1,text.right);
    if (info.has_results) {
        for (size_t i=0;i<8;++i) face(c,info.faces[i],118+8*int(i));
        face(c,info.summary_face,249);
    }
    c.rect(96,96,301,229,4);
    c.text_native(ClassicFontId::Flow,124,55,info.native_name,30,0,235);
    c.line(98,95,305,95,29);
    button(c,103,"Reicht mir");
    if (info.schedule) button(c,173,"Spielplan");
    button(c,243,{},info.reporting_mode);
    c.text_native(mini,93,102,info.native_caption,30,-1,309);
    for (const auto& text:info.rows)
        c.text_native(mini,text.x,text.baseline,text.native,text.color,-1,text.right);
}

} // namespace em::ui
