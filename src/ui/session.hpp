// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/game.hpp"
#include "game/new_game.hpp"

namespace em::ui {

struct ActionResult {
    std::string message;
    bool accepted = true;
};

struct News {
    GameDate date;
    std::string text;
};

struct ContractQuestion {
    int player = -1;
    std::string name;
    int age = 0;
    long monthly_salary = 0;  // raw one-year proposal of the season review (ovl02_0000)
    int slot = -1;            // squad slot of the player in `save`
    int index = 0;            // contract talks already answered in this season change
    int luck = 100;           // the daily mood DS:E1A4 of the dialog (renewal_demand for changed years)
    Bytes save;               // the staged game as a native save at the question (the list behind the dialog)
    int manager = 0;          // the manager whose talk this is (the talks run for 0..N-1 in index order)
    // the season review of every manager of the staged day (shown before the talks, Game::season_review(m))
    std::array<std::optional<SeasonReview>, kMaxManagers> reviews;
    std::array<std::vector<std::string>, kMaxManagers> review_messages;  // their immediate messages
    std::vector<Game::ContractOutcome> outcomes;  // the talks answered so far
};

// UI-independent controller. Each action is staged from a full checkpoint, so
// exceptions and cancelled season-review dialogs cannot leave half a game day.
// Native refusal side effects (RNG/cooldown) are committed even when not accepted.
class Session {
public:
    explicit Session(std::filesystem::path data_directory);
    bool loaded() const { return bool(game_); }
    const Game& game() const;
    GameDate date() const;  // menu date of the next day to play, as in native saves
    FinanceTables finance_tables() const;
    TrainingTables training_tables() const;
    ContractTables contract_tables() const;
    const std::filesystem::path& data_directory() const { return data_directory_; }
    const std::filesystem::path& loaded_path() const { return loaded_path_; }
    bool dirty() const { return dirty_; }
    uint64_t revision() const { return revision_; }
    const std::vector<News>& news() const { return news_; }
    const std::optional<ContractQuestion>& contract_question() const { return contract_question_; }

    void load(const std::filesystem::path& file, uint32_t seed = 1);
    // A new game from the fresh start state of the data directory (game/new_game.hpp, start menu "Los jetzt!").
    bool can_start_new_game() const;
    void start_new_game(const NewGameSetup& setup, uint32_t seed = 1);
    // Two to four managers (hot seat): the world of the new-game generator (game/new_game.hpp start_new_world).
    void start_new_game(const std::vector<NewGameSetup>& managers, uint32_t seed = 1);
    // The start menu with only the WM icon on ("Nur WM", game/wm_mode.hpp): one manager, the national coach.
    void start_wm_game(const NewGameSetup& setup, uint32_t seed = 1);
    // The manager the UI acts for (Game::select_manager). It is not part of a checkpoint, so the session keeps it and
    // gives it to every staged game; loading or starting a game selects manager 0.
    bool select_manager(int manager);
    int active_manager() const { return active_manager_; }
    void save(const std::filesystem::path& file);  // new .oem or .hok only, never overwrite
    ActionResult apply(const std::function<ActionResult(Game&)>& action);
    bool advance_day();  // false means a contract decision is pending; game remains unchanged
    bool next_day_has_match() const;  // checkpoint preview, no state or random-stream changes
    // Observer of the minute loops of the next advance_day (match display); called with the staged game, read-only.
    using MatchObserver = std::function<void(const Game& game, int minute, const std::vector<LiveMatch>& matches)>;
    void set_match_observer(MatchObserver observer) { match_observer_ = std::move(observer); }
    bool answer_contract(bool renew);  // "Mach' ich!" on the unchanged proposal or "Abbruch"
    bool answer_contract(const Game::ContractTerms& terms);
    void cancel_advance();

private:
    std::unique_ptr<Game> staged_game(const Bytes& checkpoint) const;
    void start_from(const Bytes& hok, uint32_t seed);
    void commit(std::unique_ptr<Game> next, const Bytes& before);
    void add_news(std::string text);

    std::filesystem::path data_directory_, loaded_path_;
    Bytes exe_;
    std::optional<Database> database_;
    std::unique_ptr<Game> game_;
    NativeSaveHistory save_history_;
    std::vector<News> news_;
    bool dirty_ = false;
    uint64_t revision_ = 0;
    int active_manager_ = 0;
    std::vector<Game::ContractTerms> contract_answers_;
    std::optional<ContractQuestion> contract_question_;
    MatchObserver match_observer_;
    // The staged game of a running advance_day while its minutes are shown (match display): game() and apply() act on it, so
    // the lineup, tactics and Einstellungen opened from the minute loop change the running day like in the original.
    Game* live_game_ = nullptr;
};

// Publish a complete new save atomically. Existing destinations are never
// replaced, including a racing writer. Temporary files belong to this call.
void write_new_save(const std::filesystem::path& destination, const Bytes& data);
std::string money(long raw);
std::string date_text(GameDate date);
std::string path_text(const std::filesystem::path& path);
std::filesystem::path path_from_text(std::string_view utf8);

}  // namespace em::ui
