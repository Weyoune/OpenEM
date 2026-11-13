// SPDX-License-Identifier: GPL-3.0-only
// Tests for the classic UI's stadium screen. Part 1 renders seven stadium scenes (overview,
// planned places, quote dialog, pending question, ...) from reference positions of human
// manager 2 and compares them pixel by pixel with screens recorded from the original game.
// Part 2 drives the screen with mouse/keyboard input through the same sequence as
// stadium_game_tests (ticket price, declined bowling, timed refusal notice, accepted condition
// upgrade) and requires the complete save state after each step to match the recorded one.
// Arguments: the game data folder and the folder holding the reference saves and screens.
#include "ui/classic_app.hpp"
#include "game/checkpoint.hpp"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;
// Throws `s` as a test failure unless `v`.
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
// Compares the 320x240 canvas with a reference screen stored as raw 24-bit RGB (row-major) and
// fails with the number of differing pixels and the position of the first one.
void native_rgb(const ClassicCanvas& canvas,const std::filesystem::path& path){
    const auto expected=read_file(path);const auto actual=canvas.argb();
    require(expected.size()==320*240*3,"native golden size");size_t count=0,first=0;
    for(size_t p=0;p<actual.size();++p){const uint32_t rgb=uint32_t(expected[p*3])<<16|uint32_t(expected[p*3+1])<<8|expected[p*3+2];if((actual[p]&0xffffff)!=rgb){if(!count)first=p;++count;}}
    if(count)throw std::runtime_error(path.filename().string()+": "+std::to_string(count)+" pixels, first "+std::to_string(first%320)+","+std::to_string(first/320));
    std::printf("%s:76800/76800 native pixels\n",path.filename().string().c_str());
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("stadium_input_tests DATA NATIVE");
    const std::filesystem::path data=argv[1],native=argv[2];
    // A rendered scene: screen name, start position and mouse position (affects hover output).
    struct Scene{const char* name;const char* source;int x,y;};
    const Scene scenes[]{
        {"stadium","stadium-h2-entry.hok",134,219},
        {"stadium-planned","stadium-h2-entry.hok",187,224},
        {"stadium-quote","stadium-h2-ticket14.hok",49,231},
        {"stadium-pending","stadium-h2-ticket14-seeded.oem",259,213},
        {"stadium-condition","stadium-h2-declined-bowling.hok",196,213},
        {"stadium-condition-pending","stadium-h2-condition-seeded.oem",259,213},
        {"stadium-built","stadium-h2-accepted-condition.hok",123,171}};
    int raster_failures=0;
    for(const auto& scene:scenes){try{
        ClassicApplication app(data,native/"unused-saves",native/scene.source);
        app.motion(scene.x,scene.y);app.render_screen(scene.name);app.draw();
        native_rgb(app.canvas(),native/"raster_goldens"/(std::string(scene.name)+"-h2.rgb"));
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());++raster_failures;}}
    require(!raster_failures,"stadium native raster scenes differ");
    ClassicApplication app(data,native/"unused-saves",native/"stadium-h2-seeded.oem");
    const auto initial=app.session().game().checkpoint_bytes();
    app.click(40,100,false);app.motion(134,219);app.click(134,219,false);
    require(app.screen()==ClassicScreen::Stadium&&app.session().game().checkpoint_bytes()==initial,"original menu route opens stadium without RNG or raw changes");
    app.motion(187,224);app.click(187,224,true);
    require(app.screen()==ClassicScreen::Stadium&&app.session().game().checkpoint_bytes()==initial,"lower rightclick changes only local display mode");
    app.motion(196,193);app.click(196,193,false);
    const auto matches=[&](const char* name){return decode_hok(app.native_ui_bytes()).state==decode_hok(read_file(native/(std::string("stadium-h2-")+name+".hok"))).state;};
    require(matches("ticket14"),"UI ticket click matches full native state");
    const auto ticket=app.session().game().checkpoint_bytes();
    app.motion(49,231);app.click(49,231,false);
    require(app.session().game().checkpoint_bytes()==ticket,"opening target quote consumes no RNG");
    app.motion(259,213);app.click(259,213,false);
    require(matches("pending-bowling"),"UI quote confirmation reaches native pending weeks");
    const auto pending=app.session().game().checkpoint_bytes();
    app.key(SDLK_ESCAPE);app.request_exit();
    require(app.session().game().checkpoint_bytes()==pending&&app.screen()==ClassicScreen::Stadium,"Escape/window close cannot silently resolve pending construction");
    app.motion(193,171);app.click(193,171,false);
    require(matches("declined-bowling")&&app.session().game().stadium_refusals()[0][8]==1,"UI No matches native state and refusal scratch");
    const auto declined=app.session().game().checkpoint_bytes();
    app.motion(49,231);app.click(49,231,false);app.text_input("a");app.key(SDLK_SPACE);
    app.draw();const auto timed_pixels=app.canvas().argb();
    app.click(35,203,false);app.draw();
    require(app.canvas().argb()==timed_pixels,"native timed refusal cannot be acknowledged by text, key or click");
    SDL_Delay(1500);app.tick();
    require(app.session().game().checkpoint_bytes()==declined,"blocked retry and timed notice expiry consume no RNG");
    app.motion(35,203);app.click(35,203,false);
    app.motion(196,213);app.click(196,213,false); //the unaffordable next step keeps target 3
    app.motion(259,213);app.click(259,213,false);
    require(matches("pending-condition"),"UI bounded target spinner and second offer match native pending state");
    app.motion(123,171);app.click(123,171,false);
    require(matches("accepted-condition")&&decode_game_checkpoint(app.session().game().checkpoint_bytes()).borland_random==2618836624u,"UI Yes books native cash/plan/week/RNG exactly");
    app.draw();native_rgb(app.canvas(),native/"raster_goldens"/"stadium-built-h2.rgb");
    const auto built=app.session().game().checkpoint_bytes();
    app.motion(160,74);app.click(160,74,true);
    require(app.screen()==ClassicScreen::Main&&app.session().game().checkpoint_bytes()==built,"upper rightclick returns without changing construction or RNG");
    std::puts("Native stadium input passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Stadium input: %s\n",e.what());return 1;}}
