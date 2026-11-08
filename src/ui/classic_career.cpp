// SPDX-License-Identifier: GPL-3.0-only
// Main-menu message field (ovr036:0504) and the career pages (season change, dismissal, insolvency).
#include "game/messages.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_messages.hpp"

namespace em::ui {

struct ClassicCareerState {
    ClassicMessageField messages;
};

bool ClassicApplication::main_menu_idle() const {
    return session_.loaded() && screen_ == ClassicScreen::Main && !advancing_ && notice_.empty() && !quit_question_ &&
           !talent_question_ && !session_.contract_question() && !player_detail_ && !club_info_ && !file_preview_ &&
           !native_notice_until_ && !match_banner_until_ && category_ < 0;
}

void ClassicApplication::check_new_messages() {
    // ovr034:2120: entering the main menu with the manager's flag DS:5A50 set clears it and opens the field
    if (!main_menu_idle() || (career_ && career_->messages.active())) return;
    if (session_.game().save().state.at(kMessageNewFlag) == 0) return;
    action([](Game& game) { game.clear_message_flag(0); return ActionResult{}; });
    open_messages();
}

void ClassicApplication::open_messages() {
    if (!career_) career_ = std::make_shared<ClassicCareerState>();
    career_->messages.open(session_.game().save().state, 0);
}

void ClassicApplication::show_immediate_messages() {
    if (!session_.loaded() || session_.game().immediate_messages().empty()) return;
    if (!career_) career_ = std::make_shared<ClassicCareerState>();
    career_->messages.show_immediate(session_.game().immediate_messages());
    advancing_ = false;  // the original waits for the player at each of these messages
}

bool ClassicApplication::career_click(int x, int y, bool right) {
    if (career_ && career_->messages.active()) {
        if (screen_ != ClassicScreen::Main || !session_.loaded()) { career_->messages = ClassicMessageField{}; return false; }
        if (!right) career_->messages.press(session_.game().save().state, x, y);
        return true;
    }
    // ovr034:234D: a click on the manager's face opens the message field
    if (!right && main_menu_idle() && x > 0xE8 && x < 0x110 && y > 0x21 && y < 0x48) {
        open_messages();
        return true;
    }
    return false;
}

void ClassicApplication::career_release() {
    if (career_) career_->messages.release();
}

bool ClassicApplication::career_key(int) {
    if (!career_ || !career_->messages.active()) return false;
    career_->messages.close();  // DS:01AB: any key leaves the field
    return true;
}

void ClassicApplication::draw_career() {
    if (career_ && career_->messages.active() && session_.loaded() && screen_ == ClassicScreen::Main)
        career_->messages.draw(canvas_, session_.game().save().state);
}

}  // namespace em::ui
