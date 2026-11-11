// SPDX-License-Identifier: GPL-3.0-only
#include "ui/session.hpp"
#include "game/checkpoint.hpp"
#include "game/wm_mode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace em::ui {
namespace {
struct NeedContract {};
std::string extension(const std::filesystem::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}
}

Session::Session(std::filesystem::path directory) : data_directory_(std::move(directory)) {
    exe_ = read_file(data_directory_ / "EM.EXE");
    const auto database = data_directory_ / "KB" / "23.vga";
    if (std::filesystem::exists(database)) database_ = decode_db(read_file(database));
}

const Game& Session::game() const {
    if (!game_) throw std::logic_error("Kein Spielstand geöffnet.");
    return *game_;
}

GameDate Session::date() const {
    const auto& current = game();
    return date_of_season_day(current.season_day() + 1, current.save().year);
}

FinanceTables Session::finance_tables() const { return load_finance_tables(exe_, game().save()); }
ContractTables Session::contract_tables() const { auto tables=load_contract_tables(exe_);tables.level=game().game_level();return tables; }
TrainingTables Session::training_tables() const {
    auto tables = load_training_tables(exe_);
    tables.level = game().game_level();
    return tables;
}

std::unique_ptr<Game> Session::staged_game(const Bytes& checkpoint) const {
    auto next = std::make_unique<Game>(exe_, checkpoint);
    if (database_) next->set_database(*database_);
    next->select_manager(active_manager_);
    return next;
}

bool Session::select_manager(int manager) {
    if (!game_ || !game_->select_manager(manager)) return false;
    active_manager_ = manager;
    return true;
}

void Session::load(const std::filesystem::path& file, uint32_t seed) {
    const auto ext = extension(file);
    if (ext != ".hok" && ext != ".oem") throw FormatError("Bitte einen .hok- oder .oem-Spielstand wählen.");
    const Bytes bytes=read_file(file);
    const bool native=!is_game_checkpoint(bytes);
    if(native){
        if(!native_history_header(bytes))throw FormatError("HOK: invalid native version or marker");
        const uint32_t identity=le32(bytes,0xb4);
        if(save_history_.contains(identity))throw NativeReloadError{};
        //7348:1D8B inserts after the header guard, before state checksums.
        //Preserve this native runtime side effect even if payload validation fails.
        save_history_.remember(identity);
        if(game_){game_->set_native_save_history(save_history_);dirty_=true;revision_++;}
        verify_hok_checksums(bytes);
    }
    // Install retained RNG/gates/counts before the native live-return path;
    // cold construction would restore modeB and overwrite loaded team caches.
    auto next = std::make_unique<Game>(exe_,bytes,seed,native?game_.get():nullptr);
    if(native)next->set_native_save_history(save_history_);
    else save_history_=next->native_save_history();
    if (database_) next->set_database(*database_);
    game_ = std::move(next);
    active_manager_ = 0;
    loaded_path_ = file;
    dirty_ = false;
    revision_++;
    contract_answers_.clear();
    contract_question_.reset();
    news_.clear();
    add_news("Spielstand geöffnet: " + path_text(file.filename()));
}

bool Session::can_start_new_game() const { return database_ && find_new_game_base(data_directory_).has_value(); }

void Session::start_new_game(const NewGameSetup& setup, uint32_t seed) {
    const auto base = find_new_game_base(data_directory_);
    if (!base || !database_) throw FormatError("Kein Startstand (BACKUP.HOK) und keine Datenbank KB\\23.vga gefunden.");
    start_from(encode_hok(em::start_new_game(decode_hok(read_file(*base)), *database_, setup)), seed);
}

void Session::start_new_game(const std::vector<NewGameSetup>& managers, uint32_t seed) {
    if (managers.size() == 1) return start_new_game(managers.front(), seed);
    const auto base = find_new_game_base(data_directory_);
    if (!base || !database_) throw FormatError("Kein Startstand (BACKUP.HOK) und keine Datenbank KB\\23.vga gefunden.");
    const Bytes db = read_file(data_directory_ / "KB" / "23.vga");
    start_from(encode_hok(em::start_new_world(exe_, db, decode_hok(read_file(*base)), managers, uint16_t(seed))), seed);
}

void Session::start_wm_game(const NewGameSetup& setup, uint32_t seed) {
    const auto base = find_new_game_base(data_directory_);
    if (!base || !database_) throw FormatError("Kein Startstand (BACKUP.HOK) und keine Datenbank KB\\23.vga gefunden.");
    start_from(encode_hok(em::start_wm_only_game(decode_hok(read_file(*base)), *database_, setup)), seed);
}

void Session::start_from(const Bytes& bytes, uint32_t seed) {
    // A new game is a new program run of the original: no earlier save identities, no previous game.
    save_history_ = NativeSaveHistory{};
    auto next = std::make_unique<Game>(exe_, bytes, seed, nullptr);
    next->set_native_save_history(save_history_);
    next->set_database(*database_);
    game_ = std::move(next);
    active_manager_ = 0;
    loaded_path_.clear();
    dirty_ = true;
    revision_++;
    contract_answers_.clear();
    contract_question_.reset();
    news_.clear();
    add_news("Neues Spiel gestartet.");
}

