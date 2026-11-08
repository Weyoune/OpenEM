// SPDX-License-Identifier: GPL-3.0-only
// Loan (ovr035:1118, loan flag) and talent scout (ovr035:08D5, seg002:0AC5) of the transfer page.
// The starting position is the native DOSBox capture nativepurchase-before.hok (Frosch at pool slot 3).
// Expected draw counts and record fields follow the disassembly cited in src/game/loan.cpp and talent.cpp.
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include "game/native_roster.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
using namespace em;
namespace fs = std::filesystem;

static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
static uint32_t seed_of(const Game& g) { return decode_game_checkpoint(g.checkpoint_bytes()).borland_random; }
static uint32_t after_draws(uint32_t seed, int draws) {
    BorlandRandom r(seed);
    for (int n = 0; n < draws; n++) (void)r.range(0, 1);
    return r.state();
}
static Bytes with_state(const Bytes& hok, size_t at, std::initializer_list<uint8_t> bytes) {
    HokFile h = decode_hok(hok);
    size_t n = at;
    for (uint8_t b : bytes) h.state.at(n++) = b;
    return encode_hok(h);
}
constexpr size_t kPool = 0x81CD, kSquad = 0x696D, kPlayers = 0x4FC6, kRecord = 52;

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("loan_talent_tests DATA NATIVE_CAPTURE_FOLDER");
        const fs::path data = argv[1], native = argv[2];
        const Bytes exe = read_file(data / "EM.EXE"), before = read_file(native / "nativepurchase-before.hok");
        constexpr uint32_t seed = 1617108858u;

        // ---- loan -------------------------------------------------------------------------
        {
            Game game(exe, before, seed);
            const Bytes pristine = game.checkpoint_bytes();
            const HokFile& h = game.save();
            const size_t pool = kPool + 3 * kRecord;
            const int id = h.state.at(pool + 15);
            const long price = long((le32(h.state, pool + 0x2C) & 0x7FFFFFFFu) / 3u);
            require(id == 56 && price > 0, "Frosch with a positive loan price at pool slot 3");
            require(game.loan_transfer_player(3, 0) == PurchaseResult::Invalid && game.checkpoint_bytes() == pristine,
                    "zero bid is neutral");

            // A bid of one DM is below every threshold: one draw (75..85), refusal marks the pool record.
            Game refused(exe, before, seed);
            require(refused.loan_transfer_player(3, 3) == PurchaseResult::SellerRefused, "tiny bid refused");
            require((refused.save().state.at(pool + 0x1A) & 0x81) == 0x81, "refusal sets 0x80 | manager bit");
            require(seed_of(refused) == after_draws(seed, 1), "refusal consumes exactly the first threshold draw");
            const Bytes blocked = refused.checkpoint_bytes();
            require(refused.loan_transfer_player(3, price * 2) == PurchaseResult::Blocked &&
                        refused.checkpoint_bytes() == blocked, "refused loan is blocked without draws");

            // A bid of twice the loan price is above rand(110,130) %: two threshold draws, five insertion
            // draws, the loan salary factor and the sponsor roll (one more when he offers).
            const int old_team = h.state.at(kPlayers + size_t(id - 1) * 33 + 0x1D);
            const long balance = game.budget().balance;
            const size_t squad_before = game.save().squad.size();
            const long bid = price * 2;
            require(game.loan_transfer_player(3, bid, false, false) == PurchaseResult::Purchased, "loan accepted");
            const HokFile& a = game.save();
            int slot = -1;
            for (int s = 0; s < 30; s++) if (a.state.at(kSquad + size_t(s) * kRecord + 15) == id) slot = s;
            require(slot >= 0, "loaned player in the squad block");
            const size_t row = kSquad + size_t(slot) * kRecord;
            require(a.state.at(row + 0x0B) == 1, "loan runs one season");
            require(a.state.at(row + 0x0C) == uint8_t(old_team + 0x80), "return marker = old club + 0x80");
            require(a.state.at(row + 0x04) == 0 && a.state.at(row + 0x05) == 0 && a.state.at(row + 0x1A) == 0,
                    "cup/other goals and lineup flags cleared");
            require(le32(a.state, row + 0x2C) == le32(decode_hok(before).state, pool + 0x2C),
                    "the pool record is copied, so +0x2C keeps the listed value");
            require(a.state.at(kPlayers + size_t(id - 1) * 33 + 0x1A) == 4, "global owner stays 4 for a loan");
            require(a.state.at(kPlayers + size_t(id - 1) * 33 + 0x1D) == game.human_team(), "player plays for the club");
            for (int s = 0; s < 11; s++) require(a.state.at(kPool + size_t(s) * kRecord + 15) != id, "pool row removed");
            require(game.save().squad.size() == squad_before + 1, "squad grew by one");
            const uint32_t s9 = after_draws(seed, 9), s10 = after_draws(seed, 10);
            require(seed_of(game) == s9 || seed_of(game) == s10, "2 seller + 5 insert + loan salary + sponsor draws");
            require(game.budget().balance == balance - bid, "bid paid, sponsor declined");
            const long salary = long(int32_t(le32(a.state, row + 0x28)));
            require(salary > 0, "positive loan salary");
            require(game.list_transfer_player(size_t(slot), false) == TransferMoveResult::Unavailable,
                    "a loaned player cannot be put on the market");

            // Not enough money: the seller accepts first, then the bid exceeds the cash (no insertion).
            Game poor(exe, with_state(before, 0x0DCD + 0x18F, {3, 0, 0, 0}), seed);
            const Bytes poor_before = poor.checkpoint_bytes();
            require(poor.loan_transfer_player(3, bid) == PurchaseResult::InsufficientFunds, "loan refused for money");
            require(seed_of(poor) == after_draws(seed, 2) && poor.save().state.at(pool + 15) == id,
                    "money check after both seller draws, pool unchanged");
            (void)poor_before;
        }

        // ---- talent scout ------------------------------------------------------------------
        {
            Game game(exe, before, seed);
            const long price = game.talent_search_price();
            require(price == long(int32_t(le32(game.save().state, 0x18))) && price > 0, "price from state 0x18");
            const long balance = game.budget().balance;
            require(game.talent_search_days() == 0, "no search running");
            require(game.start_talent_search() == TalentResult::Started, "search started");
            const int days = game.talent_search_days();
            BorlandRandom replica(seed);
            int expect = replica.range(3, 10);
            if (replica.range(0, 3) == 0) expect += 6;
            require(days == expect, "counter rand(3,10) (+6 at rand(0,3)==0)");
            require(game.budget().balance == balance - price, "price paid");
            const Bytes running = game.checkpoint_bytes();
            require(game.start_talent_search() == TalentResult::Running && game.checkpoint_bytes() == running,
                    "a running search blocks the button without draws");

            // Cash equal to the price is not enough (strict comparison, ovr035:09A8).
            const uint32_t p = uint32_t(price);
            Game exact(exe, with_state(before, 0x0DCD + 0x18F, {uint8_t(p), uint8_t(p >> 8), uint8_t(p >> 16), uint8_t(p >> 24)}), seed);
            const Bytes exact_before = exact.checkpoint_bytes();
            require(exact.start_talent_search() == TalentResult::InsufficientFunds && exact.checkpoint_bytes() == exact_before,
                    "cash == price refused without draws");

            // Evaluation: counter 1 ends at the next twice-weekly step. Try several seeds; both outcomes
            // must be consistent (success <=> one more squad player with the native profile).
            int successes = 0, failures = 0;
            for (uint32_t s = 1; s <= 40 && (successes == 0 || failures == 0); s++) {
                Game g(exe, with_state(before, 0x0DCD + 0xEC, {1}), s * 7919u);
                const size_t squad = g.save().squad.size();
                for (int step = 0; step < 8 && g.talent_search_days() != 0; step++) g.advance_day();
                require(g.talent_search_days() == 0, "counter ran out within a week");
                bool news = false, success = false;
                for (const auto& text : g.manager_news()) {
                    if (text.rfind("Ihre Talentsuche", 0) == 0) news = success = true;
                    if (text.rfind("Ihr Talentsp", 0) == 0) news = true;
                }
                require(news, "the search ends with a manager message");
                if (success) {
                    successes++;
                    require(g.save().squad.size() >= squad + 1, "successful search adds a player");
                    bool found = false;
                    for (int r = 0; r < 30; r++) {
                        const size_t row = kSquad + size_t(r) * kRecord;
                        const int id = g.save().state.at(row + 15);
                        if (!id || decode_hok(before).state.at(kPlayers + size_t(id - 1) * 33 + 0x1A) != 6) continue;
                        if (g.save().state.at(kPlayers + size_t(id - 1) * 33 + 0x1A) != 0) continue;
                        const int age = g.save().state.at(kPlayers + size_t(id - 1) * 33 + 0x14);
                        const int years = g.save().state.at(row + 0x0B);
                        require(id <= 179 && age >= 18 && age <= 22 && years >= 2 && years <= 3, "talent profile");
                        const int traits = g.save().state.at(row + 0x19);
                        require((traits >> 4) >= 3 && (traits >> 4) <= 12 && (traits & 15) >= 3 && (traits & 15) <= 12,
                                "talent nibbles rand(3,12)");
                        found = true;
                    }
                    require(found, "talent player found in the squad");
                } else failures++;
            }
            require(successes > 0 && failures > 0, "both outcomes observed");
            std::printf("talent scout: %d successes, %d failures across seeds\n", successes, failures);
        }
        std::puts("Loan and talent scout rules passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "Loan/talent: %s\n", e.what()); return 1; }
}
