// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ui/classic_canvas.hpp"
#include "ui/session.hpp"
#include "ui/classic_preview.hpp"
#include "ui/classic_hover.hpp"
#include "ui/classic_transfer_hover.hpp"
#include "ui/classic_advertising.hpp"
#include "ui/classic_stadium.hpp"
#include "ui/classic_youth.hpp"

namespace em::ui {
enum class ClassicScreen { Main, Files, Table, Totals, Bank, Contracts, Training, Transfer, Lineup, Tactics, Strengths, Cup, EuroCup, WorldChampionship, LeagueFixtures, LeagueCompetition, Advertising, Stadium, Youth, Settings, Statistics, Highscore, NewManager };

struct ClassicMiscState;  // classic_misc_input.cpp
struct ClassicCareerState;  // classic_career.cpp
class ClassicApplication {
public:
    ClassicApplication(std::filesystem::path data, std::filesystem::path saves, const std::filesystem::path& initial);
    void draw();
    void tick();
    void click(int x, int y, bool right);
    void release();
    void motion(int x,int y);
    void key(int key);
    void text_input(std::string_view text);
    void request_exit();
    bool finished() const { return finished_; }
    const ClassicCanvas& canvas() const { return canvas_; }
    const Session& session() const { return session_; }
    ClassicScreen screen() const { return screen_; }
    void render_screen(std::string_view name);
    Bytes native_ui_bytes(); // read-only diagnostic of the native modal working image

private:
    struct FileEntry { std::filesystem::path path; std::string name; bool directory=false; bool save=false; };
    const HokFile& snapshot();
    void invalidate() { view_.reset(); }
    void notice(std::string text);
    void timed_native_notice(std::string text,bool return_main=false);
    void action(const std::function<ActionResult(Game&)>& fn);
    void route_submenu(int item);
    void start_advance();
    void autosave();
    void open_files(bool saving);
    void refresh_files();
    void accept_file();
    void finish_timed_native_notice();
    std::string virtual_file_path(const std::filesystem::path& path) const;
    void draw_main();
    void draw_files();
    void draw_table();
    void draw_totals();
    void draw_bank();
    void draw_contracts();
    void draw_training();
    void draw_transfer();
    void draw_lineup();
    void draw_tactics();
    void open_strengths(int league);
    void draw_strengths();
    void open_club_info(int team,int mode=2);
    void close_club_info();
    void open_team_schedule(int half=-1);
    void finish_team_schedule(bool close_club);
    void begin_page_working(ClassicScreen screen);
    void finish_page();
    void open_cup();
    void draw_cup();
    void open_euro(int page);
    void draw_euro();
    void open_wm(int page,int round=-1);
    void draw_wm();
    void open_league_fixtures(int league,int round=-1,int width=1);
    void draw_league_fixtures();
    void open_league_competition(int league);
    void draw_league_competition();
    void open_advertising();
    void draw_advertising();
    void click_advertising(int x,int y,bool right);
    void update_advertising_hover();
    void resolve_advert_renewal(bool accept);
    void open_stadium();
    void draw_stadium();
    void click_stadium(int x,int y,bool right);
    void update_stadium_hover();
    void resolve_stadium_build(bool accept);
    void click_youth(int x,int y,bool right);
    void open_settings();
    void draw_settings();
    void click_settings(int x,int y,bool right);
    // Manager -> Highscore and Neuer (classic_misc_input.cpp)
    void open_highscore();
    void open_new_manager();
    void draw_misc();
    void click_misc(int x,int y,bool right);
    void key_misc(int key);
    void text_misc(std::string_view text);
    std::shared_ptr<ClassicMiscState> misc_; // incomplete here; shared_ptr keeps the deleter
    void open_start_menu();                   // new game (classic_misc_input.cpp)
    void click_start_menu(int x,int y,bool right);
    // Message field and career pages (classic_career.cpp)
    bool main_menu_idle() const;
    void check_new_messages();
    void open_messages();
    void show_immediate_messages();
    bool career_click(int x,int y,bool right);
    void career_release();
    bool career_key(int key);
    void draw_career();
    std::shared_ptr<ClassicCareerState> career_;
    void draw_dialog();
    void draw_contract_editor();
    void open_contract(int slot);
    void open_player(int slot,int manager=0);
    void submit_contract();
    void open_purchase(int pool_slot);
    void open_loan(int pool_slot);           // classic_loan.cpp
    void finish_loan(int pool_slot,long fee);
    void open_talent_question();             // classic_scout.cpp
    void answer_talent_question(bool yes);
    void draw_talent_question();
    void show_manager_news();
    void open_sale_offer(int block,int slot,int16_t native_si);
    void finish_sale_offer(bool sell);
    void draw_sale_offer();
    void finish_fee(bool cancel);
    void draw_fee_editor();
    void submit_purchase();
    void cancel_purchase(bool feedback=true);
    void finish_numeric(bool cancel);
    void button(int x,int y,std::string_view label);
    void plain_frame(int left,int top,int right,int bottom,uint8_t fill);
    // Statistiken pages (classic_stats_app.cpp)
    void open_statistics(int item);
    void draw_statistics();
    void click_statistics(int x,int y,bool right);
    bool render_statistics(std::string_view name);
    void open_tactics();
    void open_lineup();
    void finish_tactics(ClassicScreen target);
    void click_tactics(int x,int y,bool right);
    void click_lineup(int x,int y,bool right);
    void click_transfer(int x,int y,bool right);
    void apply_lineup(size_t slot,ManualLineupAction action);
    std::optional<ClassicRosterKind> roster_kind() const;
    void update_roster_hover();
    void draw_roster_hover();
    void bevel(int x1,int y1,int x2,int y2,int edge,int fill);
    std::pair<int,int> opponent() const;
    std::string recent_results(const HokFile& h,int team) const;

