// SPDX-License-Identifier: GPL-3.0-only
// Loan of a computer player from the transfer list (EM.EXE overlay 11, ovr035:1118 with the loan
// flag [bp-0Ch] = 1, set by a click at x > 250 on the "(L)" price, ovr035:0CD7).
//
// Native order for a list player whose global +0x1A is 4 (the computer market):
//   - pool +0x1A bit 7 together with the manager bit: "Angebot wurde / bereits abgelehnt."
//     (ovr035:1146..115E); only the manager bit set: the click does nothing.
//   - fee editor "NAME leihen", the bid in DM (x3 raw)
//   - ovl11_391d(slot, bid, loan 1, 0): thresholds against the listed value / 3; a refusal sets
//     pool +0x1A |= 0x80 | manager bit ("Ihr Angebot / wurde abgelehnt.")
//   - cash must cover the bid (balance >= bid, ovr035:14E8), else "Sie haben nicht / genug Geld !"
//   - 59B5:2CD1(id, 1 year, loan 99, 0, 29): capacity refusal "Schon 29 Mann / im Team." without
//     draws; otherwise the ordinary five profile draws, the salary worth, then for loan mode 99
//     salary * rand(90, 115) / 100; the player's club +0x1D becomes the manager's club
//   - movmem(pool record -> new squad record, 52); then +0x0B = 1, +0x0C = old club + 0x80,
//     +0x28 = the loan salary, +0x04 = +0x05 = +0x1A = 0 (ovr035:18DB..1945)
//   - pool row removed (59B5:25D6(slot, 1, 10), clears the notification)
//   - the global +0x1A stays 4 (ovr035:19B9 skips the owner store for loans)
//   - cash -= bid, then the purchase sponsor 1238:223e (purchase_subsidy)
#include "game/game.hpp"
#include "game/native_roster.hpp"

#include <algorithm>
#include <cstring>

namespace em {
namespace {
constexpr size_t kPoolBase = 0x81CD, kRecord = 52, kPlayerBase = 0x4FC6;
void store32(Bytes& b, size_t at, uint32_t v) { for (int n = 0; n < 4; n++) b.at(at + size_t(n)) = uint8_t(v >> (8 * n)); }
}  // namespace

// For the active manager (docs/MEHRSPIELER_PLAN.md step 5); his bit in pool +0x1A is 1 << manager (bit 0 verified only).
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
    if (flags & own_bit) return PurchaseResult::Unavailable;  // native: no reaction
    const long price = long((le32(save_.state, source + 0x2C) & 0x7FFFFFFFu) / 3u);
    if (!seller_accepts(price, fee, brng_)) {
        save_.state.at(source + 0x1A) |= refused;
        return PurchaseResult::SellerRefused;
    }
    if (mgr_[a].budget.balance < fee) return PurchaseResult::InsufficientFunds;
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

// "Aber immer!" on the sponsor question (seg001:2329..2341): balance += offer. The offer is a multiple of
// 300 raw (100 DM, seg001:2288); the question follows the last draw, so accepting draws nothing.
bool Game::accept_purchase_subsidy(long raw) {
    if (raw <= 0 || raw % 300 != 0) return false;
    const int a = active_manager_;
    mgr_[a].budget.balance = long(int32_t(uint32_t(mgr_[a].budget.balance) + uint32_t(raw)));
    log_.push_back("purchase sponsor subsidy " + std::to_string(raw / 3) + " DM accepted");
    return true;
}

}  // namespace em
