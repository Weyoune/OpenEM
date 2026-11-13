// SPDX-License-Identifier: GPL-3.0-only
// Loan of a player from the transfer list (the "(L)" price on the transfer market screen) and
// the purchase sponsor's subsidy. Part of class Game (game.hpp).
//
// Order of the original game for a list player whose global record byte +0x1A is 4 (the
// computer market):
//   - pool +0x1A bit 7 together with the manager bit: "Angebot wurde / bereits abgelehnt.";
//     only the manager bit set: the click does nothing.
//   - fee editor "NAME leihen", the bid in DM (x3 raw)
//   - the seller's decision (seller_accepts) against the listed value / 3; a refusal sets
//     pool +0x1A |= 0x80 | manager bit ("Ihr Angebot / wurde abgelehnt.")
//   - cash must cover the bid (balance >= bid), else "Sie haben nicht / genug Geld !"
//   - the squad insertion with a one-year contract in loan mode: with 29 players already the
//     refusal "Schon 29 Mann / im Team." without random draws; otherwise the ordinary five
//     profile draws, the salary worth, then for a loan salary * rand(90, 115) / 100; the
//     player's club +0x1D becomes the manager's club
//   - the pool record (52 bytes) is copied into the new squad record; then +0x0B = 1,
//     +0x0C = old club + 0x80, +0x28 = the loan salary, +0x04 = +0x05 = +0x1A = 0
//   - the pool row is removed (this also clears its notification)
//   - the global +0x1A stays 4 (a loan does not change the owner)
//   - cash -= bid, then the purchase sponsor offer (purchase_subsidy)
#include "game/game.hpp"
#include "game/native_roster.hpp"

#include <algorithm>
#include <cstring>

namespace em {
namespace {
// HOK state offsets: transfer pool records (52 bytes each, like squad records) and the global
// player records (33 bytes each).
constexpr size_t kPoolBase = 0x81CD, kRecord = 52, kPlayerBase = 0x4FC6;
// Stores a little-endian u32.
void store32(Bytes& b, size_t at, uint32_t v) { for (int n = 0; n < 4; n++) b.at(at + size_t(n)) = uint8_t(v >> (8 * n)); }
}  // namespace

// Loan offer of the active manager for transfer-list row `slot` (0..10) with the bid `fee` (raw).
// The manager's bit in pool +0x1A is 1 << manager (only bit 0 confirmed against the original).
// A refused offer is remembered in the pool record; on success the player joins the squad
// (insert_loan).
PurchaseResult Game::loan_transfer_player(size_t slot, long fee, bool accept_subsidy, bool prepare_lineup) {
    const int a = active_manager_;
    const uint8_t own_bit = uint8_t(1u << unsigned(a)), refused = uint8_t(0x80 | own_bit);
    if (slot >= 11 || fee <= 0) return PurchaseResult::Invalid;
    if (national_coach_ >= 0 || mgr_[a].team >= 52) return PurchaseResult::Unavailable;
    const size_t source = kPoolBase + slot * kRecord;
    const int id = save_.state.at(source + 15);
    HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != 4) return PurchaseResult::Unavailable;
    const uint8_t flags = save_.state.at(source + 0x1A);
    if ((flags & refused) == refused) return PurchaseResult::Blocked;
    if (flags & own_bit) return PurchaseResult::Unavailable;  // the original does not react to the click
    const long price = long((le32(save_.state, source + 0x2C) & 0x7FFFFFFFu) / 3u);
    if (!seller_accepts(price, fee, brng_)) {
        save_.state.at(source + 0x1A) |= refused;
        return PurchaseResult::SellerRefused;
    }
    if (mgr_[a].budget.balance < fee) return PurchaseResult::InsufficientFunds;
    return insert_loan(slot, fee, -1, accept_subsidy, prepare_lineup);
}

