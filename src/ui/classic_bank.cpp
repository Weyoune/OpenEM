// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_bank.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

// The classic "Bank" screen (background KB image 152): the bank account and the loans between
// human managers in the top row, the four share companies with holdings and the price chart
// in the lower half, the totals on the right and, while a party is selected, the amount
// prompt with its bar. Drawing only; input lives in the application.
//
// Save state used (raw money is DM x 3):
//   0x0030 + m * 32 + c * 8   share holding of manager m in company c: +0 i32 purchase cost,
//                             +4 u32 number of shares
//   0x00B0 + c * 72           price history of company c: 36 u16 prices in DM, oldest first,
//                             the last one (+70) is the current price
//   0x0DCD + m * 0x1AF        manager block: +0 name (20 chars), +0x18F i32 account balance,
//                             +0x19B i32[4] loan balance with each other manager
//   0x0DC6                    number of human managers

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
// Per-company colours: holding text, chart line, chart shadow.
constexpr std::array<uint8_t,4> row_colors{14,22,17,9};
constexpr std::array<uint8_t,4> line_colors{14,22,17,9};
constexpr std::array<uint8_t,4> shadow_colors{12,8,16,27};
// Company names (game text, '{' = ä).
constexpr std::array<std::string_view,4> stock_names{"Herrenh{user","Play Time","Software 2000","KRON Soft"};
// Signed i32 from the state.
int64_t raw32(const Bytes& s, size_t p) { return int32_t(le32(s,p)); }
// Thousands-grouped number with '.'; positive values padded to `width` with the '%' blank glyph.
std::string number(int64_t value, int width = 1) {
    const bool negative = value < 0;
    std::string text = std::to_string(negative ? -value : value);
    for (size_t n = text.size(); n > 3; n -= 3) text.insert(n-3,".");
    if (negative) return "-"+text;
    if (int(text.size()) < width) text.insert(0,size_t(width-int(text.size())),'%');
    return text;
}
// Raw money (DM x 3) as "<amount> DM".
std::string money(int64_t raw, int width = 1) { return number(raw/3,width)+" DM"; }
// Text colour by sign: negative 16, positive 12, zero 30.
uint8_t sign_color(int64_t raw) { return raw < 0 ? 16 : raw > 0 ? 12 : 30; }
// The 20-character name at the start of the manager block, as UTF-8.
std::string manager_name(const Bytes& state, int manager) {
    const size_t at = 0x0DCD + size_t(manager)*0x1AF;
    return iso646de_to_utf8(reinterpret_cast<const char*>(state.data()+at),20);
}

// The game's line routine: a Bresenham variant whose error accumulator starts at 0 (not at
// half the major axis), drawing both endpoints; pixels outside the image are dropped. The chart
// is drawn into a 108x133 buffer of which only 106x130 pixels reach the screen.
void plot_line(Image& image, int x, int y, int end_x, int end_y, uint8_t color) {
    const int dx = std::abs(end_x-x), dy = std::abs(end_y-y);
    const int sx = end_x < x ? -1 : 1, sy = end_y < y ? -1 : 1;
    auto pixel = [&] { if (x>=0 && x<image.width && y>=0 && y<image.height) image.at(x,y)=color; };
    int error = 0; pixel();
    if (dy < dx) for (int i=0;i<dx;++i) {
        x+=sx; error+=dy;
        if (error>=dx) { error-=dx; y+=sy; }
        pixel();
    } else for (int i=0;i<dy;++i) {
        y+=sy; error+=dx;
        if (error>=dy) { error-=dy; x+=sx; }
        pixel();
    }
}

// One chart segment of `company` from (x - 3, previous_y) to (x, y). Both endpoint heights are
// clipped to 0..130 by integer interpolation along the segment (skipped entirely when both lie
// on the same side outside); then two strokes in the line colour and one shadow stroke below.
void plot_segment(Image& image, int x, int y, int previous_y, int company) {
    int previous_x=x-3;
    if ((y>130 && previous_y>130) || (y<0 && previous_y<0)) return;
    if (y>130) { x+=(previous_x-x)*(130-y)/(previous_y-y); y=130; }
    if (y<0) { x+=(previous_x-x)*(-y)/(previous_y-y); y=0; }
    if (previous_y>130) { previous_x+=(x-previous_x)*(130-previous_y)/(y-previous_y); previous_y=130; }
    if (previous_y<0) { previous_x+=(x-previous_x)*(-previous_y)/(y-previous_y); previous_y=0; }
    if (x<0 || x>107 || previous_x<0 || previous_x>107 || y<0 || y>130 || previous_y<0 || previous_y>130) return;
    plot_line(image,previous_x,previous_y,x,y,line_colors[size_t(company)]);
    plot_line(image,previous_x,previous_y+1,x,y+1,line_colors[size_t(company)]);
    plot_line(image,previous_x,previous_y+2,x,y+2,shadow_colors[size_t(company)]);
}

