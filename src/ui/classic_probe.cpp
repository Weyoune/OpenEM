// SPDX-License-Identifier: GPL-3.0-only
// Read-only description of the current screen for scripted headless runs (classic_script.cpp). The order follows
// the input dispatch of ClassicApplication::click: the first entry is the one a click would reach.
#include "ui/classic_app.hpp"

namespace em::ui {

ClassicUiProbe ClassicApplication::probe() const {
    ClassicUiProbe p;
    p.category = category_;
    p.text = notice_;
    if (title_) { p.modal = "title"; return p; }
    if (game_end_ == 2) { p.modal = "game-end"; return p; }
    if (manager_question_ && manager_question_->cup_draw) { p.modal = "cup-question"; return p; }
    if (cup_live_) { p.modal = cup_live_waiting() ? "cup-draw-key" : cup_live_page() ? "cup-page" : "cup-draw"; return p; }
    if (!match_frames_.empty()) {
        p.modal = match_frames_[match_frame_].cup_table ? "cup-table" : "match";
        if (!match_frames_[match_frame_].cup_table) p.deadline = match_frame_until_;
        return p;
    }
    if (match_banner_until_) { p.modal = "banner"; p.deadline = match_banner_until_; return p; }
    if (native_notice_until_) { p.modal = "timed-notice"; p.deadline = native_notice_until_; return p; }
    if (!notice_.empty()) { p.modal = "notice"; return p; }
    if (sale_quote_) { p.modal = "sale-offer"; return p; }
    if (talent_question_) { p.modal = "talent-question"; return p; }
    if (quit_question_) { p.modal = "quit-question"; return p; }
    if (session_.contract_question()) {  // the review of the day comes first, then the dialog
        if (const auto career = career_modal(); !career.empty()) { p.modal = career; return p; }
        p.modal = "contract-question";
        p.text = session_.contract_question()->name;
        p.slot = session_.contract_question()->slot;
        return p;
    }
    if (advancing_) { p.modal = "advancing"; return p; }
    if (const auto career = career_modal(); !career.empty()) { p.modal = career; return p; }
    if (advert_renewal_) { p.modal = "advert-question"; return p; }
    if (stadium_build_) { p.modal = "stadium-question"; return p; }
    if (fee_editor_) { p.modal = "fee-editor"; return p; }
    if (file_preview_) { p.modal = "file-preview"; return p; }
    if (player_detail_) { p.modal = "player-detail"; return p; }
    if (club_info_) { p.modal = team_schedule_open_ ? "team-schedule" : "club-info"; return p; }
    if (contract_editor_) { p.modal = "contract-editor"; return p; }
    return p;
}

}  // namespace em::ui
