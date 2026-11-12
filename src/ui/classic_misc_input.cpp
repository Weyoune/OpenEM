// SPDX-License-Identifier: GPL-3.0-only
// Manager -> Highscore and Manager -> Neuer of the native menu.
#include <algorithm>
#include <fstream>

#include "game/new_game.hpp"

#include "game/highscore.hpp"
#include "game/wm_mode.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_misc_highscore.hpp"
#include "ui/classic_misc_newmanager.hpp"

namespace em::ui {
namespace fs = std::filesystem;

struct ClassicMiscState {
    std::optional<HighscoreTables> tables;
    HighscoreTable highscore;
    uint32_t game = 0;
    bool wm_only = false;
    ClassicNewManager setup;
    std::unique_ptr<HokFile> start_base;  // start menu of a new game: the fresh start state (game/new_game.hpp)
};

namespace {
// The original reads and writes HIGH\H.<n> in its own directory. OpenEM never writes into the data directory:
// the list lives in the OpenEM save directory and is seeded from the data directory's file once.
void store(const fs::path& file, const HighscoreTable& table) {
    fs::create_directories(file.parent_path());
    const fs::path temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(table.bytes.data()), std::streamsize(table.bytes.size()));
        if (!out) throw FormatError("Highscore: Datei nicht schreibbar");
    }
    fs::rename(temporary, file);
}
}  // namespace

void ClassicApplication::open_highscore() {
    if (!misc_) misc_ = std::make_shared<ClassicMiscState>();
    auto& m = *misc_;
    const int number = highscore_file_number(session_.game().save());
    if (number == 0) return;  // ovr040:30ED returns at once: this game setting keeps no list
    try {
        if (!m.tables) m.tables = load_highscore_tables(read_file(data_ / "EM.EXE"));
        const fs::path own = saves_ / highscore_file_name(number), seed = data_ / highscore_file_name(number);
        std::error_code ec;
        m.highscore = fs::exists(own, ec) ? read_highscore_table(read_file(own))
                     : fs::exists(seed, ec) ? read_highscore_table(read_file(seed)) : HighscoreTable{};
        m.game = highscore_game_identity(session_.game().save());
        m.wm_only = session_.game().save().state.at(0x025A) == 1;
        // ovr040:3131..3150: the score of every manager in turn (sub_698D2), each merged into the list (sub_69B4D)
        for (int manager = 0; manager < session_.game().managers(); manager++) {
            HighscoreRecord record;
            action([&](Game& game) { record = game.highscore_record(manager, *m.tables); return ActionResult{}; });
            merge_highscore(m.highscore, record, m.game);
        }
        store(own, m.highscore);
        screen_ = ClassicScreen::Highscore;
    } catch (const std::exception& e) {
        notice(e.what());
    }
}

void ClassicApplication::open_new_manager() {
    if (!misc_) misc_ = std::make_shared<ClassicMiscState>();
    misc_->setup = ClassicNewManager{};
    misc_->setup.open(snapshot());
    screen_ = ClassicScreen::NewManager;
}

void ClassicApplication::draw_misc() {
    if (!misc_) return;
    if (misc_->start_base) { misc_->setup.draw(canvas_, *misc_->start_base, mouse_x_, mouse_y_); return; }
    if (screen_ == ClassicScreen::Highscore) draw_classic_highscore(canvas_, misc_->highscore, misc_->wm_only);
    else misc_->setup.draw(canvas_, snapshot(), mouse_x_, mouse_y_);
}

void ClassicApplication::click_misc(int x, int y, bool right) {
    if (!misc_ || screen_ == ClassicScreen::Highscore) {  // ovr040:33B8 waits for any click
        screen_ = ClassicScreen::Main;
        if (game_end_ == 1) game_end_ = 2;  // the end of the game goes on with "Das wars..."
        return;
    }
    if (misc_->start_base) { click_start_menu(x, y, right); return; }
    const auto result = misc_->setup.click(snapshot(), x, y, right);
    if (result == ClassicNewManager::Result::Leave) screen_ = ClassicScreen::Main;
    else if (result == ClassicNewManager::Result::LeaveWithManager) {
        // ovr027 with var_64 = old manager count: every complete new row joins the running game in menu order
        screen_ = ClassicScreen::Main;
        try {
            const int first = session_.game().managers();
            for (const auto& r : misc_->setup.rows())
                if (!r.existing && r.face >= 0 && r.team >= 0 && r.name.size() >= 2) session_.add_manager({r.name, r.face, r.team});
            session_.begin_added_turns(first);  // the day loop goes on with the added managers' turns (REAL-CODE)
            invalidate();
        } catch (const std::exception& e) {
            notice(e.what());
        }
    }
}