// The loaned list player joins the active manager's squad. The insertion is first prepared on a
// copy of the save and a copy of the random generator, so a full squad changes nothing.
// `seller` >= 0: the list player of that human manager, who agreed (answer_manager_offer): he
// gets the fee and there is no sponsor subsidy. Otherwise the purchase sponsor may offer a
// subsidy, which is booked when `accept_subsidy` is set.
PurchaseResult Game::insert_loan(size_t slot, long fee, int seller, bool accept_subsidy, bool prepare_lineup) {
    const int a = active_manager_;
    const size_t source = kPoolBase + slot * kRecord;
    const int id = save_.state.at(source + 15);
    HokPlayer* player = player_by_exe_index(id);
    const size_t kSquadBase = squad_block(a);

    HokFile prepared = decode_hok(save_bytes());
    BorlandRandom next = brng_;
    const int old_team = player->raw[0x1D];
    const ContractRandomDraw draw = [&](int lo, int hi) { return next.range(lo, hi); };
    const NativeRosterInsertion inserted = native_insert_player(prepared, contracts_, draw, uint8_t(id), a, 1);
    if (inserted.slot >= 29) return PurchaseResult::SquadFull;
    const size_t row = kSquadBase + size_t(inserted.slot) * kRecord;
    const long salary = long(int32_t(le32(prepared.state, row + 0x28))) * next.range(90, 115) / 100;
    std::memcpy(prepared.state.data() + row, prepared.state.data() + source, kRecord);
    prepared.state.at(row + 0x0B) = 1;
    prepared.state.at(row + 0x0C) = uint8_t(old_team + 0x80);
    store32(prepared.state, row + 0x28, uint32_t(salary));
    prepared.state.at(row + 0x04) = prepared.state.at(row + 0x05) = prepared.state.at(row + 0x1A) = 0;
    std::copy_n(prepared.state.begin() + long(kSquadBase), 30 * kRecord, save_.state.begin() + long(kSquadBase));
    save_.state.at(kPlayerBase + size_t(id - 1) * 33 + 0x1D) = uint8_t(mgr_[a].team);
    player->team = mgr_[a].team;
    if (inserted.shifted)
        std::move_backward(mgr_[a].injured_role.begin() + inserted.slot, mgr_[a].injured_role.end() - 1, mgr_[a].injured_role.end());
    mgr_[a].injured_role[size_t(inserted.slot)] = 0;
    native_remove_storage_row(save_, 4, int(slot), 10);
    save_.squad_of(a) = hok_squad(save_, a);
    brng_ = next;
    mgr_[a].budget.balance -= fee;
    log_.push_back(player->name + " loaned for " + std::to_string(fee / 3) + " DM, salary " + std::to_string(salary / 3) + " DM");
    if (seller >= 0 && seller < managers_) {
        mgr_[seller].budget.balance += fee;
        last_purchase_subsidy_ = 0;
        if (prepare_lineup) prepare_human_lineup(false, a);
        refresh_budget_roster(a);
        refresh_budget_roster(seller);
        return PurchaseResult::Purchased;
    }
    const long subsidy = purchase_subsidy(fee, brng_);
    last_purchase_subsidy_ = subsidy;
    if (subsidy > 0) {
        if (accept_subsidy) mgr_[a].budget.balance = long(int32_t(uint32_t(mgr_[a].budget.balance) + uint32_t(subsidy)));
        log_.push_back("purchase sponsor subsidy " + std::to_string(subsidy / 3) + (accept_subsidy ? " DM accepted" : " DM declined"));
    }
    if (prepare_lineup) prepare_human_lineup(false, a);
    refresh_budget_roster(a);
    return PurchaseResult::Purchased;
}

// "Aber immer!" on the sponsor question: balance += offer (raw, wrapping like a 32-bit value).
// The offer is a multiple of 300 raw (100 DM); anything else is rejected and returns false. The
// question follows the last random draw, so accepting draws nothing.
bool Game::accept_purchase_subsidy(long raw) {
    if (raw <= 0 || raw % 300 != 0) return false;
    const int a = active_manager_;
    mgr_[a].budget.balance = long(int32_t(uint32_t(mgr_[a].budget.balance) + uint32_t(raw)));
    log_.push_back("purchase sponsor subsidy " + std::to_string(raw / 3) + " DM accepted");
    return true;
}

}  // namespace em
