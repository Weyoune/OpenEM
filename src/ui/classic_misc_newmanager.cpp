// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_misc_newmanager.hpp"

#include <algorithm>

namespace em::ui {
namespace {
constexpr size_t kOptions = 0x28, kFaces = 0x01EE, kFileNumber = 0x06AE;  // DS:0118, DS:0178, DS:0256
constexpr size_t kManagers = 0x0DCD, kManagerSize = 0x1AF, kTeams = 0x17E7, kTeamSize = 0x3D;
constexpr std::array<int, 4> kOptionOffset{0, 6, 10, 13};  // DS:4B3E: first KB1 cell of each option
constexpr auto kFont = ClassicFontId::Mini;                // native font 3
constexpr uint8_t kKey = 0xC0;
constexpr int kReturn = 13, kBackspace = 8, kEscape = 27, kKeypadEnter = 0x40000058;  // SDL key codes

std::string cstring(const Bytes& b, size_t at, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && b.at(at + i); i++) s.push_back(char(b.at(at + i)));
    return s;
}
int crest_asset(const HokFile& h, int team) {
    return 0x50 + (h.state.at(kTeams + size_t(team) * kTeamSize + 0x34) & 0x3F);
}
std::string team_name(const HokFile& h, int team) { return cstring(h.state, kTeams + size_t(team) * kTeamSize, 0x16); }
int row_top(int row) { return 41 + 51 * row; }
// ovr027:00E1: the two lines above and below a face or crest cell.
void mark(ClassicCanvas& c, int column, int row, uint8_t color) {
    const int x = (column - 1) * 0x84 + 7, y = row * 0x33 + 0x28;
    c.line(x, y, x + 0x23, y, color);
    c.line(x, y + 0x27, x + 0x23, y + 0x27, color);
}
// The board cell under the pointer (ovr027:0FAB), or -1.
std::pair<int, int> board_cell(ClassicNewManager::Panel panel, int x, int y) {
    if (panel == ClassicNewManager::Panel::Options || x <= 0xBC || y <= 0x28 || y >= 0xCA) return {-1, -1};
    int col, row;
    if (panel == ClassicNewManager::Panel::Clubs) { col = (x - 0xBE) / 0x28; row = (y - 0x29) / 0x28; }
    else { col = (x - 0xC1) / 0x29; row = (y - 0x27) / 0x29; }
    return {std::clamp(col, 0, 2), std::clamp(row, 0, 3)};
}
}  // namespace

void ClassicNewManager::open(const HokFile& h) {
    *this = ClassicNewManager{};
    const Bytes& s = h.state;
    const int managers = 1;  // OpenEM plays manager 0 only (DS:0386)
    for (int m = 0; m < managers; m++) {
        auto& r = rows_[size_t(m)];
        r.face = s.at(kFaces + size_t(m)) == 0xFF ? -1 : s.at(kFaces + size_t(m));
        r.team = s.at(kManagers + size_t(m) * kManagerSize + 0x14);
        r.name = cstring(s, kManagers + size_t(m) * kManagerSize, 0x14);
        r.existing = true;
    }
    for (size_t i = 0; i < 4; i++) options_.values[i] = s.at(kOptions + i);
    highscore_ = (s.at(kFileNumber) | s.at(kFileNumber + 1)) != 0;
}

void ClassicNewManager::open_start(const HokFile&) {
    *this = ClassicNewManager{};
    options_ = NewGameOptions{};  // a new program run starts with the EM.EXE values dseg:0118, var_57 = 3
    highscore_ = new_game_highscore_file(options_) != 0;  // DS:0256 = 141 for these values (H.141)
    start_mode_ = true;
}

