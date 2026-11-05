// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <vector>

namespace em::ui {
struct ClassicAdvertisingSelection {
    int contractSlot=-1,offerIndex=0; //offerIndex=-1 selects highest nonzero native offer
    bool browsingOffers=false;
    bool clearEmpty=false; //post-hover empty area; false preserves fresh KB151 pixels
};
enum class ClassicAdvertisingAction { None,Surface,PreviousOffer,NextOffer,Info,Accept };
struct ClassicAdvertisingHit {
    ClassicAdvertisingAction action=ClassicAdvertisingAction::None;
    int slot=-1,left=0,top=0,right=-1,bottom=-1;
    bool enabled=false;
};
struct ClassicAdvertisingRecord {
    int32_t amount=0;
    int boards=0,months=0,flags=0,sponsor=0,minImage=0;
};
struct ClassicAdvertisingText {
    int x=156,baseline=0,right=-1;
    uint8_t color=0;
    int shadow=-1;
    std::string native;
};
struct ClassicAdvertisingView {
    ClassicAdvertisingSelection selection;
    int kind=-1,highestOffer=0,maxOffer=9,freeBoards=0;
    bool available=false,renewalPending=false,canInfo=false,canAccept=false;
    ClassicAdvertisingRecord record;
    std::string nativeSponsor;
    int64_t income=0,expenses=0,saldo=0;
    std::string nativeIncome,nativeExpenses,nativeSaldo;
    std::array<ClassicAdvertisingHit,11> surfaces;
    std::vector<ClassicAdvertisingHit> controls;
    std::vector<ClassicAdvertisingText> detail;
    //4495:1B46 frame30,90..290,150; lines center30..290. First line
    //includes Firmen-Info prefix. These come from offerIndex, not sponsorID.
    std::array<ClassicAdvertisingText,5> info;
};

//4495:0EE0, drawing only. Applies saved levelAEB4/cost factor06B0 to
//the supplied immutable EXE table cache. No RNG, offer generation, image,
//stored-income update or contract booking is performed.
ClassicAdvertisingView classic_advertising_view(const HokFile&,const FinanceTables&,
                              ClassicAdvertisingSelection selection={},int manager=0);
ClassicAdvertisingHit classic_advertising_hit(const ClassicAdvertisingView&,int x,int y);
void draw_classic_advertising(ClassicCanvas&,const HokFile&,const FinanceTables&,
                              ClassicAdvertisingSelection selection={},int manager=0);
//Paint over an already drawn advertising page; parent owns input/restore.
void draw_classic_advertising_info(ClassicCanvas&,const ClassicAdvertisingView&);
} // namespace em::ui