void Session::save(const std::filesystem::path& file) {
    if (contract_question_) throw FormatError("Zuerst die Vertragsentscheidung abschließen oder abbrechen.");
    const auto ext = extension(file);
    if (ext != ".hok" && ext != ".oem") throw FormatError("Als .oem speichern oder als .hok exportieren.");
    const auto& current = game();
    if (ext == ".oem") write_new_save(file, current.checkpoint_bytes());
    else {
        const Bytes before = current.checkpoint_bytes();
        auto next = staged_game(before);
        const Bytes bytes = next->native_save_bytes();
        write_new_save(file, bytes);
        commit(std::move(next), before); // failed publication leaves RNG/header unchanged
    }
    // A native export deliberately does not promise exact continuation.
    if (ext == ".oem") { dirty_ = false; loaded_path_ = file; }
    add_news(ext == ".oem" ? "Spielstand gespeichert: " + path_text(file.filename())
                           : "HOK für das Original exportiert: " + path_text(file.filename()));
}

void Session::add_news(std::string text) {
    news_.push_back({date(), std::move(text)});
    if (news_.size() > 500) news_.erase(news_.begin(), news_.begin() + long(news_.size() - 500));
}

void Session::commit(std::unique_ptr<Game> next, const Bytes& before) {
    const bool changed = next->checkpoint_bytes() != before;
    next->set_contract_decider({});  // never retain callbacks referring to a staged stack frame
    next->set_match_observer({});
    game_ = std::move(next);
    save_history_=game_->native_save_history();
    dirty_ = dirty_ || changed;
    if (changed) revision_++;
    for (const auto& line : game_->log()) news_.push_back({game_->date(), line});
    if (news_.size() > 500) news_.erase(news_.begin(), news_.begin() + long(news_.size() - 500));
    game_->clear_log();
}

ActionResult Session::apply(const std::function<ActionResult(Game&)>& action) {
    if (contract_question_) throw FormatError("Eine Vertragsentscheidung wartet.");
    const Bytes before = game().checkpoint_bytes();
    auto next = staged_game(before);
    ActionResult result = action(*next);
    commit(std::move(next), before);
    if (!result.message.empty()) add_news(result.message);
    return result;
}

bool Session::advance_day() {
    if (contract_question_) return false;
    const Bytes before = game().checkpoint_bytes();
    auto next = staged_game(before);
    size_t question = 0;
    next->set_contract_terms_decider([&](const ContractPlayer& player, int slot, long proposal) {
        if (question < contract_answers_.size()) return contract_answers_[question++];
        ContractQuestion q;
        q.player = player.player;
        q.name = player.player >= 0 ? next->save().players.at(size_t(player.player)).name : "Spieler";
        q.age = player.age;
        q.monthly_salary = proposal;  // the proposal of ovl02_0000 (raw)
        q.slot = slot;
        q.index = int(question);
        q.luck = next->daily_luck();
        q.save = next->save_bytes();
        q.review = next->season_review();
        q.review_messages = next->immediate_messages();
        q.outcomes = next->contract_outcomes();
        contract_question_ = std::move(q);
        throw NeedContract{};
    });
    if (match_observer_) {
        Game* staged = next.get();
        next->set_match_observer([this, staged](int minute, const std::vector<LiveMatch>& matches) { match_observer_(*staged, minute, matches); });
    }
    try {
        next->advance_day();
    } catch (const NeedContract&) {
        return false;  // all simulation side effects belong to the discarded staged game
    } catch (...) {
        cancel_advance();
        throw;
    }
    commit(std::move(next), before);
    contract_answers_.clear();
    return true;
}

bool Session::next_day_has_match() const {
    auto next = staged_game(game().checkpoint_bytes());
    next->set_contract_decider([](const ContractPlayer&, long) -> int { throw NeedContract{}; });
    try { next->advance_day(); } catch (const NeedContract&) { return false; }
    return next->seasons_completed() == game().seasons_completed() && next->interactive_match_played();
}

bool Session::answer_contract(bool renew) {
    Game::ContractTerms terms;  // "Mach' ich!" takes the unchanged one-year proposal (Game::expire_contracts)
    terms.years = renew ? 1 : 0;
    if (contract_question_ && renew) {
        const HokFile staged = decode_hok(contract_question_->save);
        terms.insured = le32(staged.state, 0x696D + size_t(contract_question_->slot) * 52 + 0x30) != 0;
    }
    return answer_contract(terms);
}

bool Session::answer_contract(const Game::ContractTerms& terms) {
    if (!contract_question_) throw std::logic_error("Keine Vertragsentscheidung offen.");
    contract_answers_.push_back(terms);
    contract_question_.reset();
    return advance_day();
}

void Session::cancel_advance() {
    contract_question_.reset();
    contract_answers_.clear();
}

std::string money(long raw) {
    const int64_t dm = int64_t(raw) / 3;
    std::string value = std::to_string(dm < 0 ? -dm : dm);
    for (size_t n = value.size(); n > 3; n -= 3) value.insert(n - 3, ".");
    return (dm < 0 ? "-" : "") + value + " DM";
}

std::string date_text(GameDate d) {
    char text[32];
    std::snprintf(text, sizeof text, "%02d.%02d.%04d", d.day, d.month, d.year);
    return text;
}

std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}
std::filesystem::path path_from_text(std::string_view text) {
    std::u8string value;
    value.reserve(text.size());
    for (unsigned char c : text) value.push_back(char8_t(c));
    return std::filesystem::path(value);
}
}  // namespace em::ui
