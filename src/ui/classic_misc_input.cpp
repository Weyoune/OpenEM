// SPDX-License-Identifier: GPL-3.0-only
// Manager -> Highscore and Manager -> Neuer of the native menu.
#include <fstream>

#include "game/highscore.hpp"
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
        HighscoreRecord record;
        action([&](Game& game) { record = game.highscore_record(0, *m.tables); return ActionResult{}; });
        const fs::path own = saves_ / highscore_file_name(number), seed = data_ / highscore_file_name(number);
        std::error_code ec;
        m.highscore = fs::exists(own, ec) ? read_highscore_table(read_file(own))
                     : fs::exists(seed, ec) ? read_highscore_table(read_file(seed)) : HighscoreTable{};
        m.game = highscore_game_identity(session_.game().save());
        m.wm_only = session_.game().save().state.at(0x025A) == 1;
        merge_highscore(m.highscore, record, m.game);
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
    if (screen_ == ClassicScreen::Highscore) draw_classic_highscore(canvas_, misc_->highscore, misc_->wm_only);
    else misc_->setup.draw(canvas_, snapshot(), mouse_x_, mouse_y_);
}

void ClassicApplication::click_misc(int x, int y, bool right) {
    if (!misc_ || screen_ == ClassicScreen::Highscore) {  // ovr040:33B8 waits for any click
        screen_ = ClassicScreen::Main;
        return;
    }
    const auto result = misc_->setup.click(snapshot(), x, y, right);
    if (result == ClassicNewManager::Result::Leave) screen_ = ClassicScreen::Main;
    else if (result == ClassicNewManager::Result::LeaveWithManager) {
        screen_ = ClassicScreen::Main;
        notice("Weitere Manager werden noch nicht\nunterstützt.");
    }
}

void ClassicApplication::key_misc(int key) {
    if (misc_ && screen_ == ClassicScreen::NewManager) misc_->setup.key(key);
}

void ClassicApplication::text_misc(std::string_view text) {
    if (misc_ && screen_ == ClassicScreen::NewManager) misc_->setup.text(text);
}

}  // namespace em::ui
