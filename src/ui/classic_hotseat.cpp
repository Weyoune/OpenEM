// SPDX-License-Identifier: GPL-3.0-only
// Hot seat: up to four human managers share the menu (docs/MEHRSPIELER_PLAN.md step 10, docs/CLASSIC_UI.md).
//
// The original's day loop (sub_6153F, Amiga FUN_0012504c lines 17630..17660) runs, for every manager 0..N-1 in index
// order on a menu day, the transfer-list roll and the main menu sub_24F2A (Amiga FUN_0010615a) with that manager as
// DS:A70E. The menu draws the header of that manager (name, face DS:0178 + m, club, finances) and opens his message
// field when his flag DS:5A50 + m is set; the crest ("weiter") ends his turn. There is no screen between two managers
// (DOSBox: anna -> bert on 10.09.1993 directly). After the last manager the day is played and the next menu day
// starts again with manager 0. The draws of the menu pass are made by Game::menu_pass at the end of the day step.
#include "game/manager_layout.hpp"
#include "ui/classic_app.hpp"

namespace em::ui {

int ClassicApplication::active_manager() const { return session_.loaded() ? session_.active_manager() : 0; }

std::string ClassicApplication::manager_name(int manager) {
    const Bytes& s = snapshot().state;
    return iso646de_to_utf8(reinterpret_cast<const char*>(s.data() + manager_block(manager)), 0x13);
}

// The crest of manager m < N - 1 hands the menu to manager m + 1 on the same day; the last one plays the day.
bool ClassicApplication::hand_over_menu() {
    const int next = session_.active_manager() + 1;
    if (next < session_.game().managers()) {
        session_.select_manager(next);
        category_ = -1;
        selected_player_ = selected_pool_ = -1;
        lineup_cells_.reset();
        invalidate();
        return true;
    }
    session_.select_manager(0);  // the day loop and the first menu of the next day belong to manager 0
    invalidate();
    return false;
}

// Bid for a list player of another manager (Game::offer_to_manager, ovr035:1528..17A5). The owner answers at the same
// seat in a dialog with his name (DOSBox: "bert" for anna's bid, work/exp/hotseat_20261110/raster_goldens/offer-*).
bool ClassicApplication::bid_to_manager(int pool_slot, long fee, bool loan) {
    const HokFile& h = snapshot();
    const size_t record = squad_block(4) + size_t(pool_slot) * 52;
    const int id = h.state.at(record + 15);
    if (id < 1) return false;
    const size_t player = 0x4FC6 + size_t(id - 1) * 33;
    const int owner = h.state.at(player + 0x1A);
    if (owner >= session_.game().managers() || owner == active_manager()) return false;
    std::string name;
    for (size_t k = 0; k < 0x11 && h.state.at(player + k); k++) name.push_back(char(h.state.at(player + k)));
    PurchaseDraft offer;
    PurchaseResult result = PurchaseResult::Invalid;
    try {
        session_.apply([&](Game& game) { result = game.offer_to_manager(size_t(pool_slot), fee, loan, offer); return ActionResult{}; });
        invalidate();
    } catch (const std::exception& e) {
        notice(e.what());
        return true;
    }
    switch (result) {
        case PurchaseResult::Blocked: notice("Angebot wurde\nbereits abgelehnt."); break;
        case PurchaseResult::InsufficientFunds: notice("Sie haben nicht\ngenug Geld !"); break;
        // DS:2E23/2E35 with the buttons DS:41D1/41D5 ("Nie", "Nein", ovr035:15DD..1646)
        case PurchaseResult::Unfair: manager_question_ = ManagerQuestion{owner, "Dieser Betrag ist", "nicht erlaubt (unfair) !", "Nie", "Nein", true, offer}; break;
        // DS:2E4E + bid (sub_19CC8, no thousands dots) + DS:47BE, DS:2E67 + name + DS:2E6C; buttons DS:41DA/41E2
        case PurchaseResult::OwnerQuestion:
            manager_question_ = ManagerQuestion{owner, "Nehmen Sie das Angebot (" + std::to_string(fee / 3) + " DM)", "f]r " + name + " an ?",
                                                "Na gut.", "Nein !", false, offer};
            break;
        default: notice("Dieser Transfer ist derzeit nicht möglich."); break;
    }
    return true;
}

// The DEB cup draw of a new game (ovl05_4c7b(0, 0) -> ovl05_4653 = ovr029:4693..487F, Amiga FUN_00131194): with the
// DEB-Pokal display switch on (DS:0227) the empty draw board appears and the question "An Alle:" (argument 6) asks
// whether to watch the draw live. The callers check the switch (classic_misc_input.cpp for a new game, Game for the later
// rounds); the answer and the live draw: classic_cup_draw.cpp.
void ClassicApplication::ask_cup_draw() {
    cup_answer_.reset();
    ManagerQuestion q;
    q.manager = -1;  // "An Alle:"
    q.line1 = "Sind Ihre Nerven stark genug,";
    q.line2 = "um die DEB-Pokal-Auslosung";
    q.line3 = "live zu verfolgen ?";
    q.yes = "Aber klar";
    q.no = "Kein Gedanke";
    q.cup_draw = true;
    manager_question_ = q;
}

void ClassicApplication::draw_manager_question() {
    const auto& q = *manager_question_;
    if (q.cup_draw) {  // ovr029:46B4..47BD: frame, title (font 1), n / 2 rows of two KB25 bars, n = DS:03B1[round] = 32
        canvas_.background();
        canvas_.frame(3, 3, 13, 12);
        canvas_.text(ClassicFontId::NewBold, 0, 0x1A, "DEB-Pokal Auslosung", 7, 1, 0x141);
        const int teams = q.cup_clubs;
        int y = 0x80 - ((teams >> 1) * 11 >> 1);
        for (int row = 0; row < teams; row += 2, y += 11)
            for (const int x : {37, 164}) canvas_.blit(25, x, y, 0, 0, 0x78, 10, true, 0xC0);  // DS:4DD0, DS:4DD4
    }
    canvas_.frame(75, 107, 7, 4);
    // FUN_00146858: the manager's name, "An Alle:" (DS:2AC6) for a question to everyone
    canvas_.text(ClassicFontId::Flow, 105, 125, q.manager >= 0 ? manager_name(q.manager) : "An Alle:", 7, 0, 210);
    for (const int x : {83, 212}) {
        canvas_.rect(x, 112, x + 21, 133, 14);
        canvas_.outline(x + 1, 113, x + 20, 132, 12);
        canvas_.text(ClassicFontId::NewBold, x, 129, "?", 0, -1, x + 24);
    }
    canvas_.line(109, 131, 207, 131, 7);
    canvas_.line(80, 163, 237, 163, 0);
    const int y = q.line3.empty() ? 145 : 141;  // two lines from 145, three from 141, 8 apart
    canvas_.text_native(ClassicFontId::Mini, 105, y, q.line1, 7, 0, 210);
    canvas_.text_native(ClassicFontId::Mini, 105, y + 8, q.line2, 7, 0, 210);
    if (!q.line3.empty()) canvas_.text_native(ClassicFontId::Mini, 105, y + 16, q.line3, 7, 0, 210);
    button(95, 166, q.yes);
    button(165, 166, q.no);
}

void ClassicApplication::click_manager_question(int x, int y, bool right) {
    const bool yes = !right && x >= 96 && x <= 151 && y >= 167 && y <= 177;
    const bool no = !right && x >= 166 && x <= 221 && y >= 167 && y <= 177;
    if (!yes && !no) return;
    const ManagerQuestion q = *manager_question_;
    manager_question_.reset();
    if (q.cup_draw) {  // "Aber klar" / "Kein Gedanke" (DS:E068); a new game starts now (classic_cup_draw.cpp)
        cup_answer_ = yes;
        if (pending_start_) finish_new_game(yes);
        return;
    }
    // "Ihr Angebot wurde abgelehnt." is the timed notice of sub_1A2B8 (mode 75, ovr035:1A9E)
    if (q.unfair) { timed_native_notice("Ihr Angebot\nwurde abgelehnt."); return; }
    try {
        auto backing = std::make_unique<HokFile>(clone_hok(snapshot()));
        PurchaseDraft draft;
        PurchaseResult result = PurchaseResult::Invalid;
        session_.apply([&](Game& game) { result = game.answer_manager_offer(q.offer, yes, draft, false); return ActionResult{}; });
        invalidate();
        switch (result) {
            case PurchaseResult::SellerRefused: timed_native_notice("Ihr Angebot\nwurde abgelehnt."); break;
            case PurchaseResult::Negotiating:  // the contract panel of the bought player, as for a computer player
                purchase_draft_ = draft;
                purchase_backdrop_ = std::move(backing);
                open_contract(draft.club_slot);
                if (contract_editor_) contract_editor_->offset = 0;  // ovr035:185D: ovl11_41db(record, slot, 0)
                if (!contract_editor_) cancel_purchase();
                break;
            case PurchaseResult::Purchased: break;  // loan: no contract panel, no sponsor
            case PurchaseResult::SquadFull: notice("Schon 29 Mann\nim Team."); break;
            default: notice("Dieser Transfer ist derzeit nicht möglich."); break;
        }
    } catch (const std::exception& e) {
        notice(e.what());
    }
}

}  // namespace em::ui