// The share price chart at (134,107): 36 points per company, 3 pixels apart, at half scale.
// The vertical window is chosen automatically to show as many points as possible; with a
// company selected (0..3) its current price sits at the top edge instead. Throws FormatError
// when no window shows any point.
void stock_plot(ClassicCanvas& canvas, const Bytes& state, int selectedStock) {
    std::array<std::array<int,36>,4> prices{};
    int16_t sum=0;
    for (size_t c=0;c<4;++c) {
        for (size_t p=0;p<36;++p) prices[c][p]=le16(state,0xB0+c*72+p*2);
        sum=int16_t(uint16_t(sum)+uint16_t(prices[c][35]));
    }
    const int mean=sum/4;
    int best=0, offset=0;
    // Try windows from mean-200 to mean+200 in steps of 10 and count the points inside each.
    // Quirk of the original kept: the stored winner is the candidate after its +10 increment.
    for (int candidate=std::max(mean-200,0);candidate<mean+200;) {
        int inside=0;
        for (const auto& row:prices) for (const int price:row) {
            const int h=(price>>1)-candidate/2;
            inside+=h>0 && h<130;
        }
        candidate+=10;
        if (inside>best) { best=inside; offset=candidate; if (inside>140) break; }
    }
    if (!best) throw FormatError("HOK: stock history has no native plot window");
    if (selectedStock>=0) offset=prices[size_t(selectedStock)][35]-130;
    Image plot{108,133};
    for (int company=0;company<4;++company) {
        int previous=65; // start value of the original; the first point never draws a segment
        for (int p=0;p<36;++p) {
            const int y=130-prices[size_t(company)][size_t(p)]/2+offset/2;
            if (p) plot_segment(plot,p*3,y,previous,company);
            previous=y;
        }
    }
    for (int y=0;y<130;++y) for (int x=0;x<106;++x) canvas.pixels().at(134+x,107+y)=plot.at(x,y);
}
}  // namespace

