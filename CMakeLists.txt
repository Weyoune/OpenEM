# SPDX-License-Identifier: GPL-3.0-only
# OpenEM - open remake of Eishockey Manager (Software 2000, 1993).
# Build:
#   cmake -S . -B build
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
cmake_minimum_required(VERSION 3.24)
project(OpenEM VERSION 1.0.0 LANGUAGES CXX C)
# The release of this build ("1.0.0-beta.3", "1.0.0"): package name and the version text of the program, which shows
# it as "1.0 Beta 3" / "1.0" (settings page).
set(OPENEM_RELEASE "1.0.0-beta.4")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.0$" "\\1.\\2" OPENEM_RELEASE_TEXT "${OPENEM_RELEASE}")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.0-beta\\.([0-9]+)$" "\\1.\\2 Beta \\3" OPENEM_RELEASE_TEXT "${OPENEM_RELEASE_TEXT}")
add_compile_definitions(OPENEM_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

# Directory with the original game (EM.EXE, KB\, TORE\, *.HOK). Only used by the data tests;
# the original files are never copied into the build or the source tree.
# Optional private checkout with the original game, recorded test data and reference images. Without it OpenEM builds
# the program and the tests that need no original data.
set(OPENEM_PRIVATE_DIR "" CACHE PATH "Checkout of the private OpenEM_dc repository")
if(OPENEM_PRIVATE_DIR AND IS_DIRECTORY "${OPENEM_PRIVATE_DIR}/Original/Eishockey Manager (1993)/EM")
    set(EM_DATA_DEFAULT "${OPENEM_PRIVATE_DIR}/Original/Eishockey Manager (1993)/EM")
else()
    set(EM_DATA_DEFAULT "")
endif()
set(EM_DATA "${EM_DATA_DEFAULT}" CACHE PATH "Original Eishockey Manager directory (for the tests)")

# SDL2: pinned release fetched at configure time
option(EM_USE_SYSTEM_SDL2 "Use an installed SDL2 instead of fetching the pinned release" OFF)
if(EM_USE_SYSTEM_SDL2)
    find_package(SDL2 REQUIRED CONFIG)
    set(EM_SDL_TARGET SDL2::SDL2)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.32.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL2)
    set(EM_SDL_TARGET SDL2::SDL2-static)
endif()
# SDL2 renames main when SDL.h is included; link SDL2main first where it exists.
if(TARGET SDL2::SDL2main)
    list(PREPEND EM_SDL_TARGET SDL2::SDL2main)
endif()

# Original data formats (no SDL dependency)
add_library(em_formats STATIC
    src/formats/binary.cpp
    src/formats/db.cpp
    src/formats/hok.cpp
    src/formats/teamdata.cpp
    src/formats/tore.cpp
    src/formats/vga.cpp)
target_include_directories(em_formats PUBLIC src)

# em_game: game logic (schedule, ...), built on the original data read by em_formats
add_library(em_game STATIC
    src/game/calendar.cpp
    src/game/checkpoint.cpp
    src/game/result_books.cpp
    src/game/market.cpp
    src/game/competitions.cpp
    src/game/contracts.cpp
    src/game/finances.cpp
    src/game/stadium.cpp
    src/game/stadium_actions.cpp
    src/game/youth.cpp
    src/game/youth_actions.cpp
    src/game/youth_progress.cpp
    src/game/youth_intake.cpp
    src/game/annual_players.cpp
    src/game/annual_distribution.cpp
    src/game/sale_quote.cpp
    src/game/save_history.cpp
    src/game/game.cpp
    src/game/redistribution.cpp
    src/game/lineup.cpp
    src/game/match.cpp
    src/game/national_team.cpp
    src/game/native_roster.cpp
    src/game/purchase.cpp
    src/game/loan.cpp
    src/game/talent.cpp
    src/game/wm_mode.cpp
    src/game/messages.cpp
    src/game/new_game.cpp
    src/game/world_generator.cpp
    src/game/reporting.cpp
    src/game/settings.cpp
    src/game/highscore.cpp
    src/game/penalties.cpp
    src/game/schedule.cpp
    src/game/scorers.cpp
    src/game/transfers.cpp
    src/game/season.cpp
    src/game/season_end.cpp
    src/game/sponsors.cpp
    src/game/training.cpp
    src/game/tactics.cpp
    src/game/team_refresh.cpp
    src/game/injuries.cpp)
target_link_libraries(em_game PUBLIC em_formats)

