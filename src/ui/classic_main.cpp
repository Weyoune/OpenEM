// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "ui/classic_keyboard.hpp"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#include <cstdio>
#include <fstream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace {
#ifdef _WIN32
struct WindowsInputBridge { void* window=nullptr;std::ofstream* trace=nullptr; };
void SDLCALL input_message(void* user,void* window,unsigned message,Uint64 key,Sint64 details){
    const auto& bridge=*static_cast<WindowsInputBridge*>(user);
    if(window!=bridge.window)return;
    if(bridge.trace->is_open()&&(message==WM_KEYDOWN||message==WM_KEYUP))
        *bridge.trace<<"windows="<<message<<" virtual="<<key<<" details="<<details<<'\n';
    if(const auto translated=em::ui::classic_virtual_key(message,key,uint64_t(details))){
        SDL_Event event{};event.type=SDL_KEYDOWN;event.key.state=SDL_PRESSED;event.key.keysym.sym=*translated;SDL_PushEvent(&event);
    }
}
#endif
fs::path project_root(fs::path start){for(int n=0;n<7&&!start.empty();n++,start=start.parent_path())if(fs::is_directory(start/"work"/"EM"))return start;return fs::current_path();}
}
int main(int argc,char**argv){
    using namespace em::ui;
    bool headless=false;
    try{
        char* base=SDL_GetBasePath();const fs::path root=project_root(base?path_from_text(base):fs::current_path());SDL_free(base);
        fs::path data=root/"work"/"EM",saves=root/"work"/"saves",save,render,input_log;
        std::string render_screen="main";int cursor_x=-1,cursor_y=-1;bool new_game=false;
        for(int n=1;n<argc;n++){
            const std::string option=argv[n];
            if(option=="--new-game"){new_game=true;continue;}
            if(n+1>=argc)throw std::runtime_error("openem [--data folder] [--save file] [--new-game] [--render new.bmp] [--screen name]");
            const std::string value=argv[++n];
            if(option=="--data")data=path_from_text(value);else if(option=="--save")save=path_from_text(value);
            else if(option=="--input-log")input_log=path_from_text(value);
            else if(option=="--cursor"){const auto comma=value.find(',');if(comma==std::string::npos)throw std::runtime_error("--cursor x,y");cursor_x=std::stoi(value.substr(0,comma));cursor_y=std::stoi(value.substr(comma+1));}
            else if(option=="--render"){render=path_from_text(value);headless=true;}else if(option=="--screen")render_screen=value;else throw std::runtime_error("Unknown option");
        }
        // --new-game opens the start menu of a new game (game/new_game.hpp) instead of the default START.HOK
        if(save.empty()&&!new_game&&fs::exists(data/"START.HOK"))save=data/"START.HOK";
        ClassicApplication app(data,saves,save);
        if(cursor_x>=0&&cursor_y>=0)app.motion(cursor_x,cursor_y);
        if(!render.empty()){app.render_screen(render_screen);write_new_save(render,app.canvas().bmp_bytes());return 0;}
        if(render_screen!="main")app.render_screen(render_screen);
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
        SDL_Window* window=SDL_CreateWindow("OpenEM – Eishockey Manager",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,480,SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window,640,480);
        // The native canvas is tiny and already rasterized on the CPU. A
        // software presenter also remains capturable in remote/occluded windows.
        SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer)throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(renderer,320,240);SDL_RenderSetIntegerScale(renderer,SDL_TRUE);
        SDL_Texture* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,240);
        if(!texture)throw std::runtime_error(SDL_GetError());
        SDL_StartTextInput();
        SDL_ShowCursor(SDL_DISABLE);
        std::ofstream trace;
        if(!input_log.empty()){write_new_save(input_log,{});trace.open(input_log,std::ios::app);}
#ifdef _WIN32
        SDL_SysWMinfo info{};SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window,&info))throw std::runtime_error(SDL_GetError());
        WindowsInputBridge bridge{info.info.win.window,&trace};
        SDL_SetWindowsMessageHook(input_message,&bridge);
#endif
        while(!app.finished()){
            const uint64_t frame_start=SDL_GetTicks64();
            SDL_Event event;
            while(SDL_PollEvent(&event)){
                if(trace.is_open()){
                    if(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP)trace<<event.type<<" key="<<event.key.keysym.sym<<" scan="<<event.key.keysym.scancode<<'\n';
                    else if(event.type==SDL_TEXTINPUT)trace<<"text="<<event.text.text<<'\n';
                    else if(event.type==SDL_MOUSEBUTTONDOWN)trace<<"mouse="<<int(event.button.button)<<" x="<<event.button.x<<" y="<<event.button.y<<'\n';
                    trace.flush();
                }
                if(event.type==SDL_QUIT)app.request_exit();
                else if(event.type==SDL_MOUSEBUTTONDOWN)app.click(event.button.x,event.button.y,event.button.button==SDL_BUTTON_RIGHT);
                else if(event.type==SDL_MOUSEBUTTONUP)app.release();
                else if(event.type==SDL_MOUSEMOTION)app.motion(event.motion.x,event.motion.y);
                else if(event.type==SDL_KEYDOWN)app.key(event.key.keysym.sym);
                else if(event.type==SDL_TEXTINPUT)app.text_input(event.text.text);
            }
            app.tick();app.draw();const auto argb=app.canvas().argb();SDL_UpdateTexture(texture,nullptr,argb.data(),320*4);
            SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);SDL_RenderPresent(renderer);
            const uint64_t elapsed=SDL_GetTicks64()-frame_start;if(elapsed<16)SDL_Delay(Uint32(16-elapsed));
        }
#ifdef _WIN32
        SDL_SetWindowsMessageHook(nullptr,nullptr);
#endif
        SDL_ShowCursor(SDL_ENABLE);SDL_StopTextInput();SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }catch(const std::exception&e){if(headless)std::fprintf(stderr,"%s\n",e.what());else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"OpenEM",e.what(),nullptr);SDL_Quit();return 1;}
}
