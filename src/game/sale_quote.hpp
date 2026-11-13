// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Selling a player to a computer club that has shown interest: the price offered (gross amount,
// severance for a listed player, net amount), the check that the offer still matches the state,
// and carrying out or declining the sale, including the strength boost the buying club receives.
// All money values are in the internal unit (DM x 3) and computed with the original's 32-bit
// wrap-around arithmetic.
#include "game/contracts.hpp"

namespace em {
// The money of a sale offer: gross price, severance paid out of it (listed players only), net = gross - severance.
struct SaleAmounts { int32_t gross=0,severance=0,net=0; };
// A pending sale offer, captured when the offer is shown.
struct SaleQuote {
    int block=0,slot=0,id=0,bidder=0,years=0;  // squad block/slot of the player, player ID, bidding team (row +0x16), contract years
    int manager=0;  // the selling manager; block is his club block (== manager) or the transfer list 4
    // A leftover value the original carries over from the transfer screen's input handling (the
    // UI passes the last click coordinate) into the buyer lookup: the buyer's mean strength is
    // read at byte offset (buyer * 61 + native_si) into the team records, which is not
    // necessarily the buyer's own record. Kept so the exact bytes the original reads are used.
    int16_t native_si=0;
    SaleAmounts amounts;
    std::array<uint8_t,52> row{};  // copy of the squad row when the offer was made
};
// A random number between low and high, as the original's helper computes it: both bounds are
// treated as signed 16-bit values and swapped if needed, the width is a wrapped signed 16-bit
// divisor (throws FormatError when it is zero), and the result is truncated to 16 bits.
int native_sale_range(BorlandRandom&,int low,int high);
// The offer for a player of market value `value` (already computed; no further valuation draw).
// Listed players: basis = value + rand(value/20, value/10) - value/15, factor rand(85, 115) and a
// severance of gross / 7 per remaining contract year. Club players: factor rand(87, 125), or
// rand(112, 155) for a bidder from the European clubs (team > 63). gross = basis * factor / 30000 * 300.
SaleAmounts native_sale_amounts(int32_t value,bool listed,bool foreign,int years,BorlandRandom&);
// Builds the offer for the player in `block`/`slot` (the manager's club block or the transfer list
// 4) if that row carries an interest flag (+0x09 bit 6 for a club row, bit 7 for a list row), the
// player is owned by `manager`, a club row is not locked (+0x18 bit 7) and the bidder is a valid
// team. The state stays unchanged; only the random generator advances (valuation and amounts).
// `leftover_offset` becomes SaleQuote::native_si. Returns false when there is no valid offer.
bool begin_native_sale(const HokFile&,const ContractTables&,BorlandRandom&,int block,int slot,
                       int16_t leftover_offset,SaleQuote&,int manager=0);
// True while the quote still matches the state exactly (same row bytes, owner and interest flag).
bool current_native_sale(const HokFile&,const SaleQuote&);
// Carries out (`sell`) or declines the offer. Both decisions clear the interest flag and delete the
// player's message. A sale credits the net amount to the seller, removes the row, sets the player's
// team to the buyer and his owner to 6 (free), and strengthens the buying team (for a club row
// only teams below 64; see adjust_buyer in sale_quote.cpp). For a club row the original takes the buyer from the row that
// moved up into the slot after the removal, a quirk that is preserved; `actual_buyer` receives the
// team that got the player. Returns false if the quote is stale.
bool finish_native_sale(HokFile&,BorlandRandom&,const SaleQuote&,bool sell,int* actual_buyer=nullptr);
} // namespace em
