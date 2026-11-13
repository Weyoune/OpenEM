// SPDX-License-Identifier: GPL-3.0-only
// Classic-mode career layer on top of the main menu: the manager's message
// field (opened by clicking the manager's face or automatically when new
// messages arrived) and the end-of-season review pages, including the
// hot-seat sequence that shows one page per human manager. These are
// ClassicApplication members; their state lives in ClassicCareerState.
#include <optional>

#include "game/messages.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_messages.hpp"
#include "ui/classic_season_review.hpp"

namespace em::ui {

// UI state of the message field and the season review (owned by
// ClassicApplication::career_, created on first use).
struct ClassicCareerState {
    ClassicMessageField messages;  // the scrolling message field over the main menu
    // Season review of the last step: shown until a click or key, then its
    // text lines are covered and the retirement messages of the manager's
    // own players follow in the message field.
    std::optional<SeasonReview> review;
    bool review_overview = true;  // the overview page is shown first, then the manager's page
    bool review_cleared = false;  // the review text has been covered; messages may follow
    std::vector<std::string> review_messages;  // messages still to show after the review
    // Hot seat: the game shows the page of every manager 0..N-1 in index
    // order; these are the pages still to come after the current one.
    struct Page { int manager = 0; SeasonReview review; std::vector<std::string> messages; };
    std::vector<Page> next_pages;
    int review_manager = 0;  // manager whose review page is currently shown
};

// True when the main menu is shown with nothing else on top of it (no
// dialog, notice, question, overlay or running day advance). Only then may
// the message field open.
bool ClassicApplication::main_menu_idle() const {
    return session_.loaded() && screen_ == ClassicScreen::Main && !advancing_ && notice_.empty() && !quit_question_ &&
           !talent_question_ && !manager_question_ && !session_.contract_question() && !player_detail_ && !club_info_ && !file_preview_ &&
           !native_notice_until_ && !match_banner_until_ && category_ < 0 && !(career_ && career_->review);
}

// Entering the idle main menu while the active manager's "new messages" flag
// (one byte per manager at kMessageNewFlag in the save state) is set clears
// the flag and opens the message field.
void ClassicApplication::check_new_messages() {
    if (!main_menu_idle() || (career_ && career_->messages.active())) return;
    const int m = active_manager();
    if (session_.game().save().state.at(kMessageNewFlag + size_t(m)) == 0) return;
    action([m](Game& game) { game.clear_message_flag(m); return ActionResult{}; });
    open_messages();
}

// Opens the message field on the active manager's messages.
void ClassicApplication::open_messages() {
    if (!career_) career_ = std::make_shared<ClassicCareerState>();
    career_->messages.open(session_.game().save().state, active_manager());
}

// After a day advance: shows the season reviews produced by it (one page
// per manager that has one), or otherwise the messages that must be shown
// at once. If a review was already shown before this day's contract talks,
// it is not shown a second time. Stops the running advance so the player
// can read them.
void ClassicApplication::show_immediate_messages() {
    if (!session_.loaded()) return;
    const Game& game = session_.game();
    bool any_review = false;
    for (int m = 0; m < game.managers(); m++) any_review |= game.season_review(m).has_value();
    if (any_review && season_review_shown_) {  // shown before the contract talks of this day
        season_review_shown_ = false;
        return;
    }
    if (!any_review && game.immediate_messages().empty()) return;
    if (any_review) {
        std::array<std::optional<SeasonReview>, kMaxManagers> reviews;
        std::array<std::vector<std::string>, kMaxManagers> messages;
        for (int m = 0; m < game.managers(); m++) {
            reviews[size_t(m)] = game.season_review(m);
            messages[size_t(m)] = game.immediate_messages(m);
        }
        show_reviews(reviews, messages);
    } else {
        if (!career_) career_ = std::make_shared<ClassicCareerState>();
        career_->messages.show_immediate(game.immediate_messages());
    }
    advancing_ = false;  // the original waits for the player at the review and at each of these messages
}

// Starts a single season review (overview first) for the active manager,
// followed by the given messages.
void ClassicApplication::show_review(const SeasonReview& review, std::vector<std::string> messages) {
    if (!career_) career_ = std::make_shared<ClassicCareerState>();
    career_->review = review;
    career_->review_overview = true;
    career_->review_cleared = false;
    career_->review_messages = std::move(messages);
    career_->next_pages.clear();
    career_->review_manager = active_manager();
}

// The pages of managers 0..N-1 (the overview before the first). Each page is shown with its manager at the menu
// (header face, name and club); afterwards the manager of a pending contract talk, else manager 0, takes the seat.
void ClassicApplication::show_reviews(const std::array<std::optional<SeasonReview>, kMaxManagers>& reviews,
                                      const std::array<std::vector<std::string>, kMaxManagers>& messages) {
    std::vector<ClassicCareerState::Page> pages;
    for (int m = 0; m < kMaxManagers; m++)
        if (reviews[size_t(m)]) pages.push_back({m, *reviews[size_t(m)], messages[size_t(m)]});
    if (pages.empty()) return;
    if (session_.game().managers() > 1) session_.select_manager(pages.front().manager);
    show_review(pages.front().review, pages.front().messages);
    career_->review_manager = pages.front().manager;
    career_->next_pages.assign(pages.begin() + 1, pages.end());
}

// The review ends with its last message, or with the first click when there is none.
void ClassicApplication::advance_review() {
    auto& s = *career_;
    if (!s.review || s.messages.active()) return;
    if (s.review_overview) {  // the overview waits for one input, then the manager's page follows
        s.review_overview = false;
        return;
    }
    if (!s.review_cleared) {
        s.review_cleared = true;
        if (!s.review_messages.empty()) {
            s.messages.show_immediate(std::move(s.review_messages));
            s.review_messages.clear();
            return;
        }
    }
    s.review.reset();
    if (!s.next_pages.empty()) {  // the next manager's page (no second overview)
        auto page = std::move(s.next_pages.front());
        s.next_pages.erase(s.next_pages.begin());
        session_.select_manager(page.manager);
        s.review = std::move(page.review);
        s.review_manager = page.manager;
        s.review_overview = false;
        s.review_cleared = false;
        s.review_messages = std::move(page.messages);
    } else if (session_.game().managers() > 1) {
        const auto& q = session_.contract_question();
        session_.select_manager(q ? q->manager : 0);
    }
    invalidate();
}

// Mouse press while the career layer may be active. Returns true when the
// click was consumed (message field or review open, or a click on the
// manager's face in the idle main menu, which opens the message field).
bool ClassicApplication::career_click(int x, int y, bool right) {
    if (career_ && career_->messages.active()) {
        if (screen_ != ClassicScreen::Main || !session_.loaded()) { career_->messages = ClassicMessageField{}; return false; }
        if (!right) career_->messages.press(session_.game().save().state, x, y);
        if (!career_->messages.active() && career_->review_cleared) advance_review();
        return true;
    }
    if (career_ && career_->review) {
        if (!right) advance_review();
        return true;
    }
    // A click on the manager's face (x 233..271, y 34..71) opens the message field.
    if (!right && main_menu_idle() && x > 0xE8 && x < 0x110 && y > 0x21 && y < 0x48) {
        open_messages();
        return true;
    }
    return false;
}

void ClassicApplication::career_release() {
    if (career_) career_->messages.release();
}

// Any key closes the message field or advances the review. Returns true when
// consumed.
bool ClassicApplication::career_key(int) {
    if (!career_ || (!career_->messages.active() && !career_->review)) return false;
    if (career_->messages.active()) {
        career_->messages.close();  // any key leaves the field
        if (!career_->messages.active() && career_->review_cleared) advance_review();
    } else {
        advance_review();
    }
    return true;
}

// Name of the open career modal ("messages", "review-overview", "review"),
// or empty when none is open.
std::string ClassicApplication::career_modal() const {
    if (!career_) return {};
    if (career_->messages.active()) return "messages";
    if (career_->review) return career_->review_overview ? "review-overview" : "review";
    return {};
}

// Draws the review page (or overview) and/or the message field over the
// main menu. Faces come from the save state at 0x01EE + manager (modulo 12).
void ClassicApplication::draw_career() {
    if (!career_ || !session_.loaded() || screen_ != ClassicScreen::Main) return;
    if (career_->review) {
        if (career_->review_overview) {
            const auto& h = snapshot();
            if (session_.game().managers() > 1) {  // every human title holder with his face and name
                std::array<int, 4> faces{-1, -1, -1, -1};
                std::array<std::string, 4> names;
                for (int m = 0; m < session_.game().managers(); m++) {
                    faces[size_t(m)] = h.state.at(0x01EE + size_t(m)) % 12;
                    names[size_t(m)] = manager_name(m);
                }
                draw_classic_season_overview(canvas_, *career_->review, faces, names);
            } else {
                draw_classic_season_overview(canvas_, *career_->review, h.state.at(0x01EE) % 12, h.manager);
            }
            return;
        }
        // Ice background, the main-menu header in its season-review form,
        // title and panel, then the review text.
        canvas_.background(22);
        draw_main_header(&*career_->review);
        draw_classic_season_review_frame(canvas_);
        draw_classic_season_review_body(canvas_, *career_->review);
        if (career_->review_cleared) canvas_.rect(0x11, 0xA4, 0x126, 0xBB, 4);  // text lines covered with colour 4
    }
    if (career_->messages.active()) career_->messages.draw(canvas_, session_.game().save().state);
}

}  // namespace em::ui
