// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "game/game.hpp"
#include "game/new_game.hpp"

// The game session behind the user interfaces: loading, starting and saving games, applying player actions
// to a staged copy of the game, advancing days with the contract talks of the season change, and the news log.

namespace em::ui {

// Result of a UI action: a message to show (empty for none) and whether the game accepted the action.
struct ActionResult {
    std::string message;
    bool accepted = true;
};

// One entry of the session's news log (game messages with their date).
struct News {
    GameDate date;
    std::string text;
};

// A pending contract talk of the season change, with everything the dialog needs.
struct ContractQuestion {
    int player = -1;
    std::string name;
    int age = 0;
    long monthly_salary = 0;  // raw one-year proposal of the season review (internal units)
    int slot = -1;            // squad slot of the player in `save`
    int index = 0;            // contract talks already answered in this season change
    int luck = 100;           // the dialog's daily mood value (renewal_demand for changed years)
    Bytes save;               // the staged game as a native save at the question (the list behind the dialog)
    int manager = 0;          // the manager whose talk this is (the talks run for 0..N-1 in index order)
    // the season review of every manager of the staged day (shown before the talks, Game::season_review(m))
    std::array<std::optional<SeasonReview>, kMaxManagers> reviews;
    std::array<std::vector<std::string>, kMaxManagers> review_messages;  // their immediate messages
    std::vector<Game::ContractOutcome> outcomes;  // the talks answered so far
};

// UI-independent controller. Each action is staged from a full checkpoint, so
// exceptions and cancelled season-review dialogs cannot leave half a game day.
// The original's side effects of a refusal (random numbers drawn, cooldowns) are committed even when the
// action is not accepted.
class Session {
public:
    // data_directory: the folder with the user's EM.EXE, KB files and savegames.
    explicit Session(std::filesystem::path data_directory);
    bool loaded() const { return bool(game_); }
    const Game& game() const;  // the running game (during a shown match day: the staged game)
    GameDate date() const;  // menu date of the next day to play, as in the original's saves
    // Tables read from EM.EXE.
    FinanceTables finance_tables() const;
    TrainingTables training_tables() const;
    ContractTables contract_tables() const;
    const std::filesystem::path& data_directory() const { return data_directory_; }
    const std::filesystem::path& loaded_path() const { return loaded_path_; }
    bool dirty() const { return dirty_; }            // changed since loading or saving
    uint64_t revision() const { return revision_; }  // increases with every committed change
    const std::vector<News>& news() const { return news_; }
    const std::optional<ContractQuestion>& contract_question() const { return contract_question_; }

    // Loads a .hok or .oem savegame; seed initialises the session's random streams.
    void load(const std::filesystem::path& file, uint32_t seed = 1);
    // A new game from the fresh start state of the data directory (game/new_game.hpp, start menu "Los jetzt!").
    bool can_start_new_game() const;
    // options: the start menu settings (game/new_game.hpp NewGameOptions); nullptr keeps those of the start state.
    void start_new_game(const NewGameSetup& setup, uint32_t seed = 1, const NewGameOptions* options = nullptr);
    // Two to four managers (hot seat): the world of the new-game generator (game/new_game.hpp start_new_world).
    // The start menu with only the WM icon on ("Nur WM", game/wm_mode.hpp): one manager, the national coach.
    void start_wm_game(const NewGameSetup& setup, uint32_t seed = 1);
    // Manager -> Neuer in a running game (game/world_generator.hpp add_manager_to_game): the game goes on with one more manager
    void add_manager(const NewGameSetup& setup);
    // after Manager -> Neuer: the menu-pass turns of the managers from `first` on (transfer-list roll, daily factor); the
    // menu goes to manager `first` (the original's day loop goes on with the added manager)
    void begin_added_turns(int first);
    // after a generator start: Borland and GameRandom go on from the generator's end states, as in the original
    void continue_generator_streams(const std::array<uint32_t, 2>& streams);
    // A new game for one to four managers through the new-game world generator (with EM_OLD_SHIPPEDSTART set,
    // a single manager starts from the stored start state instead).
    void start_new_game(const std::vector<NewGameSetup>& managers, uint32_t seed = 1, const NewGameOptions* options = nullptr);
    // The manager the UI acts for (Game::select_manager). It is not part of a checkpoint, so the session keeps it and
    // gives it to every staged game; loading or starting a game selects manager 0.
    bool select_manager(int manager);
    int active_manager() const { return active_manager_; }
    void save(const std::filesystem::path& file);  // new .oem or .hok only, never overwrite
    // Runs action on a staged copy of the game and commits it unless it throws; the result carries the
    // action's message and acceptance.
    ActionResult apply(const std::function<ActionResult(Game&)>& action);
    // like apply, with the match observer on the staged game (a friendly of the national coach shows its minutes)
    ActionResult apply_observed(const std::function<ActionResult(Game&)>& action);
    bool advance_day();  // false means a contract decision is pending; game remains unchanged
    bool next_day_has_match() const;  // checkpoint preview, no state or random-stream changes
    // Observer of the minute loops of the next advance_day (match display); called with the staged game, read-only.
    using MatchObserver = std::function<void(const Game& game, int minute, const std::vector<LiveMatch>& matches)>;
    void set_match_observer(MatchObserver observer) { match_observer_ = std::move(observer); }
    // The live DEB cup draw of later rounds (Game::set_cup_draw_decider / set_cup_reveal_observer), given to every staged
    // game of advance_day; the observer also gets the staged game (read-only).
    using CupRevealObserver = std::function<void(const Game& game, const std::vector<int>& order, const std::vector<int>& places)>;
    void set_cup_draw_handlers(Game::CupDrawDecider decider, CupRevealObserver observer) {
        cup_draw_decider_ = std::move(decider);
        cup_reveal_observer_ = std::move(observer);
    }
    // The first DEB cup round of the last new game: clubs per place and, after "Aber klar" (NewGameOptions::cup_live),
    // the uncovering order. A new game from the start state shows its stored pairs (home, away) and draws the order
    // from a copy of the Borland random state, so the game itself stays as without the live draw (a documented
    // deviation from the original).
    const std::vector<int>& start_cup_places() const { return start_cup_places_; }
    const std::vector<int>& start_cup_reveal() const { return start_cup_reveal_; }
    bool answer_contract(bool renew);  // "Mach' ich!" on the unchanged proposal or "Abbruch"
    bool answer_contract(const Game::ContractTerms& terms);  // the edited terms
    void cancel_advance();  // drops a pending contract question and the answers given so far (the day is not played)

private:
    // A game restored from checkpoint with the session's handlers and active manager.
    std::unique_ptr<Game> staged_game(const Bytes& checkpoint) const;
    // Starts a game from the bytes of a save.
    void start_from(const Bytes& hok, uint32_t seed);
    // Makes next the running game; before is the checkpoint it was staged from.
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
    Game::CupDrawDecider cup_draw_decider_;
    CupRevealObserver cup_reveal_observer_;
    std::vector<int> start_cup_places_, start_cup_reveal_;
};

// Publish a complete new save atomically. Existing destinations are never
// replaced, including a racing writer. Temporary files belong to this call.
void write_new_save(const std::filesystem::path& destination, const Bytes& data);
// Internal money (3 units = 1 DM) as "<n> DM" with thousands points.
std::string money(long raw);
// A date as "dd.mm.yyyy".
std::string date_text(GameDate date);
// Path <-> UTF-8 text for display and input.
std::string path_text(const std::filesystem::path& path);
std::filesystem::path path_from_text(std::string_view utf8);

}  // namespace em::ui
