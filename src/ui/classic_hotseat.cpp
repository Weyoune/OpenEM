// SPDX-License-Identifier: GPL-3.0-only
// Hot seat: up to four human managers share the menu.
//
// On every menu day the original's day loop runs, for each manager 0..N-1 in index order, the transfer-list roll and
// the main menu with that manager as the active one. The menu draws the header of that manager (name, face, club,
// finances) and opens his message field when his "new messages" flag is set; the crest ("weiter") ends his turn. There
// is no screen between two managers: the next manager's menu follows directly. After the last manager the day is
// played and the next menu day starts again with manager 0. The random draws of the menu pass are made by
// Game::menu_pass at the end of the day step.
//
// This file also holds the dialogs between human managers (a bid for another manager's list player, answered by the
// owner at the same seat) and the "An Alle:" question before the DEB cup draw.
#include "game/manager_layout.hpp"
#include "ui/classic_app.hpp"

namespace em::ui {

// Index of the manager whose turn it is (0 when no game is loaded).
int ClassicApplication::active_manager() const { return session_.loaded() ? session_.active_manager() : 0; }
// the squad of the line-up page and its player pages: the national squad (block 5) for the national coach
int ClassicApplication::squad_manager() const { return session_.loaded() ? session_.game().lineup_manager() : 0; }

// Manager name (first 0x13 bytes of the manager record, German ISO 646) as UTF-8.
std::string ClassicApplication::manager_name(int manager) {
    const Bytes& s = snapshot().state;
    return iso646de_to_utf8(reinterpret_cast<const char*>(s.data() + manager_block(manager)), 0x13);
}

// The crest of manager m < N - 1 hands the menu to manager m + 1 on the same day (returns true, menu selections are
// reset); the last one plays the day (returns false, manager 0 is selected again).
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

// Bid for a list player of another human manager (Game::offer_to_manager). The owner answers at the same seat in a
// dialog headed with his name. pool_slot: slot in the transfer list (squad block 4, 52-byte records whose +15 is the
// 1-based player id); fee: internal money (DM * 3); loan: a loan instead of a purchase. Player records are 33 bytes at
// 0x4FC6 (name in the first 0x11 bytes, owning manager at +0x1A). Returns false if the player does not belong to
// another human manager (the caller then handles the bid as usual), true when handled here.
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
        // an unfair amount: the owner only gets the buttons "Nie" and "Nein"
        case PurchaseResult::Unfair: manager_question_ = ManagerQuestion{owner, "Dieser Betrag ist", "nicht erlaubt (unfair) !", "Nie", "Nein", true, offer}; break;
        // the owner is asked; the bid is shown in DM without thousands separators
        case PurchaseResult::OwnerQuestion:
            manager_question_ = ManagerQuestion{owner, "Nehmen Sie das Angebot (" + std::to_string(fee / 3) + " DM)", "f]r " + name + " an ?",
                                                "Na gut.", "Nein !", false, offer};
            break;
        default: notice("Dieser Transfer ist derzeit nicht möglich."); break;
    }
    return true;
}

// The DEB cup draw question: with the DEB-Pokal display switch on, the empty draw board appears and the question
// "An Alle:" asks whether to watch the draw live. The callers check the switch (classic_misc_input.cpp for a new game,
// Game for the later rounds); the answer and the live draw are handled in classic_cup_draw.cpp.
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

// Draws the question dialog of one manager (or "An Alle:"): name, two "?"
// boxes, two or three text lines and the two answer buttons. For the cup
// draw the empty board is drawn behind it first.
void ClassicApplication::draw_manager_question() {
    const auto& q = *manager_question_;
    if (q.cup_draw) {  // frame, title, n / 2 rows of two KB/25 bars for the n clubs of the round
        canvas_.background();
        canvas_.frame(3, 3, 13, 12);
        canvas_.text(ClassicFontId::NewBold, 0, 0x1A, "DEB-Pokal Auslosung", 7, 1, 0x141);
        const int teams = q.cup_clubs;
        int y = 0x80 - ((teams >> 1) * 11 >> 1);
        for (int row = 0; row < teams; row += 2, y += 11)
            for (const int x : {37, 164}) canvas_.blit(25, x, y, 0, 0, 0x78, 10, true, 0xC0);  // left and right column
    }
    canvas_.frame(75, 107, 7, 4);
    // the manager's name, or "An Alle:" for a question to everyone
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

// Left click on one of the two answer buttons. For the cup draw it records
// the answer (and starts a pending new game). For a bid it lets the game
// apply the owner's answer: refusal (timed notice), contract talks for a
// purchase, or a completed loan.
void ClassicApplication::click_manager_question(int x, int y, bool right) {
    const bool yes = !right && x >= 96 && x <= 151 && y >= 167 && y <= 177;
    const bool no = !right && x >= 166 && x <= 221 && y >= 167 && y <= 177;
    if (!yes && !no) return;
    const ManagerQuestion q = *manager_question_;
    manager_question_.reset();
    if (q.cup_draw) {  // "Aber klar" / "Kein Gedanke"; a pending new game starts now (classic_cup_draw.cpp)
        cup_answer_ = yes;
        if (pending_start_) finish_new_game(yes);
        return;
    }
    // "Ihr Angebot wurde abgelehnt." is shown as a timed notice that closes by itself
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
                if (contract_editor_) contract_editor_->offset = 0;  // the game opens this dialog at the top position
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
