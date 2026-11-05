// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/finances.hpp"
#include <functional>

namespace em {
using YouthRandomDraw=std::function<int(int,int)>;
// 5396:041C, before league games on every calendar event with bits0..2.
// first_matchday is DS07A9 (all managers use the first league's counter).
void youth_fixture_day(Bytes&,const FinanceTables&,int first_matchday,int manager,const YouthRandomDraw&);
// 5396:076B: annual promotion/relegation and mixing the three age groups.
void youth_new_season(Bytes&,int manager);
} // namespace em
