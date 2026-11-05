// SPDX-License-Identifier: GPL-3.0-only
// Fixed read-only DOSBox observations: pending quote, Behalten and the shifted-row sale.
#include "ui/classic_app.hpp"
#include "game/checkpoint.hpp"
#include <SDL.h>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;namespace fs=std::filesystem;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void same_state(const Bytes& actual,const Bytes& expected,const char* message){
    const auto a=decode_hok(actual),b=decode_hok(expected);size_t differences=0;
    for(size_t n=0;n<a.state.size();++n)if(a.state[n]!=b.state[n]){if(differences<12)std::fprintf(stderr,"state%04zx:%u expected%u\n",n,a.state[n],b.state[n]);++differences;}
    if(differences)throw std::runtime_error(std::string(message)+":"+std::to_string(differences)+" bytes");
}
static uint32_t seed(const Game& g){return decode_game_checkpoint(g.checkpoint_bytes()).borland_random;}
static void native_rgb(const ClassicCanvas& canvas,const fs::path& path){
    const auto expected=read_file(path);const auto actual=canvas.argb();require(expected.size()==320*240*3,"native sale golden size");size_t count=0,first=0;
    for(size_t p=0;p<actual.size();++p){const uint32_t rgb=uint32_t(expected[p*3])<<16|uint32_t(expected[p*3+1])<<8|expected[p*3+2];if((actual[p]&0xffffff)!=rgb){if(!count)first=p;++count;}}
    if(count)throw std::runtime_error(path.filename().string()+":"+std::to_string(count)+" pixels, first "+std::to_string(first%320)+","+std::to_string(first/320));
    std::printf("%s:76800/76800 native pixels\n",path.filename().string().c_str());
}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("sale_quote_tests DATA NATIVE WORK");
    const fs::path data=argv[1],native=argv[2],work=argv[3];const auto exe=read_file(data/"EM.EXE");const auto tables=load_contract_tables(exe);
    BorlandRandom boundary(1);const auto amount=native_sale_amounts(600000,true,false,2,boundary);
    require(amount.gross==504600&&amount.severance==144170&&amount.net==360430&&boundary.state()==2156045615u,"native signed16 jitter and division-before-multiply vector");
    BorlandRandom divisor(7);bool zero_divisor=false;try{(void)native_sale_range(divisor,-32768,32767);}catch(const FormatError&){zero_divisor=true;}
    require(zero_divisor&&divisor.state()==7,"native zero divisor is reported without a fabricated draw");
    const auto before=read_file(native/"native-transfer-entry.hok");auto h=decode_hok(before);const auto original=h.state;BorlandRandom random(1829654223u);SaleQuote quote;
    require(begin_native_sale(h,tables,random,0,6,51,quote),"native club quote opens");
    require(quote.id==26&&quote.bidder==3&&quote.amounts.gross==1130100&&quote.amounts.net==1130100&&quote.amounts.severance==0,"native SCHIFFL quote");
    require(h.state==original&&random.state()==1941922524u,"begin mutates only one native random draw");
    require(finish_native_sale(h,random,quote,false),"Behalten completes");same_state(encode_hok(h),read_file(native/"native-club-offer-kept.hok"),"native Behalten all47761");
    require(random.state()==1941922524u,"Behalten has no further draws");const auto kept=h.state;
    require(!finish_native_sale(h,random,quote,true)&&h.state==kept&&random.state()==1941922524u,"stale quote cannot sell");
    h=decode_hok(read_file(native/"native-club-sale-before.hok"));random=BorlandRandom(1941922524u);
    require(begin_native_sale(h,tables,random,0,6,51,quote)&&quote.amounts.gross==1207500&&random.state()==3826679693u,"second native sale quote");
    same_state(encode_hok(h),read_file(native/"native-club-sale-open.hok"),"native sale open all47761");int buyer=-1;
    require(finish_native_sale(h,random,quote,true,&buyer)&&buyer==0&&random.state()==797554904u,"native shifted bidder and three buyer draws");
    same_state(encode_hok(h),read_file(native/"native-club-sold.hok"),"native sold all47761");
    const auto pool_before=read_file(native/"native-pool-sale-before.hok");
    h=decode_hok(pool_before);random=BorlandRandom(797554904u);
    require(begin_native_sale(h,tables,random,4,0,32767,quote),"unsupported buyer pointer still permits Behalten");
    const auto unsupported=h.state;const auto unsupported_seed=random.state();bool outside=false;
    try{(void)finish_native_sale(h,random,quote,true);}catch(const FormatError&){outside=true;}
    require(outside&&h.state==unsupported&&random.state()==unsupported_seed,"unavailable native heap context is rejected before mutation");
    require(finish_native_sale(h,random,quote,false),"Behalten never dereferences buyer SI");
    h=decode_hok(pool_before);random=BorlandRandom(797554904u);
    require(begin_native_sale(h,tables,random,4,0,250,quote)&&quote.id==26&&quote.bidder==3,"native owned pool quote opens");
    require(quote.amounts.gross==798600&&quote.amounts.severance==228170&&quote.amounts.net==570430&&random.state()==2464783399u,"native pool worth, jitter, factor and severance");
    same_state(encode_hok(h),read_file(native/"native-pool-sale-open.hok"),"native pool open all47761");
    require(finish_native_sale(h,random,quote,true,&buyer)&&buyer==3&&random.state()==3007881130u,"native pool buyer and three further draws");
    same_state(encode_hok(h),read_file(native/"native-pool-sold.hok"),"native pool sold all47761");
    Game pool_game(exe,pool_before,797554904u);
    require(seed(pool_game)==797554904u&&pool_game.offers().size()==1&&pool_game.offers()[0].amount==0,"owned pool import does not price offers");
    require(pool_game.begin_sale_offer(4,0,250,quote)&&pool_game.finish_sale_offer(quote,true),"Game pool quote and sell");
    same_state(pool_game.save_bytes(),read_file(native/"native-pool-sold.hok"),"Game pool sold all47761");
    require(seed(pool_game)==3007881130u&&pool_game.offers().empty(),"Game pool final seed and metadata");
    Game game(exe,read_file(native/"native-club-sale-before.hok"),1941922524u);
    require(seed(game)==1941922524u&&game.offers().size()==1&&game.offers()[0].amount==0,"import keeps price unquoted and seed unchanged");
    require(game.begin_sale_offer(0,6,51,quote)&&seed(game)==3826679693u,"Game quote commits random state");
    Game resumed(exe,game.checkpoint_bytes());require(seed(resumed)==seed(game),"checkpoint keeps quote-time random advance");
    require(resumed.finish_sale_offer(quote,true)&&seed(resumed)==797554904u,"Game finishes restored quote context");
    same_state(resumed.save_bytes(),read_file(native/"native-club-sold.hok"),"Game exported native sold all47761");
    require(resumed.offers().empty()&&resumed.budget().balance==2762300,"Game budget and interest metadata refreshed");
    fs::create_directories(work);fs::path initial;
    for(int n=1;;++n){initial=work/("sale-"+std::to_string(n)+".OEM");if(!fs::exists(initial))break;}
    Game ui_game(exe,before,1829654223u);write_new_save(initial,ui_game.checkpoint_bytes());
    ClassicApplication app(data,work,initial);
    //The captured native process previously visited Stadium; its exit patches KB4 edge2 to3.
    const auto cold=app.session().game().checkpoint_bytes();app.render_screen("stadium");app.click(20,50,true);
    require(app.screen()==ClassicScreen::Main&&app.session().game().checkpoint_bytes()==cold,"stadium atlas lifetime is independent of game state/RNG");
    app.render_screen("transfer");app.motion(51,66);app.draw();app.click(51,66,false);app.draw();
    write_new_save(initial.string()+".bmp",app.canvas().bmp_bytes());
    require(seed(app.session().game())==1941922524u,"original row opens price modal once");native_rgb(app.canvas(),native/"raster_goldens"/"club-sale-offer.rgb");
    const auto opened=app.session().game().checkpoint_bytes();
    app.draw();app.key(SDLK_ESCAPE);app.key(SDLK_RETURN);app.text_input("x");app.click(200,100,true);app.request_exit();app.draw();
    require(!app.finished()&&app.screen()==ClassicScreen::Transfer&&app.session().game().checkpoint_bytes()==opened,"modal repaint, keys, rightclick and close cannot consume or reroll quote");
    app.click(40,88,false);same_state(app.native_ui_bytes(),read_file(native/"native-club-offer-kept.hok"),"UI Behalten all47761");
    require(seed(app.session().game())==1941922524u,"UI Behalten no extra draws");
    const fs::path pool_initial=initial.string()+"-pool.OEM";Game pool_ui(exe,pool_before,797554904u);write_new_save(pool_initial,pool_ui.checkpoint_bytes());
    ClassicApplication pool(data,work,pool_initial);pool.render_screen("stadium");pool.click(20,50,true);
    pool.render_screen("transfer");pool.motion(250,42);pool.draw();pool.click(250,42,false);pool.draw();
    write_new_save(pool_initial.string()+".bmp",pool.canvas().bmp_bytes());
    native_rgb(pool.canvas(),native/"raster_goldens"/"pool-sale-offer.rgb");
    require(seed(pool.session().game())==2464783399u,"UI pool open draws exactly three");
    pool.click(118,88,false);same_state(pool.native_ui_bytes(),read_file(native/"native-pool-sold.hok"),"UI pool sell all47761");
    require(seed(pool.session().game())==3007881130u,"UI pool sell commits buyer helper");
    std::puts("Native sale quote, decision, shifted buyer, checkpoint and modal input passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Sale quote: %s\n",e.what());return 1;}}
