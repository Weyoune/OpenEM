// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "game/checkpoint.hpp"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void native_rgb(const ClassicCanvas& canvas,const std::filesystem::path& path){
    const auto expected=read_file(path);const auto actual=canvas.argb();
    require(expected.size()==320*240*3,"native youth golden size");size_t count=0,first=0;
    for(size_t p=0;p<actual.size();++p){const uint32_t rgb=uint32_t(expected[p*3])<<16|uint32_t(expected[p*3+1])<<8|expected[p*3+2];if((actual[p]&0xffffff)!=rgb){if(!count)first=p;++count;}}
    if(count)throw std::runtime_error(path.filename().string()+": "+std::to_string(count)+" pixels, first "+std::to_string(first%320)+","+std::to_string(first/320));
    std::printf("%s:76800/76800 native pixels\n",path.filename().string().c_str());
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("youth_input_tests DATA NATIVE");
    const std::filesystem::path data=argv[1],native=argv[2];
    struct Scene{const char* name;int x,y;};
    const Scene scenes[]{{"entry",187,219},{"step",200,173},{"mixed",190,221},{"max",190,197},{"reduced",110,221}};
    int failures=0;
    for(const auto& scene:scenes){try{
        ClassicApplication app(data,native/"unused-saves",native/(std::string("youth-")+scene.name+".hok"));
        const auto before=app.session().game().checkpoint_bytes();
        app.motion(scene.x,scene.y);app.render_screen("youth");
        native_rgb(app.canvas(),native/"raster_goldens"/(std::string("youth-")+scene.name+".rgb"));
        require(app.session().game().checkpoint_bytes()==before,"view and raster generation are pure");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());++failures;}}
    require(!failures,"native youth raster scenes differ");
    ClassicApplication app(data,native/"unused-saves",native/"youth-entry.hok");
    const auto before=app.session().game().checkpoint_bytes();
    app.click(40,100,false);app.click(187,219,false);
    require(app.screen()==ClassicScreen::Youth&&app.session().game().checkpoint_bytes()==before,"original Manager3 route is RNG/state neutral");
    app.key(SDLK_ESCAPE);app.text_input("a");
    require(app.screen()==ClassicScreen::Youth,"native youth ignores keyboard including Escape");
    for(int y:{167,178,191,202,215,226})app.click(319,y,false);
    require(app.session().game().checkpoint_bytes()==before,"strict row boundaries cannot edit funding");
    const auto matches=[&](const char* name){require(decode_hok(app.native_ui_bytes()).state==decode_hok(read_file(native/(std::string("youth-")+name+".hok"))).state,"UI matches all native state bytes");};
    app.click(200,173,false);matches("step");
    app.click(155,197,false);app.click(190,221,false);matches("mixed");
    app.click(190,173,false);app.click(190,197,false);matches("max");
    app.click(121,197,false);app.click(110,221,false);matches("reduced");
    const auto changed=app.session().game().checkpoint_bytes();
    app.click(160,74,true);
    require(app.screen()==ClassicScreen::Main&&app.session().game().checkpoint_bytes()==changed,"rightclick closes with changes already persisted");
    std::puts("Native youth input passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Youth input: %s\n",e.what());return 1;}}