# em_dump: command-line inspection of original files (see src/tools/em_dump.cpp)
add_executable(em_dump src/tools/em_dump.cpp)
target_link_libraries(em_dump PRIVATE em_formats em_game)
# em_world: writes the new-game world of game/world_generator.hpp as a HOK save
add_executable(em_world src/tools/em_world.cpp)
target_link_libraries(em_world PRIVATE em_formats em_game)
# em_addmanager: adds a manager (Manager -> Neuer) to an existing save
add_executable(em_addmanager src/tools/em_addmanager.cpp)
target_link_libraries(em_addmanager PRIVATE em_formats em_game)

# em_view: SDL window showing one KB image with its palette
add_executable(em_view src/tools/em_view.cpp)
target_link_libraries(em_view PRIVATE em_formats ${EM_SDL_TARGET})

# The frontend uses the original graphics, fonts and native320x240 coordinates.
add_library(em_frontend STATIC src/ui/session.cpp src/ui/save_io.cpp)
target_link_libraries(em_frontend PUBLIC em_game)
add_library(em_classic STATIC
    src/ui/classic_canvas.cpp
    src/ui/classic_font.cpp
    src/ui/classic_graph.cpp
    src/ui/classic_status.cpp
    src/ui/classic_totals.cpp
    src/ui/classic_roster.cpp
    src/ui/classic_bank.cpp
    src/ui/classic_lineup.cpp
    src/ui/classic_tactics.cpp
    src/ui/classic_transfer.cpp
    src/ui/classic_player.cpp
    src/ui/classic_preview.cpp
    src/ui/classic_opponent.cpp
    src/ui/classic_hover.cpp
    src/ui/classic_transfer_hover.cpp
    src/ui/classic_strengths.cpp
    src/ui/classic_club_info.cpp
    src/ui/classic_team_schedule.cpp
    src/ui/classic_cup.cpp
    src/ui/classic_euro.cpp
    src/ui/classic_wm.cpp
    src/ui/classic_league_fixtures.cpp
    src/ui/classic_playoff.cpp
    src/ui/classic_advertising.cpp
    src/ui/classic_stadium.cpp
    src/ui/classic_youth.cpp
    src/ui/classic_settings.cpp
    src/ui/classic_misc_highscore.cpp
    src/ui/classic_misc_newmanager.cpp
    src/ui/classic_messages.cpp)
target_link_libraries(em_classic PUBLIC em_game)
target_compile_definitions(em_classic PRIVATE OPENEM_RELEASE_TEXT="${OPENEM_RELEASE_TEXT}")  # settings page
# Statistiken pages (Frueher, Verein, Spieler, Fairplay, WM)
target_sources(em_classic PRIVATE src/ui/classic_stats_players.cpp src/ui/classic_stats_history.cpp src/ui/classic_stats_club.cpp src/ui/classic_stats_wm.cpp src/ui/classic_stats_fairplay.cpp)
# Match display (scoreboards, minute panel)
target_sources(em_classic PRIVATE src/ui/classic_match_view.cpp src/ui/classic_goal_scene.cpp)
# Sound: VOC/CMF decoders and the OPL2 model, no SDL
target_sources(em_formats PRIVATE src/formats/sound.cpp src/formats/opl2.cpp)

