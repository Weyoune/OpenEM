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
    long monthly_salary = 0;  // raw actual bid (demand + 10%) for the two-year review offer
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
    void save(const std::filesystem::path& file);  // new .oem or .hok only, never overwrite
    ActionResult apply(const std::function<ActionResult(Game&)>& action);
    bool advance_day();  // false means a contract decision is pending; game remains unchanged
    bool next_day_has_match() const;  // checkpoint preview, no state or random-stream changes
    // Observer of the minute loops of the next advance_day (match display); called with the staged game, read-only.
    using MatchObserver = std::function<void(const Game& game, int minute, const std::vector<LiveMatch>& matches)>;
    void set_match_observer(MatchObserver observer) { match_observer_ = std::move(observer); }
    bool answer_contract(bool renew);
    void cancel_advance();

private:
    std::unique_ptr<Game> staged_game(const Bytes& checkpoint) const;
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
    std::vector<int> contract_answers_;
    std::optional<ContractQuestion> contract_question_;
    MatchObserver match_observer_;
};

// Publish a complete new save atomically. Existing destinations are never
// replaced, including a racing writer. Temporary files belong to this call.
void write_new_save(const std::filesystem::path& destination, const Bytes& data);
std::string money(long raw);
std::string date_text(GameDate date);
std::string path_text(const std::filesystem::path& path);
std::filesystem::path path_from_text(std::string_view utf8);

}  // namespace em::ui
