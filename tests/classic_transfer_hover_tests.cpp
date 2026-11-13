// SPDX-License-Identifier: GPL-3.0-only
// Hover help of the transfer pool on the transfer page (ui/classic_transfer_hover.cpp): which pool row lies under
// the mouse, and the help line naming where the listed player comes from ("von <club or manager>" for players of
// a club, "aus <country>" for foreign players with an id above 179), with the exact text branches, row geometry
// and drawing area of the original game.
//
// Usage: classic_transfer_hover_tests [DATA] — DATA holds EM.EXE and the graphics; without it only the rejection
// of an empty EXE is checked.
#include "ui/classic_transfer_hover.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace em;
using namespace em::ui;
namespace {
// Number of failed CHECKs.
int failures = 0;
// Reports a failed condition with file and line and counts it, without stopping the test.
#define CHECK(value) do { if (!(value)) { \
    std::fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#value); \
    ++failures; } } while (0)
// True when `f` throws a std::exception.
template<class F> bool throws(F f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}
// State offsets: transfer pool rows (52 bytes; +15 player id), global record of player id 1 (33 bytes; +0x1A
// status, +0x1D club), team records (61 bytes, name first) and manager blocks (0x1AF bytes, name first).
constexpr size_t kPool=0x81cd,kPlayers=0x4fc6,kTeams=0x17e7,kManagers=0x0dcd;
// Writes the zero-terminated `text` at state offset `at`.
void name(Bytes& state,size_t at,const std::string& text) {
    std::copy(text.begin(),text.end(),state.begin()+std::ptrdiff_t(at));
    state.at(at+text.size())=0;
}
// Row geometry, help texts for club, foreign and manager-owned players, and the pixels the help drawing touches.
void original_goldens(const std::filesystem::path& data) {
    ClassicTransferHelp help(read_file(data/"EM.EXE"));
    HokFile h;h.state.resize(0x8dfd);
    h.state[kPool+15]=179;h.state[kPool+52+15]=180;h.state[kPool+104+15]=1;
    const size_t p179=kPlayers+178*33,p180=kPlayers+179*33;
    h.state[p179+0x1a]=4;h.state[p179+0x1d]=5;
    h.state[p180+0x1a]=4;h.state[p180+0x1d]=6;
    h.state[kPlayers+0x1a]=2;h.state[kPlayers+0x1d]=7;
    const std::string club=std::string("Club [")+char(0x99);
    name(h.state,kTeams+5*61,club);name(h.state,kTeams+6*61,"Schweiz");
    name(h.state,kTeams+7*61,"WRONG TEAM");
    const std::string owner=std::string("m")+char(0x81)+"l"+char(0x84)+"r"+char(0x94)+" {~";
    const std::string uppercase=std::string("M")+char(0x9a)+"L"+char(0x8e)+"R"+char(0x99)+" {~";
    name(h.state,kManagers+2*0x1af,owner);
    const Bytes before=h.state;
    const auto at=[&](int x,int y,int manager=0){return classic_transfer_hover(h,manager,x,y,help);};
    CHECK(at(175,36).row==-1 && at(176,36).row==0);
    CHECK(at(1000,36).row==0); //native x>175 has no extra upper bound
    CHECK(at(176,20).row==-1 && at(176,21).row==0);
    CHECK(at(176,51).row==0 && at(176,52).row==1);
    CHECK(at(176,67).row==1 && at(176,68).row==2);
    CHECK(at(176,83).row==2 && at(176,84).row==-1);
    CHECK(at(176,36).native_help=="von "+club); //team branch preserves case and bytes
    CHECK(at(176,52).native_help=="aus Schweiz"); //strict ID>179 prefix boundary
    CHECK(at(176,68).native_help=="von "+uppercase);
    CHECK(at(176,68,1).native_help=="von "+uppercase); //current manager is not owner selector
    CHECK(h.state==before);
    h.state[p180+0x1a]=2;
    CHECK(at(176,52).native_help=="aus "+uppercase); //prefix does not choose name source
    h.state[kPool+104+12]=255;
    CHECK(at(176,68).native_help=="von "+uppercase); //pool loan byte is not the selector
    h.state=before;h.state[kPool+52+15]=0;
    CHECK(at(176,52).row==1 && at(176,68).row==-1); //count-limited, un-compacted gap
    std::fill(h.state.begin()+std::ptrdiff_t(kPool),h.state.begin()+std::ptrdiff_t(kPool+12*52),0);
    h.state[kPool+12*52+15]=1;
    CHECK(at(176,36).row==-1); //record12 is outside the native12-record count scan
    for(int slot=0;slot<12;++slot)h.state[kPool+size_t(slot)*52+15]=179;
    CHECK(at(176,227).row==11 && at(176,228).row==-1);
    CHECK(throws([&]{at(176,36,6);}));
    h.state.resize(1);CHECK(throws([&]{at(176,36);}));

    ClassicCanvas canvas(data);
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),55);
    draw_classic_transfer_hover(canvas,{1,""});
    CHECK(canvas.pixels().at(180,52)==16 && canvas.pixels().at(314,68)==16);
    CHECK(canvas.pixels().at(179,52)==55 && canvas.pixels().at(315,68)==55);
    CHECK(canvas.pixels().at(7,211)==4 && canvas.pixels().at(163,217)==4);
    CHECK(canvas.pixels().at(6,211)==55 && canvas.pixels().at(164,217)==55);
    CHECK(canvas.pixels().at(7,218)==55);
    const auto drawn=canvas.pixels().pixels;
    draw_classic_transfer_hover(canvas,{});
    CHECK(canvas.pixels().pixels==drawn); //native leave retains help; App restores old rules31
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),55);
    draw_classic_transfer_hover(canvas,{1,""},true,false);
    CHECK(canvas.pixels().at(180,52)==16 && canvas.pixels().at(7,211)==55);
    std::fill(canvas.pixels().pixels.begin(),canvas.pixels().pixels.end(),55);
    draw_classic_transfer_hover(canvas,{1,""},false,true);
    CHECK(canvas.pixels().at(180,52)==55 && canvas.pixels().at(7,211)==4);
}
}
int main(int argc,char** argv) {
    try {
        CHECK(throws([]{ClassicTransferHelp help(Bytes{});}));
        if(argc>1)original_goldens(argv[1]);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Classic transfer hover: unexpected exception: %s\n",e.what());return 2;
    }
    return failures?1:0;
}
