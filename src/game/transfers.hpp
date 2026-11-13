// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Transfer market rules. Amounts are raw (shown Deutschmark times 3).
//
// The transfer list is squad block 4 (HOK 0x696D + 4 * 0x618), up to 11 occupied slots;
// the refresh routine visits the first 10. Player
// record byte +0x1A tells where a player belongs: 0..3 = the club of human manager n, 4 = on the
// transfer list, 6 = an ordinary computer club player.
//
// Refresh (run from the day loop): listed players that drop off the list become ordinary players
// again (+0x1A = 6); for every freed slot a random slot
// 0..9 is filled (skipped if it holds a listed player): candidate = rand(1, 179), with chance
// 1/6 rand(180, 199) instead, up to 1500 tries until the player is free (+0x1A = 6). The player
// gets form rand(45, 55); a player of a human club is first moved to a random computer club of
// the same class. He enters the list (+0x1A = 4) with market value and salary (game/contracts.hpp)
// and the nibble byte +0x19 = rand(3, 12) << 4 | rand(3, 12).
//
// Buying (seller_accepts): price = listed value (bit 31 masked); the club
//   refuses a bid below price * rand(75, 85) / 100,
//   accepts a bid above price * rand(110, 130) / 100,
//   otherwise accepts if rand(70, 120) < bid * 100 / price.
// The player then signs if he accepts the contract (see player_accepts).
//
// Offers to the manager, made in the weekly squad update (twice a week; v = value / 10000):
//   bit 7 of +0x09, a player the manager has put on the transfer list (block 4 record, player
//     +0x1A = the manager): rand(0, 180) < v and rand(0, 2) != 0
//   bit 6, any other player of his squad, until season day 232: rand(0, 200) < v and
//     rand(0, 450) == 0; the bidder is foreign when v > 90 and rand(0, 10) == 0
// both only when the player has no pending offer message (+0x1E) and squad +0x18 bit 7 is clear;
// the bidder goes to +0x16 (pick_bidder) and a message to the manager into player +0x1E.
// The amounts: bit 6 value * rand(87, 125) / 30000 * 300 (rand(112, 155) when
// +0x16 > 63, a foreign club); bit 7 (value + rand(value / 20, value / 10) - value / 15) *
// rand(85, 115) / 30000 * 300. On acceptance the player goes to +0x16 and the balance rises.
#include <cstdint>
#include <vector>

#include "formats/hok.hpp"

#include "game/contracts.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kTransferRefreshSlots = 10;  // the refresh visits indices 0..9
constexpr int kTransferListSlots = 11;     // a sale to the list can insert at index 10

// Candidate for the transfer list as a 1-based player ID (= HOK player record index + 1);
// `is_free(p)` tells whether that player is an ordinary club player (+0x1A == 6). Returns -1 after 1500 failed tries.
template <class IsFree>
int pick_transfer_candidate(BorlandRandom& rng, IsFree is_free) {
    for (int tries = 0; tries < 1500; tries++) {
        int p = rng.range(1, 179);
        if (rng.range(0, 5) == 0) p = rng.range(180, 199);
        if (is_free(p)) return p;
    }
    return -1;
}

// The club that bids for a player of strength s (skill 1 + skill 2 + form) / 3. Clubs
// rand(0, 42) (foreign: rand(64, 149)) without a manager are drawn until |c - s| < rand(2, 10 * m)
// for the club strength c = (skill 1 + skill 2 of both kinds + 100) / 6; m = 1, from the 100th
// try 2, from the 200th 3; after 300 tries the drawn club is taken.
int pick_bidder(const std::vector<HokTeam>& teams, int player_strength, bool foreign, BorlandRandom& rng);

// Does the selling club accept `bid` for a listed player worth `price` (rules above)? With a price
// of 0 the two threshold draws are still made and any positive bid is accepted.
bool seller_accepts(long price, long bid, BorlandRandom& rng);

// With probability 1/7 a sponsor offers 25..65 percent of
// the paid transfer fee, rounded down to 100 DM (raw: fee * p / 30000 * 300). The manager may decline it.
long purchase_subsidy(long fee, BorlandRandom& rng);

// Offer of a club for a player of the manager's squad (flag bit 6); `foreign_club` selects the
// higher factor range for a bidder from the European clubs.
long sale_offer(long value, bool foreign_club, BorlandRandom& rng);
// Offer of the interested club +0x16 for a player the manager has put on the list (flag bit 7).
long club_offer(long value, BorlandRandom& rng);

// Does the player accept `years` seasons at `salary` (0 = keep the current value)? Same length as
// now: any raise is accepted, a cut refused; shorter: accepted above 120 % of the current salary,
// refused up to 105 %, both without a draw; a longer contract has no early decision. Otherwise
// the player wants
//   salary (player_worth) * rand(85, 115) / 100 * rand(w - 6, w + 4) / 100,
//   w = (((years - 1) * 100 + rest of season %) / 6 + 110) * 8 / 10,
//   after season day 232 also * rand(132 - age, 152 - age) / 100,
// refuses anything if that reaches 100000 raw, and accepts an offer above it.
bool player_accepts(const ContractTables& t, const ContractPlayer& p, int years, long salary, int matchday,
                    int matchdays, int season_day, BorlandRandom& rng);

}  // namespace em
