// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "game/stadium.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>
#include <vector>

namespace em::ui {
struct ClassicStadiumSelection {
    bool planned = false;
    int hover = -1, quoteItem = -1, target = -1;
    int numberWidth = 2; //cached values inherit DS0374 before the Konto helper resets2
};
enum class ClassicStadiumAction {
    None, Item, TicketIncrease, TicketDecrease, TargetIncrease, TargetDecrease,
    QuoteConfirm, QuoteCancel, TogglePlanned, Back
};
struct ClassicStadiumHit {
    ClassicStadiumAction action = ClassicStadiumAction::None;
    int item = -1;
    bool enabled = true;
};
struct ClassicStadiumOverlay { int item, x, y, sx, sy, width, height; };
struct ClassicStadiumValue {
    int item = 0, label_x = 1, value_x = 55, baseline = 177;
    uint8_t color = 31;
    std::string native_label, native_value;
};
struct ClassicStadiumView {
    ClassicStadiumSelection selection;
    std::array<ClassicStadiumValue,11> values;
    std::vector<ClassicStadiumOverlay> overlays;
    int ticket = 0;
    uint32_t cash_bits = 0;
    std::string native_ticket, native_cash, native_hint;
    std::optional<StadiumTarget> quote;
    std::string native_quote_cost, native_quote_target;
    uint8_t quote_color = 8;
};

//56D1:0858: original current/planned page and optional in-page nominal quote.
//Pure raw reads; no RNG, refusal-gate mutation, construction or ticket writes.
ClassicStadiumView classic_stadium_view(const HokFile&,const FinanceTables&,
    const StadiumTables&,ClassicStadiumSelection selection={},int manager=0);
void draw_classic_stadium(ClassicCanvas&,const HokFile&,const FinanceTables&,
    const StadiumTables&,ClassicStadiumSelection selection={},int manager=0);
ClassicStadiumHit classic_stadium_hit(const ClassicStadiumView&,int x,int y,bool right=false);
//Native0968 response -1 increases target,+1 decreases it. Invalid/unaffordable
//steps retain the current target; caller owns the resulting transient state.
int classic_stadium_target_step(const HokFile&,const StadiumTables&,int item,
    int target,int response,int manager=0);
} // namespace em::ui
