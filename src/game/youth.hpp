// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/finances.hpp"

namespace em {
// Original 5396:087B: the three monthly youth grants, in display currency.
struct YouthFundingScale { int32_t step, maximum; };
YouthFundingScale youth_funding_scale(const FinanceTables&,int kind);
int32_t youth_funding_amount(const HokFile&,int kind,int manager=0);
// Native left/right step zones and the quantized proportional zone X121..190.
// Existing non-quantized HOK values follow the original signed32 arithmetic.
int32_t youth_funding_target(int32_t raw,const FinanceTables&,int kind,int x);
int youth_funding_row(int y);
} // namespace em