void ClassicNewManager::draw(ClassicCanvas& c, const HokFile& h, int mx, int my) const {
    c.background(150);
    for (int r = 0; r < 4; r++) {
        const auto& row = rows_[size_t(r)];
        const int y = row_top(r);
        if (row.face >= 0) {
            c.blit(0, 6, y, 1 + 41 * (row.face % 3), 1 + 41 * (row.face / 3), 38, 38, true, kKey);
            if (!(editing_ && r == row_)) c.text_native(kFont, 47, 48 + 51 * r, row.name, 7, -1, 133);
        }
        if (row.team >= 0) {
            if (!row.existing) c.rect(138, y, 175, y + 37, 0x0B);  // copied from the colour 11 club page
            c.blit(crest_asset(h, row.team), 138, y, 0, 0, 38, 38, true, kKey);
            c.text_native(kFont, 47, 56 + 51 * r, team_name(h, row.team), 7, -1, 133);
        }
    }
    if (editing_ && row_ >= 0) {  // ovr027:130B: the name field (sub_19CFB, 19 letters in 80 pixels)
        c.rect(0x2F, 0x2A + 51 * row_, 0x85, 0x31 + 51 * row_, 0);
        c.rect(0x30, 0x2B + 51 * row_, 0x84, 0x30 + 51 * row_, 6);
        c.text_native(kFont, 0x32, 0x2F + 51 * row_, draft_, 7);
    }
    for (const auto& e : erased_) mark(c, e.first, e.second, 8);
    if (mark_.first > 0) mark(c, mark_.first, mark_.second, 0x18);
    if (panel_ == Panel::Options) {
        c.blit(1, 0xC2, 0xC2, 0x71, highscore_ ? 0x26 : 0, 0x36, 0x26, true, kKey);  // notepad / nail
        c.blit(1, 0xC1, 0x2F, 0x71, 0x4C + 0x0D * (options_.mode & 1), 0x39, 0x0D);         // league/cup mode (var_57 bit 0)
        c.blit(1, 0x103, 0x2F, 0x71, 0x66 + 0x0D * ((options_.mode & 2) != 0), 0x39, 0x0D);  // WM mode (bit 1)
        for (int i = 0; i < 4; i++)
            c.blit(1, 0xC3, 0x51 + 0x1C * i, 0, 10 * (options_.values[size_t(i)] + kOptionOffset[size_t(i)]), 0x71, 10, true, kKey);
        return;
    }
    if (panel_ == Panel::Faces) c.rect(0xBE, 0xCA, 0x13D, 0xE7, 0x0B);
    c.blit(7, 0xB8, 0x20, 0, 0, 0x88, 0xD0, true, kKey);
    if (panel_ == Panel::Faces) {
        c.blit(0, 0xB8 + 9, 0x20 + 7, 0, 0, 0x7A, 0xA3, true, kKey);
        c.text(kFont, 0xBC, 0xD6, "Suchen Sie sich einen", 0, -1, 0x13F);
        c.text(kFont, 0xBC, 0xDE, "passenden Kopf aus.", 0, -1, 0x13F);
    } else {
        c.rect(0xBE, 0x29, 0xBE + 0x76, 0x29 + 0x9E, 0x0B);
        for (int k = 0; k < 12; k++)
            c.blit(crest_asset(h, page_ * 12 + k), 0xBE + 40 * (k % 3), 0x29 + 40 * (k / 3), 0, 0, 38, 38, true, kKey);
        c.blit(4, 0x136, 0x27, 0, 0, 9, 0xA3, true, kKey);
        if (pressed_arrow_) c.blit(4, 0x136, 0x27 + 0x51 * (pressed_arrow_ > 0), 10, 0x51 * (pressed_arrow_ > 0), 9, 0x52, true, kKey);
    }
    hover_ = board_cell(panel_, mx, my).first >= 0 ? board_cell(panel_, mx, my) : hover_;
    if (hover_.first < 0) return;
    const int col = hover_.first, row = hover_.second;
    if (panel_ == Panel::Clubs) {
        c.outline(0xBD + 40 * col, 0x28 + 40 * row, 0xE4 + 40 * col, 0x4F + 40 * row, 0x12);
        c.rect(0xBE, 0xCA, 0x13D, 0xE7, 0x0B);
        c.text_native(kFont, 0xBE, 0xE1, team_name(h, page_ * 12 + row * 3 + col), 0, -1, 0x13D);
    } else {
        c.outline(0xC0 + 41 * col, 0x26 + 41 * row, 0xE9 + 41 * col, 0x4F + 41 * row, 0x12);
    }
}

void ClassicNewManager::close_panel() {
    if (mark_.first > 0) erased_.insert(mark_);
    mark_ = {-1, -1};
    panel_ = Panel::Options;
    hover_ = {-1, -1};
    pressed_arrow_ = 0;
}

void ClassicNewManager::finish_name() {
    if (draft_.size() < 2) return;  // ovr027:13BC: at least two letters, the field stays open
    rows_[size_t(row_)].name = draft_;
    editing_ = false;
}