// Draws the bank screen for `manager` (0..3). `selectedParty`: 0..3 another manager, 4 the bank,
// 5..8 share company 0..3; it is outlined when `bankSelected`. With `showPrompt` the amount
// prompt for that party is shown, for a deposit/sale (`deposit`) or a withdrawal/purchase,
// with `amount` (raw money, or shares) drawn as a bar relative to the allowed limit.
// Interest shown per month: bank 1% on savings, 4% on a loan; between managers 2.5%.
// Bar limits: bank deposit up to the balance; bank withdrawal up to the larger of the savings
// and a credit line of 6,000,000 raw (2 million DM) minus the positive entry stored for this
// manager in manager block 4; lending to a manager up to the balance, at most 3,000,000 raw
// minus what is already lent to him; borrowing up to the larger of what this manager has lent
// to him and min(his balance, 3,000,000 raw minus what he has already lent to this manager);
// shares: sell up to the holding, buy as many as the balance pays for.
void draw_classic_bank(ClassicCanvas& canvas, const HokFile& snapshot, const ManagerBudget& budget,
                       int selectedParty, bool bankSelected, int64_t amount, bool deposit, int manager,bool showPrompt) {
    if (manager<0 || manager>=4 || selectedParty<0 || selectedParty>8)
        throw FormatError("HOK: invalid Bank view selection");
    const Bytes& state=snapshot.state;
    const size_t block=0x0DCD+size_t(manager)*0x1AF;
    const int human_count=std::min<int>(4,state.at(0x0DC6));
    canvas.background(152);

    auto principal=[&](int party) { return party==4 ? int64_t(budget.bank) : raw32(state,block+0x19B+size_t(party)*4); };
    auto interest=[&](int party) {
        const int64_t value=principal(party);
        return party==4 ? value*(value<0?4:1)/100 : value*25/1000;
    };
    std::array<int,3> partners{-1,-1,-1};
    int shown=0;
    for (int party=0;party<human_count;++party) if (party!=manager) {
        partners[size_t(shown)]=party;
        canvas.blit(0,128+71*shown,10,39*party,0,38,38);
        ++shown;
    }
    constexpr std::array<int,5> top_bounds{0,112,181,253,315};
    for (int column=0;column<4;++column) {
        const int party=column==0?4:partners[size_t(column-1)];
        if (party<0) continue;
        const int64_t value=principal(party), monthly=interest(party);
        canvas.text_native(mini,top_bounds[size_t(column)],62,money(value,9),sign_color(value),-1,top_bounds[size_t(column+1)]);
        canvas.text_native(mini,top_bounds[size_t(column)],70,money(monthly,9)+"/M.",sign_color(monthly),-1,top_bounds[size_t(column+1)]);
    }
    if (bankSelected) {
        if (selectedParty==4) canvas.outline(23,6,83,50,7);
        for (int i=0;i<shown;++i) if (partners[size_t(i)]==selectedParty)
            canvas.outline(127+71*i,9,166+71*i,48,7);
    }

    std::array<int64_t,4> costs{}, quantities{}, values{};
    uint32_t total_cost=0,total_value=0;
    constexpr std::array<int,4> row_y{110,144,179,214};
    constexpr std::array<std::string_view,4> labels{"Kurs:","Zahl:","Kauf:","Wert:"};
    for (size_t company=0;company<4;++company) {
        const size_t holding=0x30+size_t(manager)*32+company*8;
        const uint32_t quantity=le32(state,holding+4);
        const int price=le16(state,0xB0+company*72+70);
        costs[company]=raw32(state,holding); quantities[company]=int32_t(quantity);
        values[company]=int32_t(quantity*uint32_t(price));
        total_cost+=uint32_t(costs[company]); total_value+=uint32_t(values[company]);
        canvas.rect(47,104+int(company)*34+(company>1),127,132+int(company)*34+(company==3),8);
        const std::array<std::string,4> text{money(price),number(quantities[company])+" St.",money(costs[company]),money(values[company])};
        for (size_t row=0;row<4;++row) {
            canvas.text_native(mini,48,row_y[company]+7*int(row),labels[row],row_colors[company]);
            canvas.text_native(mini,70,row_y[company]+7*int(row),text[row],row_colors[company]);
        }
        canvas.outline(4,106+int(company)*34,38,130+int(company)*34,
                       bankSelected && selectedParty==5+int(company)?24:0);
    }
    stock_plot(canvas,state,bankSelected && selectedParty>=5?selectedParty-5:-1);

    int64_t between=0,between_interest=0;
    for (int party=0;party<human_count;++party) if (party!=manager) {
        between+=principal(party); between_interest+=interest(party);
    }
    constexpr std::array<std::pair<int,std::string_view>,7> right_labels{{
        {109,"Kontostand:"},{127,"Untereinander:"},{153,"Bank:"},{179,"Aktien:"},
        {189,"Kaufpreis:"},{207,"Depot-Wert:"},{225,"Depot-Saldo:"}
    }};
    for (const auto& label:right_labels) if (label.first)
        canvas.text_native(mini,249,label.first,label.second,30,6);
    const std::array<std::pair<int,int64_t>,8> right_values{{
        {117,budget.balance},{135,between},{143,between_interest},{161,budget.bank},
        {169,interest(4)},{197,int32_t(total_cost)},{215,int32_t(total_value)},{233,int32_t(total_value-total_cost)}
    }};
    for (const auto& value:right_values)
        canvas.text_native(mini,246,value.first,money(value.second,9),sign_color(value.second));

    if (!bankSelected || !showPrompt) return;
    std::string first,second;
    int64_t limit=0;
    if (selectedParty==4) {
        first=deposit?"Geld aufs Sparbuch":"Geld von der Bank";
        second=deposit?"einzahlen (1#)":"abheben/leihen (4#).";
        const int64_t counter=raw32(state,0x0DCD+4*0x1AF+0x19B+size_t(manager)*4);
        limit=deposit?std::max<int64_t>(0,budget.balance):std::max<int64_t>(0,std::max<int64_t>(budget.bank,6000000-std::max<int64_t>(0,counter)));
    } else if (selectedParty>=5) {
        const size_t stock=size_t(selectedParty-5);
        first="Aktien von "+std::string(stock_names[stock]);
        second=deposit?" verkaufen.":" kaufen.";
        const int price=le16(state,0xB0+stock*72+70);
        if (!price) throw FormatError("HOK: zero current stock price");
        limit=deposit?quantities[stock]:std::max<int64_t>(0,budget.balance)/price;
    } else {
        first=(deposit?"Geld an ":"Geld von ")+ClassicFont::utf8_to_native(manager_name(state,selectedParty));
        second=deposit?" verleihen (2.5#).":" leihen (2.5#).";
        const int64_t own=principal(selectedParty);
        const size_t other=0x0DCD+size_t(selectedParty)*0x1AF;
        const int64_t counter=raw32(state,other+0x19B+size_t(manager)*4);
        if (deposit) limit=std::max<int64_t>(0,std::min<int64_t>(budget.balance,3000000-std::max<int64_t>(own,0)));
        else limit=std::max<int64_t>(std::max<int64_t>(own,0),std::max<int64_t>(0,std::min<int64_t>(raw32(state,other+0x18F),3000000-std::max<int64_t>(counter,0))));
    }
    const int left=deposit?167:36,right=left+115;
    canvas.text_native(mini,left,80,first,30,-1,right);
    canvas.text_native(mini,left,86,second,30,-1,right);
    if (amount<=0) return;
    const int width=int(std::clamp<int64_t>(amount/(limit/154+1),0,154));
    canvas.rect(4,89,313,96,0);
    if (deposit) canvas.rect(160,89,159+width,96,9);
    else canvas.rect(158-width,89,158,96,9);
    canvas.text_native(mini,left,95,selectedParty>=5?number(amount)+" St.":money(amount),30,-1,right);
}

}  // namespace em::ui