// Start menu of a new game (ovr027:0135 with argument 1): "Los jetzt!" with a complete row starts the game.
void ClassicApplication::open_start_menu() {
    const auto base = find_new_game_base(data_);
    if (!base) { open_files(false); return; }
    if (!misc_) misc_ = std::make_shared<ClassicMiscState>();
    misc_->start_base = std::make_unique<HokFile>(decode_hok(read_file(*base)));
    misc_->setup.open_start(*misc_->start_base);
    screen_ = ClassicScreen::NewManager;
}

bool ClassicApplication::start_menu_open() const { return misc_ && misc_->start_base; }
void ClassicApplication::close_start_menu() { if (misc_) misc_->start_base.reset(); }

void ClassicApplication::click_start_menu(int x, int y, bool right) {
    auto& m = *misc_;
    // ovr027:0E87..0F24: with argument 1 a left click on the header (y < 0x21) opens "Laden" (sub_24FA1(1)); cancelled,
    // the start menu waits again, a loaded game leaves it
    if (!right && y < 0x21) { open_files(false); return; }
    if (m.setup.click(*m.start_base, x, y, right) != ClassicNewManager::Result::LeaveWithManager) return;
    // ovr027:1666: the complete rows become managers 0..N-1 in menu order (hot seat, classic_hotseat.cpp)
    std::vector<NewGameSetup> managers;
    for (const auto& r : m.setup.rows())
        if (r.face >= 0 && r.team >= 0 && r.name.size() >= 2) managers.push_back({r.name, r.face, r.team});
    if (managers.empty()) return;
    // "Nur WM" (game/wm_mode.hpp, classic_wm_mode.cpp): one manager, the national coach; ovr029:467B skips the cup question
    if (m.setup.mode_bits() == kStartModeWm) {
        if (managers.size() > 1) return;  // the icon switches only for a single manager
        try {
            session_.start_wm_game(managers.front(), new_game_seed_);
            m.start_base.reset();
            invalidate();
            screen_ = ClassicScreen::Main;
            table_order_ = session_.game().league(division_).ranking;
        } catch (const std::exception& e) {
            notice(e.what());
        }
        return;
    }
    // ovl05_4c7b(0, 0) during the set-up asks the DEB cup question (ovl05_4653) before its draw: with the display switch
    // of the start state (DS:0227, state 0xA526) on and not in the WM-only mode (var_57 = 2 -> DS:D0B9 = 1) the game
    // waits for the answer (classic_cup_draw.cpp finish_new_game), else it starts at once
    pending_start_ = PendingStart{managers, m.setup.options()};
    if (m.start_base->state.at(0xA526) && m.setup.options().mode != 2) {
        ask_cup_draw();
        return;
    }
    finish_new_game(false);
}

void ClassicApplication::key_misc(int key) {
    if (misc_ && screen_ == ClassicScreen::NewManager) misc_->setup.key(key);
}

void ClassicApplication::text_misc(std::string_view text) {
    if (misc_ && screen_ == ClassicScreen::NewManager) misc_->setup.text(text);
}

// The end year (Game::game_over, day loop 6af8 before 162c:1c5b): the highscore of all managers (ovl16_30ed, nothing
// when the settings keep no list), then ovl14_00ad: the screen cleared to colour 0 (1902:0800), font 1, "Das wars..."
// (DS:543D) centred in 8..0x13F at baseline 100 in colour 7 and "Carpe diem !" (DS:5449) at 0x8C in colour 0x1E, in the
// palette of the page before; a press ends the program (ovl00_058a).
void ClassicApplication::begin_game_end() {
    advancing_ = false;
    category_ = -1;
    match_frames_.clear();
    game_end_ = 2;
    open_highscore();
    if (screen_ == ClassicScreen::Highscore) game_end_ = 1;
}

void ClassicApplication::draw_game_end() {
    canvas_.rect(0, 0, 0x13F, 0xEF, 0);
    canvas_.text(ClassicFontId::NewBold, 8, 100, "Das wars...", 7, -1, 0x13F);
    canvas_.text(ClassicFontId::NewBold, 8, 0x8C, "Carpe diem !", 0x1E, -1, 0x13F);
}

}  // namespace em::ui