ClassicNewManager::Result ClassicNewManager::click(const HokFile&, int x, int y, bool right) {
    pressed_arrow_ = 0;
    if (editing_) {
        if (!right) finish_name();
        return Result::None;
    }
    const bool manager_field = y > 0x26 && y < 0xEB && (x < 0x2D || (x > 0x88 && x < 0xB2));
    if (manager_field) {
        const int row = std::clamp((y - 0x26) / 0x33, 0, 3), column = x > 0x2D ? 2 : 1;
        if (rows_[size_t(row)].existing) return Result::None;  // the running game's managers stay
        if (right) {  // ovr027:0AE4: remove the new manager of this row
            rows_[size_t(row)] = Row{};
            for (int col = 1; col <= 2; col++) erased_.insert({col, row});
            if (mark_.first > 0) erased_.insert(mark_);
            mark_ = {-1, -1};
            panel_ = Panel::Options;
            return Result::None;
        }
        if (mark_.first > 0) erased_.insert(mark_);
        mark_ = {column, row};
        row_ = row;
        const Panel next = column == 1 ? Panel::Faces : Panel::Clubs;
        if (next != panel_) hover_ = {-1, -1};
        panel_ = next;
        return Result::None;
    }
    if (right) return Result::None;
    if (panel_ == Panel::Clubs && x > 0x135 && y > 0x28 && y < 0xCA && y != 0x78) {  // scroll arrows
        page_ = (page_ + (y > 0x78) - (y < 0x78) + 4) % 4;  // the pressed arrow shows only while held
        if (mark_.first > 0) erased_.insert(mark_);  // ovr027:0E71 overdraws the mark, the row stays active
        mark_ = {-1, -1};
        hover_ = {-1, -1};
        return Result::None;
    }
    if (panel_ != Panel::Options) {
        const auto cell = board_cell(panel_, x, y);
        if (cell.first >= 0 && row_ >= 0) {
            const int index = cell.second * 3 + cell.first + (panel_ == Panel::Clubs ? page_ * 12 : 0);
            for (const auto& other : rows_)  // ovr027:121E: taken faces and clubs cannot be chosen
                if ((panel_ == Panel::Faces ? other.face : other.team) == index) return Result::None;
            auto& r = rows_[size_t(row_)];
            if (panel_ == Panel::Faces) {
                r.face = index;
                draft_.clear();
                editing_ = true;
            } else {
                r.team = index;
            }
            close_panel();
            return Result::None;
        }
        if (y <= 0xF0 && y > 0x2C && x > 0x2D && x < 0x88) close_panel();  // ovr027:0F35
        return Result::None;
    }
    if (start_mode_) {  // ovr027:06AA: only a new game (var_64 = 0) changes the settings
        if (x > 0xBE && y > 0x2D && y < 0x3D) {  // mode buttons
            int named = 0;
            for (const auto& r : rows_) named += !r.name.empty();
            if (toggle_new_game_mode(options_, x > 0xFD ? 1 : 0, named)) highscore_ = new_game_highscore_file(options_) != 0;
            return Result::None;
        }
        for (int i = 0; i < 4 && x > 0x134; i++) {  // the arrows of the four settings (ovr027:07AF)
            const int top = 0x1C * i;
            if (y <= top + 0x4D || y >= top + 0x5F) continue;
            step_new_game_option(options_, i, (y > top + 0x56) - (y < top + 0x56));
            highscore_ = new_game_highscore_file(options_) != 0;
            return Result::None;
        }
    }
    if (x > 0xFD && y > 0xC1 && x < 0x135 && y < 0xE9) {  // Los jetzt!
        for (const auto& r : rows_)
            if (!r.existing && ((r.face >= 0) != (r.team >= 0))) return Result::None;  // incomplete row
        for (const auto& r : rows_)
            if (!r.existing && r.face >= 0) return Result::LeaveWithManager;
        if (start_mode_) return Result::None;  // ovr027:15AF: a new game needs a manager (var_57 != 0)
        return Result::Leave;
    }
    return Result::None;
}

void ClassicNewManager::key(int key) {
    if (!editing_) return;
    if (key == kReturn || key == kKeypadEnter) finish_name();
    else if (key == kBackspace && !draft_.empty()) draft_.pop_back();
    else if (key == kEscape) draft_.clear();
}

void ClassicNewManager::text(std::string_view text) {
    if (!editing_) return;
    for (unsigned char ch : text)  // sub_19CFB with letters: 'A'..'z' and the space
        if (draft_.size() < 19 && ((ch >= 'A' && ch <= 'z') || ch == ' ')) draft_.push_back(char(ch));
}
}  // namespace em::ui
