// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "game/stadium.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>
#include <vector>

// Stadion page of the classic frontend: the stadium picture with the built extensions, the eleven stadium
// values (current or planned), ticket price, balance and the in-page quote for an extension. Pure view and
// hit-test functions; the input handling is in classic_stadium_input.cpp, the rules in game/stadium.hpp.

namespace em::ui {
// Transient state of the page held by the application.
struct ClassicStadiumSelection {
    bool planned = false;  // show the planned (under construction) values instead of the current ones
    int hover = -1, quoteItem = -1, target = -1;  // hovered item, item with an open quote, chosen target level
    // Pad width of the cached numbers. As in the original, the values keep the width left over from the
    // previous dialog until the balance display resets it to 2.
    int numberWidth = 2;
};
// What a click on the page does.
enum class ClassicStadiumAction {
    None, Item, TicketIncrease, TicketDecrease, TargetIncrease, TargetDecrease,
    QuoteConfirm, QuoteCancel, TogglePlanned, Back
};
// Result of classic_stadium_hit; enabled is false for a button that cannot act (ticket price at its limit,
// unaffordable or unavailable quote).
struct ClassicStadiumHit {
    ClassicStadiumAction action = ClassicStadiumAction::None;
    int item = -1;
    bool enabled = true;
};
// One extension sprite drawn over the stadium picture: item, destination (x, y), source (sx, sy) in KB8, size.
struct ClassicStadiumOverlay { int item, x, y, sx, sy, width, height; };
// One value line: item 0..8 (levels) or 9/10 (seats / standing places), positions, colour and texts.
struct ClassicStadiumValue {
    int item = 0, label_x = 1, value_x = 55, baseline = 177;
    uint8_t color = 31;
    std::string native_label, native_value;
};
// Everything the page shows.
struct ClassicStadiumView {
    ClassicStadiumSelection selection;
    std::array<ClassicStadiumValue,11> values;
    std::vector<ClassicStadiumOverlay> overlays;
    int ticket = 0;          // ticket price in DM (manager block +0xE4)
    uint32_t cash_bits = 0;  // balance, i32 at manager block +0x18F (internal units)
    std::string native_ticket, native_cash, native_hint;  // "Eintritt" and "Konto" texts, "<item> ausbauen" hint
    std::optional<StadiumTarget> quote;                   // open quote, if any
    std::string native_quote_cost, native_quote_target;   // " Kosten: ..." and the target level/capacity
    uint8_t quote_color = 8;                              // 8 affordable, 17 not
};

// The current or planned stadium page with an optional in-page nominal quote, as in the original.
// Pure reads of the save state; no random numbers, refusal changes, construction or ticket writes.
// Throws FormatError on an invalid manager, selection or stored level.
ClassicStadiumView classic_stadium_view(const HokFile&,const FinanceTables&,
    const StadiumTables&,ClassicStadiumSelection selection={},int manager=0);
// Paints the page.
void draw_classic_stadium(ClassicCanvas&,const HokFile&,const FinanceTables&,
    const StadiumTables&,ClassicStadiumSelection selection={},int manager=0);
// Hit test at (x, y). With an open quote every click acts on the quote (outside the buttons it cancels);
// otherwise a right click toggles current/planned (lower part) or leaves the page.
ClassicStadiumHit classic_stadium_hit(const ClassicStadiumView&,int x,int y,bool right=false);
// One step of the quote target: response -1 (upper arrow) increases the target, +1 decreases it, as the
// original's arrow helper reports them. Invalid or unaffordable steps retain the current target; the caller
// owns the resulting transient state.
int classic_stadium_target_step(const HokFile&,const StadiumTables&,int item,
    int target,int response,int manager=0);
} // namespace em::ui