    std::filesystem::path data_, saves_, file_directory_;
    Session session_;
    ClassicCanvas canvas_;
    ClassicRosterHelp roster_help_;
    ClassicTransferHelp transfer_help_;
    ClassicRosterHover roster_hover_;
    ClassicTransferHover transfer_hover_;
    bool pool_rules_=false,pool_help_visible_=false;
    std::optional<ClassicRosterKind> hover_kind_;
    bool roster_rules_=false;
    SeasonCalendar calendar_;
    ScheduleTables schedule_;
    std::unique_ptr<HokFile> view_;
    std::unique_ptr<HokFile> menu_working_;
    uint64_t menu_working_revision_=0;
    bool menu_date_dirty_=false,team_schedule_open_=false;
    int menu_pairing_league_=-1,team_schedule_half_=0;
    uint64_t view_revision_ = uint64_t(-1);
    ClassicScreen screen_ = ClassicScreen::Main;
    int category_=-1, division_=0, table_view_=1;
    std::array<int,12> table_order_{};
    bool finished_=false, quit_question_=false, advancing_=false, advance_first_=false;
    int advanced_days_=0;
    int strengths_league_=0,strengths_mode_=0;
    int euro_page_=3;
    int euro_table_page_=3,euro_fixture_page_=3; // DS:5123 / DS:5126
    bool euro_from_fixtures_=false;
    int stats_page_=0,stats_mode_=2,stats_scroll_=0; // 1 Frueher .. 5 WM; Spieler mode; Frueher scroll
    bool stats_managers_=false;
    int wm_table_page_=11,wm_fixture_page_=8,wm_previous_fixtures_=8,wm_page_=11,wm_round_=-1;
    int fixture_league_=0,fixture_round_=1,fixture_width_=1,fixture_next_width_=1;
    int league_page_=7;
    std::unique_ptr<HokFile> fixture_backdrop_;
    ClassicAdvertisingSelection advertising_selection_;
    bool advertising_info_=false,advert_renewal_refused_=false;
    std::optional<AdvertRenewalDraft> advert_renewal_;
    std::string advert_renewal_name_;
    ClassicStadiumSelection stadium_selection_;
    std::optional<StadiumBuildDraft> stadium_build_;
    int stadium_next_value_width_=2;
    bool settings_rink_hint_shown_=false; //native byte DS:3014: the Torszenen mode 2 hint appears once per program run
    std::string notice_;
    std::vector<std::string> pending_notices_;
    bool save_mode_=false, filename_focus_=false;
    std::optional<uint64_t> native_notice_until_;
    std::optional<uint64_t> match_banner_until_; //native ovr030:081D "Ligaspiel" banner before the own league match
    int match_banner_day_=-1;
    void draw_banner(std::string_view text);
    bool native_notice_return_main_=false;
    std::string filename_;
    std::vector<FileEntry> files_;
    size_t file_page_=0;
    int file_selection_=-1;
    struct FilePreview { ClassicPreview header;std::string path; };
    std::optional<FilePreview> file_preview_;
    int selected_player_=-1, selected_pool_=-1;
    int selected_line_=0;
    std::optional<std::array<std::array<int,6>,5>> lineup_cells_;
    bool lineup_six_=false;
    std::optional<TacticsPlan> tactics_plan_;
    std::unique_ptr<HokFile> tactics_view_;
    struct ContractEditor {
        int slot=0,player=-1,years=1,offset=0,field=-1;
        long salary=0,premium=0;
        bool insured=false,original_insured=false;
        std::string digits;
    };
    std::optional<ContractEditor> contract_editor_;
    struct FeeEditor { int pool_slot;std::string digits;bool loan=false; };
    std::optional<FeeEditor> fee_editor_;
    std::optional<PurchaseDraft> purchase_draft_;
    std::optional<SaleQuote> sale_quote_;
    bool talent_question_=false;
    int16_t transfer_native_si_=0;
    std::unique_ptr<HokFile> purchase_backdrop_;
    struct PlayerDetail { int player,manager,slot;int64_t quote; };
    std::optional<PlayerDetail> player_detail_;
    std::optional<int> club_info_;
    std::optional<std::array<uint8_t,36>> club_pairings_;
    int club_info_mode_=2;
    int training_kind_=0;
    bool training_changed_=false;
    TrainingPlan training_plan_{};
    int64_t bank_amount_=0;
    int bank_party_=4;
    bool bank_deposit_=true, bank_selected_=false;
    int mouse_x_=-1,mouse_y_=-1;
};
}  // namespace em::ui
