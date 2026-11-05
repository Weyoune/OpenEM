// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/native_roster.hpp"
#include <optional>

namespace em {
struct YouthIntake { uint8_t player=0; int slot=0; bool shifted=false; };
// 4184:0373..03A3. Both draws occur even before the first eligible year.
int youth_review_attempts(int calendar_year,const ContractRandomDraw&);
// 146E:089F. Reuses a free global ID, retaining its name and natural position.
// Mutates native bytes only; the caller synchronizes derived DTOs after insertion.
std::optional<YouthIntake> youth_intake(HokFile&,const ContractTables&,int manager,
                                      int quality,const ContractRandomDraw&);
} // namespace em
