// SPDX-License-Identifier: GPL-3.0-only
// Tests for share trading (Game::trade_shares) on the start position START.HOK: refusals,
// purchase and sale bookings with their commission, the two-stage rounding of the cost basis,
// the maximum purchase that ignores the commission (and may overdraw), 32-bit holdings in the
// HOK, and the 32-bit wrap of the commission product. Optionally a recorded sale (B3.HOK ->
// S1.HOK, saved by the original game) is replayed and compared byte for byte.
// Share block of manager 0 in the state: from 0x30, 8 bytes per company (i32 cost basis, i32
// quantity), so company 1 is at 0x38/0x3C and company 3 at 0x48/0x4C.
// Arguments: the game data folder (EM.EXE) and the reference folder with START.HOK.
#include "game/game.hpp"
#include <cstdio>
#include <stdexcept>

using namespace em;
// Throws `message` as a test failure unless `condition`.
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
// Writes a little-endian u32 into `bytes` at `at`.
static void put32(Bytes& bytes,size_t at,uint32_t value){for(int b=0;b<4;b++)bytes.at(at+size_t(b))=uint8_t(value>>(b*8));}

int main(int argc,char**argv){
    try{
        if(argc!=3)throw std::runtime_error("stock_tests DATA REFERENCE");
        const auto exe=read_file(std::filesystem::path(argv[1])/"EM.EXE");
        const auto start=read_file(std::filesystem::path(argv[2])/"START.HOK");
        Game game(exe,start,1);
        const auto before=game.checkpoint_bytes();
        require(game.trade_shares(4,3,false)==StockResult::Invalid,"company bounds");
        require(game.trade_shares(1,0,false)==StockResult::Invalid,"zero quantity");
        require(game.trade_shares(1,1,true)==StockResult::InsufficientShares,"empty portfolio sale");
        require(game.trade_shares(1,6251,false)==StockResult::InsufficientCash,"native gross affordability limit");
        require(game.checkpoint_bytes()==before,"refused transactions leave checkpoint intact");
        // PlayTime (company 1) starts at a price of 240. Buying 3 costs 720 + 10 commission;
        // selling 1 nets 240 - 3; the remaining cost basis is floor(730 * floor(200 / 3) / 100) = 481.
        require(game.trade_shares(1,3,false)==StockResult::Traded,"buy three PlayTime shares");
        auto bought=decode_hok(game.save_bytes());
        require(le32(bought.state,0x38)==730&&le32(bought.state,0x3C)==3,"8byte company1 purchase posting");
        require(game.budget().balance==1499270,"buy fee charged to cash");
        require(bought.finance.shares[0]==0&&bought.finance.shares[1]==3,"decoded company1 holding");
        const auto checkpoint=game.checkpoint_bytes();
        Game continued(exe,checkpoint);
        require(continued.trade_shares(1,1,true)==StockResult::Traded,"partial sale after reload");
        auto sold=decode_hok(continued.save_bytes());
        require(le32(sold.state,0x38)==481&&le32(sold.state,0x3C)==2,"native two-stage cost basis rounding");
        require(continued.budget().balance==1499507,"sale net of commission");
        require(continued.trade_shares(1,2,true)==StockResult::Traded,"sell remainder");
        auto empty=decode_hok(continued.save_bytes());
        require(le32(empty.state,0x38)==0&&le32(empty.state,0x3C)==0,"full sale clears cost and quantity");
        require(continued.budget().balance==1499980,"round-trip loses buy and sell fees");
        // The original's maximum-buy limit ignores the commission, so it can overdraw.
        Game maximum(exe,start,1);
        require(maximum.trade_shares(1,6250,false)==StockResult::Traded,"maximum native buy");
        require(maximum.budget().balance==-22500,"maximum buy may overdraw by fee");
        // Distinguish u32 holdings from the previous u16/six-byte decoder.
        auto wide=decode_hok(start);put32(wide.state,0x48,123456);put32(wide.state,0x4C,65537);
        const auto wide_bytes=encode_hok(wide);
        auto decoded=decode_hok(wide_bytes);
        require(decoded.finance.shares[3]==65537&&decoded.finance.share_cost[3]==41152,"fourth holding uses full32bit quantity");
        require(encode_hok(decoded)==wide_bytes,"wide holdings round-trip exactly");
        // The commission product wraps at 32 bits before the unsigned division, as in the original.
        // (0x0F5C = manager 0's balance, manager block 0x0DCD + 0x18F.)
        auto large=decode_hok(start);put32(large.state,0x0F5C,300000000);
        Game overflow(exe,encode_hok(large),1);
        require(overflow.trade_shares(1,1250000,false)==StockResult::Traded,"large native purchase");
        require(overflow.budget().balance==-205032,"fee multiplication retains native32bit wrap");
        const auto native_buy=std::filesystem::path(argv[2])/"B3.HOK",native_sale=std::filesystem::path(argv[2])/"S1.HOK";
        if(std::filesystem::exists(native_buy)&&std::filesystem::exists(native_sale)){
            Game replay(exe,read_file(native_buy),1);
            require(replay.trade_shares(1,1,true)==StockResult::Traded,"replay actual native partial sale");
            auto actual=decode_hok(replay.save_bytes());const auto expected_bytes=read_file(native_sale);const auto expected=decode_hok(expected_bytes);
            require(actual.state==expected.state,"native partial sale matches all47761 decrypted state bytes");
            // Only the state is compared: the original's writer refreshes the plain preview header
            // and the cipher key on each save, which are not part of this action.
        }
        std::printf("Native stock booking/rounding/checkpoint tests passed\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Stocks: %s\n",e.what());return 1;}
}