option(EM_BUILD_GUI "Build the playable OpenEM frontend" ON)
if(EM_BUILD_GUI)
    add_library(em_classic_app STATIC src/ui/classic_app.cpp src/ui/classic_screens.cpp src/ui/classic_contracts.cpp src/ui/classic_tactics_input.cpp src/ui/classic_lineup_input.cpp src/ui/classic_transfer_input.cpp src/ui/classic_hover_state.cpp src/ui/classic_purchase.cpp src/ui/classic_sale.cpp src/ui/classic_loan.cpp src/ui/classic_scout.cpp src/ui/classic_advertising_input.cpp src/ui/classic_stadium_input.cpp src/ui/classic_youth_input.cpp src/ui/classic_misc_input.cpp src/ui/classic_career.cpp src/ui/classic_season_review.cpp src/ui/classic_hotseat.cpp src/ui/classic_cup_draw.cpp src/ui/classic_wm_mode.cpp)
    target_link_libraries(em_classic_app PUBLIC em_frontend em_classic ${EM_SDL_TARGET})
    target_sources(em_classic_app PRIVATE src/ui/classic_stats_app.cpp)
    target_sources(em_classic_app PRIVATE src/ui/classic_sound.cpp)  # SDL audio output
    target_sources(em_classic_app PRIVATE src/ui/classic_probe.cpp src/ui/classic_script.cpp)  # scripted headless runs
    add_executable(em_classic_contract_season_tests tests/classic_contract_season_tests.cpp)
    target_link_libraries(em_classic_contract_season_tests PRIVATE em_classic_app)
    add_executable(em_classic_season_run_tests tests/classic_season_run_tests.cpp)
    target_link_libraries(em_classic_season_run_tests PRIVATE em_classic_app)
    add_executable(em_sound_tests tests/sound_tests.cpp)
    target_link_libraries(em_sound_tests PRIVATE em_classic_app)
    # System "open file" dialog for choosing EM.EXE at the first start: nativefiledialog-extended (zlib licence),
    # pinned like SDL2. Linux uses GTK 3 (build dependency libgtk-3-dev).
    include(FetchContent)
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nfd
        GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
        GIT_TAG v1.2.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nfd)
    add_executable(openem WIN32 src/ui/classic_main.cpp src/ui/game_folder.cpp)
    target_link_libraries(openem PRIVATE em_classic_app nfd::nfd)
    add_executable(em_classic_app_tests tests/classic_app_tests.cpp)
    target_link_libraries(em_classic_app_tests PRIVATE em_classic_app)
    add_executable(em_classic_loan_scout_tests tests/classic_loan_scout_tests.cpp)
    target_link_libraries(em_classic_loan_scout_tests PRIVATE em_classic_app)
    add_executable(em_classic_hold_tests tests/classic_hold_tests.cpp)
    target_link_libraries(em_classic_hold_tests PRIVATE em_classic_app)
    add_executable(em_classic_raster_tests tests/classic_raster_tests.cpp)
    target_link_libraries(em_classic_raster_tests PRIVATE em_classic_app)
    add_executable(em_classic_settings_tests tests/classic_settings_tests.cpp)
    target_link_libraries(em_classic_settings_tests PRIVATE em_classic_app)
    add_executable(em_classic_misc_tests tests/classic_misc_tests.cpp)
    target_link_libraries(em_classic_misc_tests PRIVATE em_classic_app)
    add_executable(em_classic_career_tests tests/classic_career_tests.cpp)
    target_link_libraries(em_classic_career_tests PRIVATE em_classic_app)
    add_executable(em_classic_ui_gaps_tests tests/classic_ui_gaps_tests.cpp)
    target_link_libraries(em_classic_ui_gaps_tests PRIVATE em_classic_app)
    add_executable(em_classic_hotseat_tests tests/classic_hotseat_tests.cpp)
    target_link_libraries(em_classic_hotseat_tests PRIVATE em_classic_app)
    add_executable(em_wm_mode_tests tests/wm_mode_tests.cpp)  # WM-only mode "Nur WM"
    target_link_libraries(em_wm_mode_tests PRIVATE em_classic_app)
    add_executable(em_classic_match_tests tests/classic_match_tests.cpp)
    target_link_libraries(em_classic_match_tests PRIVATE em_classic_app)
    add_executable(em_classic_match_view_tests tests/classic_match_view_tests.cpp)
    target_link_libraries(em_classic_match_view_tests PRIVATE em_classic_app)
endif()

enable_testing()
add_executable(em_format_tests tests/format_tests.cpp)
target_link_libraries(em_format_tests PRIVATE em_formats em_game)
add_test(NAME formats_unit COMMAND em_format_tests)
if(TARGET em_sound_tests)
    add_test(NAME sound_unit COMMAND em_sound_tests)
    if(IS_DIRECTORY "${EM_DATA}")
        add_test(NAME sound_original COMMAND em_sound_tests "${EM_DATA}")
    endif()
