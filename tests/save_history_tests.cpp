// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "game/checkpoint.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("save_history_tests DATA EVIDENCE OUTPUT");
    const std::filesystem::path data=argv[1],evidence=argv[2];
    const auto output=std::filesystem::path(argv[3])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto fixture=read_file(evidence/"load_guard"/"native_history.bin");
    require(fixture.size()==201&&fixture[0]==5,"independent native process history fixture");
    NativeSaveHistory native;native.count=fixture[0];
    for(size_t i=0;i<50;++i)native.identities[i]=le32(fixture,1+4*i);
    require(native.contains(0xFFFF9082u)&&native.contains(0xFFFFD886u)&&native.contains(0xFFFFABCDu),"RAM explains YF01/MONAT1 refusals and successful YF02 insertion");
    NativeSaveHistory fifo;require(fifo.contains(0),"native scan includes all unused zero slots");
    fifo.identities[49]=0x12345678u;require(fifo.contains(0x12345678u),"guard ignores insertion count");
    fifo={};for(uint32_t id=1;id<=55;++id)fifo.remember(id);
    require(fifo.count==50&&fifo.identities.front()==6&&fifo.identities.back()==55&&!fifo.contains(5),"fifty-entry FIFO shifts196bytes");
    fifo.remember(55);require(fifo.count==50&&fifo.identities[48]==55&&fifo.identities[49]==55,"native save insertion does not deduplicate");
    const auto exe=read_file(data/"EM.EXE"),source=read_file(evidence/"gui-yf02-export.hok");
    require(native_history_header(source),"native fixed version and marker guard");
    Game initial(exe,source,1234);initial.set_native_save_history(native);
    auto initial_phase=decode_game_checkpoint(initial.checkpoint_bytes());
    initial_phase.annual_distribution_cursors={16,7,2};
    const auto native_counts=read_file(evidence/"load_line_counts"/"native_counts.bin");
    require(native_counts==Bytes({2,3,0,0,0,0,0,0,0,0,0,0}),"independent native CC37..42 fixture");
    for(size_t m=0;m<6;++m)for(size_t kind=0;kind<2;++kind)
        require(initial_phase.line_counts[m][kind]==native_counts[m*2+kind],"cold entry updates only active club; inactive native BSS count pairs stay zero");
    for(size_t m=0;m<6;++m)for(size_t kind=0;kind<2;++kind)initial_phase.line_counts[m][kind]=native_counts[m*2+kind];
    write_new_save(output/"phase.oem",encode_game_checkpoint(initial_phase));
    Session session(data);session.load(output/"phase.oem");
    const auto before=session.game().checkpoint_bytes();bool blocked=false;
    try{session.load(evidence/"gui-yf01-export.hok");}catch(const NativeReloadError&){blocked=true;}
    require(blocked&&session.game().checkpoint_bytes()==before&&!session.dirty(),"duplicate identity fails without state, RNG, history or dirty changes");
    Game fresh(exe,source,424242);const auto fresh_bytes=fresh.native_save_bytes();
    require(!native.contains(le32(fresh_bytes,0xb4)),"fresh test identity is independently distinct");
    const auto before_native_load=decode_game_checkpoint(session.game().checkpoint_bytes());
    write_new_save(output/"fresh.hok",fresh_bytes);session.load(output/"fresh.hok");
    const auto after_native_load=decode_game_checkpoint(session.game().checkpoint_bytes());
    require(after_native_load.borland_random==before_native_load.borland_random&&after_native_load.game_random==before_native_load.game_random,"live HOK loading preserves ongoing random streams instead of reseeding");
    require(after_native_load.line_counts==before_native_load.line_counts,"live HOK loading preserves all six native count pairs, including inactive zeros");
    require(after_native_load.annual_distribution_cursors==before_native_load.annual_distribution_cursors,"live HOK loading preserves all three distribution cursors");
    require(decode_hok(session.game().save_bytes()).state==decode_hok(source).state&&session.game().native_save_history().count==6,"valid new identity loads identical native state and retains process history");
    auto corrupted=fresh_bytes;
    for(int b=0;b<4;++b)corrupted[0xb4+size_t(b)]=uint8_t(0x12345678u>>(8*b));
    corrupted.back()^=0x55;write_new_save(output/"bad.hok",corrupted);
    const auto raw_before=session.game().save_bytes();bool checksum_failed=false;
    try{session.load(output/"bad.hok");}catch(const NativeReloadError&){throw std::runtime_error("new identity reached wrong guard");}catch(const FormatError&){checksum_failed=true;}
    require(checksum_failed&&session.game().native_save_history().contains(0x12345678u)&&session.game().native_save_history().count==7&&session.game().save_bytes()==raw_before,"history insertion precedes checksum failure, leaving native state intact");
    blocked=false;try{session.load(output/"bad.hok");}catch(const NativeReloadError&){blocked=true;}
    require(blocked,"second corrupt-file attempt hits identity guard before checksum");
    session.save(output/"export.hok");
    require(session.game().native_save_history().count==8&&session.game().native_save_history().identities[7]==le32(read_file(output/"export.hok"),0xb4),"published native save appends its actual identity");
    const auto exported=session.game().checkpoint_bytes();blocked=false;
    try{session.save(output/"export.hok");}catch(const FormatError&){blocked=true;}
    require(blocked&&session.game().checkpoint_bytes()==exported,"failed publication commits neither history nor RNG");
    session.save(output/"resume.oem");Session restored(data);restored.load(output/"resume.oem",999);
    require(restored.game().checkpoint_bytes()==exported&&decode_game_checkpoint(exported).save_history==session.game().native_save_history(),"OEMv4 restores full native history exactly");
    Session legacy(data);legacy.load(evidence/"gui-yf01-checkpoint.oem");
    require(legacy.game().native_save_history()==NativeSaveHistory{},"legacy OEMv3 initializes an empty native history");
    // This fixture deliberately distinguishes cold day-menu setup from the
    // native live return1D1D: A0/B1, manually retained roles and stale caches.
    auto manual=decode_hok(source);manual.state[0x078a]=0;manual.state[0x078b]=1;
    const size_t team=0x17e7+size_t(manual.state[0x0dcd+0x14])*61;
    for(size_t n=0;n<6;++n)manual.state[team+0x18+n]=uint8_t(11+n);
    manual.state[team+0x2e]=1;manual.state[team+0x30]=2;manual.state[team+0x32]=4;
    manual.state[0x696d+10]=0;
    for(int b=0;b<4;++b)manual.header[0xb4+size_t(b)]=uint8_t(0xF1234567u>>(8*b));
    const auto manual_bytes=encode_hok(manual);write_new_save(output/"manual.hok",manual_bytes);
    auto manual_phase=initial_phase;manual_phase.line_counts[0]={4,1};manual_phase.line_counts[5]={3,2};
    write_new_save(output/"manual-phase.oem",encode_game_checkpoint(manual_phase));
    Session live(data);live.load(output/"manual-phase.oem");live.load(output/"manual.hok",9999);
    require(decode_hok(live.game().save_bytes()).state==decode_hok(manual_bytes).state,"live load retains A0/B1, manual roster and every loaded team-cache byte");
    require(decode_game_checkpoint(live.game().checkpoint_bytes()).line_counts==manual_phase.line_counts,"live counts installed before any lineup/cache operation");
    Game cold(exe,manual_bytes);
    require(decode_hok(cold.save_bytes()).state!=decode_hok(manual_bytes).state,"fixture distinguishes cold setup from live-load path");
    ClassicApplication app(data,output,output/"phase.oem");
    const auto open_file=[&](const char* filename){app.click(190,160,false);app.click(25,219,false);app.click(120,200,false);app.text_input(filename);app.key(SDLK_RETURN);};
    open_file("D:\\fresh.hok");require(app.screen()==ClassicScreen::Main,"fresh native file closes chooser normally");
    const auto app_before=app.session().game().checkpoint_bytes();
    open_file("D:\\fresh.hok");require(app.screen()==ClassicScreen::Files&&app.session().game().checkpoint_bytes()==app_before,"original timed refusal stays over file chooser");
    app.text_input("a");app.key(SDLK_SPACE);app.click(0,0,false);app.request_exit();
    require(app.screen()==ClassicScreen::Files&&!app.finished(),"key, text, mouse and window close cannot shorten native timer");
    SDL_Delay(1500);app.tick();
    require(app.screen()==ClassicScreen::Main&&app.session().game().checkpoint_bytes()==app_before,"timer expiry returns to main without repeating chooser or changing game/RNG");
    std::puts("Native identity history and OEMv4 passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Save history: %s\n",e.what());return 1;}}
