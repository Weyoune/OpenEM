// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/contracts.hpp"

namespace em {
struct SaleAmounts { int32_t gross=0,severance=0,net=0; };
struct SaleQuote {
    int block=0,slot=0,id=0,bidder=0,years=0;
    int16_t native_si=0;
    SaleAmounts amounts;
    std::array<uint8_t,52> row{};
};
// 199F:0847 sorts signed16 bounds and uses a wrapped signed16 divisor.
int native_sale_range(BorlandRandom&,int low,int high);
// Starts at the already-computed3D36 value; no hidden valuation draw.
SaleAmounts native_sale_amounts(int32_t value,bool listed,bool foreign,int years,BorlandRandom&);
// Native5F7C:0E17/1347. Raw bytes stay unchanged; only the live RNG advances.
bool begin_native_sale(const HokFile&,const ContractTables&,BorlandRandom&,int block,int slot,
                       int16_t incoming_si,SaleQuote&);
bool current_native_sale(const HokFile&,const SaleQuote&);
// Both decisions consume the exact interest flag and notification.
// Buyer adjustment retains the caller's incoming SI semantics.
bool finish_native_sale(HokFile&,BorlandRandom&,const SaleQuote&,bool sell,int* actual_buyer=nullptr);
} // namespace em