endif()
add_executable(em_ui_tests tests/ui_tests.cpp)
target_link_libraries(em_ui_tests PRIVATE em_frontend)
add_executable(em_multimanager_tests tests/multimanager_tests.cpp)
target_link_libraries(em_multimanager_tests PRIVATE em_frontend)
add_executable(em_classic_tests tests/classic_tests.cpp)
add_executable(em_stock_tests tests/stock_tests.cpp)
target_link_libraries(em_stock_tests PRIVATE em_game)
add_executable(em_lineup_action_tests tests/lineup_action_tests.cpp)
target_link_libraries(em_lineup_action_tests PRIVATE em_game)
add_test(NAME lineup_actions COMMAND em_lineup_action_tests)
add_executable(em_tactics_tests tests/tactics_tests.cpp)
target_link_libraries(em_tactics_tests PRIVATE em_game)
add_test(NAME tactics_actions COMMAND em_tactics_tests)
add_executable(em_classic_opponent_tests tests/classic_opponent_tests.cpp)
target_link_libraries(em_classic_opponent_tests PRIVATE em_classic)
add_test(NAME classic_opponent COMMAND em_classic_opponent_tests)
add_executable(em_native_roster_tests tests/native_roster_tests.cpp)
target_link_libraries(em_native_roster_tests PRIVATE em_game)
add_executable(em_world_generator_tests tests/world_generator_tests.cpp)
target_link_libraries(em_world_generator_tests PRIVATE em_game)
add_executable(em_rules_tests tests/rules_tests.cpp)
target_link_libraries(em_rules_tests PRIVATE em_game)
add_test(NAME native_roster COMMAND em_native_roster_tests)
add_executable(em_purchase_tests tests/purchase_tests.cpp)
target_link_libraries(em_purchase_tests PRIVATE em_game)
add_executable(em_loan_talent_tests tests/loan_talent_tests.cpp)
target_link_libraries(em_loan_talent_tests PRIVATE em_game)
add_executable(em_team_refresh_tests tests/team_refresh_tests.cpp)
target_link_libraries(em_team_refresh_tests PRIVATE em_game)
add_executable(em_reporting_tests tests/reporting_tests.cpp)
target_link_libraries(em_reporting_tests PRIVATE em_game)
add_executable(em_advertising_actions_tests tests/advertising_actions_tests.cpp)
target_link_libraries(em_advertising_actions_tests PRIVATE em_game)
add_executable(em_classic_hover_tests tests/classic_hover_tests.cpp)
target_link_libraries(em_classic_hover_tests PRIVATE em_classic)
add_executable(em_classic_transfer_hover_tests tests/classic_transfer_hover_tests.cpp)
target_link_libraries(em_classic_transfer_hover_tests PRIVATE em_classic)
add_executable(em_classic_strengths_tests tests/classic_strengths_tests.cpp)
target_link_libraries(em_classic_strengths_tests PRIVATE em_classic)
add_executable(em_classic_club_info_tests tests/classic_club_info_tests.cpp)
target_link_libraries(em_classic_club_info_tests PRIVATE em_classic)
add_executable(em_classic_team_schedule_tests tests/classic_team_schedule_tests.cpp)
target_link_libraries(em_classic_team_schedule_tests PRIVATE em_classic)
add_executable(em_classic_cup_tests tests/classic_cup_tests.cpp)
target_link_libraries(em_classic_cup_tests PRIVATE em_classic)
add_executable(em_classic_euro_tests tests/classic_euro_tests.cpp)
target_link_libraries(em_classic_euro_tests PRIVATE em_classic)
add_executable(em_classic_wm_tests tests/classic_wm_tests.cpp)
target_link_libraries(em_classic_wm_tests PRIVATE em_classic)
add_executable(em_classic_league_fixtures_tests tests/classic_league_fixtures_tests.cpp)
target_link_libraries(em_classic_league_fixtures_tests PRIVATE em_classic)
add_executable(em_classic_promotion_tests tests/classic_promotion_tests.cpp)
target_link_libraries(em_classic_promotion_tests PRIVATE em_classic)
add_executable(em_classic_playoff_tests tests/classic_playoff_tests.cpp)
target_link_libraries(em_classic_playoff_tests PRIVATE em_classic)
add_executable(em_classic_advertising_tests tests/classic_advertising_tests.cpp)
target_link_libraries(em_classic_advertising_tests PRIVATE em_classic)
add_executable(em_stadium_actions_tests tests/stadium_actions_tests.cpp)
target_link_libraries(em_stadium_actions_tests PRIVATE em_game)
add_executable(em_classic_stadium_tests tests/classic_stadium_tests.cpp)
target_link_libraries(em_classic_stadium_tests PRIVATE em_classic)
add_executable(em_youth_actions_tests tests/youth_actions_tests.cpp)
target_link_libraries(em_youth_actions_tests PRIVATE em_game)
add_executable(em_youth_progress_tests tests/youth_progress_tests.cpp)
target_link_libraries(em_youth_progress_tests PRIVATE em_game)
add_executable(em_youth_intake_tests tests/youth_intake_tests.cpp)
target_link_libraries(em_youth_intake_tests PRIVATE em_game)
add_executable(em_annual_players_tests tests/annual_players_tests.cpp)
target_link_libraries(em_annual_players_tests PRIVATE em_game)
add_executable(em_annual_distribution_tests tests/annual_distribution_tests.cpp)
target_link_libraries(em_annual_distribution_tests PRIVATE em_game)
if(TARGET em_frontend)
    add_executable(em_stadium_game_tests tests/stadium_game_tests.cpp)
    target_link_libraries(em_stadium_game_tests PRIVATE em_frontend)
