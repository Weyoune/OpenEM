// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <vector>

// The classic "Werbung" (advertising) screen: the stadium picture (KB image 151) with the
// club's 11 advertising surfaces, the sponsor logos of running contracts, the detail panel of
// the selected surface (current contract or the offers on the table), the "Firmen-Info" pop-up
// and the month's income/expenses/balance line. This header holds the pure view model, the hit
// test and the drawing; classic_advertising_input.cpp turns clicks into game actions.
//
// Surfaces (contract slots): 0..6 the seven rink boards (kind 0), 7..10 the single surfaces of
// kind 1..4; slot 8 takes TV sponsors (sponsor names 10..14, at most 5 offers), slots 9 and 10
// exist only once the corresponding stadium facility has been built.
// Save layout per manager m (0..3):
//   0xACFC + m * 0x6E + slot * 10    running contract of surface `slot` (ClassicAdvertisingRecord)
//   0xA52C + m * 0x1F4 + kind * 100 + i * 10   offer i (0..9) for surface kind `kind`

namespace em::ui {
// What the screen currently shows.
struct ClassicAdvertisingSelection {
    int contractSlot=-1;        // selected surface 0..10, -1 = none
    int offerIndex=0;           // offer shown while browsing (0..9); -1 selects the highest stored offer
    bool browsingOffers=false;  // true: the detail panel browses offers, false: shows the running contract
    bool clearEmpty=false;      // after hovering an empty area; false keeps the freshly drawn background
};
// What a click on the screen means.
enum class ClassicAdvertisingAction {
    None,           // nothing hit
    Surface,        // an advertising surface (select it)
    PreviousOffer,  // arrow up: previous offer
    NextOffer,      // arrow down: next offer
    Info,           // "Info" button: company information pop-up
    Accept          // "Annehmen" button: accept the shown offer
};
// A clickable rectangle (inclusive screen coordinates) and the action it triggers.
struct ClassicAdvertisingHit {
    ClassicAdvertisingAction action=ClassicAdvertisingAction::None;
    int slot=-1;                              // surface the hit belongs to
    int left=0,top=0,right=-1,bottom=-1;      // inclusive bounds; the default is empty
    bool enabled=false;                       // false: drawn/known but not usable now
};
// One 10-byte advertising record (running contract or offer) from the save state.
struct ClassicAdvertisingRecord {
    int32_t amount=0;  // +0 i32 monthly payment in internal currency units; 0 = empty
    int boards=0;      // +4 offers: number of boards wanted; contracts: id shared by all boards
                       //    of one deal (0xFF = free)
    int months=0;      // +5 remaining/offered months (signed byte)
    int flags=0;       // +6 bits 0..6: renewal option in months; bit 7: shown as "Kündbar" and
                       //    the surface counts as free for new offers
    int sponsor=0;     // +7 sponsor index 0..9 (0..4 for TV, names 10..14)
    int minImage=0;    // +8 u16 club image the sponsor required
};
// One line of screen text in the game's font encoding.
struct ClassicAdvertisingText {
    int x=156,baseline=0,right=-1;  // position and baseline; right = right limit passed to the
                                    // text renderer for alignment (-1 = none)
    uint8_t color=0;
    int shadow=-1;                  // shadow colour, -1 = none
    std::string native;             // text in the game's character encoding ('{' = ä, '}' = ü, ...)
};
// Everything the screen shows, derived from the save without changing it.
struct ClassicAdvertisingView {
    ClassicAdvertisingSelection selection;  // the selection, with offerIndex -1 resolved
    int kind=-1;            // surface kind of the selected slot (0 boards, 1..4), -1 = none
    int highestOffer=0;     // highest offer index with a non-zero amount
    int maxOffer=9;         // last offer index (4 for the TV surface)
    int freeBoards=0;       // boards 0..6 that are empty or have flag bit 7 set
    bool available=false;   // the selected surface exists (facility built)
    bool renewalPending=false;  // the running contract is at 0 months with a renewal option
    bool canInfo=false,canAccept=false;  // the "Info" / "Annehmen" buttons are shown
    ClassicAdvertisingRecord record;     // the record shown in the detail panel
    std::string nativeSponsor;           // its sponsor name
    int64_t income=0,expenses=0,saldo=0; // this month's totals, internal currency units
    std::string nativeIncome,nativeExpenses,nativeSaldo;  // the same, formatted "... DM"
    std::array<ClassicAdvertisingHit,11> surfaces;        // the 11 surface hit areas
    std::vector<ClassicAdvertisingHit> controls;          // arrows and buttons while browsing
    std::vector<ClassicAdvertisingText> detail;           // lines of the detail panel
    // The five lines of the "Firmen-Info" pop-up (frame 30,90..290,150, lines centred between
    // x 30 and 290). The first line carries the "Firmen-Info: " prefix. As in the original the
    // text is chosen by the offer index, not by the sponsor of the offer.
    std::array<ClassicAdvertisingText,5> info;
};

// Builds the view for `manager` (0..3). Uses the difficulty level (state 0xAEB4) and cost factor
// (state 0x06B0) stored in the save in place of those in `tables`. Read-only: no random draws,
// no offer generation and no booking. Throws FormatError on invalid arguments or save data.
ClassicAdvertisingView classic_advertising_view(const HokFile&,const FinanceTables&,
                              ClassicAdvertisingSelection selection={},int manager=0);
// The control or surface at (x, y): enabled controls first, then any surface (an unavailable
// surface is returned with enabled = false); a default hit when nothing is there.
ClassicAdvertisingHit classic_advertising_hit(const ClassicAdvertisingView&,int x,int y);
// Draws the complete screen for the selection.
void draw_classic_advertising(ClassicCanvas&,const HokFile&,const FinanceTables&,
                              ClassicAdvertisingSelection selection={},int manager=0);
// Paints the "Firmen-Info" pop-up over an already drawn advertising screen; the caller handles
// input and restores the screen afterwards. Does nothing when the view has no info.
void draw_classic_advertising_info(ClassicCanvas&,const ClassicAdvertisingView&);
} // namespace em::ui