endif()
if(TARGET em_classic_app)
    add_executable(em_stadium_input_tests tests/stadium_input_tests.cpp)
    target_link_libraries(em_stadium_input_tests PRIVATE em_classic_app)
    add_executable(em_youth_input_tests tests/youth_input_tests.cpp)
    target_link_libraries(em_youth_input_tests PRIVATE em_classic_app)
    add_executable(em_sale_quote_tests tests/sale_quote_tests.cpp)
    target_link_libraries(em_sale_quote_tests PRIVATE em_classic_app)
    add_executable(em_save_history_tests tests/save_history_tests.cpp)
    target_link_libraries(em_save_history_tests PRIVATE em_classic_app)
endif()
target_link_libraries(em_classic_tests PRIVATE em_classic)
# Reference saves recorded with the original game (private checkout)
set(EM_REFERENCE "${OPENEM_PRIVATE_DIR}/work/EM" CACHE PATH "Directory with reference savegames (START/TAG1/MONAT1.HOK)")
if(IS_DIRECTORY "${EM_DATA}")
    add_test(NAME formats_original_kb COMMAND em_format_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME world_generator COMMAND em_world_generator_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/tests/data/world/newgame_4880.bin" "${OPENEM_PRIVATE_DIR}/tests/data/world/addmanager_4880.bin")
    # Version 1.30 rules (patched EM.EXE of the private checkout)
    if(EXISTS "${OPENEM_PRIVATE_DIR}/Original/1.30/EM.exe")
        add_test(NAME rules_v130 COMMAND em_rules_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/Original/1.30/EM.exe" "${OPENEM_PRIVATE_DIR}/work/exp/menu_20261104")
    endif()
    add_test(NAME ui_session COMMAND em_ui_tests "${EM_DATA}" "${EM_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/ui-test-work")
    add_test(NAME multimanager_career COMMAND em_multimanager_tests "${EM_DATA}")
    if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103/nativepurchase-cancel.hok")
        add_test(NAME purchase_dialog COMMAND em_purchase_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103")
        add_test(NAME loan_talent COMMAND em_loan_talent_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103")
        add_test(NAME team_refresh COMMAND em_team_refresh_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103")
        add_test(NAME reporting COMMAND em_reporting_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103")
        if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103/advertising-monat1-accepted.hok")
            add_test(NAME advertising_actions COMMAND em_advertising_actions_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103")
        endif()
    endif()
    add_test(NAME classic_graphics COMMAND em_classic_tests "${EM_DATA}")
    add_test(NAME classic_hover COMMAND em_classic_hover_tests "${EM_DATA}")
    add_test(NAME classic_transfer_hover COMMAND em_classic_transfer_hover_tests "${EM_DATA}")
    add_test(NAME classic_strengths COMMAND em_classic_strengths_tests "${EM_REFERENCE}")
    add_test(NAME classic_club_info COMMAND em_classic_club_info_tests "${EM_REFERENCE}")
    add_test(NAME classic_team_schedule COMMAND em_classic_team_schedule_tests "${EM_REFERENCE}")
    add_test(NAME classic_cup COMMAND em_classic_cup_tests "${EM_REFERENCE}")
    add_test(NAME classic_euro COMMAND em_classic_euro_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_wm COMMAND em_classic_wm_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_league_fixtures COMMAND em_classic_league_fixtures_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_promotion COMMAND em_classic_promotion_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_playoff COMMAND em_classic_playoff_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_advertising COMMAND em_classic_advertising_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME stadium_actions COMMAND em_stadium_actions_tests "${EM_DATA}" "${EM_REFERENCE}")
    add_test(NAME classic_stadium COMMAND em_classic_stadium_tests "${EM_DATA}" "${EM_REFERENCE}")
    if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103/youth-reduced.hok")
        add_test(NAME youth_actions COMMAND em_youth_actions_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103")
        add_test(NAME youth_progress COMMAND em_youth_progress_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103/youth_progress/scripted.vectors")
        add_test(NAME youth_intake COMMAND em_youth_intake_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103/youth_intake/scripted.vectors")
        add_test(NAME annual_players COMMAND em_annual_players_tests "${EM_REFERENCE}" "${EM_DATA}")
        add_test(NAME annual_distribution COMMAND em_annual_distribution_tests "${EM_REFERENCE}")
        if(TARGET em_youth_input_tests)
            add_test(NAME youth_input COMMAND em_youth_input_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103")
            add_test(NAME save_history COMMAND em_save_history_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/token_20261103" "${CMAKE_CURRENT_BINARY_DIR}/save-history-work")
        endif()
    endif()
    if(TARGET em_stadium_game_tests AND EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/evening_20261103/stadium-h2-accepted-condition.hok")
        add_test(NAME stadium_game COMMAND em_stadium_game_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/evening_20261103")
        if(TARGET em_stadium_input_tests)
            add_test(NAME stadium_input COMMAND em_stadium_input_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/evening_20261103")
        endif()
    endif()
    if(TARGET em_sale_quote_tests AND EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/menu_20261104/native-club-sold.hok")
        add_test(NAME sale_quote COMMAND em_sale_quote_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/menu_20261104" "${CMAKE_CURRENT_BINARY_DIR}/sale-quote-work")
    endif()
    add_test(NAME stock_actions COMMAND em_stock_tests "${EM_DATA}" "${EM_REFERENCE}")
    if(EM_BUILD_GUI)
        add_test(NAME classic_input COMMAND em_classic_app_tests "${EM_DATA}" "${EM_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-test-work")
        set(EM_UI_REFERENCE "${OPENEM_PRIVATE_DIR}/work/exp/ui_20261107/raster_goldens" CACHE PATH "Native DOSBox RGB captures of the later UI pages")
        if(EXISTS "${EM_UI_REFERENCE}/match-banner.rgb")
            add_test(NAME classic_match COMMAND em_classic_match_tests "${EM_DATA}" "${EM_REFERENCE}" "${EM_UI_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-match-work")
        endif()
        if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/match_20261108/raster_goldens")
            add_test(NAME classic_match_view COMMAND em_classic_match_view_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/match_20261108/raster_goldens" "${CMAKE_CURRENT_BINARY_DIR}/classic-match-view-work")
        endif()
        if(EXISTS "${EM_UI_REFERENCE}/settings-start.rgb")
            add_test(NAME classic_settings COMMAND em_classic_settings_tests "${EM_DATA}" "${EM_REFERENCE}" "${EM_UI_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-settings-work")
        endif()
        add_test(NAME classic_loan_scout COMMAND em_classic_loan_scout_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/transfer_20261110/raster_goldens" "${CMAKE_CURRENT_BINARY_DIR}/classic-test-work")
        add_test(NAME classic_hold COMMAND em_classic_hold_tests "${EM_DATA}" "${EM_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-test-work")
        if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/national_team_20261102/NATAUTO_native.hok")
            add_test(NAME wm_mode COMMAND em_wm_mode_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/national_team_20261102" "${CMAKE_CURRENT_BINARY_DIR}/wm-mode-work")
        endif()
        if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/hotseat_20261110/HOT2.HOK")
            add_test(NAME classic_hotseat COMMAND em_classic_hotseat_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/hotseat_20261110" "${CMAKE_CURRENT_BINARY_DIR}/classic-hotseat-work")
        endif()
        set(EM_MISC_REFERENCE "${OPENEM_PRIVATE_DIR}/work/exp/ui_20261108/raster_goldens" CACHE PATH "Native DOSBox RGB captures of Gemischtes, Neuer and Highscore")
        if(EXISTS "${EM_MISC_REFERENCE}/highscore-start.rgb")
            add_test(NAME classic_misc COMMAND em_classic_misc_tests "${EM_DATA}" "${EM_REFERENCE}" "${EM_MISC_REFERENCE}" "${OPENEM_PRIVATE_DIR}/tests/data/highscore" "${CMAKE_CURRENT_BINARY_DIR}/classic-misc-work")
            add_test(NAME classic_career COMMAND em_classic_career_tests "${EM_DATA}" "${EM_REFERENCE}" "${EM_MISC_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-career-work" "${OPENEM_PRIVATE_DIR}/work/exp/career_20261109")
        endif()
        add_test(NAME classic_ui_gaps COMMAND em_classic_ui_gaps_tests "${EM_DATA}" "${EM_REFERENCE}" "${CMAKE_CURRENT_BINARY_DIR}/classic-ui-gaps-work")
        # version 1.0 acceptance: a season and the season change through the classic UI (tests/scripts)
        add_test(NAME classic_contract_season COMMAND em_classic_contract_season_tests "${EM_DATA}" "${OPENEM_PRIVATE_DIR}/work/exp/contract_20261110" "${CMAKE_CURRENT_BINARY_DIR}/classic-contract-season-work")
        add_test(NAME classic_season_run COMMAND em_classic_season_run_tests "${EM_DATA}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/classic_season_run.txt" "${CMAKE_CURRENT_BINARY_DIR}/classic-season-run-work")
        # line-up changes from the board of a running match (live minutes through the script pump)
        if(EXISTS "${EM_REFERENCE}/MONAT1.HOK")
            add_test(NAME classic_live_lineup COMMAND em_classic_season_run_tests "${EM_DATA}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/classic_live_lineup.txt" "${CMAKE_CURRENT_BINARY_DIR}/classic-live-lineup-work" "${EM_REFERENCE}/MONAT1.HOK")
        endif()
        set(EM_CLASSIC_REFERENCE "${OPENEM_PRIVATE_DIR}/work/exp/day_20261103/raster_goldens" CACHE PATH "Native DOSBox RGB reference captures")
        if(EXISTS "${EM_CLASSIC_REFERENCE}/main-start.rgb")
            add_test(NAME classic_raster COMMAND em_classic_raster_tests "${EM_DATA}" "${EM_REFERENCE}" "${EM_CLASSIC_REFERENCE}")
        endif()
        if(EXISTS "${OPENEM_PRIVATE_DIR}/work/exp/stats_20261108/raster_goldens/stats-players-monat1.rgb")
            add_executable(em_classic_stats_tests tests/classic_stats_tests.cpp)
            target_link_libraries(em_classic_stats_tests PRIVATE em_classic_app)
            add_test(NAME classic_stats COMMAND em_classic_stats_tests "${EM_DATA}" "${EM_REFERENCE}" "${OPENEM_PRIVATE_DIR}/work/exp/stats_20261108/raster_goldens")
        endif()
    endif()
endif()

# Pixel comparisons with captures of the original draw the few deliberately changed places (save page path and
# names, settings header, cup draw remnant) as the original does (classic_original_look in classic_canvas.hpp).
foreach(test classic_settings classic_misc classic_career classic_contract_season)
    if(TEST ${test})
        set_tests_properties(${test} PROPERTIES ENVIRONMENT "EM_ORIGINAL_LOOK=1")
    endif()
endforeach()

# Differential tests against the original program (private checkout only, see OPENEM_PRIVATE_DIR).
option(EM_REALCODE "Build the differential tests of the private checkout" OFF)
if(EM_REALCODE AND EXISTS "${OPENEM_PRIVATE_DIR}/cmake/realcode.cmake")
    include("${OPENEM_PRIVATE_DIR}/cmake/realcode.cmake")
endif()

# em_editor: asset editor for the original data (pixel editor for KB images); ships with the OpenEM release.
# Two front ends over the same document logic: on Windows the native Win32 one (menu, toolbar, list view, status bar,
# system dialogs) is `em_editor`; the portable SDL2 + Dear ImGui one (src/editor/sdl_*.cpp) is `em_editor` on Linux
# and macOS and is additionally built as `em_editor_sdl` on Windows so it can be tested there.
# The document logic (em_editor_lib) has no UI dependency and is built and tested everywhere.
add_library(em_editor_lib STATIC src/editor/pixel_editor.cpp src/editor/palette_doc.cpp src/editor/team_doc.cpp src/editor/map_doc.cpp)
target_link_libraries(em_editor_lib PUBLIC em_formats)
add_executable(em_editor_tests tests/editor_tests.cpp)
target_link_libraries(em_editor_tests PRIVATE em_editor_lib)
if(IS_DIRECTORY "${EM_DATA}")
    add_test(NAME editor_original COMMAND em_editor_tests "${EM_DATA}")
endif()
if(WIN32)
    add_executable(em_editor WIN32 src/editor/win_main.cpp src/editor/em_editor.manifest)
    target_compile_definitions(em_editor PRIVATE UNICODE _UNICODE)
    target_link_libraries(em_editor PRIVATE em_editor_lib)
endif()
if(EM_BUILD_GUI)
    # Dear ImGui (pinned release, SDL2 platform + SDL_Renderer backends, no OpenGL) and stb_image / stb_image_write
    # (pinned commit) for the picture import/export, fetched as source archives at configure time.
    include(FetchContent)
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
        URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(stb
        URL https://github.com/nothings/stb/archive/31c1ad37456438565541f4919958214b6e762fb4.tar.gz
        URL_HASH SHA256=e4e3bba9c572a4a4148373a914d88ea0f0d11de8cc2c66739926e7eca0223319
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(imgui stb)  # neither has a CMakeLists.txt: only downloaded and unpacked
    add_library(em_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
    target_include_directories(em_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(em_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(em_imgui PUBLIC ${EM_SDL_TARGET})
    # The UI font (Roboto Medium from the ImGui sources, Apache 2.0) is compiled into the editor, so it looks the same
    # everywhere and needs no file next to the program.
    set(EM_EDITOR_FONT_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/em_editor_font.cpp")
    set(EM_EDITOR_FONT_TTF "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf")
    if(NOT EXISTS "${EM_EDITOR_FONT_CPP}" OR "${EM_EDITOR_FONT_TTF}" IS_NEWER_THAN "${EM_EDITOR_FONT_CPP}")
        file(READ "${EM_EDITOR_FONT_TTF}" font_hex HEX)
        file(SIZE "${EM_EDITOR_FONT_TTF}" font_size)
        string(REGEX REPLACE "([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])" "\\1\n" font_hex "${font_hex}")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_hex "${font_hex}")
        file(WRITE "${EM_EDITOR_FONT_CPP}" "// Generated by CMakeLists.txt from Roboto-Medium.ttf (Apache License 2.0)\n"
            "extern const unsigned char em_editor_font[];\nextern const unsigned int em_editor_font_size;\n"
            "const unsigned char em_editor_font[] = {\n${font_hex}};\nconst unsigned int em_editor_font_size = ${font_size};\n")
    endif()
    if(WIN32)
        set(EM_EDITOR_SDL_NAME em_editor_sdl)  # the Win32 front end keeps the name em_editor
    else()
        set(EM_EDITOR_SDL_NAME em_editor)
    endif()
    add_executable(${EM_EDITOR_SDL_NAME} WIN32
        src/editor/sdl_main.cpp
        src/editor/sdl_app.cpp
        src/editor/sdl_tabs.cpp
        src/editor/sdl_dialogs.cpp
        src/editor/sdl_image_io.cpp
        src/editor/sdl_stb.cpp
        "${EM_EDITOR_FONT_CPP}")
    target_include_directories(${EM_EDITOR_SDL_NAME} PRIVATE "${stb_SOURCE_DIR}")
    target_link_libraries(${EM_EDITOR_SDL_NAME} PRIVATE em_editor_lib em_imgui)
    # stb is third-party code: no warnings from the file that compiles its implementation
    if(MSVC)
        set_source_files_properties(src/editor/sdl_stb.cpp PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties(src/editor/sdl_stb.cpp PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

# Release layout: the game and the editor side by side (cmake --install build --config Release --prefix <folder>)
if(TARGET openem)
    install(TARGETS openem RUNTIME DESTINATION .)
endif()
if(TARGET em_editor)
    install(TARGETS em_editor RUNTIME DESTINATION .)
    install(FILES docs/EDITOR.md DESTINATION docs)
endif()
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)

# Release archive: cpack -C Release (in the build folder) packs the install layout above
set(CPACK_PACKAGE_NAME "OpenEM")
set(CPACK_PACKAGE_VENDOR "OpenEM")
set(CPACK_PACKAGE_VERSION "${OPENEM_RELEASE}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Nachbau des Eishockey Managers (1993); benötigt die Originaldaten")
# ZIP on Windows, tar.gz elsewhere; the archive name carries the platform (win64, linux-x86_64, macos-arm64)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_SYSTEM_NAME "win64")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SYSTEM_NAME "macos-${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_OSX_ARCHITECTURES)
        set(CPACK_SYSTEM_NAME "macos-${CMAKE_OSX_ARCHITECTURES}")
    endif()
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_SYSTEM_NAME "linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
include(CPack)
