// SPDX-License-Identifier: GPL-3.0-only
// Format tests. Without arguments: unit tests on synthetic data. With the original EM directory
// as argument: every KB\*.vga is classified, all images decode, and the number of images the RLE
// scheme fills completely must not drop below the known count (regression guard while the
// remaining variants are worked out).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>

#include "formats/db.hpp"
#include "formats/teamdata.hpp"
#include "formats/hok.hpp"
#include "formats/tore.hpp"
#include "formats/vga.hpp"
#include "game/calendar.hpp"
#include "game/checkpoint.hpp"
#include "game/competitions.hpp"
#include "game/contracts.hpp"
#include "game/finances.hpp"
#include "game/game.hpp"
#include "game/injuries.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/national_team.hpp"
#include "game/penalties.hpp"
#include "game/schedule.hpp"
#include "game/scorers.hpp"
#include "game/season.hpp"
#include "game/season_end.hpp"
#include "game/sponsors.hpp"
#include "game/training.hpp"
#include "game/transfers.hpp"

using namespace em;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void unit_tests() {
    // 4x2 image: run of 5 x 0x07 (0x84), literal of 3 bytes (0x02 a b c)
    Bytes d = {4, 0, 2, 0, 6, 0, 0x84, 0x07, 0x02, 1, 2, 3};
    CHECK(classify_vga(d) == VgaKind::Image);
    Image img = decode_vga(d);
    CHECK(img.width == 4 && img.height == 2 && img.complete);
    CHECK(img.pixels == (std::vector<uint8_t>{7, 7, 7, 7, 7, 1, 2, 3}));

    // stream shorter than the image: rest stays 0, flagged incomplete
    Bytes s = {4, 0, 1, 0, 2, 0, 0x81, 0x09};
    Image part = decode_vga(s);
    CHECK(!part.complete && part.pixels[0] == 9 && part.pixels[1] == 9 && part.pixels[2] == 0);

    // 1x202 image: chunk 1 (rows 0..199) is only 3 pixels long, 0x80 ends it; chunk 2 starts at
    // row 200 regardless and fills rows 200..201
    Bytes t = {1, 0, 202, 0, 6, 0, 0x82, 0x05, 0x80, 0x81, 0x06, 0x80};
    Image tall = decode_vga(t);
    CHECK(tall.pixels[0] == 5 && tall.pixels[2] == 5 && tall.pixels[3] == 0);
    CHECK(tall.pixels[200] == 6 && tall.pixels[201] == 6 && !tall.complete);

    // 6-bit palette values are scaled to 8 bit
    Bytes p(765, 0);
    p[0] = 63;
    CHECK(classify_vga(p) == VgaKind::Palette);
    CHECK(decode_palette(p).rgb[0][0] == 255);
    // Native KB21 starts with E4 EC F0 after black. Its high bits are not an
    // eight-bit RGB indicator: native PNG color is exactly 92 B2 C3.
    p[0] = 228; p[1] = 236; p[2] = 240;
    CHECK((decode_palette(p).rgb[0] == std::array<uint8_t,3>{146,178,195}));

    // encoder: round trip of runs, literals, long runs, tall images
    for (int h : {1, 7, 200, 202, 450}) {
        Image src(37, h);
        for (size_t i = 0; i < src.pixels.size(); i++)
            src.pixels[i] = (i / 300) % 3 == 0 ? uint8_t(i * 7) : uint8_t(i / 50);
        Image back = decode_vga(encode_vga(src));
        CHECK(back.complete && back.width == 37 && back.height == h && back.pixels == src.pixels);
    }
    Palette pal;
    for (size_t i = 0; i < 256; i++) pal.rgb[i] = {uint8_t((i & 63) << 2 | (i & 63) >> 4), 0, 0};
    Bytes withpal = encode_vga(Image(2, 2, 5), &pal);
    CHECK(classify_vga(withpal) == VgaKind::ImageWithPalette);
    CHECK(decode_palette(withpal, withpal.size() - 765).rgb[3] == pal.rgb[3]);
}

static void original_kb(const fs::path& em_dir) {
    int images = 0, complete = 0, files = 0;
    for (auto& e : fs::directory_iterator(em_dir / "KB")) {
        if (e.path().extension() != ".vga") continue;
        files++;
        Bytes d = read_file(e.path());
        VgaKind k = classify_vga(d);
        if (k == VgaKind::Image || k == VgaKind::ImageWithPalette) {
            images++;
            Image img = decode_vga(d);
            complete += img.complete;
            // encoder round trip on the original (palette from the file, six-bit)
            if (k == VgaKind::Image) {
                Image back = decode_vga(encode_vga(img));
                CHECK(back.pixels == img.pixels);
            } else {
                Palette pal = decode_palette(d, d.size() - 765);
                Bytes re = encode_vga(img, &pal);
                CHECK(decode_vga(re).pixels == img.pixels);
                Bytes want(d.end() - 765, d.end());
                for (auto& b : want) b &= 63;
                CHECK(encode_palette(pal) == want);
            }
        }
    }
    std::printf("KB: %d files, %d images, %d complete\n", files, images, complete);
    CHECK(files == 114);
    CHECK(images == 94);     // 88 plain + 6 screens with palette
    CHECK(complete == 94);   // every image fills width*height
}

// Both original savegames: header fields and a sane decrypted state. The state of a correctly
// decrypted file is mostly 0 (empty slots) with 0xFF fill blocks; a wrong key stream gives
// near-uniform bytes, so the zero share is a reliable check.
static void original_hok(const fs::path& em_dir) {
    for (const char* name : {"AUTOSAVE.HOK", "BACKUP.HOK"}) {
        HokFile h = decode_hok(read_file(em_dir / name));
        size_t zeros = 0, ff = 0;
        for (uint8_t b : h.state) {
            zeros += b == 0;
            ff += b == 0xFF;
        }
        std::printf("%s: year %d, manager %s, club %s, %zu state bytes, %zu zero, %zu 0xFF\n", name, h.year,
                    h.manager.c_str(), h.club.c_str(), h.state.size(), zeros, ff);
        CHECK(h.year == 1993);
        CHECK(h.club == "MANNHEIMER ERC");
        CHECK(h.state.size() == 47761);
        CHECK(zeros > h.state.size() * 3 / 4);
        // Right after the first check byte both files hold 32 distinct values below 36 (the cup
        // draw). It only decodes if the key stream skips the check byte correctly.
        size_t from = hok_state_index(kHokCheckByte + 1);
        bool found = false;
        for (size_t s = from; s < from + 64 && !found; s++) {
            bool seen[36] = {};
            bool perm = true;
            for (size_t j = 0; j < 32 && perm; j++) {
                uint8_t v = h.state[s + j];
                perm = v < 36 && !seen[v];
                if (perm) seen[v] = true;
            }
            found = perm;
        }
        CHECK(found);
    }
}

// Master database: 48 named clubs (player ids 0..1199, so the pool starts at PIELER1200) and the
// European club list.
static void original_db(const fs::path& em_dir) {
    Database db = decode_db(read_file(em_dir / "KB" / "23.vga"));
    CHECK(db.clubs.size() == 64 && db.clubs[52].name == "RUSSLAND" && db.clubs[63].name == "TSCHECHEI");
    CHECK(db.clubs[0].name == "EISBÄREN BERLIN");
    CHECK(db.clubs[1].name == "SC PREUSSEN BERLIN" && db.clubs[1].players[0].name == "APPEL");
    CHECK(db.clubs[8].name == "MANNHEIMER ERC");
    CHECK(db.clubs[47].name == "ESV SCHIERKE");
    CHECK(db.europe_clubs.size() == 86 && db.europe_clubs.front() == "KLAGENFURTER AC");
    std::printf("DB: %zu clubs, %zu European clubs\n", db.clubs.size(), db.europe_clubs.size());

    // writer: unchanged database -> identical bytes; one edited name changes only its field
    const Bytes orig = read_file(em_dir / "KB" / "23.vga");
    CHECK(encode_db(orig, db) == orig);
    Database edited = db;
    edited.clubs[0].name = "HAMBURGER SV";
    edited.clubs[1].players[0].name = "MÜLLER";
    edited.clubs[1].players[1].name = "";
    Bytes out = encode_db(orig, edited);
    size_t diff = 0;
    for (size_t i = 0; i < orig.size(); i++) diff += orig[i] != out[i];
    Database back = decode_db(out);
    CHECK(back.clubs[0].name == "HAMBURGER SV" && back.clubs[1].players[0].name == "MÜLLER");
    CHECK(back.clubs[1].players[1].name.empty() && back.clubs[1].players[2].name == db.clubs[1].players[2].name);
    CHECK(diff > 0 && diff < 80 && out.size() == orig.size());
    // team tables
    {
        auto td = decode_team_data(orig);
        CHECK(td.size() == 150 && encode_team_data(orig, td) == orig);
        bool crests_ok = true;
        for (int t = 0; t < 64; t++) crests_ok = crests_ok && td[t].crest >= 0 && td[t].crest <= 63;
        CHECK(crests_ok);
        // Bundesliga: place = rank by strength (largest a0 -> 1)
        bool rank_ok = true;
        for (int t = 0; t < 12; t++) {
            int better = 0;
            for (int u = 0; u < 12; u++) better += td[u].strength[0] > td[t].strength[0];
            rank_ok = rank_ok && td[t].place == better + 1;
        }
        CHECK(rank_ok);
        auto ed = td;
        ed[3].crest = 17; ed[3].strength = {50, 51, 52}; ed[3].place = 4; ed[3].jersey = 0x0F0F;
        auto re = decode_team_data(encode_team_data(orig, ed));
        CHECK(re[3].crest == 17 && re[3].strength[1] == 51 && re[3].place == 4 && re[3].jersey == 0x0F0F);
        CHECK(re[4].crest == td[4].crest && re[4].strength == td[4].strength);
        ed[3].crest = 64;
        bool bad = false;
        try { encode_team_data(orig, ed); } catch (const FormatError&) { bad = true; }
        CHECK(bad);
    }
    bool threw = false;
    try { edited.clubs[0].name = "ÅÅ"; encode_db(orig, edited); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
}

// Goal scenes: every TORE script parses up to its 0xAAAA end mark with the frame count implied by
// the file size, and all objects stay on (or just beside) the 672 x 1152 portrait rink. The rink
// and sprite graphics unpack to the sizes the loader allocates (DS:5400).
static void original_tore(const fs::path& em_dir) {
    int files = 0;
    bool frames_ok = true, bounds_ok = true;
    for (auto& e : fs::directory_iterator(em_dir / "TORE")) {
        if (e.path().filename() == "anzahl") continue;
        Bytes d = read_file(e.path());
        TorScene s = decode_tor(d);
        files++;
        frames_ok = frames_ok && s.frames.size() * 28 + 0x54 + 2 == d.size();
        for (size_t n = 0; n <= s.frames.size(); n += 10) {
            TorState st = play_tor(s, n);
            bounds_ok = bounds_ok && st.puck_x >= -16 && st.puck_x < 1000 && st.puck_y >= -16 && st.puck_y < 1250;
        }
    }
    CHECK(files == 48 && frames_ok && bounds_ok);
    CHECK(decode_tor(read_file(em_dir / "TORE" / "2.at")).frames.size() == 139);
    CHECK(decode_tor_move(0x94).dx == 2 && decode_tor_move(0x94).dy == -4);
    const fs::path kb = em_dir / "KB";
    CHECK(unpack_kb_block(read_file(kb / "231.vga")).size() == 64000);
    CHECK(unpack_kb_block(read_file(kb / "233.vga")).size() == 40960);
    CHECK(unpack_kb_block(read_file(kb / "235.vga")).size() == 12288);
    for (const char* n : {"231.vga", "232.vga", "233.vga", "234.vga", "235.vga"}) {
        Bytes raw = unpack_kb_block(read_file(kb / n));
        CHECK(unpack_kb_block(pack_kb_block(raw)) == raw);
    }
    {
        auto sp = decode_tor_sprites(read_file(kb / "233.vga"));
        auto back = decode_tor_sprites(encode_tor_sprites(sp));
        CHECK(back.size() == sp.size() && !sp.empty());
        for (size_t i = 0; i < sp.size() && i < back.size(); i++) CHECK(back[i].pixels == sp[i].pixels);
        Image panel = decode_tor_panel(read_file(kb / "246.vga"));
        CHECK(decode_tor_panel(encode_tor_panel(panel)).pixels == panel.pixels);
    }
    Image rink = decode_rink(read_file(kb / "231.vga"), read_file(kb / "232.vga"), read_file(kb / "236.vga"));
    CHECK(rink.width == 672 && rink.height == 1152);
    // centre spot: the faceoff dot in the middle of the rink is not ice colour
    CHECK(rink.at(336, 576) != rink.at(336, 500));
    std::printf("TORE: %d scripts\n", files);
}

// Season calendar from EM.EXE: date arithmetic and event days against the reference saves and the
// dates the original shows (European cup group days, DEB cup Achtelfinale on 20.10.).
static void calendar_vs_saves(const fs::path& em_dir, const fs::path& ref_dir) {
    SeasonCalendar cal = load_season_calendar(read_file(em_dir / "EM.EXE"));
    int league = 0, cup = 0, euro = 0, playoff = 0;
    for (size_t d = 0; d < cal.events.size(); d++) {
        league += cal.league_day(int(d));
        cup += cal.events[d] == kEventCup;
        euro += cal.events[d] == kEventEuroCup;
        playoff += cal.events[d] == kEventPlayoff;
    }
    CHECK(league == 44 && cup == 15 && euro == 12 && playoff == 15 && cal.events.size() == 233);
    GameDate first = date_of_season_day(1, 1993);
    CHECK(first.day == 10 && first.month == 9 && first.year == 1993 && first.weekday == 0);
    GameDate jan = date_of_season_day(113 + 1, 1993);  // the day after 31.12.
    CHECK(jan.day == 1 && jan.month == 1 && jan.year == 1994);
    const int euro_days[6] = {4, 7, 9, 11, 14, 16};
    for (int i = 0; i < 6; i++) {
        GameDate g = date_of_season_day(cal.nth_day(kEventEuroCup, i + 1), 1993);
        CHECK(g.day == euro_days[i] && g.month == 11);
    }
    GameDate achtel = date_of_season_day(cal.nth_day(kEventCup, 4), 1993);  // round 2, first date
    CHECK(achtel.day == 20 && achtel.month == 10);
    for (const char* name : {"START.HOK", "TAG1.HOK", "MONAT1.HOK"}) {
        if (!fs::exists(ref_dir / name)) continue;
        HokFile h = decode_hok(read_file(ref_dir / name));
        GameDate g = date_of_season_day(h.date.day_index, h.year);
        CHECK(g.day == h.date.day && g.month == h.date.month && g.weekday == h.date.weekday());
        // the next league matchday is today or later, the previous one lies in the past
        CHECK(cal.league_matchday_day(h.matchday[0]) >= h.date.day_index);
        CHECK(h.matchday[0] == 1 || cal.league_matchday_day(h.matchday[0] - 1) < h.date.day_index);
    }
    CHECK(cal.league_matchday_day(10) == 32);  // MONAT1: Wednesday 11.10., matchday 10
}

// Monthly finances (ovl08) against the "Totale" screen: AKTIE is MONAT1 with 134000 DM put into
// the savings book; the screen shows income 14715 DM and expenses 482566 DM, and the save holds
// the income ovl08_16b7 computed for it. MONAT1 still holds the START value 40000.
static void finances(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "AKTIE.HOK") || !fs::exists(ref_dir / "MONAT1.HOK")) return;
    FinanceTables t = load_finance_tables(read_file(em_dir / "EM.EXE"));
    CHECK(t.league_bonus[1] == 1800 && t.place_cost[1] == 12 && t.cost_factor == 100 && t.level == 2);
    HokFile a = decode_hok(read_file(ref_dir / "AKTIE.HOK"));
    ManagerBudget b = budget_from_save(a, 0);
    CHECK(b.team == 7 && b.league == 0 && b.players == 25 && b.team_strength == 321);
    CHECK(b.bank == 402000 && savings_interest(b) == 4020 && loan_interest(b) == 0);
    ManagerBudget rich = b;
    rich.bank = 500000000;
    CHECK(savings_interest(rich) == 5000000);  // native *1/100; *10/1000 overflows a Windows long
    CHECK(monthly_income(b, true) == b.stored_income && b.stored_income / 3 == 14715);
    CHECK(monthly_expenses(t, b, true, true) / 3 == 482566);
    HokFile m = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    ManagerBudget mb = budget_from_save(m, 0);
    CHECK(mb.stored_income == 40000 && monthly_income(mb, true) == 40125);
    store_budget(m, mb, 0);
    CHECK(le32(m.state, 0x0F64) == 40000);  // saving does not silently open the full-income view
    CHECK(monthly_expenses(t, mb, false, false) == 1447700);
    // a 31-day month: interest accrues daily and is booked with income and expenses at the end
    ManagerBudget day = b;
    const long start = day.balance;
    for (int d = 1; d <= 31; d++) finance_day(t, day, d, 31);
    CHECK(day.accrued == 0 && day.balance == start + 40125 - 1447700 + 31 * (4020 / 32));
    CHECK(day.stored_income == monthly_income(day, false));
    // a bank loan costs 4 % a month, an overdraft 10 %
    ManagerBudget loan = b;
    loan.bank = -300000;
    loan.balance = -90000;
    CHECK(loan_interest(loan) == 12000 && monthly_income(loan, true) == 40125);
    CHECK(monthly_expenses(t, loan, true, true) == 1447700 + 12000 + 9000);
    // DS:0ED8 and DS:02BC are runtime HOK fields, not immutable EXE defaults.
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const long base_cost = monthly_expenses(t, b, false, false);
    for (int level : {0, 5}) {
        a.state[0xAEB4] = uint8_t(level);
        const FinanceTables saved_tables = load_finance_tables(exe, a);
        CHECK(monthly_expenses(saved_tables, b, false, false) == base_cost + (2 - level) * 10000);
        Game game(exe, encode_hok(a), 6);
        CHECK(game.game_level() == level);
        Game restored(exe, game.checkpoint_bytes(), 999);
        CHECK(restored.game_level() == level && restored.save_bytes() == game.save_bytes());
    }
    a.state[0xAEB4] = 2;
    const uint32_t higher_cost = uint32_t(t.cost_factor + 10);
    for (int byte = 0; byte < 4; byte++) a.state[0x06B0 + size_t(byte)] = uint8_t(higher_cost >> (byte * 8));
    CHECK(monthly_expenses(load_finance_tables(exe, a), b, false, false) == base_cost + 35000);
}

// Attendance (ovl08_21e5): Köln played four home games before MONAT1, all sold out (7500 places,
// total 30000). With the MONAT1 teams and table every first-division guest should fill the hall
// nearly always at 13 DM; at 30 DM fewer come, and the ticket money is booked.
static void attendance(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    FinanceTables t = load_finance_tables(read_file(em_dir / "EM.EXE"));
    CHECK(t.class_fans[0] == 4800 && t.extension_fans_high[0] == 450 && t.money_num == 3 && t.class_limit[1] == 24);
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    ManagerBudget b = budget_from_save(h, 0);
    CHECK(b.ticket_price == 13 && b.home_games == 4 && b.places[0] + b.places[1] == 7500);
    CHECK(b.attendance_total == 30000 && b.attendance_record == 7500 && b.attendance_min == 7500);
    {
        ManagerBudget league = b, cup = b;
        AttendanceMatch m;
        m.guest = 10;
        BorlandRandom league_rng(6), cup_rng(6);
        const long a = home_attendance(t, league, m, league_rng);
        m.league_statistics = false;
        CHECK(home_attendance(t, cup, m, cup_rng) == a);
        CHECK(cup.balance == league.balance && cup_rng.state() == league_rng.state());
        CHECK(cup.home_games == b.home_games && cup.attendance_total == b.attendance_total);
        CHECK(cup.attendance_record == b.attendance_record && cup.attendance_min == b.attendance_min);
        CHECK(cup.record_guest == b.record_guest && cup.min_guest == b.min_guest);
    }
    auto rank = [&](int team) {
        for (int i = 0; i < 12; i++)
            if (h.table_order[0][size_t(i)] == team) return i;
        return 5;
    };
    BorlandRandom rng(7);
    long full = 0, games = 0, sum13 = 0, sum30 = 0;
    for (int guest = 0; guest < 12; guest++) {
        if (guest == b.team) continue;
        AttendanceMatch m;
        m.home_class = team_class(t, b.team);
        m.away_class = team_class(t, guest);
        m.home_strength = attendance_strength(h.teams[size_t(b.team)]);
        m.away_strength = attendance_strength(h.teams[size_t(guest)]);
        m.home_rank = rank(b.team);
        m.away_rank = rank(guest);
        for (int i = 0; i < 50; i++) {
            ManagerBudget g = b;
            const long a = home_attendance(t, g, m, rng);
            CHECK(g.balance == b.balance + 13 * a * 3 && g.home_games == 5 && g.attendance_total == 30000 + a);
            full += a == 7500;
            sum13 += a;
            g = b;
            g.ticket_price = 30;
            sum30 += home_attendance(t, g, m, rng);
            games++;
        }
    }
    std::printf("attendance Köln: sold out %ld / %ld, mean %ld at 13 DM, %ld at 30 DM\n", full, games,
                sum13 / games, sum30 / games);
    CHECK(full * 10 >= games * 9 && sum30 < sum13);
    // START -> MONAT1: five sold-out home games (four league, one cup) at 13 DM plus the month end
    // of 30.09. (income 40000 raw, the expenses of MONAT1) give the MONAT1 balance exactly
    ManagerBudget start = budget_from_save(decode_hok(read_file(ref_dir / "START.HOK")), 0);
    CHECK(start.balance + 5 * 13 * 7500 * 3 + 40000 - monthly_expenses(t, b, false, false) == b.balance);
    std::printf("attendance Köln vs. Preussen by price:");
    for (int price = 10; price <= 30; price += 2) {
        AttendanceMatch m;
        m.home_strength = attendance_strength(h.teams[size_t(b.team)]);
        m.away_strength = attendance_strength(h.teams[10]);
        m.home_rank = rank(b.team);
        m.away_rank = rank(10);
        long sum = 0;
        for (int i = 0; i < 100; i++) {
            ManagerBudget g = b;
            g.places = {12500, 12500};  // a full-size hall shows the demand
            g.ticket_price = price;
            sum += home_attendance(t, g, m, rng);
        }
        std::printf(" %d DM %ld,", price, sum / 100);
    }
    std::printf("\n");
}

// Market value and salary (ovl11_3d36): START holds the values the game computed for the Köln
// squad at the start (ovl03_0135); all 25 must come out exactly. Renewal demands (ovl11_41db)
// grow with the contract length and shrink with the age at the season end.
static void contracts(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    ContractTables t = load_contract_tables(read_file(em_dir / "EM.EXE"));
    HokFile h = decode_hok(read_file(ref_dir / "START.HOK"));
    BorlandRandom rng(5);
    int checked = 0;
    for (int slot = 0; slot < kHokSquadSlots; slot++) {
        int p = -1;
        ContractPlayer c = contract_player(h, 0, slot, &p);
        if (p < 0) continue;
        CHECK(player_worth(t, c, Worth::Value, rng) == c.value);
        CHECK(player_worth(t, c, Worth::Salary, rng) == c.salary);
        checked++;
    }
    CHECK(checked == 25);
    ContractPlayer heiss = contract_player(h, 0, 0);
    CHECK(heiss.value / 3 == 515000 && heiss.salary / 3 == 17100);
    const long one = renewal_demand(t, heiss, 1, 100, 10, 44, 32, rng);
    const long three = renewal_demand(t, heiss, 3, 100, 10, 44, 32, rng);
    const long late = renewal_demand(t, heiss, 3, 100, 44, 44, 233, rng);
    std::printf("contracts: Heiss (22) asks %ld / %ld DM for 1 / 3 years, %ld DM for 3 years at the season end\n",
                one / 3, three / 3, late / 3);
    CHECK(one == 51300L * 112 / 100 && three > one && late > three);
}

// Transfer market (overlay 11): the listed players of START and MONAT1 (squad block 4) are on the
// list (+0x1A = 4) with form 45..55 and value / salary as ovl11_3d36 computes them; bids, offers
// and contract answers behave as decoded.
static void transfers(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    ContractTables t = load_contract_tables(read_file(em_dir / "EM.EXE"));
    BorlandRandom rng(11);
    for (const char* name : {"START.HOK", "MONAT1.HOK"}) {
        HokFile h = decode_hok(read_file(ref_dir / name));
        int listed = 0;
        for (int slot = 0; slot < kTransferListSlots; slot++) {
            int p = -1;
            ContractPlayer c = contract_player(h, 4, slot, &p);
            if (p < 0) continue;
            listed++;
            CHECK(h.state[0x4FC6 + size_t(p) * 0x21 + 0x1A] == 4);
            const int form = h.state[0x4FC6 + size_t(p) * 0x21 + 0x17];
            CHECK(form >= 45 && form <= 55);
            CHECK(player_worth(t, c, Worth::Value, rng) == c.value);
            CHECK(player_worth(t, c, Worth::Salary, rng) == c.salary);
        }
        CHECK(listed >= 6);
    }
    // bids for a player worth 300000 DM
    const long price = 900000;
    int low = 0, mid = 0, high = 0;
    for (int i = 0; i < 1000; i++) {
        low += seller_accepts(price, price * 70 / 100, rng);
        mid += seller_accepts(price, price, rng);
        high += seller_accepts(price, price * 135 / 100, rng);
    }
    std::printf("transfers: bids of 70/100/135 %% accepted %d/%d/%d of 1000\n", low, mid, high);
    CHECK(low == 0 && high == 1000 && mid > 300 && mid < 900);
    long smin = 1L << 30, smax = 0;
    for (int i = 0; i < 1000; i++) {
        const long o = sale_offer(price, false, rng);
        smin = std::min(smin, o);
        smax = std::max(smax, o);
        CHECK(o % 300 == 0);
    }
    CHECK(smin >= price * 87 / 100 - 300 && smax <= price * 125 / 100);
    // contract answers: a raise at the same length is always accepted, a cut never
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    ContractPlayer c = contract_player(h, 0, 0);
    CHECK(player_accepts(t, c, c.years, c.salary + 300, 10, 44, 32, rng));
    CHECK(!player_accepts(t, c, c.years, c.salary - 300, 10, 44, 32, rng));
    CHECK(!player_accepts(t, c, c.years + 1, c.salary, 10, 44, 32, rng));  // longer for the same money
    int yes = 0;
    for (int i = 0; i < 200; i++) yes += player_accepts(t, c, c.years + 1, c.salary * 3 / 2, 10, 44, 32, rng);
    CHECK(yes > 150);
}

// Sponsors (ovl08_2893 / 2ba7): MONAT1 stores image 70 from an earlier refresh.
// Current packed history is decoded independently; generated offers stay in range.
static void image_competitions(const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    HokFile source = decode_hok(read_file(ref_dir / "START.HOK"));
    const int human = source.state[0x0DCD + 0x14];
    auto factors = [&] { return image_competition_factors(source, 0); };
    CHECK(factors().playoffs == 0 && factors().cup == 2 && factors().euro == 0);
    source.state[0x8ED1] = 4;
    source.state[0x8E51] = uint8_t(human);
    source.state[0x8E52] = 1;
    source.state[0x8E53] = 0xFF;
    CHECK(factors().cup == 5);
    source.state[0x8E51] = 2;
    CHECK(factors().cup == 0);
    source.state[0x8ED1] = 0;
    CHECK(factors().cup == 2);  // native pre-draw fallback
    source.state[0x8F86] = 3;
    source.state[0x0448] = uint8_t(human);
    CHECK(factors().euro == 4);  // return round uses the first three schedule rows
    source.state[0x8F86] = 6;
    CHECK(factors().euro == 0);  // the old group alone no longer qualifies
    source.state[0x0490] = uint8_t(human);
    CHECK(factors().euro == 7);
    source.state[0x8F86] = 12;
    CHECK(factors().euro == 0);
    source.state[0x03B6] = 0;
    source.state[0x8E91] = uint8_t(human);
    source.state[0x8E92] = 0xFF;
    source.state[0x8F40] = 100;  // a decided current series still counts
    CHECK(factors().playoffs == 1);
    source.state[0x03B6] = 2;
    source.state[0x8E93] = 0xFF;
    CHECK(factors().playoffs == 0);
    source.state[0x0400] = uint8_t(human);
    CHECK(factors().playoffs == 1);  // relegation belongs to native code 0x40 too
    source.state[0x8F87] = 2;
    source.state[0x0259] = 5;
    CHECK(factors().playoffs == 0);  // all six relegation dates are over
    source.state[0x03B6] = 0;
    source.state[0x17E7 + size_t(human) * 61 + 0x16] = 2;
    CHECK(factors().playoffs == 0);
    CHECK(image_competition_factors(source, 1).playoffs == 1);
    for (const auto& fixture : {std::pair{"EUOWN", 8}, std::pair{"EUOWNR", 9}}) {
        const auto path = ref_dir / (std::string(fixture.first) + ".HOK");
        if (fs::exists(path)) {
            const HokFile native = decode_hok(read_file(path));
            CHECK(image_competition_factors(native, 0).euro == fixture.second);
        }
    }
    bool rejected = false;
    try { (void)image_competition_factors(source, 6); } catch (const FormatError&) { rejected = true; }
    CHECK(rejected);
}

static void sponsors(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    SponsorTables t = load_sponsor_tables(read_file(em_dir / "EM.EXE"));
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    const size_t blk = 0x0DCD;
    const int stored = le16(h.state, blk + 0x173);
    std::array<int, 9> stadium{};
    for (size_t i = 0; i < 9; i++) stadium[i] = h.state[blk + 0x116 + 2 * i];
    const std::array<int, 2> places = {le16(h.state, blk + 0x133), le16(h.state, blk + 0x137)};
    ImageInputs in;
    in.stadium = stadium_score(t, stadium, places);
    CHECK(in.stadium == 42 && stored == 70);
    const int team = h.state[blk + 0x14];
    in.matchday = h.matchday[0];
    for (int j = std::max(in.matchday - 5, 1); j < in.matchday; j++) {
        const int b = h.state[0x3BAC + size_t(team) * 0x50 + 0x2D + size_t(j / 2)];
        const int position = (b >> (4 * (j % 2))) & 15;
        CHECK(fever_curve_position(h.table[size_t(team)], j) == position);
        in.recent_results.push_back(position);
    }
    for (int slot = 0; slot < kHokSquadSlots; slot++) {
        int p = -1;
        ContractPlayer c = contract_player(h, 0, slot, &p);
        if (p >= 0) in.squad_values.push_back(c.value);
    }
    BorlandRandom rng(17);
    int lo = 100, hi = 0;
    for (int i = 0; i < 50; i++) {
        const int img = club_image(in, rng);
        lo = std::min(lo, img);
        hi = std::max(hi, img);
    }
    std::printf("sponsors: image %d..%d (stored %d)\n", lo, hi, stored);
    CHECK(lo >= 0 && hi <= 100);  // stored image and current calculation have different dates
    ImageInputs fixed;
    fixed.stadium = 42;
    fixed.matchday = 10;
    fixed.recent_results = {6, 5, 3, 3, 0};
    CHECK(club_image(fixed, rng) == 67);  // no squad: S=500+84+126=710, (710/12+20)*85/100
    // Original value-call order: three fixed image draws, two separate value
    // calls for the listed player, ten star thresholds and one star bonus.
    const ContractTables contracts = load_contract_tables(read_file(em_dir / "EM.EXE"));
    std::vector<ContractPlayer> image_squad(10);
    for (auto& player : image_squad) {
        player.age = 45;
        player.position = 75;
        player.team = 7;
        player.value = 2000000000;  // stale cache must not enter the native calculation
    }
    image_squad[0].age = 18;
    image_squad[0].skill1 = image_squad[0].skill2 = 100;
    image_squad[0].flags = 0x80;
    ImageInputs live_values;
    live_values.in_competition = 0;
    live_values.cup = 0;
    live_values.matchday = 1;
    BorlandRandom image_rng(17);
    // The two 97/98-percent values round to 1,179,000 / 1,191,000 raw.
    // Mean 21, one star, bonus 22: S=336+21+22=379, image 43.
    CHECK(club_image(live_values, image_squad, contracts, image_rng) == 43);
    CHECK(image_rng.state() == 3600261441u);
    CHECK(image_squad[0].value == 2000000000);
    SponsorOffers offers{};
    generate_sponsor_offers(t, stored, true, offers, rng);
    for (int k = 0; k < kSponsorKinds; k++)
        for (int i = 0; i < kSponsorOffers; i++) {
            const AdRecord& o = offers[size_t(k)][size_t(i)];
            const long base = 700L * 20 * t.kind_factor[size_t(k)] / 10 * ((i + 3 * (k == 2)) * 8 + 65) * 6 / 100 * 90 / 100;
            CHECK(o.amount >= base * 99 / 100 && o.amount <= base * 22 / 20 * 13 / 10 + 10);
            CHECK((o.months == 12 || o.months == 24) && o.min_image == 70 && o.boards >= 1 && o.boards <= 3);
        }
    std::printf("sponsors: kind 2 offer 0 %ld DM / month for %d months\n", offers[2][0].amount / 3, offers[2][0].months);
    AdRecord c;
    c.min_image = 70;
    CHECK(sponsor_renews(c, 100, rng) && !sponsor_renews(c, 50, rng));
}

// Training (146e:0653 / 00bc): the plan saved in START and MONAT1 (state 0x070C) is the default
// plan; four weeks of it applied to the START squad move condition / technique about as far as
// the original did until MONAT1 (most players lose 0..2 points).
static void training(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK") || !fs::exists(ref_dir / "MONAT1.HOK")) return;
    TrainingTables t = load_training_tables(read_file(em_dir / "EM.EXE"));
    HokFile start = decode_hok(read_file(ref_dir / "START.HOK"));
    HokFile month = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    TrainingPlan plan = default_training_plan(t);
    for (int d = 0; d < kTrainingDays; d++)
        for (int u = 0; u < kTrainingUnits; u++) {
            CHECK(start.state[0x070C + size_t(d * 4 + u)] == plan[size_t(d)][size_t(u)]);
            CHECK(month.state[0x070C + size_t(d * 4 + u)] == plan[size_t(d)][size_t(u)]);
        }
    long sim = 0, real = 0;
    int players = 0;
    BorlandRandom rng(23);
    for (int slot = 0; slot < 25; slot++) {
        const size_t r = 0x696D + size_t(slot) * 0x34;
        const int pl = start.state[r + 0x0F] - 1;
        TrainingPlayer p;
        p.position = std::min(2, start.state[0x4FC6 + size_t(pl) * 0x21 + 0x18] / 25);
        p.condition = start.state[r + 0x10];
        p.technique = start.state[r + 0x11];
        p.nibbles = start.state[r + 0x19];
        p.exhaustion = start.state[r + 0x13];
        p.morale = start.state[r + 0x0E];
        p.trend = start.state[r + 0x14];
        const int before = p.condition + p.technique;
        long change = 0;
        for (int run = 0; run < 20; run++) {
            TrainingPlayer q = p;
            for (int w = 0; w < 4; w++) train_week(t, plan, q, rng);
            change += q.condition + q.technique - before;
        }
        sim += change;
        real += month.state[r + 0x10] + month.state[r + 0x11] - before;
        players++;
    }
    std::printf("training: condition + technique per player after 4 weeks: simulated %.2f, original %.2f\n",
                double(sim) / (20.0 * players), double(real) / players);
    CHECK(double(sim) / (20.0 * players) < 0.5 && double(sim) / (20.0 * players) > -3.0);
    TrainingPlayer m;
    m.morale = 50;
    m.trend = 0x80 | 0x30 | 4;  // up by 3 for 4 weeks
    int up = 0;
    for (int i = 0; i < 100; i++) {
        TrainingPlayer q = m;
        morale_week(q, rng);
        up += q.morale == 53;
        CHECK((q.trend & 15) == 3);
    }
    CHECK(up > 60 && up < 90);
    // 146e:0c89 renews an expired trend via ovl11_307a. The old
    // movement is discarded, including the old morale itself. Borland
    // seed 6 draws 1,49,13,2,1 for the movement check and fresh profile.
    TrainingPlayer expired;
    expired.morale = 68;
    expired.trend = 0xB1;
    BorlandRandom renewal_rng(6);
    morale_week(expired, renewal_rng);
    CHECK(expired.morale == 49 && expired.trend == 0xAD);
    CHECK(renewal_rng.state() == 4226127819u);
    TrainingPlayer other_old = expired;
    other_old.morale = 30;
    other_old.trend = 0x11;
    BorlandRandom same_renewal(6);
    morale_week(other_old, same_renewal);
    CHECK(other_old.morale == expired.morale && other_old.trend == expired.trend);
    TrainingPlayer zero_counter;
    zero_counter.morale = 68;
    zero_counter.trend = 0xB0;
    BorlandRandom zero_rng(6);
    morale_week(zero_counter, zero_rng);
    CHECK(zero_counter.morale == 68 && zero_counter.trend == 0xAF);
    CHECK(zero_rng.state() == 136172863u);  // zero -> -1 does not renew in the original
}

// Season end (ovl02_0000): prize money, retirement chances and the replacement players.
static void season_end() {
    CHECK(!financial_license_due(-2000000, 0, 0, 0));
    CHECK(financial_license_due(-2000001, 0, 0, 0));
    CHECK(financial_license_due(-3000000, 1, 0x10, 1));  // cup flag is not an exemption
    CHECK(!financial_license_due(-3000000, 0, 0x08, 0));
    CHECK(!financial_license_due(-3000000, 1, 0x20, 0));
    CHECK(!financial_license_due(-3000000, 2, 0, 0));
    CHECK(!financial_license_due(-3000000, 0, 0, 2));
    {
        std::array<int, 4> work{11, 9, 12, 19};
        insert_financial_relegation(work, {2, 3, 0, 1}, 0, 7, 0);
        CHECK((work == std::array<int, 4>{7, 9, 12, 19}));  // native LIC_summary working bytes
        work = {0, 1, 12, 13};
        insert_financial_relegation(work, {0, 1, 2, 3}, 0, 7, 0);
        CHECK((work == std::array<int, 4>{12, 1, 7, 13}));
        insert_financial_relegation(work, {0, 1, 2, 3}, 0, 8, 1);
        CHECK((work == std::array<int, 4>{12, 13, 7, 8}));
        work = {12, 13, 7, 9};
        insert_financial_relegation(work, {0, 1, 2, 3}, 0, 9, 0);
        CHECK((work == std::array<int, 4>{12, 13, 9, 9}));  // preserve native duplicate quirk
        bool invalid = false;
        try { insert_financial_relegation(work, {0, 1, 2, 4}, 0, 7, 0); }
        catch (const FormatError&) { invalid = true; }
        CHECK(invalid);
    }
    CHECK(season_prizes(true, 0, true, false, false) / 3 == 600000);
    CHECK(season_prizes(true, 1, false, false, false) / 3 == 300000);
    CHECK(season_prizes(false, 0, true, true, true) == 900000);
    CHECK(season_prizes(false, 0, false, false, false) == 0);
    {
        Bytes state(47761, 0x5A);
        constexpr size_t base = 0x0DCD + 0x18;
        std::fill(state.begin() + base, state.begin() + base + 200, uint8_t(0));
        append_career_season(state, 0, 0, {51, 0, false, 2, false, 30, false});
        CHECK((std::array<uint8_t,4>{state[base],state[base+1],state[base+2],state[base+3]} ==
               std::array<uint8_t,4>{51,0,2,0}));  // native LICPOST2 and RCHPOST1
        append_career_season(state, 0, 1, {53, 0, true, 6, true, 5, true});
        CHECK((std::array<uint8_t,4>{state[base+4],state[base+5],state[base+6],state[base+7]} ==
               std::array<uint8_t,4>{181,0,134,141}));
        for (size_t row = 0; row < 50; row++)
            for (size_t column = 0; column < 4; column++) state[base + row * 4 + column] = uint8_t(row + column);
        state[0x0DCD + 0xEE] = 3;
        append_career_season(state, 0, 50, {12, 2, false, 2, false, 30, false});
        CHECK(state[base] == 1 && state[base+3] == 4 && state[base+48*4] == 49);
        CHECK(state[base+49*4] == 36 && state[base+49*4+1] == 2 && state[base+49*4+2] == 2);
        CHECK(state[base+49*4+3] == 52);  // native leaves the nonparticipant's old Euro byte
        CHECK(state[0x0DCD + 0xEE] == 4);
        CHECK(state[base-1] == 0x5A && state[base+200] == 0x5A);
    }
    {
        Bytes state(47761);
        ManagerBudget budget;
        FinanceTables tables;
        tables.level = 2;
        // All eleven kinds finish together; the second item needs two weeks.
        for (size_t item = 0; item < 11; item++) state[0x0DCD + 0x128 + item] = 1;
        state[0x0DCD + 0x129] = 2;
        for (size_t item = 0; item < 9; item++) state[0x0DCD + 0x117 + item * 2] = uint8_t(item + 1);
        state[0x0DCD + 0x135] = 0x88; state[0x0DCD + 0x136] = 0x13;  // 5000
        state[0x0DCD + 0x139] = 0xC4; state[0x0DCD + 0x13A] = 0x09;  // 2500
        BorlandRandom first(1);
        CHECK(stadium_week(tables, budget, state, 0, first) == 10);
        CHECK(budget.stadium[0] == 1 && budget.stadium[1] == 0 && budget.stadium[8] == 9);
        CHECK(budget.places[0] == 5000 && budget.places[1] == 2500);
        CHECK(state[0x0DCD + 0x129] == 1 && state[0x0DCD + 0x119] == 2);
        CHECK(le16(state, 0x0DCD + 0x135) == 0 && le16(state, 0x0DCD + 0x133) == 5000);
        BorlandRandom second(1);
        CHECK(stadium_week(tables, budget, state, 0, second) == 1);
        CHECK(budget.stadium[1] == 2 && state[0x0DCD + 0x119] == 0);
    }
    {
        // Native scorer sort: points, then fewer games; goalkeeper ratio is
        // goals against * 6000 / minutes and requires at least 399 minutes.
        std::vector<SeasonScorer> players{
            {7, 80, 30, 20, 40}, {8, 80, 25, 25, 39},
            {7, 0, 20, 600, 40}, {8, 0, 30, 1200, 40},
            {9, 0, 0, 398, 1}, {13, 80, 200, 0, 1}};
        CHECK((season_scoring_winners(players, 0, 11) == std::array<int, 2>{8, 8}));
        players[0].games = 38;
        players[2].goals = 10;
        CHECK((season_scoring_winners(players, 0, 11) == std::array<int, 2>{7, 7}));
        CHECK((season_scoring_winners(players, 12, 23) == std::array<int, 2>{13, -1}));
        players[0].goals = players[0].assists_or_minutes = 0;
        players[1].goals = 1; players[1].assists_or_minutes = 0;
        CHECK(season_scoring_winners(players, 0, 11)[0] == -1);
        players[2].assists_or_minutes = 399;
        players[3].assists_or_minutes = 398;
        CHECK(season_scoring_winners(players, 0, 11)[1] == 7);
    }
    BorlandRandom rng(31);
    int at37 = 0, at40 = 0, at43 = 0;
    for (int i = 0; i < 6000; i++) {
        at37 += player_retires(37, rng);
        at40 += player_retires(40, rng);
        at43 += player_retires(43, rng);
    }
    std::printf("season end: retire at 37/40/43: %d/%d/%d of 6000\n", at37, at40, at43);
    CHECK(at37 == 0 && at43 == 6000 && at40 > 2500 && at40 < 3500);
    for (int i = 0; i < 500; i++) {
        NewPlayer p = new_player(rng);
        CHECK(p.age >= 18 && p.age <= 25 && p.skill1 >= 35 && p.skill1 <= 97 && std::abs(p.skill1 - p.skill2) <= 10);
    }
}

// Day loop (game/game.hpp): 70 days from START reach 18.11.1993; every team of the three
// leagues has played the 19 matchdays of that span, the cup's first round is decided, the
// balance moves with the month ends and home games, and the image stays in 0..100.
static void day_loop(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 5);
    const long start = g.budget().balance;
    int home_games = 0;
    for (int i = 0; i < 70; i++) {
        g.advance_day();
        for (const std::string& line : g.log()) home_games += line.find("spectators") != std::string::npos;
        g.clear_log();
    }
    const GameDate d = g.date();
    CHECK(g.season_day() == 70 && d.day == 18 && d.month == 11);
    for (int l = 0; l < 3; l++) {
        CHECK(g.next_matchday(l) == 20);
    }
    {  // the pair-table error of the original moves games between teams (DEVIATIONS.md), the total stays
        int all = 0;
        for (int l = 0; l < 3; l++) for (const HokTableRow& r : g.league(l).rows) all += r.total(r.games);
        CHECK(all == 36 * 19);
    }
    CHECK(g.budget().home_games == g.league(0).rows[size_t(g.human_team())].games[0]);
    CHECK(g.budget().home_games == 9 && home_games >= g.budget().home_games);
    CHECK(home_games >= 9 && home_games <= 15);  // nine league homes plus up to six cup homes
    CHECK(g.budget().balance != start && g.image() > 0 && g.image() <= 100);
    std::printf("day loop: 70 days, %d home games, balance %ld -> %ld DM, image %d\n", home_games, start / 3,
                g.budget().balance / 3, g.image());
}

// ovl14_2630 records only league home attendance, including the 22 graph bytes.
// The September cup home still sells tickets; it must not shift that graph.
static void attendance_history(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game game(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 6);
    constexpr size_t graph = 0x0DCD + 0xFE;
    int ticket_games = 0;
    for (int day = 0; day < 31; day++) {
        const int count = game.budget().home_games;
        const long total = game.budget().attendance_total;
        const Bytes before = game.save().state;
        game.advance_day();
        for (const auto& line : game.log()) ticket_games += line.find("spectators") != std::string::npos;
        game.clear_log();
        CHECK(game.budget().home_games == game.league(0).rows[size_t(game.human_team())].games[0]);
        for (size_t slot = 0; slot < 22; slot++) {
            if (game.budget().home_games == count + 1 && slot == size_t(count))
                CHECK(game.save().state[graph + slot] == (game.budget().attendance_total - total) / 1000);
            else CHECK(game.save().state[graph + slot] == before[graph + slot]);
        }
    }
    CHECK(ticket_games == 5 && game.budget().home_games == 4);
    //The former1495754 was a historical synthetic RNG snapshot, not a
    //native cash reference. The recovered eleven daily construction-reset
    //draws legitimately change this trajectory. Still require that updating
    //the attendance graph never makes exported money disagree with the model.
    CHECK(int32_t(le32(decode_hok(game.save_bytes()).state,0x0DCD+0x18F))==game.budget().balance);
    std::printf("attendance history: 4 league homes, 5 ticket sales; cup excluded from graph\n");
}

// A whole season from START (232 days): 44 matchdays per league, a European cup winner from the final
// round of four, a German champion from the first eight after three best-of-five rounds.
static void full_season(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 9);
    for (int i = 0; i < 231; i++) {  // day 231 (28.04.) is the last one played; the next call changes the season (REAL-CODE period 30)
        g.advance_day();
        g.clear_log();
    }
    {  // the pair-table error of the original moves games between teams (DEVIATIONS.md), the total stays
        int all = 0;
        for (int l = 0; l < 3; l++) for (const HokTableRow& r : g.league(l).rows) all += r.total(r.games);
        CHECK(all == 36 * 44);
    }
    CHECK(g.euro_champion() >= 0);
    const League& first = g.league(0);
    CHECK(g.champion() >= first.first_team && g.champion() < first.first_team + kHokLeagueSize &&
          first.rows[size_t(g.champion() - first.first_team)].position < 8);
    // world championship: every nation played five group games, places 1-8 are eight different
    // nations and the champion is first of them
    for (int grp = 0; grp < 2; grp++) {
        const auto& t = g.world_group(grp);
        int points = 0;
        for (size_t i = 0; i < 6; i++) {
            CHECK(t.teams[i] == 52 + grp * 6 + int(i));
            points += t.points[i];
        }
        CHECK(points == 15 * 2);  // 15 games, two points each
    }
    const std::vector<int>& wm = g.world_ranking();
    CHECK(wm.size() == 8 && g.world_champion() == wm[0]);
    std::vector<int> sorted = wm;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    for (int t : wm) CHECK(t >= 52 && t < 64);
    std::printf("full season: European cup %s, champion %s, world champion %s\n",
                g.save().teams[size_t(g.euro_champion())].name.c_str(), g.save().teams[size_t(g.champion())].name.c_str(),
                wm.empty() ? "-" : g.save().teams[size_t(wm[0])].name.c_str());
}

// Season change: after the last day the relegation group winners from below play in the upper
// division (team indices swapped like ovl06_0c29), the year advances, tables start empty and
// the second season plays its 44 matchdays again.
static void season_change(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 4);
    g.set_database(decode_db(read_file(em_dir / "KB" / "23.vga")));
    for (int i = 0; i < 231; i++) {  // the WM final on day 231; the next call changes the season
        g.advance_day();
        g.clear_log();
    }
    std::vector<std::string> promoted;
    for (int grp = 0; grp < 2; grp++)
        for (int i = 0; i < 2; i++) {
            const int t = g.relegation_order(grp)[size_t(i)];
            if (t >= 12 * (grp + 1)) promoted.push_back(g.save().teams[size_t(t)].name);
        }
    const std::string human = g.save().teams[size_t(g.human_team())].name;
    CHECK(g.champion() >= 0);
    const std::string champion = g.save().teams[size_t(g.champion())].name;
    CHECK(g.cup_winner() >= 0);
    const std::string cup_winner = g.save().teams[size_t(g.cup_winner())].name;
    std::vector<std::pair<std::string, Bytes>> nation_history;
    for (size_t nation = 52; nation < 64; nation++) {
        const auto h = g.save().state.begin() + long(0x3BD9 + nation * 0x50);
        nation_history.push_back({g.save().teams[nation].name, Bytes(h, h + 16)});
    }
    const HokTableRow own = g.league(g.human_team() / 12).rows[size_t(g.human_team() % 12)];  // for the balance
    std::vector<std::array<int, 3>> before;  // age, skills of every player record
    for (const HokPlayer& pl : g.save().players) before.push_back({pl.age, pl.skill_a, pl.skill_b});
    std::map<size_t, std::string> listed_names;
    for (const auto& pl : g.save().players)
        if (pl.raw[0x1A] == 4) listed_names[size_t(pl.raw - g.save().state.data() - 0x4FC6) / 33] = pl.name;
    std::vector<std::string> oberliga_down;  // 11th and 12th go to the regional league
    for (int s = 0; s < 12; s++)
        if (g.league(2).rows[size_t(s)].position >= 10) oberliga_down.push_back(g.save().teams[size_t(24 + s)].name);
    const int euro = g.euro_champion();  // a German European cup winner enters again (ovl05_51a5)
    const std::string euro_winner = euro >= 0 && euro < 64 ? g.save().teams[size_t(euro)].name : champion;
    g.advance_day();  // the season change stops in the menu before the first matchday (REAL-CODE period 30)
    CHECK(g.seasons_completed() == 1 && g.season_day() == 0);
    g.advance_day();  // new season, day 1
    CHECK(g.seasons_completed() == 1 && g.save().year == 1994 && g.season_day() == 1);
    const HokFile next_titles = decode_hok(g.save_bytes());
    CHECK(next_titles.teams[next_titles.state[0x0DC7]].name == cup_winner);
    CHECK(next_titles.teams[next_titles.state[0x0DC8]].name == champion);
    for (size_t nation = 52; nation < 64; nation++) {
        const auto h = next_titles.state.begin() + long(0x3BD9 + nation * 0x50);
        const auto old = std::find_if(nation_history.begin(), nation_history.end(), [&](const auto& entry) {
            return entry.first == next_titles.teams[nation].name;
        });
        CHECK(old != nation_history.end() && Bytes(h, h + 16) == old->second);
    }
    {  // skills move by rand(-3, 3) within 30..99 (players not replaced: one year older)
        int moved = 0, kept = 0, bad = 0;
        for (size_t i = 0; i < before.size() && i < g.save().players.size(); i++) {
            const HokPlayer& pl = g.save().players[i];
            if (pl.age != before[i][0] + 1) continue;
            kept++;
            const int d1 = pl.skill_a - before[i][1], d2 = pl.skill_b - before[i][2];
            if (std::abs(d1) > 15 && std::abs(d2) > 15) continue;  // a slot refilled by a replacement of the same age (not the same player)
            if (!(d1 >= -3 && d1 <= 3 && d2 >= -3 && d2 <= 3 && pl.skill_a >= 30 && pl.skill_b <= 99)) bad++;  // a transfer-list refresh regenerates the skills of a player (ovr035:381E)
            moved += d1 != 0 || d2 != 0;
        }
        CHECK(kept > 150 && moved > kept * 3 / 4 && bad <= kept / 10);
    }
    // ovl16_0480 assigns DB names before the late1238/146E club allocation.
    // That later phase changes only the club, so names can belong to another
    // DB roster. Their original DB foreigner byte and uniqueness still hold.
    {
        const Database db = decode_db(read_file(em_dir / "KB" / "23.vga"));
        std::map<std::string, int> names;
        int free_players = 0, from_roster = 0, market_releases = 0;
        for (const HokPlayer& pl : g.save().players) {
            names[pl.name]++;
            const size_t rec = size_t(pl.raw - g.save().state.data() - 0x4FC6) / 0x21;
            if (pl.raw[0x1A] != 6 || pl.team == g.human_team()) continue;
            // Owner 4 is excluded from ovl16_0480. The next day's transfer refresh
            // may release him to owner 6 afterwards, retaining his previous name.
            if (const auto old = listed_names.find(rec); old != listed_names.end() && old->second == pl.name) {
                market_releases++;
                continue;
            }
            free_players++;
            const bool nation = rec >= 179;  // 178 is in both ranges; mode 1 runs last
            CHECK(nation ? pl.team >= 52 && pl.team <= 63 : pl.team >= 0 && pl.team <= 42);
            const int old_count = from_roster;
            bool db_match=false;
            for(const DbClub& c:db.clubs)for(const DbPlayer& d:c.players)
                db_match|=d.name==pl.name&&pl.raw[0x13]==d.copy[19];
            from_roster+=db_match;
            if (from_roster - old_count != 1)
                std::fprintf(stderr, "free player %zu %s team %d %s: %d roster matches\n", rec, pl.name.c_str(),
                             pl.team, g.save().teams[size_t(pl.team)].name.c_str(), from_roster - old_count);
        }
        CHECK(free_players + market_releases > 150 && from_roster == free_players);
        for (const auto& [name, n] : names) CHECK(n == 1);
        std::printf("season change: %d free players renamed; %d later market releases retain their names\n", from_roster, market_releases);
    }
    {  // manager balance (state 0x0F08, u16 pairs home/away) = the club's season; one title each
        const Bytes& st = g.save().state;
        CHECK(le16(st, 0x0F08) == 2 * own.wins[0] + own.draws(0) && le16(st, 0x0F0C) == own.goals_for[0] &&
              le16(st, 0x0F12) == own.goals_against[1] && le16(st, 0x0F1C) == own.wins[0] && le16(st, 0x0F22) == own.losses[1]);
        int titles[3] = {};
        for (size_t t = 0; t < 48; t++)
            for (size_t k = 0; k < 3; k++) titles[k] += st[0x3BA5 + t * 0x50 + k];
        CHECK(titles[0] == 1 && titles[1] == 1);
    }
    {  // ovl14_47ea: eternal table after the first season (u32 at state 0x3BF1 + team * 0x50)
        auto eternal = [&](size_t t) { return int(le32(g.save().state, 0x3BF1 + t * 0x50)); };
        for (size_t t = 0; t < 24; t++) {
            const int v = eternal(t), lo = t < 12 ? 41 : 21, hi = t < 12 ? 52 + 15 + 25 + 40 + 10 : 35 + 25 + 40;
            CHECK(v >= lo && v <= hi);
        }
        int best = 0;
        for (size_t t = 0; t < 12; t++) best = std::max(best, eternal(t));
        CHECK(best >= 66);  // the champion: 40 + 12 - position + 15 + 10, position < 8
        std::printf("season change: eternal table best %d, first division %d..%d\n", best,
                    eternal(0), eternal(11));
    }
    CHECK(oberliga_down.size() == 2);
    for (const std::string& name : oberliga_down) {
        int idx = -1;
        for (size_t t = 0; t < 52; t++)
            if (g.save().teams[t].name == name) idx = int(t);
        CHECK(idx >= 36 && idx <= 41);  // regional slots of the draw rand(36, 41)
    }
    // European cup draw (ovl05_4c7b(1)): eight different clubs, the German champion among them
    // (and a German European cup winner), the others European clubs; every pair of a group
    // meets twice in the six rounds
    std::vector<int> drawn;
    int german = 0;
    for (const auto& grp : g.save().euro_groups)
        for (int t : grp) {
            drawn.push_back(t);
            CHECK(t >= 64 || (t < 52 && (g.save().teams[size_t(t)].name == champion ||
                                         g.save().teams[size_t(t)].name == euro_winner)));
            german += t < 64;
        }
    CHECK(german == (euro_winner == champion ? 1 : 2));
    std::sort(drawn.begin(), drawn.end());
    CHECK(std::adjacent_find(drawn.begin(), drawn.end()) == drawn.end());
    for (size_t grp = 0; grp < 2; grp++) {
        std::map<std::pair<int, int>, int> meet;
        for (const auto& round : g.save().euro_rounds[grp])
            for (const HokPairing& p : round) meet[{std::min(p.home, p.away), std::max(p.home, p.away)}]++;
        CHECK(meet.size() == 6);
        for (const auto& [pair, n] : meet) CHECK(n == 2);
    }
    // saved and decoded again, the draw stays
    const HokFile again = decode_hok(g.save_bytes());
    CHECK(again.euro_groups == g.save().euro_groups);
    CHECK(std::find(drawn.begin(), drawn.end(), int(again.state[0x0DC8])) != drawn.end());  // DS:D0E6
    CHECK(again.state[0x8E33] == again.state[0x0DC8] && again.state[0x8E3D] == again.state[0x0DC8]);
    CHECK(again.state[0x8E3D + size_t(german)] == 0x80);
    std::printf("season change: European cup groups");
    for (const auto& grp : again.euro_groups)
        for (int t : grp) std::printf(" %s;", again.teams[size_t(t)].name.c_str());
    std::printf("\n");
    for (const std::string& name : promoted) {
        int idx = -1;
        for (size_t t = 0; t < 36; t++)
            if (g.save().teams[t].name == name) idx = int(t);
        CHECK(idx >= 0 && idx < 24);
    }
    CHECK(g.save().teams[size_t(g.human_team())].name == human);
    for (int i = 1; i < 231; i++) {
        g.advance_day();
        g.clear_log();
    }
    {  // the pair-table error of the original moves games between teams (DEVIATIONS.md), the total stays
        int all = 0;
        for (int l = 0; l < 3; l++) for (const HokTableRow& r : g.league(l).rows) all += r.total(r.games);
        CHECK(all == 36 * 44);
    }
    for (int t = 0; t < 36; t++) {  // new season values in the class bounds (162c:1c5b)
        if (t == g.human_team()) continue;  // follows its squad (162c:146c)
        const HokTeam& tm = g.save().teams[size_t(t)];
        // the twice-weekly drift of the daily loop (162c:1c5b mode 1) lets the values wander over the season: only 1..99 holds
        const int lo = 1, hi = 99;
        for (int k = 0; k < 2; k++)
            CHECK(tm.skill1[k] >= lo && tm.skill1[k] <= hi && tm.skill2[k] >= lo && tm.skill2[k] <= hi);
    }
    std::printf("season change: %zu promoted, second season played to day %d\n", promoted.size(), g.season_day());
}

// HOK writer: decode -> store_hok -> encode_hok gives every reference save back byte for byte.
// Transfer list refresh in the day loop (ovl11_34ef): after 60 days the list holds only listed
// players (+0x1A = 4) of computer clubs, sorted by class, with value and salary from ovl11_3d36,
// and every listed player is on the list.
// A regional manager has no row in any of the three simulated league tables.
// This controlled import exercises the image update without inventing league results.
static void regional_manager_image(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile h = decode_hok(read_file(ref_dir / "START.HOK"));
    const int old_team = h.state[0x0DE1], regional = 36;
    std::copy_n(h.teams[size_t(regional)].raw, 19, h.header.begin() + 0x25);
    h.state[0x0DE1] = uint8_t(regional);
    h.state[0x0DCD + 0xEB] = 2;
    for (auto& player : h.players) if (player.team == old_team && player.raw[0x1A] == 0) player.team = regional;
    const size_t old_record = size_t(h.teams[size_t(old_team)].raw - h.state.data());
    const size_t new_record = size_t(h.teams[size_t(regional)].raw - h.state.data());
    h.state[old_record + 0x16] = 0; h.state[old_record + 0x34] &= 0x7F;
    h.state[new_record + 0x16] = 1; h.state[new_record + 0x34] |= 0x80;
    store_hok(h);
    Game game(exe, encode_hok(h), 1);
    CHECK(game.human_team() == regional);
    for (int day = 0; day < 31; day++) { game.advance_day(); game.clear_log(); }
    CHECK(game.image() >= 0 && game.image() <= 100);
    for (size_t slot = 0; slot < h.squad.size(); slot++)
        CHECK(game.save().squad[slot].league_games == h.squad[slot].league_games);
    const Bytes saved = game.save_bytes();
    Game restored(exe, saved, 1);
    CHECK(restored.human_team() == regional && restored.save_bytes() == saved);
    restored.advance_day();
    std::printf("regional manager: image update without a league row, save/load and continuation checked\n");
}

static void financial_license_save(const fs::path& em_dir, const fs::path& ref_dir) {
    const Bytes exe = read_file(em_dir / "EM.EXE");
    for (const char* name : {"LICCHK22", "LICRICH1", "LICCUP02"}) {
        const fs::path path = ref_dir / (std::string(name) + ".HOK");
        if (!fs::exists(path)) continue;
        const HokFile source = decode_hok(read_file(path));
        const bool indebted = name == std::string("LICCHK22");
        const bool cup_rescue = name == std::string("LICCUP02");
        if (cup_rescue) {
            Game april(exe, read_file(path), 6);
            for (int day = 0; day < 13; day++) april.advance_day();
            // Without a national coach the squad day (sub_15369, with the training) runs on the WM match dates of
            // the weekly rhythm only: 217 and 221 up to day 229, then 231 (REAL-CODE period 30, whose trend nibbles
            // fall by 3 up to the season change). Five blanket weekly calls were incorrect.
            const HokFile before_review = decode_hok(april.save_bytes());
            for (int slot : {1, 2, 3, 4, 5, 6}) {
                const size_t at = 0x696D + size_t(slot) * 0x34 + 0x14;
                CHECK((before_review.state[at] & 15) == (source.state[at] & 15) - 2);
            }
        }
        Game game(exe, read_file(path), 6);
        for (int day = 0; day < 20 && game.seasons_completed() == 0; day++) game.advance_day();
        CHECK(game.seasons_completed() == 1);
        CHECK(game.save().teams[size_t(game.human_team())].name == source.club);
        CHECK(game.human_team() / 12 == (indebted ? 1 : 0));
        const HokFile saved = decode_hok(game.save_bytes());
        CHECK(le16(saved.state, 0xB951) == le16(source.state, 0xB951) + 1);
        const char* native_name = indebted ? "LICPOST2" : cup_rescue ? "CUPPOS02" : "RCHPOST1";
        if (fs::exists(ref_dir / (std::string(native_name) + ".HOK"))) {
            const HokFile native = decode_hok(read_file(ref_dir / (std::string(native_name) + ".HOK")));
            CHECK(std::equal(saved.state.begin() + 0x0DE5, saved.state.begin() + 0x0DE9,
                             native.state.begin() + 0x0DE5));
        }
        CHECK(saved.state[0x0DCD + 0xF3] == (indebted ? 0x04 : cup_rescue ? 0x10 : 0));
        CHECK(saved.state[0x0400] == (indebted ? 7 : 11));
        CHECK(std::equal(source.state.begin() + 0x039C, source.state.begin() + 0x03A4,
                         saved.state.begin() + 0x039C));
        // Five month starts pass during the summer (May through September).
        for (size_t slot = 0; slot < 11; slot++) {
            const size_t at = 0xACFC + slot * 10;
            if (le32(source.state, at) != 0)
                CHECK(saved.state[at + 5] == std::max(0, int(source.state[at + 5]) - 5));
        }
        CHECK(game.budget().balance < (cup_rescue ? 0 : indebted ? -3000000 : 30000000));
        CHECK(game.budget().home_games <= 1);
        CHECK(game.budget().attendance_total == game.budget().attendance_record);
        CHECK(game.budget().attendance_min == (game.budget().home_games ? game.budget().attendance_total : 99999));
        // Native NEUSAIS keeps the old graph until each new league home overwrites its entry.
        for (size_t i = size_t(game.budget().home_games); i < 22; i++)
            CHECK(saved.state[0x0DCD + 0xFE + i] == source.state[0x0DCD + 0xFE + i]);
        Game restored(exe, game.checkpoint_bytes(), 999);
        CHECK(restored.save_bytes() == game.save_bytes());
        game.advance_day(); restored.advance_day();
        CHECK(restored.save_bytes() == game.save_bytes());
    }
}

static void human_promotion(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "PROMO01.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    Game game(exe, read_file(ref_dir / "PROMO01.HOK"), 6);
    CHECK(game.human_team() == 12 && game.budget().league == 1);
    while (game.seasons_completed() == 0) game.advance_day();
    const HokFile saved = decode_hok(game.save_bytes());
    CHECK(game.budget().league == 0 && game.human_team() / 12 == 0);
    CHECK(saved.state[0x0DCD + 0xF3] == 1);
    CHECK((std::array<uint8_t,4>{saved.state[0x0DE5], saved.state[0x0DE6],
                               saved.state[0x0DE7], saved.state[0x0DE8]} ==
           std::array<uint8_t,4>{13,1,2,0}));
    CHECK(std::find(game.log().begin(), game.log().end(), "prize money 700000 DM") != game.log().end());
    // PROMO02_personal confirms the option bit in all eleven slots, even empty ones.
    for (size_t slot = 0; slot < 11; slot++) CHECK(saved.state[0xACFC + slot * 10 + 6] & 0x80);
    if (fs::exists(ref_dir / "PROMPOS1.HOK")) {
        const HokFile actual = decode_hok(read_file(ref_dir / "PROMPOS1.HOK"));
        CHECK(actual.state[0x0DCD + 0xF3] == saved.state[0x0DCD + 0xF3]);
        CHECK(actual.state[0x0DCD + 0xEB] == 0);
        CHECK(std::equal(saved.state.begin() + 0x0DE5, saved.state.begin() + 0x0DE9,
                         actual.state.begin() + 0x0DE5));
        for (size_t slot = 0; slot < 11; slot++) CHECK(actual.state[0xACFC + slot * 10 + 6] & 0x80);
    }
    Game resumed(exe, game.checkpoint_bytes(), 999);
    for (int day = 0; day < 20; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.save_bytes() == resumed.save_bytes());
}

static void human_oberliga_relegation(const fs::path& em_dir, const fs::path& ref_dir) {
    const fs::path path = ref_dir / "OLHUM01.HOK";
    if (!fs::exists(path)) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const HokFile source = decode_hok(read_file(path));
    CHECK(source.state[0x0DE1] == 27 && source.table[27].position == 11);
    Game game(exe, read_file(path), 6);
    for (int day = 0; day < 20 && game.seasons_completed() == 0; day++) game.advance_day();
    CHECK(game.seasons_completed() == 1);
    CHECK(game.human_team() >= 24 && game.human_team() < 36);
    CHECK(game.save().teams[size_t(game.human_team())].name == source.club);
    const HokFile saved = decode_hok(game.save_bytes());
    CHECK(saved.state[0x0EC0] == 3 && saved.state[0x0EB8] == 2);
    CHECK(le16(saved.state, 0xB951) == 1);
    CHECK((std::array<uint8_t,4>{saved.state[0x0DE5],saved.state[0x0DE6],saved.state[0x0DE7],saved.state[0x0DE8]} ==
           std::array<uint8_t,4>{36,2,2,0}));  // native OLPOST01
    // Native OL01_overall resets the previously positive 30M account to
    // 1.2M raw. The summer then consumes money; no promotion bonus applies.
    CHECK(game.budget().balance < 1200000);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    CHECK(resumed.save_bytes() == game.save_bytes());

    for (int day = 0; day < 20; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(resumed.save_bytes() == game.save_bytes());
}

static void native_summer_finances(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "OLPERS01.HOK") || !fs::exists(ref_dir / "OLPOST01.HOK")) return;
    // OLPERS01 is explicitly RAM-derived during the native personal review;
    // OLPOST01 is the original dialog's September save from that same run.
    const HokFile personal = decode_hok(read_file(ref_dir / "OLPERS01.HOK"));
    const HokFile september = decode_hok(read_file(ref_dir / "OLPOST01.HOK"));
    const FinanceTables tables = load_finance_tables(read_file(em_dir / "EM.EXE"));
    ManagerBudget replay = budget_from_save(personal, 0);
    const ManagerBudget expected = budget_from_save(september, 0);
    CHECK(replay.balance == 1500000 && replay.team_strength == 172 && replay.players == 25);
    CHECK(monthly_income(replay, false) == 21500);
    CHECK(monthly_expenses(tables, replay, false, false) == 1213400);
    CHECK(replay.stadium == expected.stadium && replay.places == expected.places);
    CHECK(replay.squad_costs == expected.squad_costs && replay.pool_costs == expected.pool_costs);
    constexpr int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (int day = 234; day <= 365; day++) {
        const GameDate date = date_of_season_day(day, personal.season_year);
        finance_day(tables, replay, date.day, month_days[date.month - 1]);
    }
    finance_day(tables, replay, 10, 30);
    CHECK(replay.balance == expected.balance && replay.balance == -3562565);
    CHECK(replay.accrued == expected.accrued && replay.accrued == -114920);
    CHECK(replay.stored_income == expected.stored_income && replay.stored_income == 21500);
    for (size_t slot = 0; slot < replay.adverts.size(); slot++)
        CHECK(replay.adverts[slot].months_left == expected.adverts[slot].months_left);
    std::printf("native summer: 132 days and September 10, balance %ld, accrued %ld, income %ld exact\n",
                replay.balance, replay.accrued, replay.stored_income);
}

static void summer_healing_and_construction(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "LICRICH1.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile source = decode_hok(read_file(ref_dir / "LICRICH1.HOK"));
    // Two prepared healing/stadium dates remain in April, then nineteen
    // multiples of seven in the native May 1..September 9 summer interval.
    source.state[0x696D + 9] |= 2;
    source.state[0x696D + 13] = 22;
    source.state[0x696D + 0x34 + 9] |= 2;
    source.state[0x696D + 0x34 + 13] = 255;
    const int disabled_id = source.state[0x696D + 0x34 + 15];
    source.state[0x0DCD + 0x116] = 0;
    source.state[0x0DCD + 0x117] = 2;
    source.state[0x0DCD + 0x128] = 21;
    Game game(exe, encode_hok(source), 6);
    for (int day = 0; day < 20 && game.seasons_completed() == 0; day++) {
        game.advance_day();
        if (game.seasons_completed() == 0) CHECK(game.save().state[0x696D + 0x34 + 13] == 255);
    }
    CHECK(game.seasons_completed() == 1);
    const HokFile saved = decode_hok(game.save_bytes());
    CHECK(saved.state[0x696D + 13] == 1 && (saved.state[0x696D + 9] & 2));
    CHECK(saved.squad.size() == source.squad.size() - 1);
    for (size_t slot = 0; slot < saved.squad.size(); slot++) CHECK(saved.state[0x696D + slot * 0x34 + 15] != disabled_id);
    CHECK(saved.state[0x0DCD + 0x128] == 0 && saved.state[0x0DCD + 0x117] == 0);
    CHECK(saved.state[0x0DCD + 0x116] == 2 && game.budget().stadium[0] == 2);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    CHECK(resumed.save_bytes() == game.save_bytes());
    game.advance_day(); resumed.advance_day();
    CHECK(resumed.save_bytes() == game.save_bytes());
}

static void february_sponsor_boundary(const fs::path& em_dir, const fs::path& ref_dir) {
    const fs::path path = ref_dir / "MONAT4.HOK";
    if (!fs::exists(path)) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const HokFile source = decode_hok(read_file(path));
    CHECK(source.date.day == 2 && source.date.month == 2);
    Game game(exe, read_file(path), 6);
    for (int day = source.date.day; day < 13; day++) game.advance_day();
    const HokFile before = decode_hok(game.save_bytes());
    CHECK(before.date.day == 13 && before.date.month == 2);
    CHECK(std::equal(source.state.begin() + 0xA52C, source.state.begin() + 0xA52C + 500,
                     before.state.begin() + 0xA52C));
    Game resumed(exe, game.checkpoint_bytes(), 999);
    game.advance_day(); resumed.advance_day();
    const HokFile midpoint = decode_hok(game.save_bytes());
    CHECK(midpoint.date.day == 14 && midpoint.date.month == 2);
    CHECK(!std::equal(before.state.begin() + 0xA52C, before.state.begin() + 0xA52C + 500,
                      midpoint.state.begin() + 0xA52C));
    CHECK(resumed.save_bytes() == game.save_bytes());
}

static void saved_economy(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT4.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile source = decode_hok(read_file(ref_dir / "MONAT4.HOK"));
    // Empty initial offers isolate newly generated prices from retained offers.
    std::fill(source.state.begin() + 0xA52C, source.state.begin() + 0xA52C + 500, uint8_t(0));
    Game normal(exe, encode_hok(source), 6);
    for (int byte = 0; byte < 4; byte++) source.state[0x06B4 + size_t(byte)] = byte == 0 ? 20 : 0;
    Game zero_margin(exe, encode_hok(source), 6);
    for (int day = source.date.day; day < 14; day++) { normal.advance_day(); zero_margin.advance_day(); }
    int positive = 0;
    for (size_t kind = 0; kind < 5; kind++)
        for (size_t index = 0; index < 10; index++) {
            const auto& a = normal.sponsor_offers()[kind][index];
            const auto& b = zero_margin.sponsor_offers()[kind][index];
            positive += a.amount > 0;
            CHECK(b.amount == 0);  // native coefficient (saved economy - 20) is zero
            CHECK(a.boards == b.boards && a.months == b.months && a.renewal == b.renewal &&
                  a.sponsor == b.sponsor && a.min_image == b.min_image);
        }
    CHECK(positive > 0 && normal.image() == zero_margin.image());
}

static void forfeits(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile initial = decode_hok(read_file(ref_dir / "START.HOK"));
    HumanSquad original{&initial.squad, &initial.players};
    CHECK(squad_match_load(initial.teams[7], original) == 21);  // native START -> TAG1
    {
        std::vector<HokPlayer> players(9);
        std::vector<HokSquadPlayer> squad(9);
        for (size_t slot = 0; slot < 9; slot++) {
            players[slot].position = 75;
            squad[slot].player = int(slot);
            squad[slot].role = 1; squad[slot].line = 129;
            squad[slot].skill_a = 5;
        }
        HumanSquad human{&squad, &players};
        CHECK(squad_match_load(initial.teams[7], human) == 29);  // first line is counted three times
        for (auto& player : squad) { player.skill_a = 0; player.skill_b = 5; }
        CHECK(squad_match_load(initial.teams[7], human) == 11);
        for (auto& player : squad) player.skill_b = 99;
        CHECK(squad_match_load(initial.teams[7], human) == 0);
        for (auto& player : squad) { player.skill_b = 0; player.skill_a = 99; }
        CHECK(squad_match_load(initial.teams[7], human) == 40);
        squad[8].line = 0;
        CHECK(squad_match_load(initial.teams[7], human) == 100);
    }
    std::vector<HokSquadPlayer> selected(30, initial.squad.front());
    for (auto& p : selected) { p.role = 0; p.line = 0; }
    for (size_t i = 0; i < 8; i++) { selected[i].role = 1; selected[i].line = 1; }
    selected[8].role = 1;  // a bench player does not count
    selected[29].role = selected[29].line = 1;  // nor does the scratch record
    HumanSquad human{&selected, &initial.players};
    human.minimum_players = 9;
    BorlandRandom player_rng(6);
    human.selection_rng = &player_rng;
    CHECK(selected_match_players(human) == 8);
    for (bool home : {false, true}) {
        GameRandom match_rng(6);
        MatchStats stats;
        const auto result = league_match(initial.teams[6], initial.teams[7], match_rng, 0,
                                          home ? &human : nullptr, home ? nullptr : &human, &stats);
        CHECK(result.home == (home ? 0 : 2) && result.away == (home ? 2 : 0));
        CHECK(stats.forfeit_side == (home ? 1 : 2));
        CHECK(match_rng.state() == 6 && player_rng.state() == 6);
        CHECK(stats.final_squads[0].empty() && stats.final_squads[1].empty());
        CHECK(!result.shootout && result.decision_marker == 0);
    }
    selected[8].line = 1;
    CHECK(selected_match_players(human) == 9);
    GameRandom enough_rng(6);
    MatchStats enough;
    league_match(initial.teams[6], initial.teams[7], enough_rng, 0, &human, nullptr, &enough);
    CHECK(enough.forfeit_side == 0 && enough_rng.state() != 6);

    const SeasonCalendar calendar = load_season_calendar(exe);
    struct CompetitionProbe { const char* source; uint8_t event; bool national; bool relegation; };
    for (const auto probe : {CompetitionProbe{"START", kEventCup, false, false},
                             CompetitionProbe{"EUOWN", kEventEuroCup, false, false},
                             CompetitionProbe{"POSEMI1", kEventPlayoff, false, false},
                             CompetitionProbe{"START", kEventPlayoff, false, true},
                             CompetitionProbe{"NATAUTO", kEventWorldChampionship, true, false}}) {
        const auto path = ref_dir / (std::string(probe.source) + ".HOK");
        if (!fs::exists(path)) continue;
        Game preparation(exe, read_file(path), 6);
        int target = preparation.season_day() + 1;
        while (target < int(calendar.events.size()) && calendar.event(target) != probe.event) target++;
        CHECK(target < int(calendar.events.size()));
        while (preparation.season_day() < target - 1) preparation.advance_day();
        HokFile depleted = decode_hok(preparation.save_bytes());
        if (probe.relegation) {
            const int own = preparation.human_team();
            const int previous = depleted.table[size_t(own)].position;
            for (int team = 0; team < 12; team++)
                if (depleted.table[size_t(team)].position == 10) depleted.table[size_t(team)].position = previous;
            depleted.table[size_t(own)].position = 10;
        }
        const int manager = probe.national ? 5 : 0;
        const size_t block = 0x696D + size_t(manager) * 0x618;
        depleted.state[0x078A + size_t(manager) * 2] = 0;
        depleted.state[0x078B + size_t(manager) * 2] = 0;
        for (size_t slot = 0; slot < 29; slot++) {
            depleted.state[block + slot * 52 + 10] = 0;
            depleted.state[block + slot * 52 + 21] = 0;
            if (!probe.national && slot < depleted.squad.size()) {
                depleted.squad[slot].role = 0;
                depleted.squad[slot].line = 0;
            }
        }
        store_hok(depleted);
        Game game(exe, encode_hok(depleted), 6);
        const int homes = game.budget().home_games;
        const long national_balance = int32_t(le32(depleted.state, 0x17C7));
        game.advance_day();
        CHECK(std::count_if(game.log().begin(), game.log().end(), [](const std::string& s) {
            return s.find("forfeit:") == 0;
        }) == 1);
        CHECK(game.budget().home_games == homes);
        const HokFile saved = decode_hok(game.save_bytes());
        for (size_t slot = 0; slot < 29; slot++)
            for (size_t byte : {size_t(0),size_t(1),size_t(3),size_t(4),size_t(5),size_t(6),size_t(7),size_t(8),
                                size_t(0x1C),size_t(0x1D),size_t(0x1E),size_t(0x1F),size_t(0x20),size_t(0x21),
                                size_t(0x22),size_t(0x23),size_t(0x24),size_t(0x25),size_t(0x26),size_t(0x27)})
                CHECK(saved.state[block + slot * 52 + byte] == depleted.state[block + slot * 52 + byte]);
        if (probe.national) {
            CHECK(int32_t(le32(saved.state, 0x17C7)) == national_balance - 600000);
            CHECK(saved.state[0x1638 + 0xF0] == 100);
        }
        if (probe.event == kEventCup || (probe.event == kEventPlayoff && !probe.relegation)) {
            const auto& series = probe.event == kEventCup ? game.cup_series() : game.playoff_series();
            bool found = false;
            for (const auto& s : series) if (s.higher == game.human_team() || s.lower == game.human_team()) {
                CHECK(!s.games.empty());
                const auto& result = s.games.back();
                CHECK(result.home + result.away == 2 && !result.shootout);
                found = true;
            }
            CHECK(found);
        }
        Game resumed(exe, game.checkpoint_bytes(), 999);
        game.advance_day(); resumed.advance_day();
        CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
    }

    // Genuine native eight-player probe: awarded result/fine, no own appearances,
    // but the CPU opponent still receives its normal league statistics.
    if (fs::exists(ref_dir / "FORFPR01.HOK") && fs::exists(ref_dir / "FORFPOS1.HOK")) {
        const Bytes bytes = read_file(ref_dir / "FORFPR01.HOK");
        const HokFile source = decode_hok(bytes), native = decode_hok(read_file(ref_dir / "FORFPOS1.HOK"));
        Game game(exe, bytes, 6);
        game.advance_day();
        CHECK(game.budget().balance == 900000 && game.budget().home_games == 0);
        CHECK(game.save().state[0x0DCD + 0xF0] == 100);
        const HokFile first = decode_hok(game.save_bytes());
        for (size_t slot = 0; slot < source.squad.size(); slot++)
            for (size_t byte : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(4),size_t(5),size_t(6),size_t(7),size_t(8),
                                size_t(0x1C),size_t(0x1D),size_t(0x1E),size_t(0x1F),size_t(0x20),size_t(0x21),
                                size_t(0x22),size_t(0x23),size_t(0x24),size_t(0x25),size_t(0x26),size_t(0x27)})
                CHECK(first.state[0x696D + slot * 52 + byte] == source.state[0x696D + slot * 52 + byte]);
        int cpu_players = 0;
        for (size_t i = 0; i < first.players.size(); i++)
            if (source.players[i].team == 6) {
                CHECK(first.players[i].league_games == source.players[i].league_games + 1);
                cpu_players++;
            }
        CHECK(cpu_players > 0);
        Game resumed(exe, game.checkpoint_bytes(), 999);
        for (int day = 0; day < 2; day++) { game.advance_day(); resumed.advance_day(); }
        CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
        const HokFile saved = decode_hok(game.save_bytes());
        CHECK(saved.date.day_index == native.date.day_index && saved.date.day_index == 4);
        CHECK(saved.state[0x079C] == native.state[0x079C] && saved.state[0x079D] == native.state[0x079D]);
        CHECK(saved.state[0x8F88 + 6 * 10 + 1] == native.state[0x8F88 + 6 * 10 + 1]);
        CHECK(saved.state[0x8F88 + 6 * 10 + 1] == 0x20);
    }
    // Empty-roster home game after 64 seasons, including four days' overdraft interest.
    if (fs::exists(ref_dir / "INS64L01.HOK") && fs::exists(ref_dir / "INS64POS.HOK")) {
        Game game(exe, read_file(ref_dir / "INS64L01.HOK"), 6);
        const HokFile native = decode_hok(read_file(ref_dir / "INS64POS.HOK"));
        const int home_games = game.budget().home_games;
        for (int day = 0; day < 4; day++) game.advance_day();
        const HokFile saved = decode_hok(game.save_bytes());
        CHECK(saved.date.day_index == native.date.day_index && saved.date.day_index == 71);
        CHECK(game.budget().balance == -680790 && game.budget().accrued == -12748);
        CHECK(game.budget().home_games == home_games && game.save().state[0x0DCD + 0xF0] == 100);
        const size_t result = 0x0796 + 2 * 0x210 + 18 * 12 + 5 * 2;
        CHECK(saved.state[result] == 0 && saved.state[result + 1] == 2);
        CHECK(saved.state[result] == native.state[result] && saved.state[result + 1] == native.state[result + 1]);
    }
    std::printf("forfeits: eight/nine selected players, no minute RNG, native fine/results/statistics checked\n");
}

static void contract_cooldowns(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile source = decode_hok(read_file(ref_dir / "START.HOK"));
    constexpr uint8_t initial[] = {1, 2, 127, 128, 255};
    for (size_t slot = 0; slot < 5; slot++) source.state[0x696D + slot * 0x34 + 0x18] = initial[slot];
    Game game(exe, encode_hok(source), 6);
    for (int day = 0; day < 2; day++) game.advance_day();
    for (size_t slot = 0; slot < 5; slot++) CHECK(game.save().state[0x696D + slot * 0x34 + 0x18] == initial[slot]);
    game.advance_day();  // preparation of season day 4 (a mood marker above 99 is not counted down: 146e:119D)
    constexpr uint8_t once[] = {0, 1, 127, 128, 255};
    for (size_t slot = 0; slot < 5; slot++) CHECK(game.save().state[0x696D + slot * 0x34 + 0x18] == once[slot]);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    for (int day = 0; day < 3; day++) { game.advance_day(); resumed.advance_day(); }
    constexpr uint8_t twice[] = {0, 0, 127, 128, 255};
    for (size_t slot = 0; slot < 5; slot++) CHECK(game.save().state[0x696D + slot * 0x34 + 0x18] == twice[slot]);
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
}

static void transfer_purchases(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "KAUFPR01.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), input = read_file(ref_dir / "KAUFPR01.HOK");
    const HokFile source = decode_hok(input);
    constexpr size_t pool = 0x696D + 4 * 0x618, listed = pool + 6 * 0x34;
    CHECK(source.state[listed + 15] == 159);
    Game game(exe, input, 6);
    CHECK(game.game_level() == 0);
    const Bytes untouched = game.checkpoint_bytes();
    CHECK(game.buy_transfer_player(11, 750000, 1, 15000) == PurchaseResult::Invalid);
    CHECK(game.buy_transfer_player(6, 0, 1, 15000) == PurchaseResult::Invalid);
    CHECK(game.buy_transfer_player(6, 750000, 5, 15000) == PurchaseResult::Invalid);
    CHECK(game.buy_transfer_player(6, 750000, 1, 0) == PurchaseResult::Invalid);
    CHECK(game.checkpoint_bytes() == untouched);
    Game unchanged_salary(exe, input, 6);
    CHECK(unchanged_salary.buy_transfer_player(6, 750000, 1, 3300) == PurchaseResult::Purchased);
    // The physical thirtieth record is native scratch, not a purchasable place.
    HokFile scratch = decode_hok(input);
    const size_t scratch_at = 0x696D + 29 * 0x34;
    scratch.state[scratch_at + 0x17] = 21;
    scratch.state[scratch_at + 0x28] = 0x5A;
    Game preserve_scratch(exe, encode_hok(scratch), 6);
    CHECK(preserve_scratch.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Purchased);
    CHECK(std::equal(scratch.state.begin() + long(scratch_at), scratch.state.begin() + long(scratch_at + 0x34),
                     preserve_scratch.save().state.begin() + long(scratch_at)));
    const long old_costs = game.budget().squad_costs;
    CHECK(game.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Purchased);
    CHECK(game.budget().balance == 29250000 && game.budget().players == 26);
    CHECK(game.budget().squad_costs == old_costs + 15000);
    const HokFile bought = decode_hok(game.save_bytes());
    CHECK(bought.players[158].team == game.human_team() && bought.players[158].raw[0x1A] == 0);
    CHECK(bought.squad.size() == 26 && bought.squad.back().player == 158);
    for (size_t old = 0; old < source.squad.size(); old++) CHECK(bought.squad[old].player == source.squad[old].player);
    const size_t at = 0x696D + 25 * 0x34;
    CHECK(bought.state[at + 0x0B] == 1 && le32(bought.state, at + 0x28) == 15000);
    CHECK(le32(bought.state, at + 0x2C) == 750000 && le32(bought.state, at + 0x30) == 0);
    CHECK(bought.state[at + 0x19] == 0 && bought.state[at + 0x18] == 0);
    for (size_t slot = 0; slot < 10; slot++) CHECK(bought.state[pool + slot * 0x34 + 15] != 159);
    CHECK(le32(bought.state, 0x0F64) == le32(source.state, 0x0F64));  // no implicit full-income view
    if (fs::exists(ref_dir / "KAUFPOS1.HOK")) {
        const HokFile native = decode_hok(read_file(ref_dir / "KAUFPOS1.HOK"));
        CHECK(le32(native.state, 0x0F5C) == le32(bought.state, 0x0F5C));
        CHECK(native.squad.size() == bought.squad.size());
        CHECK(native.players[158].raw[0x1A] == 0 && native.players[158].team == game.human_team());
        for (size_t byte : {size_t(0x0B), size_t(0x0F), size_t(0x10), size_t(0x11), size_t(0x12),
                            size_t(0x18), size_t(0x19)})
            CHECK(native.state[at + byte] == bought.state[at + byte]);
        for (size_t money : {size_t(0x28), size_t(0x2C), size_t(0x30)})
            CHECK(le32(native.state, at + money) == le32(bought.state, at + money));
        CHECK(native.state[at + 0x0E] >= 35 && native.state[at + 0x0E] <= 65);
        CHECK(native.state[at + 0x13] >= 80 && native.state[at + 0x13] <= 120);
        CHECK(std::equal(native.state.begin() + long(pool), native.state.begin() + long(pool + 10 * 0x34),
                         bought.state.begin() + long(pool)));
    }
    Game native_reload(exe, game.save_bytes(), 6);
    CHECK(native_reload.save_bytes() == game.save_bytes());
    Game resumed(exe, game.checkpoint_bytes(), 999);
    for (int day = 0; day < 20; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(resumed.save_bytes() == game.save_bytes());

    // Buying in an ongoing career must retain every existing player's latest
    // appearances, goals and penalties, including when imported from .oem.
    Game career(exe, input, 6);
    career.advance_day();
    const HokFile played = decode_hok(career.save_bytes());
    Game career_reload(exe, career.checkpoint_bytes(), 999);
    int available_slot = -1;
    for (size_t slot = 0; slot < 10; slot++) {
        const int id = played.state[pool + slot * 52 + 15];
        if (id && played.state[0x4FC6 + size_t(id - 1) * 33 + 0x1A] == 4) { available_slot = int(slot); break; }
    }
    CHECK(available_slot >= 0);
    CHECK(career.buy_transfer_player(size_t(available_slot), 3000000, 1, 300000) == PurchaseResult::Purchased);
    CHECK(career_reload.buy_transfer_player(size_t(available_slot), 3000000, 1, 300000) == PurchaseResult::Purchased);
    const HokFile after_purchase = decode_hok(career.save_bytes());
    int played_players = 0;
    for (const auto& previous : played.squad) {
        const auto found = std::find_if(after_purchase.squad.begin(), after_purchase.squad.end(), [&](const auto& p) {
            return p.player == previous.player;
        });
        CHECK(found != after_purchase.squad.end());
        if (found == after_purchase.squad.end()) continue;
        const size_t before_at = size_t(&previous - played.squad.data()) * 52 + 0x696D;
        const size_t after_at = size_t(found - after_purchase.squad.begin()) * 52 + 0x696D;
        for (size_t byte : {size_t(0),size_t(1),size_t(3),size_t(4),size_t(5),size_t(6),size_t(7),size_t(8),
                            size_t(0x1C),size_t(0x1D),size_t(0x1E),size_t(0x1F),size_t(0x20),size_t(0x21),
                            size_t(0x22),size_t(0x23),size_t(0x24),size_t(0x25),size_t(0x26),size_t(0x27)})
            CHECK(played.state[before_at + byte] == after_purchase.state[after_at + byte]);
        played_players += previous.league_games > 0;
    }
    CHECK(played_players > 0 && career.checkpoint_bytes() == career_reload.checkpoint_bytes());

    Game refused(exe, input, 6);
    const long balance = refused.budget().balance;
    CHECK(refused.buy_transfer_player(6, 100000, 1, 15000) == PurchaseResult::SellerRefused);
    CHECK(refused.budget().balance == balance && (refused.save().state[listed + 0x1A] & 0x81) == 0x81);
    const Bytes blocked = refused.checkpoint_bytes();
    CHECK(refused.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Blocked);
    CHECK(refused.checkpoint_bytes() == blocked);
    Game blocked_reload(exe, refused.save_bytes(), 6);
    CHECK(blocked_reload.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Blocked);

    Game player_refused(exe, input, 6);
    const HokFile before_refusal = decode_hok(player_refused.save_bytes());
    CHECK(player_refused.buy_transfer_player(6, 750000, 1, 1) == PurchaseResult::PlayerRefused);
    const HokFile after_refusal = decode_hok(player_refused.save_bytes());
    CHECK(player_refused.budget().balance == balance && player_refused.save().squad.size() == source.squad.size());
    int changed = 0;
    for (size_t byte = 0; byte < before_refusal.state.size(); byte++)
        if (before_refusal.state[byte] != after_refusal.state[byte]) {
            CHECK(byte == listed + 0x1A); changed++;
        }
    CHECK(changed == 1 && after_refusal.players[158].raw[0x1A] == 4);

    HokFile poor = decode_hok(input);
    for (int byte = 0; byte < 4; byte++) poor.state[0x0F5C + size_t(byte)] = uint8_t(749999u >> (byte * 8));
    Game no_funds(exe, encode_hok(poor), 6);
    const Bytes before_money = no_funds.save_bytes(), before_money_checkpoint = no_funds.checkpoint_bytes();
    CHECK(no_funds.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::InsufficientFunds);
    CHECK(no_funds.save_bytes() == before_money && no_funds.checkpoint_bytes() != before_money_checkpoint);

    // Seed 4: the native 1/7 purchase subsidy offers 26 percent, rounded to 100 DM.
    Game decline(exe, input, 4), accept(exe, input, 4);
    CHECK(decline.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Purchased);
    CHECK(accept.buy_transfer_player(6, 750000, 1, 15000, true) == PurchaseResult::Purchased);
    CHECK(accept.budget().balance - decline.budget().balance == 195000);

    // A listed own player reserves a future roster place, even outside the 29 visible slots.
    for (int occupied : {28, 29}) {
        HokFile full = decode_hok(input);
        int next = 25;
        for (size_t index = 0; index < full.players.size() && next < occupied; index++) {
            const size_t pr = size_t(full.players[index].raw - full.state.data());
            if (full.state[pr + 0x1A] != 6) continue;
            const size_t dest = 0x696D + size_t(next++) * 0x34;
            std::copy_n(full.state.begin() + 0x696D, 0x34, full.state.begin() + long(dest));
            full.state[dest + 15] = uint8_t(index + 1);
            full.state[pr + 0x1A] = 0; full.state[pr + 0x1D] = uint8_t(full.state[0x0DE1]);
        }
        CHECK(next == occupied);
        if (occupied == 28) {
            const int id = full.state[pool + 15];
            const size_t pr = 0x4FC6 + size_t(id - 1) * 0x21;
            full.state[pr + 0x1A] = 0; full.state[pr + 0x1D] = full.state[0x0DE1];
        }
        Game capacity(exe, encode_hok(full), 6);
        const Bytes before_capacity = capacity.save_bytes();
        CHECK(capacity.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::SquadFull);
        CHECK(capacity.save_bytes() == before_capacity);
    }
    // A valid purchase fills/reorders a roster containing a hole without losing another player.
    HokFile hole = decode_hok(input);
    const int missing = hole.state[0x696D + 3 * 0x34 + 15];
    hole.state[0x696D + 3 * 0x34 + 15] = 0;
    hole.state[0x4FC6 + size_t(missing - 1) * 0x21 + 0x1A] = 6;
    hole.state[0x4FC6 + size_t(missing - 1) * 0x21 + 0x1D] = 0;
    Game with_hole(exe, encode_hok(hole), 6);
    CHECK(with_hole.buy_transfer_player(6, 750000, 1, 15000) == PurchaseResult::Purchased);
    const HokFile filled = decode_hok(with_hole.save_bytes());
    CHECK(filled.squad.size() == 25);
    for (const auto& old : source.squad)
        if (old.player != missing - 1)
            CHECK(std::count_if(filled.squad.begin(), filled.squad.end(), [&](const auto& p) { return p.player == old.player; }) == 1);
    std::puts("transfer purchases: native ownership/fee/contract fields, refusals, capacity, subsidy and checkpoints checked");
}

static void player_insurance(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "KAUFCPP1.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), input = read_file(ref_dir / "KAUFCPP1.HOK");
    constexpr size_t at = 0x696D + 25 * 0x34;
    Game game(exe, input, 6);
    const long costs = game.budget().squad_costs, balance = game.budget().balance;
    const auto rng_before = decode_game_checkpoint(game.checkpoint_bytes()).borland_random;
    CHECK(!game.set_player_insurance(29, true));
    CHECK(game.set_player_insurance(25, true));
    CHECK(le32(game.save().state, at + 0x30) == 2400);  // fresh salary 4800, cached salary 15000
    CHECK((le32(game.save().state, at + 0x2C) & 0x7FFFFFFF) == 750000);
    CHECK(game.save().state[at + 0x2F] & 0x80);
    CHECK(game.budget().squad_costs == costs + 2400 && game.budget().balance == balance);
    CHECK(decode_game_checkpoint(game.checkpoint_bytes()).borland_random == rng_before);
    if (fs::exists(ref_dir / "INSPOST1.HOK")) {
        const HokFile native = decode_hok(read_file(ref_dir / "INSPOST1.HOK"));
        CHECK(std::equal(native.state.begin() + 0x696D, native.state.begin() + 0x696D + 30 * 0x34,
                         game.save().state.begin() + 0x696D));
        CHECK(le32(native.state, 0x0F5C) == uint32_t(game.budget().balance));
    }
    const Bytes covered = game.checkpoint_bytes();
    CHECK(game.set_player_insurance(25, true));
    CHECK(!game.set_player_insurance(25, false));
    CHECK(game.checkpoint_bytes() == covered);
    Game imported(exe, game.save_bytes(), 999), resumed(exe, covered, 999);
    CHECK(!imported.set_player_insurance(25, false));
    for (int day = 0; day < 19; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.save().state[at + 0x2F] & 0x80);  // September 29: not a rolling 30-day counter
    game.advance_day(); resumed.advance_day();
    CHECK((game.save().state[at + 0x2F] & 0x80) == 0);  // next month-end, September 30
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
    CHECK(game.set_player_insurance(25, false));
    CHECK(le32(game.save().state, at + 0x30) == 0);

    HokFile hurt = decode_hok(input);
    hurt.state[at + 0x09] = 2;
    hurt.state[at + 0x0D] = 3;
    Game unavailable(exe, encode_hok(hurt), 6);
    const Bytes no_cover = unavailable.checkpoint_bytes();
    CHECK(!unavailable.set_player_insurance(25, true));
    CHECK(unavailable.checkpoint_bytes() == no_cover);
    for (bool loan : {false, true}) {
        HokFile blocked = decode_hok(input);
        blocked.state[at + (loan ? 0x0C : 0x18)] = loan ? 5 : 0x80;
        Game cannot_open(exe, encode_hok(blocked), 6);
        const Bytes before = cannot_open.checkpoint_bytes();
        CHECK(!cannot_open.set_player_insurance(25, true));
        CHECK(cannot_open.checkpoint_bytes() == before);
    }

    // Hold the random stream fixed across insured/uninsured squads. A short
    // interval avoids month-end premiums and interest, so only payouts differ.
    HokFile insured = decode_hok(input);
    for (size_t slot = 0; slot < insured.squad.size(); slot++) {
        const size_t row = 0x696D + slot * 0x34;
        insured.state[row + 0x30] = 0xB8; insured.state[row + 0x31] = 0x0B;  // 3000 raw
    }
    int payouts = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        Game paid(exe, encode_hok(insured), seed), unpaid(exe, input, seed);
        int payments = 0;
        for (int day = 0; day < 19; day++) {
            paid.advance_day(); unpaid.advance_day();
            for (const auto& line : paid.log()) if (line.find(" injury insurance payment ") != std::string::npos) payments++;
            CHECK(paid.budget().balance - unpaid.budget().balance == payments * 12000L);
            CHECK(decode_game_checkpoint(paid.checkpoint_bytes()).borland_random ==
                  decode_game_checkpoint(unpaid.checkpoint_bytes()).borland_random);
            paid.clear_log(); unpaid.clear_log();
        }
        payouts += payments;
    }
    CHECK(payouts > 0);
    if (fs::exists(ref_dir / "INSPR01.HOK")) {
        const Bytes disabled_input = read_file(ref_dir / "INSPR01.HOK");
        HokFile no_insurance = decode_hok(disabled_input);
        const size_t disabled_at = 0x696D + 24 * 0x34;
        const int disabled_id = no_insurance.state[disabled_at + 0x0F];
        std::fill_n(no_insurance.state.begin() + long(disabled_at + 0x30), 4, uint8_t(0));
        Game covered_disability(exe, disabled_input, 6), uncovered_disability(exe, encode_hok(no_insurance), 6);
        int callbacks = 0;
        covered_disability.set_contract_decider([&](const ContractPlayer& p, long) { CHECK(p.age != 0); callbacks++; return 2; });
        for (int day = 0; day < 17; day++) { covered_disability.advance_day(); uncovered_disability.advance_day(); }
        CHECK(callbacks == 0);  // the disabled player's two-year contract is removed before expiry talks
        // the payout only: the season change follows the WM final directly, without the April month-end that charged
        // one more premium (REAL-CODE period 30)
        CHECK(covered_disability.budget().balance - uncovered_disability.budget().balance == 180000);
        CHECK(covered_disability.save().squad.size() == 24 && uncovered_disability.save().squad.size() == 24);
        for (const auto& player : covered_disability.save().squad)
            CHECK(covered_disability.save().players[size_t(player.player)].raw !=
                  covered_disability.save().state.data() + 0x4FC6 + size_t(disabled_id - 1) * 0x21);
        const HokFile replaced = decode_hok(covered_disability.save_bytes());
        const uint8_t* global = replaced.state.data() + 0x4FC6 + size_t(disabled_id - 1) * 0x21;
        CHECK(global[0x1A] == 6 && global[0x14] >= 18 && global[0x14] <= 25);
        CHECK(global[0x1D] == covered_disability.human_team());  // native freed replacement keeps this team byte
        CHECK(global[0x19] == 0);
        if (fs::exists(ref_dir / "INSPOS01.HOK")) {
            const HokFile native = decode_hok(read_file(ref_dir / "INSPOS01.HOK"));
            CHECK(native.squad.size() == covered_disability.save().squad.size());
            const size_t pr = 0x4FC6 + size_t(disabled_id - 1) * 0x21;
            CHECK(native.state[pr + 0x1A] == 6 && native.state[pr + 0x1D] == native.state[0x0DE1]);
            CHECK(native.state[pr + 0x14] >= 18 && native.state[pr + 0x14] <= 25);
            for (size_t slot = 0; slot < native.squad.size(); slot++) CHECK(native.state[0x696D + slot * 0x34 + 15] != disabled_id);
        }
        int settlements = 0;
        for (const auto& line : covered_disability.log())
            if (line.find(" disability insurance payment 60000 DM") != std::string::npos) settlements++;
        CHECK(settlements == 1);
        Game restart(exe, covered_disability.checkpoint_bytes(), 999);
        for (int day = 0; day < 20; day++) { covered_disability.advance_day(); restart.advance_day(); }
        CHECK(covered_disability.checkpoint_bytes() == restart.checkpoint_bytes());
    }
    std::printf("player insurance: native premium, month-end lock, checkpoint and %d injury payouts checked\n", payouts);
}

static void contract_negotiations(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "KAUFCPP1.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), input = read_file(ref_dir / "KAUFCPP1.HOK");
    constexpr size_t at = 0x696D + 25 * 0x34;
    Game game(exe, input, 6);
    const Bytes initial = game.checkpoint_bytes();
    CHECK(game.negotiate_contract(29, 1, 18000) == ContractResult::Invalid);
    CHECK(game.negotiate_contract(25, 5, 18000) == ContractResult::Invalid);
    CHECK(game.negotiate_contract(25, 1, 0) == ContractResult::Invalid);
    CHECK(game.checkpoint_bytes() == initial);
    CHECK(game.negotiate_contract(25, 1, 15000) == ContractResult::Signed);
    CHECK(game.checkpoint_bytes() == initial);  // unchanged deal: no state or RNG mutation
    const long costs = game.budget().squad_costs, balance = game.budget().balance;
    CHECK(game.negotiate_contract(25, 1, 18000) == ContractResult::Signed);
    CHECK(game.budget().squad_costs == costs + 3000 && game.budget().balance == balance);
    CHECK(le32(game.save().state, at + 0x28) == 18000 && game.save().squad[25].salary == 6000);
    CHECK(decode_game_checkpoint(game.checkpoint_bytes()).borland_random == decode_game_checkpoint(initial).borland_random);
    CHECK(le32(game.save().state, at + 0x2C) == 750000 && le32(game.save().state, at + 0x30) == 0);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    for (int day = 0; day < 20; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());

    if (fs::exists(ref_dir / "CNTCPP01.HOK") && fs::exists(ref_dir / "CNTPOS01.HOK")) {
        Game native_action(exe, read_file(ref_dir / "CNTCPP01.HOK"), 6);
        CHECK(native_action.negotiate_contract(25, 1, 19500) == ContractResult::Signed);
        CHECK(decode_hok(native_action.save_bytes()).state == decode_hok(read_file(ref_dir / "CNTPOS01.HOK")).state);
    }

    Game refused(exe, input, 6);
    const HokFile before = decode_hok(refused.save_bytes());
    CHECK(refused.negotiate_contract(25, 1, 1) == ContractResult::PlayerRefused);
    const int cooldown = refused.save().state[at + 0x18];
    CHECK(cooldown >= 10 && cooldown <= 18);
    const HokFile after = decode_hok(refused.save_bytes());
    int changes = 0;
    for (size_t byte = 0; byte < before.state.size(); byte++) if (before.state[byte] != after.state[byte]) {
        CHECK(byte == at + 0x18); changes++;
    }
    CHECK(changes == 1);
    const Bytes blocked = refused.checkpoint_bytes();
    CHECK(refused.negotiate_contract(25, 1, 18000) == ContractResult::Blocked);
    CHECK(refused.checkpoint_bytes() == blocked);
    Game reloaded(exe, refused.save_bytes(), 999);
    CHECK(reloaded.negotiate_contract(25, 1, 18000) == ContractResult::Blocked);
    for (int day = 0; day < 3; day++) refused.advance_day();
    CHECK(refused.save().state[at + 0x18] == cooldown - 1);

    Game shorter(exe, read_file(ref_dir / "START.HOK"), 6);
    const ContractPlayer current = contract_player(shorter.save(), 0, 0);
    const Bytes unconfirmed = shorter.checkpoint_bytes();
    CHECK(shorter.negotiate_contract(0, 1, current.salary * 2) == ContractResult::ShorteningNotConfirmed);
    CHECK(shorter.checkpoint_bytes() == unconfirmed);
    CHECK(shorter.negotiate_contract(0, 1, current.salary * 2, true) == ContractResult::Signed);
    CHECK(shorter.save().state[0x696D + 0x0B] == 1);
    std::puts("contract negotiations: explicit bids, unchanged/raised deals, saved refusal, cooldown and confirmed shortening checked");
}

static void training_actions(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const TrainingTables tables = load_training_tables(exe);
    TrainingPlan plan;
    for (auto& day : plan) day.fill(kFreeTime);
    CHECK(training_recovery_cost(tables, plan, 0) == 0);
    plan[0][0] = kRecovery;
    CHECK(training_recovery_cost(tables, plan, 0) == 33000);
    CHECK(training_recovery_cost(tables, plan, 1) == 25800);
    CHECK(training_recovery_cost(tables, plan, 2) == 18600);
    plan[6][3] = kRecovery;
    CHECK(training_recovery_cost(tables, plan, 0) == 45000);
    for (auto& day : plan) day.fill(kRecovery);
    CHECK(training_recovery_cost(tables, plan, 0) == 357000);
    auto discounted = tables;
    discounted.recovery_cost_factor = 75;
    CHECK(training_recovery_cost(discounted, plan, 0) == 273000);
    discounted.recovery_cost_factor = 0;
    CHECK(training_recovery_cost(discounted, plan, 0) == 0);

    Game paid(exe, read_file(ref_dir / "START.HOK"), 6);
    Game unpaid(exe, read_file(ref_dir / "START.HOK"), 6);
    auto one_recovery = paid.training_plan();
    one_recovery[0][0] = kRecovery;
    CHECK(paid.set_training_plan(one_recovery));
    CHECK(paid.budget().balance == unpaid.budget().balance);
    // The first three dates include the first away game and prepare the
    // first weekly update. The fee belongs to that update, not the editor.
    for (int day = 0; day < 3; day++) { paid.advance_day(); unpaid.advance_day(); }
    CHECK(unpaid.budget().balance - paid.budget().balance == 33000);

    Game game(exe, read_file(ref_dir / "START.HOK"), 6);
    const HokFile before = decode_hok(game.save_bytes());
    const auto rng = decode_game_checkpoint(game.checkpoint_bytes());
    CHECK(game.set_training_plan(plan));
    CHECK(game.training_plan() == plan && game.budget().open_97ee == 357000);
    CHECK(game.budget().balance == int32_t(le32(before.state, 0xF5C)));
    const HokFile changed = decode_hok(game.save_bytes());
    for (size_t byte = 0; byte < changed.state.size(); byte++) if (changed.state[byte] != before.state[byte])
        CHECK((byte >= 0x070C && byte < 0x0728) || (byte >= 0xF60 && byte < 0xF64));
    CHECK(le32(changed.state, 0xF64) == le32(before.state, 0xF64));
    const auto changed_rng = decode_game_checkpoint(game.checkpoint_bytes());
    CHECK(changed_rng.game_random == rng.game_random && changed_rng.borland_random == rng.borland_random);
    TrainingPlan invalid = plan;
    invalid[6][3] = kTrainingKinds;
    const Bytes valid = game.checkpoint_bytes();
    CHECK(!game.set_training_plan(invalid));
    CHECK(game.checkpoint_bytes() == valid);
    Game resumed(exe, valid, 999);
    CHECK(resumed.training_plan() == plan && resumed.budget().open_97ee == 357000);
    Game hok_reload(exe, game.save_bytes(), 6);
    CHECK(hok_reload.training_plan() == plan && hok_reload.budget().open_97ee == 357000);
    for (int day = 0; day < 60; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
    for (auto& day : plan) day.fill(kFreeTime);
    CHECK(game.set_training_plan(plan) && game.budget().open_97ee == 0);
    if (fs::exists(ref_dir / "NATAUTO.HOK")) {
        Game national(exe, read_file(ref_dir / "NATAUTO.HOK"), 6);
        const Bytes saved = national.checkpoint_bytes();
        CHECK(!national.set_training_plan(plan));
        CHECK(national.checkpoint_bytes() == saved);
    }
    if (fs::exists(ref_dir / "TRNBASE1.HOK") && fs::exists(ref_dir / "TRNPOS01.HOK")) {
        Game native(exe, read_file(ref_dir / "TRNBASE1.HOK"), 6);
        auto native_plan = native.training_plan();
        native_plan[0][0] = kRecovery;
        CHECK(native.set_training_plan(native_plan));
        CHECK(decode_hok(native.save_bytes()).state == decode_hok(read_file(ref_dir / "TRNPOS01.HOK")).state);
    }
    if (fs::exists(ref_dir / "TRNPOS01.HOK")) {
        Game native_days(exe, read_file(ref_dir / "TRNPOS01.HOK"), 6);
        for (int day = 0; day < 3; day++) native_days.advance_day();
        CHECK(native_days.budget().balance == 35217000);
        CHECK(native_days.budget().bank == -6000000);
        CHECK(native_days.budget().open_97ee == 33000);
        CHECK(native_days.budget().accrued == -23223);
    }
    std::puts("training actions: native league fees, plan/cost-only change, no RNG and 60 checkpoint days checked");
}

static void bank_actions(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "AKTIE.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    if (fs::exists(ref_dir / "RECLPOS1.HOK") && fs::exists(ref_dir / "BANKD001.HOK")) {
        Game native_action(exe, read_file(ref_dir / "RECLPOS1.HOK"), 6);
        CHECK(native_action.bank_transfer(12345000) == BankResult::Transferred);
        CHECK(decode_hok(native_action.save_bytes()).state == decode_hok(read_file(ref_dir / "BANKD001.HOK")).state);
        CHECK(native_action.bank_transfer(-12345000) == BankResult::Transferred);
        CHECK(decode_hok(native_action.save_bytes()).state == decode_hok(read_file(ref_dir / "RECLPOS1.HOK")).state);
    }
    if (fs::exists(ref_dir / "RECLPOS1.HOK") && fs::exists(ref_dir / "BANKLN01.HOK")) {
        Game native_loan(exe, read_file(ref_dir / "RECLPOS1.HOK"), 6);
        CHECK(native_loan.bank_transfer(-6000000) == BankResult::Transferred);
        CHECK(decode_hok(native_loan.save_bytes()).state == decode_hok(read_file(ref_dir / "BANKLN01.HOK")).state);
        const Bytes capped = native_loan.checkpoint_bytes();
        CHECK(native_loan.bank_transfer(-1) == BankResult::CreditLimit);
        CHECK(native_loan.checkpoint_bytes() == capped);
        CHECK(native_loan.bank_transfer(6000000) == BankResult::Transferred);
        CHECK(decode_hok(native_loan.save_bytes()).state == decode_hok(read_file(ref_dir / "RECLPOS1.HOK")).state);
        if (fs::exists(ref_dir / "BANKPAY1.HOK")) {
            Game saved_loan(exe, read_file(ref_dir / "BANKLN01.HOK"), 6);
            CHECK(saved_loan.bank_transfer(6000000) == BankResult::Transferred);
            CHECK(decode_hok(saved_loan.save_bytes()).state == decode_hok(read_file(ref_dir / "BANKPAY1.HOK")).state);
        }
    }
    Game game(exe, read_file(ref_dir / "AKTIE.HOK"), 6);
    const HokFile before = decode_hok(game.save_bytes());
    const long initial_cash = game.budget().balance, initial_bank = game.budget().bank;
    const auto rng = decode_game_checkpoint(game.checkpoint_bytes());
    CHECK(game.bank_transfer(0) == BankResult::InvalidAmount);
    CHECK(game.bank_transfer(initial_cash + 1) == BankResult::InsufficientCash);
    CHECK(game.bank_transfer(-6000001) == BankResult::CreditLimit);
    CHECK(game.bank_transfer(std::numeric_limits<long>::min()) == BankResult::CreditLimit);
    CHECK(decode_hok(game.save_bytes()).state == before.state);
    CHECK(game.bank_transfer(300000) == BankResult::Transferred);
    CHECK(game.budget().balance == initial_cash - 300000 && game.budget().bank == initial_bank + 300000);
    const HokFile deposited = decode_hok(game.save_bytes());
    CHECK(int32_t(le32(deposited.state, 0x1618)) == int32_t(le32(before.state, 0x1618)) + 300000);
    CHECK(int32_t(le32(deposited.state, 0x1624)) == -game.budget().bank);
    CHECK(le32(deposited.state, 0xF64) == le32(before.state, 0xF64));
    int changes = 0;
    for (size_t byte = 0; byte < before.state.size(); byte++) if (deposited.state[byte] != before.state[byte]) {
        CHECK((byte >= 0xF5C && byte < 0xF60) || (byte >= 0xF78 && byte < 0xF7C) ||
              (byte >= 0x1618 && byte < 0x161C) || (byte >= 0x1624 && byte < 0x1628));
        changes++;
    }
    CHECK(changes > 0);
    const auto after_rng = decode_game_checkpoint(game.checkpoint_bytes());
    CHECK(after_rng.game_random == rng.game_random && after_rng.borland_random == rng.borland_random);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    CHECK(game.bank_transfer(-6000000) == BankResult::Transferred);
    CHECK(resumed.bank_transfer(-6000000) == BankResult::Transferred);
    const long remaining = 6000000 + game.budget().bank;
    CHECK(remaining > 0 && game.bank_transfer(-remaining - 1) == BankResult::CreditLimit);
    CHECK(game.bank_transfer(-remaining) == BankResult::Transferred);
    CHECK(resumed.bank_transfer(-remaining) == BankResult::Transferred);
    CHECK(game.budget().bank == -6000000 && loan_interest(game.budget()) == 240000);
    CHECK(game.bank_transfer(-1) == BankResult::CreditLimit);
    CHECK(game.bank_transfer(300000) == BankResult::Transferred);
    CHECK(resumed.bank_transfer(300000) == BankResult::Transferred);
    CHECK(game.bank_transfer(5700000) == BankResult::Transferred);
    CHECK(resumed.bank_transfer(5700000) == BankResult::Transferred);
    CHECK(game.budget().bank == 0 && game.budget().balance == initial_cash + initial_bank);
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
    for (int day = 0; day < 30; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());

    if (fs::exists(ref_dir / "START.HOK")) {
        // Native postings update each stored matrix side independently;
        // they do not normalize an imported asymmetric counter account.
        HokFile asymmetric = decode_hok(read_file(ref_dir / "START.HOK"));
        for (int byte = 0; byte < 4; byte++) asymmetric.state.at(0x1624 + size_t(byte)) = uint8_t(3000000u >> (8 * byte));
        Game counter_limit(exe, encode_hok(asymmetric), 6);
        const Bytes counter_before = counter_limit.checkpoint_bytes();
        CHECK(counter_limit.bank_transfer(-3000001) == BankResult::CreditLimit);
        CHECK(counter_limit.checkpoint_bytes() == counter_before);
        Game counter_posting(exe, encode_hok(asymmetric), 6);
        CHECK(counter_posting.bank_transfer(-3000000) == BankResult::Transferred);
        CHECK(counter_posting.budget().bank == -3000000);
        CHECK(int32_t(le32(counter_posting.save().state, 0x1624)) == 6000000);
        CHECK(counter_posting.bank_transfer(3000000) == BankResult::Transferred);
        CHECK(counter_posting.budget().bank == 0);
        CHECK(int32_t(le32(counter_posting.save().state, 0x1624)) == 3000000);

        HokFile high = decode_hok(read_file(ref_dir / "START.HOK"));
        auto budget = budget_from_save(high, 0);
        budget.balance = 500000000;
        store_budget(high, budget, 0);
        Game rich(exe, encode_hok(high), 6);
        CHECK(rich.bank_transfer(400000000) == BankResult::Transferred);
        CHECK(rich.budget().balance == 100000000 && savings_interest(rich.budget()) == 4000000);
        const Bytes large_savings = rich.checkpoint_bytes();
        CHECK(rich.bank_transfer(-400000001) == BankResult::CreditLimit);
        CHECK(rich.checkpoint_bytes() == large_savings);
        Game large_withdrawal(exe, large_savings, 999);
        CHECK(large_withdrawal.bank_transfer(-400000000) == BankResult::Transferred);
        CHECK(large_withdrawal.budget().balance == 500000000 && large_withdrawal.budget().bank == 0);
        Game reload(exe, rich.checkpoint_bytes(), 999);
        for (int day = 0; day < 60; day++) { rich.advance_day(); reload.advance_day(); }
        CHECK(rich.checkpoint_bytes() == reload.checkpoint_bytes());
    }
    if (fs::exists(ref_dir / "NATAUTO.HOK")) {
        Game national(exe, read_file(ref_dir / "NATAUTO.HOK"), 6);
        const Bytes checkpoint = national.checkpoint_bytes();
        CHECK(national.bank_transfer(-300000) == BankResult::Unavailable);
        CHECK(national.checkpoint_bytes() == checkpoint);
    }
    std::puts("bank actions: four-sided postings, cash/credit limits, no RNG, cached income and checkpoint checked");
}

static void owned_transfers(const fs::path& em_dir, const fs::path& ref_dir) {
    const auto path = ref_dir / (fs::exists(ref_dir / "CNTREF01.HOK") ? "CNTREF01.HOK" : "KAUFCPP1.HOK");
    if (!fs::exists(path)) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), input = read_file(path);
    constexpr size_t club = 0x696D, pool = club + 4 * 0x618;
    if (fs::exists(ref_dir / "CNTREF01.HOK") && fs::exists(ref_dir / "LISTPOS1.HOK")) {
        Game native_action(exe, read_file(ref_dir / "CNTREF01.HOK"), 6);
        CHECK(native_action.list_transfer_player(25) == TransferMoveResult::Moved);
        CHECK(decode_hok(native_action.save_bytes()).state == decode_hok(read_file(ref_dir / "LISTPOS1.HOK")).state);
    }
    if (fs::exists(ref_dir / "LISTPOS1.HOK") && fs::exists(ref_dir / "RECLPOS1.HOK")) {
        Game native_action(exe, read_file(ref_dir / "LISTPOS1.HOK"), 6);
        CHECK(native_action.recall_transfer_player(8) == TransferMoveResult::Moved);
        CHECK(decode_hok(native_action.save_bytes()).state == decode_hok(read_file(ref_dir / "RECLPOS1.HOK")).state);
    }
    Game game(exe, input, 6);
    const size_t slot = game.save().squad.size() - 1;
    CHECK(game.set_player_insurance(slot, true));
    const HokFile before = decode_hok(game.save_bytes());
    const int id = before.state[club + slot * 52 + 15];
    const long balance = game.budget().balance, costs = game.budget().squad_costs + game.budget().pool_costs;
    const Bytes untouched = game.checkpoint_bytes();
    CHECK(game.list_transfer_player(29) == TransferMoveResult::Invalid);
    CHECK(game.recall_transfer_player(11) == TransferMoveResult::Invalid);
    CHECK(game.recall_transfer_player(0) == TransferMoveResult::Unavailable);
    CHECK(game.checkpoint_bytes() == untouched);
    BorlandRandom expected(decode_game_checkpoint(untouched).borland_random);
    expected.range(80,120); expected.range(35,65); expected.range(1,13); expected.range(1,3); expected.range(0,1);
    CHECK(game.list_transfer_player(slot) == TransferMoveResult::Moved);
    CHECK(decode_game_checkpoint(game.checkpoint_bytes()).borland_random == expected.state());
    CHECK(game.budget().balance == balance && game.budget().squad_costs + game.budget().pool_costs == costs);
    CHECK(game.budget().players == int(before.squad.size()) - 1);
    const HokFile listed = decode_hok(game.save_bytes());
    int market_slot = -1;
    for (size_t i = 0; i < kTransferListSlots; i++) if (listed.state[pool + i * 52 + 15] == id) market_slot = int(i);
    CHECK(market_slot >= 0);
    for (size_t byte = 0; byte < 52; byte++)
        CHECK(listed.state[pool + size_t(market_slot) * 52 + byte] == (byte == 10 ? 0 : before.state[club + slot * 52 + byte]));
    CHECK(listed.state[0x4FC6 + size_t(id - 1) * 33 + 0x1A] == 0);
    Game resumed(exe, game.checkpoint_bytes(), 999);
    CHECK(game.recall_transfer_player(size_t(market_slot)) == TransferMoveResult::Moved);
    CHECK(resumed.recall_transfer_player(size_t(market_slot)) == TransferMoveResult::Moved);
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());
    CHECK(game.budget().balance == balance && game.budget().squad_costs + game.budget().pool_costs == costs);
    const HokFile returned = decode_hok(game.save_bytes());
    CHECK(returned.squad.size() == before.squad.size());
    for (size_t i = 0; i < before.squad.size(); i++)
        CHECK(std::equal(before.state.begin() + long(club + i * 52), before.state.begin() + long(club + (i + 1) * 52),
                         returned.state.begin() + long(club + i * 52)));
    for (int day = 0; day < 20; day++) { game.advance_day(); resumed.advance_day(); }
    CHECK(game.checkpoint_bytes() == resumed.checkpoint_bytes());

    HokFile empty = decode_hok(input);
    {
        Game career(exe, input, 6);
        career.advance_day();
        const HokFile played = decode_hok(career.save_bytes());
        size_t active = 0;
        while (active < played.squad.size() && played.squad[active].league_games == 0) active++;
        CHECK(active < played.squad.size());
        const int playing_id = played.state[club + active * 52 + 15];
        CHECK(career.list_transfer_player(active) == TransferMoveResult::Moved);
        int listed_slot = -1;
        for (size_t i = 0; i < kTransferListSlots; i++) if (career.save().state[pool + i * 52 + 15] == playing_id) listed_slot = int(i);
        CHECK(listed_slot >= 0);
        Game checkpoint(exe, career.checkpoint_bytes(), 999);
        CHECK(career.recall_transfer_player(size_t(listed_slot)) == TransferMoveResult::Moved);
        CHECK(checkpoint.recall_transfer_player(size_t(listed_slot)) == TransferMoveResult::Moved);
        CHECK(career.checkpoint_bytes() == checkpoint.checkpoint_bytes());
        const HokFile recalled = decode_hok(career.save_bytes());
        for (size_t old = 0; old < played.squad.size(); old++) {
            const auto found = std::find_if(recalled.squad.begin(), recalled.squad.end(), [&](const auto& p) {
                return p.player == played.squad[old].player;
            });
            CHECK(found != recalled.squad.end());
            if (found == recalled.squad.end()) continue;
            const size_t new_slot = size_t(found - recalled.squad.begin());
            for (size_t byte : {size_t(0),size_t(1),size_t(3),size_t(4),size_t(5),size_t(6),size_t(7),size_t(8),
                                size_t(0x1C),size_t(0x1D),size_t(0x1E),size_t(0x1F),size_t(0x20),size_t(0x21),
                                size_t(0x22),size_t(0x23),size_t(0x24),size_t(0x25),size_t(0x26),size_t(0x27)})
                CHECK(played.state[club + old * 52 + byte] == recalled.state[club + new_slot * 52 + byte]);
        }
    }
    for (size_t i = 0; i < 12; i++) {
        const int old_id = empty.state[pool + i * 52 + 15];
        if (old_id) empty.state[0x4FC6 + size_t(old_id - 1) * 33 + 0x1A] = 6;
    }
    std::fill_n(empty.state.begin() + long(pool), 12 * 52, uint8_t(0));
    Game limits(exe, encode_hok(empty), 6);
    for (int i = 0; i < 3; i++) CHECK(limits.list_transfer_player(limits.save().squad.size() - 1) == TransferMoveResult::Moved);
    const Bytes three = limits.checkpoint_bytes();
    CHECK(limits.list_transfer_player(limits.save().squad.size() - 1) == TransferMoveResult::OwnLimit);
    CHECK(limits.checkpoint_bytes() == three);

    // Eleven occupied slots are valid; a twelfth listing is not. Index 10 is
    // both a usable entry and the native end record cleared by removal.
    HokFile last = decode_hok(input);
    std::vector<int> cpu;
    for (const auto& p : last.players) if (p.raw[0x1A] >= 4 && p.team != last.state[0x0DE1])
        cpu.push_back(int((p.raw - last.state.data() - 0x4FC6) / 33) + 1);
    CHECK(cpu.size() >= 11);
    for (size_t i = 0; i < 11; i++) {
        std::fill_n(last.state.begin() + long(pool + i * 52), 52, uint8_t(0));
        last.state[pool + i * 52 + 15] = uint8_t(cpu[i]);
        last.state[0x4FC6 + size_t(cpu[i] - 1) * 33 + 0x1A] = 4;
    }
    Game full(exe, encode_hok(last), 6);
    const Bytes full_before = full.checkpoint_bytes();
    CHECK(full.list_transfer_player(slot) == TransferMoveResult::PoolFull);
    CHECK(full.checkpoint_bytes() == full_before);
    std::copy_n(before.state.begin() + long(club + slot * 52), 52, last.state.begin() + long(pool + 10 * 52));
    last.state[pool + 10 * 52 + 10] = 0;
    std::fill_n(last.state.begin() + long(club + slot * 52), 52, uint8_t(0));
    last.squad.pop_back();
    store_hok(last);
    const Bytes end_record(last.state.begin() + long(pool + 10 * 52), last.state.begin() + long(pool + 11 * 52));
    Game eleventh(exe, encode_hok(last), 6);
    CHECK(eleventh.recall_transfer_player(10) == TransferMoveResult::Moved);
    const auto after = decode_hok(eleventh.save_bytes());
    for (size_t byte = 0; byte < 52; byte++) CHECK(after.state[pool + 10 * 52 + byte] == (byte == 15 ? 0 : end_record[byte]));
    CHECK(after.squad.size() == before.squad.size());

    HokFile blocked = decode_hok(input);
    blocked.state[club + slot * 52 + 0x18] |= 0x80;
    Game unavailable(exe, encode_hok(blocked), 6);
    const Bytes blocked_before = unavailable.checkpoint_bytes();
    CHECK(unavailable.list_transfer_player(slot) == TransferMoveResult::Blocked);
    CHECK(unavailable.checkpoint_bytes() == blocked_before);
    std::puts("own transfers: contracts/insurance/stats, no fee, five draws, three-own/eleven-total limits and checkpoint checked");
}

static void owned_transfer_seasons(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "LICRICH1.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile base = decode_hok(read_file(ref_dir / "LICRICH1.HOK"));
    constexpr size_t club = 0x696D, pool = club + 4 * 0x618;
    for (size_t slot = 0; slot < 12; slot++) {
        const int id = base.state.at(pool + slot * 52 + 15);
        if (id) base.state.at(0x4FC6 + size_t(id - 1) * 33 + 0x1A) = 6;
    }
    std::fill_n(base.state.begin() + long(pool), 12 * 52, uint8_t(0));
    const int older_id = base.state.at(club + 24 * 52 + 15);
    base.state.at(0x4FC6 + size_t(older_id - 1) * 33 + 0x14) = 50;
    Game listing(exe, encode_hok(base), 6);
    CHECK(listing.list_transfer_player(24) == TransferMoveResult::Moved);
    CHECK(listing.list_transfer_player(0) == TransferMoveResult::Moved);
    CHECK(listing.list_transfer_player(5) == TransferMoveResult::Moved);
    HokFile staged = decode_hok(listing.save_bytes());
    std::vector<int> returning;
    for (size_t slot = 0; slot < kTransferListSlots; slot++) {
        const size_t record = pool + slot * 52;
        const int id = staged.state.at(record + 15);
        if (!id || staged.state.at(0x4FC6 + size_t(id - 1) * 33 + 0x1A) != 0) continue;
        returning.push_back(id);
        staged.state.at(record + 9) |= 0x80;
        staged.state.at(record + 0x18) = 0x85;
        staged.state.at(record + 0x0C) = 1;
        for (int byte = 0; byte < 4; byte++) staged.state.at(record + 0x30 + size_t(byte)) = uint8_t(3000u >> (8 * byte));
    }
    CHECK(returning.size() == 3);
    Game direct(exe, encode_hok(staged), 6);
    auto resumed = std::make_unique<Game>(exe, direct.checkpoint_bytes(), 999);
    for (int day = 0; direct.seasons_completed() == 0 && day < 50; day++) {
        direct.advance_day(); resumed->advance_day();
        resumed = std::make_unique<Game>(exe, resumed->checkpoint_bytes(), 999);
    }
    CHECK(direct.seasons_completed() == 1 && direct.checkpoint_bytes() == resumed->checkpoint_bytes());
    const HokFile after = decode_hok(direct.save_bytes());
    CHECK(after.squad.size() == base.squad.size());
    for (int id : returning) {
        for (size_t slot = 0; slot < kTransferListSlots; slot++) CHECK(after.state.at(pool + slot * 52 + 15) != id);
        int found = -1;
        for (size_t slot = 0; slot < after.squad.size(); slot++)
            if (after.state.at(club + slot * 52 + 15) == id) found = int(slot);
        CHECK(found >= 0);
        if (found < 0) continue;
        const size_t record = club + size_t(found) * 52;
        CHECK(after.state.at(record + 11) == 1);
        CHECK(after.state.at(record + 12) == 0 && (after.state.at(record + 0x18) & 0x80) == 0);
        CHECK(le32(after.state, record + 0x30) == 3000);
        const size_t player = 0x4FC6 + size_t(id - 1) * 33;
        CHECK(after.state.at(player + 0x1A) == 0 && after.state.at(player + 0x1D) == direct.human_team());
    }
    CHECK(after.state.at(0x4FC6 + size_t(older_id - 1) * 33 + 0x14) == 51);
    if (fs::exists(ref_dir / "POOLPR01.HOK")) {
        Game native_case(exe, read_file(ref_dir / "POOLPR01.HOK"), 6);
        for (int day = 0; native_case.seasons_completed() == 0 && day < 50; day++) native_case.advance_day();
        const HokFile native_after = decode_hok(native_case.save_bytes());
        CHECK(native_case.seasons_completed() == 1 && native_after.squad.size() == 25);
        int found = -1;
        for (size_t slot = 0; slot < native_after.squad.size(); slot++)
            if (native_after.state.at(club + slot * 52 + 15) == 110) found = int(slot);
        CHECK(found >= 0);
        if (found >= 0) CHECK(native_after.state.at(club + size_t(found) * 52 + 11) == 1);
        CHECK(native_after.state.at(0x4FC6 + 109 * 33 + 20) == 23);
        for (size_t slot = 0; slot < kTransferListSlots; slot++) CHECK(native_after.state.at(pool + slot * 52 + 15) != 110);
    }
    for (bool disabled : {false, true}) {
        Game offered(exe, encode_hok(base), 6);
        CHECK(offered.list_transfer_player(24) == TransferMoveResult::Moved);
        HokFile special = decode_hok(offered.save_bytes());
        int pool_slot = -1;
        for (size_t slot = 0; slot < kTransferListSlots; slot++)
            if (special.state.at(pool + slot * 52 + 15) == older_id) pool_slot = int(slot);
        CHECK(pool_slot >= 0);
        if (pool_slot < 0) continue;
        const size_t record = pool + size_t(pool_slot) * 52;
        special.state.at(record + 11) = disabled ? 2 : 1;
        if (disabled) {
            special.state.at(record + 9) |= 2;
            special.state.at(record + 13) = kCareerOver;
        }
        for (int byte = 0; byte < 4; byte++) special.state.at(record + 0x30 + size_t(byte)) = uint8_t(3000u >> (8 * byte));
        Game review(exe, encode_hok(special), 6);
        int target_talks = 0;
        const std::string target_name = base.players[size_t(older_id - 1)].name;
        review.set_contract_decider([&](const ContractPlayer& player, long) {
            target_talks += player.age == 51;
            return 0;
        });
        for (int day = 0; review.seasons_completed() == 0 && day < 50; day++) review.advance_day();
        CHECK(review.seasons_completed() == 1);
        const HokFile reviewed = decode_hok(review.save_bytes());
        CHECK(reviewed.state.at(0x4FC6 + size_t(older_id - 1) * 33 + 0x1A) == 6);
        for (size_t slot = 0; slot < 29; slot++) CHECK(reviewed.state.at(club + slot * 52 + 15) != older_id);
        for (size_t slot = 0; slot < kTransferListSlots; slot++) CHECK(reviewed.state.at(pool + slot * 52 + 15) != older_id);
        CHECK(target_talks == (disabled ? 0 : 1));
        const std::string payment = target_name + " disability insurance payment 60000 DM";
        CHECK(std::count(review.log().begin(), review.log().end(), payment) == (disabled ? 1 : 0));
    }
    std::puts("own transfer seasons: three returns, expiry/disability review, contracts, insurance and daily checkpoints checked");
}

static void transfer_list(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    Game g(exe, read_file(ref_dir / "START.HOK"), 3);
    int refreshes = 0;
    for (int i = 0; i < 60; i++) {
        g.advance_day();
        for (const std::string& line : g.log()) refreshes += line.rfind("transfer list:", 0) == 0;
        g.clear_log();
    }
    CHECK(refreshes >= 1 && refreshes < 30);  // chance 1/9 a day (rand(0, 6 + 2), whole-program test)
    const HokFile h = decode_hok(g.save_bytes());
    const ContractTables t = load_contract_tables(exe);
    BorlandRandom rng(1);
    std::vector<int> listed;
    int last_class = 0;
    for (int slot = 0; slot < kTransferListSlots; slot++) {
        int p = -1;
        const ContractPlayer c = contract_player(h, 4, slot, &p);
        if (p < 0) continue;
        const uint8_t* pr = h.state.data() + 0x4FC6 + size_t(p) * 0x21;
        CHECK(pr[0x1A] == 4 && int(pr[0x1D]) != g.human_team());
        const int cls = pr[0x18] == 0 ? 0 : pr[0x18] < 60 ? 1 : 2;
        CHECK(cls >= last_class);
        last_class = cls;
        CHECK(c.experience >= 35 && c.experience <= 65);
        CHECK(player_worth(t, c, Worth::Value, rng) == c.value && player_worth(t, c, Worth::Salary, rng) == c.salary);
        listed.push_back(p);
    }
    CHECK(!listed.empty());
    int owner4 = 0;
    for (int p = 0; p < 199; p++) owner4 += h.state[0x4FC6 + size_t(p) * 0x21 + 0x1A] == 4;
    CHECK(owner4 == int(listed.size()));
    // An own player in the market keeps his contract and ownership through
    // refreshes. The native replacement branch accepts only empty/CPU slots.
    HokFile reserved = decode_hok(read_file(ref_dir / "START.HOK"));
    constexpr size_t pool = 0x696D + 4 * 0x618;
    const size_t own_slot = reserved.squad.size() - 1;
    const int own_id = reserved.state[0x696D + own_slot * 52 + 15];
    // Replace the last occupied CPU slot with an own forward, retaining class order.
    int pool_slot = 0;
    for (int slot = 0; slot < 10; slot++) if (reserved.state[pool + size_t(slot) * 52 + 15]) pool_slot = slot;
    const int old_id = reserved.state[pool + size_t(pool_slot) * 52 + 15];
    reserved.state[0x4FC6 + size_t(old_id - 1) * 33 + 0x1A] = 6;
    std::copy_n(reserved.state.begin() + long(0x696D + own_slot * 52), 52,
                reserved.state.begin() + long(pool + size_t(pool_slot) * 52));
    std::fill_n(reserved.state.begin() + long(0x696D + own_slot * 52), 52, uint8_t(0));
    reserved.squad.pop_back();
    store_hok(reserved);
    const Bytes own_pool = encode_hok(reserved);
    int refresh_count = 0;
    for (uint32_t seed = 1; seed <= 12; seed++) {
        Game game(exe, own_pool, seed);
        for (int day = 0; day < 19; day++) {
            game.advance_day();
            for (const auto& line : game.log()) refresh_count += line.rfind("transfer list:", 0) == 0;
            game.clear_log();
            const auto& state = game.save().state;
            int occurrences = 0;
            for (size_t slot = 0; slot < kTransferListSlots; slot++) occurrences += state[pool + slot * 52 + 15] == own_id;
            CHECK(occurrences == 1 && state[0x4FC6 + size_t(own_id - 1) * 33 + 0x1A] == 0);
            CHECK(state[0x4FC6 + size_t(own_id - 1) * 33 + 0x1D] == game.human_team());
        }
        Game restored(exe, game.checkpoint_bytes(), 999);
        game.advance_day(); restored.advance_day();
        CHECK(game.checkpoint_bytes() == restored.checkpoint_bytes());
    }
    CHECK(refresh_count > 3);
    std::printf("transfer list: %d refreshes in 60 days, %zu players listed\n", refreshes, listed.size());
}

// Injuries (1000:3d1a, 146e:0c89, ovl06_364f): tables from EM.EXE, match injuries only of types
// 0-21, weeks within the type's range; over a season the human club has a plausible number of
// injuries, an injured player has no role and comes back when fit.
static void injuries(const fs::path& em_dir, const fs::path& ref_dir) {
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const InjuryTables t = load_injury_tables(exe);
    CHECK(t.names[0] == "Schädelbruch" && t.names[27] == "Darmerkrankung" && t.weeks[14][1] == 40);
    BorlandRandom rng(7);
    int career = 0;
    for (int i = 0; i < 2000; i++) {
        const int kind = i % 2;
        const Injury in = draw_injury(t, kind, 2, rng);
        CHECK(in.type >= 0 && in.type <= 27 - 6 * kind);
        if (in.weeks == kCareerOver) {
            career++;
            CHECK(std::find(t.career_types.begin(), t.career_types.end(), in.type) != t.career_types.end());
        } else {
            CHECK(in.weeks >= t.weeks[size_t(in.type)][0] && in.weeks <= t.weeks[size_t(in.type)][1]);
        }
    }
    CHECK(career > 0 && career < 100);
    if (!fs::exists(ref_dir / "START.HOK")) return;
    for (bool automatic : {false, true}) {
        HokFile recovering = decode_hok(read_file(ref_dir / "START.HOK"));
        constexpr size_t slot = 1, record = 0x696D + slot * 52;
        const uint8_t original_role = recovering.state.at(record + 10);
        recovering.state.at(0x078A) = recovering.state.at(0x078B) = automatic ? 4 : 0;
        recovering.state.at(record + 9) |= 2;
        recovering.state.at(record + 13) = 1;
        recovering.state.at(record + 10) = 0;
        Game source(exe, encode_hok(recovering), 6);
        auto injured = decode_game_checkpoint(source.checkpoint_bytes());
        injured.injured_roles[slot] = original_role;
        Game direct(exe, encode_game_checkpoint(injured), 999);
        auto resumed = std::make_unique<Game>(exe, direct.checkpoint_bytes(), 123);
        for (int day = 0; day < 6; day++) {
            direct.advance_day();
            resumed->advance_day();
            resumed = std::make_unique<Game>(exe, resumed->checkpoint_bytes(), 123);
        }
        CHECK(direct.save().state.at(record + 13) == 0);
        CHECK((direct.save().state.at(record + 9) & 3) == 0);
        // 146e:1732 clears the injury and requests ordinary auto_lineup.
        // With manual selection it never restores the remembered role.
        CHECK(automatic ? direct.save().state.at(record + 10) != 0 : direct.save().state.at(record + 10) == 0);
        CHECK(decode_game_checkpoint(direct.checkpoint_bytes()).injured_roles[slot] == 0);
        CHECK(direct.checkpoint_bytes() == resumed->checkpoint_bytes());
    }
    int hurt = 0, fit = 0;  // three seasons with different seeds: the numbers vary a lot
    for (uint32_t seed = 11; seed <= 13; seed++) {
        Game g(exe, read_file(ref_dir / "START.HOK"), seed);
        for (int i = 0; i < 232; i++) {
            g.advance_day();
            for (const std::string& line : g.log()) {
                hurt += line.find(", out for ") != std::string::npos || line.find(", career over") != std::string::npos;
                fit += line.find(" is fit again") != std::string::npos;
            }
            g.clear_log();
        }
        for (size_t i = 0; i < g.save().squad.size(); i++) {
            const uint8_t* r = g.save().state.data() + 0x696D + i * 0x34;
            if (r[0x09] & 2) CHECK(r[0x0A] == 0 && r[0x0D] > 0);
        }
    }
    CHECK(hurt >= 10 && hurt <= 120 && fit > 0);
    std::printf("injuries: %d in three seasons, %d back (2000 draws: %d careers over)\n", hurt, fit, career);
}

// Offers to the manager (146e:0c89, 1000:3bb8, ovl11_05f1): find an offer in a bounded set of
// seasons, then check the actual sale. A single random stream depends on unrelated cup results.
static void manager_offers(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const Bytes initial = read_file(ref_dir / "START.HOK");
    std::unique_ptr<Game> candidate;
    int days = 0;
    uint32_t offered_seed = 0;
    for (uint32_t seed = 1; seed <= 40; seed++) {  // the first seed whose first offer is an unlisted computer bid (the random stream decides)
        candidate = std::make_unique<Game>(exe, initial, seed);
        days = 0;
        while (candidate->offers().empty() && days < 232) {
            candidate->advance_day();
            candidate->clear_log();
            days++;
        }
        if (!candidate->offers().empty() && !candidate->offers()[0].listed && candidate->offers()[0].amount == 0 && candidate->offers()[0].bidder >= 0 && candidate->offers()[0].bidder < 43 && candidate->offers()[0].bidder != candidate->human_team()) {
            offered_seed = seed;
            break;
        }
    }
    Game& g = *candidate;
    CHECK(!g.offers().empty());
    if (g.offers().empty()) return;
    const ManagerOffer o = g.offers()[0];
    CHECK(!o.listed && o.bidder >= 0 && o.bidder < 43 && o.bidder != g.human_team() && o.amount == 0);
    const long before = g.budget().balance;
    const size_t squad = g.save().squad.size();
    const std::string name = g.save().players[size_t(o.player)].name;
    {
        const Bytes bytes = g.save_bytes();
        Game restored(exe, bytes, 12);
        CHECK(restored.offers().size() == g.offers().size());
        CHECK(restored.offers()[0].player == o.player && restored.offers()[0].bidder == o.bidder);
        CHECK(!restored.offers()[0].listed && restored.offers()[0].amount == 0);
        CHECK(restored.save_bytes() == bytes);
        const long balance = restored.budget().balance;
        // Resolve by the decoded global player identity, not an eager saved price.
        int slot=0;while(slot<int(restored.save().squad.size())&&restored.save().squad[size_t(slot)].player!=o.player)++slot;
        SaleQuote quote;CHECK(restored.begin_sale_offer(0,slot,0,quote));
        CHECK(restored.finish_sale_offer(quote,true));
        CHECK(restored.budget().balance == balance + quote.amounts.net);
        Game declined(exe, bytes, 12);
        declined.decline_offer(0);
        Game no_offer(exe, declined.save_bytes(), 12);
        CHECK(no_offer.offers().size() + 1 == g.offers().size());  // one offer fewer (the seed may produce a second one)
    }
    {
        Game base(exe, initial, 12);
        HokFile listed = decode_hok(base.save_bytes());
        const size_t own_at = 0x696D + 12 * 52, at = 0x696D + 4 * 0x618;
        std::copy_n(listed.state.begin() + long(own_at), 52, listed.state.begin() + long(at));
        listed.state[at + 9] |= 0x80;
        listed.state[at + 0x16] = 12;
        // A duplicate interest in the club record must not create a second offer.
        listed.state[own_at + 9] |= 0x40;
        listed.state[own_at + 0x16] = 13;
        const Bytes bytes = encode_hok(listed);
        Game restored(exe, bytes, 12);
        CHECK(restored.offers().size() == 1 && restored.offers()[0].listed && restored.offers()[0].bidder == 12);
        CHECK(restored.save_bytes() == bytes);
        restored.decline_offer(0);
        Game declined(exe, restored.save_bytes(), 12);
        CHECK(declined.offers().size()==1 && !declined.offers()[0].listed);
        CHECK((declined.save().state[at + 9] & 0x80) == 0 && (declined.save().state[own_at + 9] & 0x40) != 0);
        declined.decline_offer(0);CHECK(declined.offers().empty());
        listed.state[at + 0x16] = 255;
        listed.state[own_at + 9] &= 0x3F;
        Game invalid(exe, encode_hok(listed), 12);
        CHECK(invalid.offers().empty());
    }
    const long old_costs = g.budget().squad_costs;
    long sold_costs = 0;
    for (size_t slot = 0; slot < g.save().squad.size(); slot++) if (g.save().squad[slot].player == o.player) {
        const size_t at = 0x696D + slot * 52;
        sold_costs = long(le32(g.save().state, at + 0x28) + le32(g.save().state, at + 0x30));
    }
    int offer_slot=0;while(offer_slot<int(g.save().squad.size())&&g.save().squad[size_t(offer_slot)].player!=o.player)++offer_slot;
    const int actual_buyer=g.save().state[0x696D+size_t(offer_slot+1)*52+22];
    SaleQuote sale;CHECK(g.begin_sale_offer(0,offer_slot,0,sale));CHECK(g.finish_sale_offer(sale,true));
    CHECK(g.budget().balance == before + sale.amounts.net && g.save().squad.size() == squad - 1);
    CHECK(g.budget().players == int(squad - 1) && g.budget().squad_costs == old_costs - sold_costs);
    CHECK(g.save().players[size_t(o.player)].team == actual_buyer && g.save().players[size_t(o.player)].raw[0x1A] == 6);
    CHECK(g.save().players[size_t(o.player)].raw[0x1D] == actual_buyer);  // immediate raw readers, before export
    for (const HokSquadPlayer& sp : g.save().squad) CHECK(g.save().players[size_t(sp.player)].name != name);
    const HokFile again = decode_hok(g.save_bytes());
    CHECK(again.squad.size() == squad - 1);
    for (size_t i = 0; i < again.squad.size(); i++)  // slot k still holds the k-th player of the club
        CHECK(again.state[0x696D + i * 0x34 + 0x0F] == again.squad[i].player + 1);
    for (int i = 0; i < 30; i++) g.advance_day();
    std::printf("offers: seed %u, day %d %s sold for %ld DM\n", unsigned(offered_seed), days, name.c_str(), long(sale.amounts.net / 3));
}

// Contract expiry (ovl02_0000): START contracts have 2 years, so they run out at the second
// season change. The decider lets the first player go and offers the rest 2 years: the one let
// go leaves for half his value, the others either sign (2 years, new salary) or refuse and leave.
static void contract_expiry(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 2);
    int asked = 0;
    long demand_sum = 0;
    g.set_contract_decider([&](const ContractPlayer&, long demand) {
        demand_sum += demand;
        return asked++ == 0 ? 0 : 2;
    });
    const size_t squad = g.save().squad.size();
    int signed_up = 0, left = 0, disabled = 0, joined = 0;
    while (g.seasons_completed() < 2) {
        g.advance_day();
        for (const std::string& line : g.log()) {
            signed_up += line.find(" signs for 2 years") != std::string::npos;
            left += line.find(" leaves the club") != std::string::npos;
            disabled += line.find(" leaves after permanent injury") != std::string::npos;
            joined += line.find(" joins from the youth team") != std::string::npos;  // annual youth intake refills the squad
        }
        g.clear_log();
    }
    CHECK(asked + disabled == int(squad) && left >= 1 && signed_up + left == asked);
    CHECK(g.save().squad.size() == squad - size_t(left + disabled) + size_t(joined));
    int short_contracts = 0;  // youth joiners get 1..2 years, everybody else signed 2
    for (size_t i = 0; i < g.save().squad.size(); i++) {
        const uint8_t* r = g.save().state.data() + 0x696D + i * 0x34;
        short_contracts += r[0x0B] == 1;
        CHECK((r[0x0B] == 2 || r[0x0B] == 1) && r[0x0F] == g.save().squad[i].player + 1);
    }
    CHECK(short_contracts <= joined);
    const HokFile again = decode_hok(g.save_bytes());
    CHECK(again.squad.size() == g.save().squad.size());
    std::printf("contracts: %d asked (about %ld DM each), %d signed, %d left\n", asked,
                asked ? demand_sum / asked / 3 : 0, signed_up, left);
}

// Years: the header holds the calendar year (MONAT4 in February 1994: 1994), state 0x8E31 the
// year the season started (1993); loading such a save keeps the date right and saving keeps both.
static void save_years(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT4.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "MONAT4.HOK"), 1);
    g.advance_day();
    CHECK(g.date().year == 1994 && g.date().month == 2);
    const HokFile h = decode_hok(g.save_bytes());
    CHECK(h.year == 1994 && h.season_year == 1993);
    std::printf("years: MONAT4 plays %02d.%02d.%d, header %d, season %d\n", g.date().day, g.date().month, g.date().year,
                h.year, h.season_year);
}

// Scorers of the computer clubs (1000:3228 / 2fb6): after 9 league matchdays from START the
// first division clubs look like MONAT1 (skaters 291 goals, 285 assists; goalkeepers 5940 minutes).
static void club_scorers(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK") || !fs::exists(ref_dir / "MONAT1.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 3);
    while (g.next_matchday(0) < 10) {
        g.advance_day();
        g.clear_log();
    }
    auto sum = [&](const HokFile& h, int& goals, int& assists, int& minutes, int& max_games) {
        goals = assists = minutes = max_games = 0;
        for (const HokPlayer& pl : h.players) {
            if (pl.team < 0 || pl.team >= 12 || pl.team == g.human_team()) continue;
            const int x = pl.raw[0x1F] | pl.raw[0x20] << 8;
            if (pl.position == 0) minutes += x;
            else goals += pl.league_goals, assists += x;
            max_games = std::max(max_games, pl.league_games);
        }
    };
    int g1, a1, m1, n1, g2, a2, m2, n2;
    const HokFile ours = decode_hok(g.save_bytes()), orig = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    sum(ours, g1, a1, m1, n1);
    sum(orig, g2, a2, m2, n2);
    CHECK(m1 == m2 && n1 == n2 && g1 > g2 / 2 && g1 < g2 * 2 && a1 > a2 / 2 && a1 < a2 * 2);
    std::printf("club scorers: goals %d (MONAT1 %d), assists %d (%d), goalkeeper minutes %d (%d), games %d (%d)\n",
                g1, g2, a1, a2, m1, m2, n1, n2);
}

// Head-to-head block (ovl13_22b8): after 9 matchdays from START Köln has a result against the same
// first division opponents in the same column (home / away) as in MONAT1, and the results agree
// with the table (wins, draws, losses).
static void human_match_statistics(const fs::path& em_dir, const fs::path& ref_dir) {
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const auto calendar = load_season_calendar(exe);
    auto check = [](const HokFile& before, const HokFile& after, int own, int against, int competition, bool full, int shootout = 0) {
        const bool cup = competition == 1;
        int goals = 0, bytes = 0, assists = 0, player_goals = 0, games = 0, player_games = 0, minutes = 0, conceded = 0;
        for (const auto& sp : before.squad) {
            if (sp.player < 0) continue;
            const auto found = std::find_if(after.squad.begin(), after.squad.end(), [&](const auto& p) { return p.player == sp.player; });
            CHECK(found != after.squad.end());
            if (found == after.squad.end()) continue;
            const auto& next = *found;
            const auto& a = before.players[size_t(sp.player)];
            const auto& b = after.players[size_t(sp.player)];
            if (competition == 2) {
                const size_t old_at = 0x696D + size_t(&sp - before.squad.data()) * 52;
                const size_t new_at = 0x696D + size_t(&next - after.squad.data()) * 52;
                const int count = le16(after.state, new_at + 0x20) - le16(before.state, old_at + 0x20);
                CHECK(after.state[new_at + 8] - before.state[old_at + 8] == count);
                games += count;
            } else games += cup ? next.cup_games - sp.cup_games : next.league_games - sp.league_games;
            player_games += b.league_games - a.league_games;
            if (a.position == 0) {
                minutes += le16(after.state, size_t(b.raw - after.state.data()) + 0x1F) -
                           le16(before.state, size_t(a.raw - before.state.data()) + 0x1F);
                conceded += next.league_goals_byte - sp.league_goals_byte;
                continue;
            }
            goals += competition == 2 ? next.other_goals - sp.other_goals : cup ? next.cup_goals - sp.cup_goals : next.league_goals - sp.league_goals;
            bytes += competition == 2 ? next.other_goals_byte - sp.other_goals_byte : cup ? next.cup_goals_byte - sp.cup_goals_byte : next.league_goals_byte - sp.league_goals_byte;
            assists += le16(after.state, size_t(b.raw - after.state.data()) + 0x1F) -
                       le16(before.state, size_t(a.raw - before.state.data()) + 0x1F);
            player_goals += b.league_goals - a.league_goals;
        }
        CHECK(goals == own - shootout && bytes == own - shootout && assists == own);
        CHECK(player_goals == (competition == 0 ? own - shootout : 0));
        CHECK(games > 0 && player_games == (full ? games : 0));
        CHECK(minutes == (full ? 60 : 0) && conceded == (full ? against : 0));
    };
    // Independent native observations establish the competition-specific field meanings.
    for (const auto& p : {std::array<const char*, 2>{"CUPCHECK", "CUPPOST"}, {"EUCHECK", "EUPOST"}, {"POSEMI1", "POSEMIP"}}) {
        if (!fs::exists(ref_dir / (std::string(p[0]) + ".HOK")) || !fs::exists(ref_dir / (std::string(p[1]) + ".HOK"))) continue;
        const bool cup = std::string(p[0]) == "CUPCHECK", full = std::string(p[0]) == "EUCHECK";
        check(decode_hok(read_file(ref_dir / (std::string(p[0]) + ".HOK"))),
              decode_hok(read_file(ref_dir / (std::string(p[1]) + ".HOK"))), cup ? 8 : full ? 4 : 2, cup ? 3 : full ? 1 : 5, cup, full);
    }
    if (fs::exists(ref_dir / "EUOWN.HOK") && fs::exists(ref_dir / "EUOWNR.HOK"))
        check(decode_hok(read_file(ref_dir / "EUOWN.HOK")), decode_hok(read_file(ref_dir / "EUOWNR.HOK")), 3, 3, 2, false);
    if (fs::exists(ref_dir / "CUPCHECK.HOK")) {
        // Equalise the opponent's team strength for this field-booking probe.
        // CUPCHECK's actual Memmingen/Köln strength gap made a sampled draw
        // depend on unrelated daily RNG calls. Native reference pairs above
        // still validate the competition's real, unmodified field meanings.
        HokFile balanced = decode_hok(read_file(ref_dir / "CUPCHECK.HOK"));
        const int human = balanced.state.at(0x0DE1);
        for (const auto& pair : balanced.cup_draw) {
            if (pair.home != human && pair.away != human) continue;
            const int opponent = pair.home == human ? pair.away : pair.home;
            for (int kind = 0; kind < 2; kind++) {
                balanced.teams[size_t(opponent)].skill1[kind] = balanced.teams[size_t(human)].skill1[kind];
                balanced.teams[size_t(opponent)].skill2[kind] = balanced.teams[size_t(human)].skill2[kind];
                balanced.teams[size_t(opponent)].form[kind] = balanced.teams[size_t(human)].form[kind];
            }
            balanced.teams[size_t(opponent)].bonus = balanced.teams[size_t(human)].bonus;
        }
        store_hok(balanced);
        const Bytes balanced_input = encode_hok(balanced);
        bool found = false;
        for (uint32_t seed = 1; seed <= 128 && !found; seed++) {
            Game game(exe, balanced_input, seed);
            while (calendar.event(game.season_day() + 1) != kEventCup) game.advance_day();
            const HokFile before = decode_hok(game.save_bytes());
            game.advance_day();
            for (const auto& series : game.cup_series()) {
                if (series.higher != game.human_team() && series.lower != game.human_team()) continue;
                const auto& result = series.games.back();
                if (!result.shootout) continue;
                const bool home = (series.games.size() % 2 == 1) == (series.higher == game.human_team());
                const int side = home ? 0 : 1;
                if (result.shootout_goals[side] == 0) continue;
                const int own = home ? result.home : result.away, against = home ? result.away : result.home;
                CHECK(result.decision_marker == 60);
                check(before, decode_hok(game.save_bytes()), own, against, 1, false, result.shootout_goals[side]);
                Game reloaded(exe, game.save_bytes(), seed);
                CHECK(reloaded.save_bytes() == game.save_bytes());
                std::printf("human cup shoot-out: seed %u, %d:%d including %d own penalty goals\n", seed, own, against, result.shootout_goals[side]);
                found = true;
            }
        }
        CHECK(found);
    }
    for (const char* input : {"START", "CUPCHECK", "EUCHECK", "POSEMI1"}) {
        const fs::path path = ref_dir / (std::string(input) + ".HOK");
        if (!fs::exists(path)) continue;
        const bool league = std::string(input) == "START", cup = std::string(input) == "CUPCHECK",
                   euro = std::string(input) == "EUCHECK";
        const int event = cup ? kEventCup : euro ? kEventEuroCup : kEventPlayoff;
        HokFile source = decode_hok(read_file(path));
        if (euro) source.state[0x03AC] = source.state[0x0DE1];  // controlled human participant in the final group
        Game game(exe, encode_hok(source), 12);
        while (league ? !calendar.league_day(game.season_day() + 1) : calendar.event(game.season_day() + 1) != event) game.advance_day();
        const HokFile before = decode_hok(game.save_bytes());
        const int team = game.human_team();
        game.advance_day();
        const HokFile after = decode_hok(game.save_bytes());
        int own = -1, against = -1, shootout_goals = 0;
        if (league) {
            const int md = before.matchday[0] - 1;
            for (size_t pair = 0; pair < before.pairings[0].size(); pair++) {
                const auto p = before.pairings[0][pair];
                if (p.home != team && p.away != team) continue;
                const size_t at = 0x0796 + size_t(md) * 12 + pair * 2;
                own = after.state[at + (p.home == team ? 0 : 1)];
                against = after.state[at + (p.home == team ? 1 : 0)];
            }
        } else if (euro) {
            const int round = before.state[0x8F86], kind = round < 6 ? 2 : 3;
            for (int group = 0; group < (round < 6 ? 2 : 1); group++)
                for (int pair = 0; pair < 2; pair++) {
                    const size_t offset = size_t(kind * 0x48 + group * 0x24 + (round % 6) * 6 + pair * 2);
                    const int home = before.state[0x03B8 + offset], away = before.state[0x03B9 + offset];
                    if (home != team && away != team) continue;
                    own = after.state[0x04D8 + offset + (home == team ? 0 : 1)];
                    against = after.state[0x04D8 + offset + (home == team ? 1 : 0)];
                }
        } else {
            for (const auto& series : cup ? game.cup_series() : game.playoff_series()) {
                if (series.higher != team && series.lower != team) continue;
                if (series.games.empty()) continue;
                const auto score = series.games.back();
                const bool home = (series.games.size() % 2 == 1) == (series.higher == team);
                own = home ? score.home : score.away;
                against = home ? score.away : score.home;
                shootout_goals = score.shootout_goals[home ? 0 : 1];
            }
        }
        if (own < 0) std::fprintf(stderr, "human statistics: no own fixture for %s on day %d, euro round %u\n", input, game.season_day(), unsigned(before.state[0x8F86]));
        CHECK(own >= 0 && against >= 0);
        if (own >= 0) check(before, after, own, against, cup ? 1 : euro ? 2 : 0, league, shootout_goals);
        Game restored(exe, game.save_bytes(), 12);
        CHECK(restored.save_bytes() == game.save_bytes());
    }
    std::printf("human statistics: native and simulated goals, assists, games and goalkeeper fields across competitions checked\n");
}

static void head_to_head(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK") || !fs::exists(ref_dir / "MONAT1.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 3);
    while (g.next_matchday(0) < 10) {
        g.advance_day();
        g.clear_log();
    }
    const HokFile ours = decode_hok(g.save_bytes()), orig = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    int same = 0, entries = 0, w = 0, d = 0, l = 0;
    for (size_t t = 0; t < 12; t++)
        for (size_t c = 0; c < 2; c++) {
            const uint8_t x = ours.state[0x8F88 + t * 10 + c], y = orig.state[0x8F88 + t * 10 + c];
            same += (x == 0xFF) == (y == 0xFF);
            if (x == 0xFF) continue;
            entries++;
            const int own = x & 15, against = x >> 4;
            w += own > against, d += own == against, l += own < against;
        }
    const HokTableRow& row = ours.table[size_t(g.human_team())];
    CHECK(same == 24 && entries == 9);
    CHECK(w == row.total(row.wins) && l == row.total(row.losses) && d == 9 - w - l);
    // club records (ovl13_3e43): highest home defeat / away win never fill (unsigned compare), like MONAT1
    auto rec = [](const HokFile& h, size_t t, size_t r) { return int(h.state[0xA11C + t * 16 + r * 2] | h.state[0xA11D + t * 16 + r * 2] << 8); };
    int filled = 0, filled_orig = 0, best_home = 0;
    for (size_t t = 0; t < 12; t++) {
        CHECK(rec(ours, t, 1) == 0 && rec(ours, t, 4) == 0);
        for (size_t r : {0, 2, 3, 5, 6, 7}) filled += rec(ours, t, r) != 0, filled_orig += rec(orig, t, r) != 0;
    }
    for (size_t t = 0; t < 12; t++) {
        const uint8_t x = ours.state[0x8F88 + t * 10];  // Köln home games against t (first slot)
        if (x != 0xFF) best_home = std::max(best_home, x & 15);
    }
    CHECK(filled > 60 && (rec(ours, size_t(g.human_team()), 2) >> 8) >= best_home);
    std::printf("records: %d of 72 filled (MONAT1 %d)\n", filled, filled_orig);
    std::printf("head to head: 9 results in the same slots as MONAT1, %d-%d-%d\n", w, d, l);
}

static void hok_round_trip(const fs::path& em_dir, const fs::path& ref_dir) {
    int files = 0;
    for (const fs::path& dir : {em_dir, ref_dir})
        for (const auto& e : fs::directory_iterator(dir)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
            if (ext != ".HOK") continue;
            if (e.path().stem().string() == "OPENEM") continue;  // written by an early OpenEM test, not by EM.EXE
            const Bytes original = read_file(e.path());
            HokFile h = decode_hok(original);
            store_hok(h);
            CHECK(encode_hok(h) == original);
            files++;
        }
    std::printf("hok round trip: %d files identical\n", files);
    CHECK(files >= 6);
}

// Saving the running game: 30 days from START saved and loaded again continue on day 31 with the
// same balance, tables and squad values; the saved file decodes with the usual reader.
static void game_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    {
        HokFile hole = decode_hok(read_file(ref_dir / "START.HOK"));
        hole.state[0x696D + 15] = 0;  // a free slot may retain old role/line/contract bytes
        const Bytes bytes = encode_hok(hole);
        HokFile parsed = decode_hok(bytes);
        CHECK(parsed.squad.size() == hole.squad.size() && parsed.squad[0].player == -1 && parsed.squad[0].role == 0);
        store_hok(parsed);
        CHECK(encode_hok(parsed) == bytes);
        Game resumed(exe, bytes, 6);
        for (int day = 0; day < 10; day++) resumed.advance_day();
        CHECK(decode_hok(resumed.save_bytes()).state[0x696D + 15] == 0);
        hole.state[0x696D + 15] = 200;
        bool rejected = false;
        try { (void)decode_hok(encode_hok(hole)); } catch (const FormatError&) { rejected = true; }
        CHECK(rejected);
    }
    if (fs::exists(ref_dir / "SAISEND.HOK")) {
        // Empty native records remain at their original slots across a season
        // boundary; they are not player -1 in the retirement membership table.
        HokFile depleted = decode_hok(read_file(ref_dir / "SAISEND.HOK"));
        for (size_t slot : {size_t(0), size_t(7), size_t(12)}) depleted.state[0x696D + slot * 52 + 15] = 0;
        Game rollover(exe, encode_hok(depleted), 6);
        for (int day = 0; day < 20; day++) {
            rollover.advance_day();
            CHECK(rollover.image() >= 0 && rollover.image() <= 100);
        }
        CHECK(rollover.seasons_completed() == 1);
        for (size_t slot : {size_t(0), size_t(7), size_t(12)}) CHECK(rollover.save().squad.at(slot).player == -1);
        Game resumed(exe, rollover.checkpoint_bytes(), 999);
        for (int day = 0; day < 3; day++) { rollover.advance_day(); resumed.advance_day(); }
        CHECK(rollover.save_bytes() == resumed.save_bytes());
        for (size_t slot : {size_t(0), size_t(7), size_t(12)})
            CHECK(decode_hok(resumed.save_bytes()).state[0x696D + slot * 52 + 15] == 0);
    }
    Game g(exe, read_file(ref_dir / "START.HOK"), 6);
    // The cup books the series too (ovl13_2f93, DS:D0C2 = 8): the league-form check runs before the first cup date.
    auto check_series = [](const HokFile& h) {
        for (int t = 0; t < 12; t++) {  // series (state 0x9988): the current streaks follow the form string
            const std::string& form = h.table[size_t(t)].form;
            int won = 0, lost = 0;
            for (size_t i = form.size(); i-- > 0 && (form[i] == 's' || form[i] == 'S');) won++;
            for (size_t i = form.size(); i-- > 0 && (form[i] == 'n' || form[i] == 'N');) lost++;
            const size_t b = 0x9988 + size_t(t) * 0x15;
            CHECK((won < 8 ? h.state[b] == won : h.state[b] >= 8) && (lost < 8 ? h.state[b + 3] == lost : h.state[b + 3] >= 8));
        }
    };
    for (int i = 0; i < 30; i++) {
        g.advance_day();
        g.clear_log();
        if (i == 9) check_series(decode_hok(g.save_bytes()));
    }
    const Bytes saved = g.save_bytes();
    CHECK(saved.size() == read_file(ref_dir / "START.HOK").size());
    HokFile h = decode_hok(saved);
    CHECK(h.date.day_index == 31 && h.matchday[0] == g.next_matchday(0) && h.year == 1993);
    CHECK(le16(h.state, 0xB953) == h.date.day_index);  // DS:4F59 drives the original's event calendar
    for (int t = 0; t < 36; t++) {  // table rows as EM.EXE expects them: points, copies, fever curve
        const size_t b = 0x3BA8 + size_t(t) * 0x50;
        const HokTableRow& r = h.table[size_t(t)];
        CHECK(h.state[b] == 2 * r.wins[0] + r.draws(0) && h.state[b + 1] == 2 * r.wins[1] + r.draws(1));
        CHECK(h.state[b + 2] == h.state[b] && h.state[b + 0x18] == h.state[b + 0x16] && h.state[b + 0x28] == r.wins[0]);
        CHECK(r.position_history[g.next_matchday(0) - 2] == r.position);
    }
    CHECK(le16(h.state, 0x0F4C) > 0 && le16(h.state, 0x0F4E) > 0);  // power plays / penalty kills counted
    Game back(exe, saved, 6);
    CHECK(back.season_day() == 30 && back.budget().balance == g.budget().balance && back.image() == g.image());
    for (int l = 0; l < 3; l++)
        for (int s = 0; s < kHokLeagueSize; s++) {
            const HokTableRow& a = g.league(l).rows[size_t(s)];
            const HokTableRow& b = back.league(l).rows[size_t(s)];
            CHECK(a.total(a.games) == b.total(b.games) && a.total(a.goals_for) == b.total(b.goals_for) &&
                  a.position == b.position && a.points_plus() == b.points_plus());
        }
    for (size_t i = 0; i < g.save().squad.size(); i++) {
        CHECK(g.save().squad[i].skill_a == back.save().squad[i].skill_a &&
              g.save().squad[i].contract_years == back.save().squad[i].contract_years);
        CHECK(g.save().squad[i].raw_exhaustion == back.save().squad[i].raw_exhaustion &&
              g.save().squad[i].fitness == back.save().squad[i].fitness);
    }
    back.advance_day();
    CHECK(back.season_day() == 31);
    std::printf("game save: saved on day 30, loaded and continued (balance %ld DM)\n", back.budget().balance / 3);
}

// Interest already accrued in the original must survive loading and subsequent saves.
// Advertising records use native HOK offsets, independently of the game serializers.
static void finance_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile h = decode_hok(read_file(ref_dir / "START.HOK"));
    auto put = [&](size_t o, uint32_t v) {
        for (int i = 0; i < 4; i++) h.state[o + size_t(i)] = uint8_t(v >> (8 * i));
    };
    put(0, uint32_t(-12345));  // DS:02A0, manager 0's accrued interest
    put(0x0F78, 402000);      // savings: 4020 raw interest per month
    put(0xACFC, 90000);       // DS:C15B, first advertising contract, raw amount
    h.state[0xAD00] = 7;     // board sponsor
    h.state[0xAD01] = 3;     // months left
    h.state[0xAD02] = 12;    // option
    h.state[0xAD03] = 8;     // sponsor
    h.state[0xAD04] = 70;    // required image
    h.state[0xAD05] = 0;
    const Bytes initial = encode_hok(h);
    Game g(exe, initial, 6);
    const HokFile untouched = decode_hok(g.save_bytes());
    CHECK(le32(untouched.state, 0x0F64) == le32(h.state, 0x0F64));
    CHECK(std::equal(h.state.begin() + 0xA52C, h.state.begin() + 0xACFC,
                     untouched.state.begin() + 0xA52C));
    CHECK(g.budget().accrued == -12345);
    CHECK(g.budget().adverts[0].amount == 90000 && g.budget().adverts[0].months_left == 3 &&
          g.budget().adverts[0].flags == 12);
    CHECK(monthly_income(g.budget(), false) == g.budget().team_strength * 5L * g.budget().players + 90000);
    // Cross the offer refresh on September 15 and the contract countdown on October 1.
    for (int i = 0; i < 22; i++) {
        g.advance_day();
        if (i == 5) {
            Game midmonth(exe, g.save_bytes(), 6);
            CHECK(g.budget().accrued == -12345 + 6 * (4020 / 31));
            CHECK(midmonth.budget().accrued == g.budget().accrued);
        }
    }
    CHECK(g.budget().adverts[0].months_left == 2);
    CHECK(g.budget().accrued == 2 * (4020 / 32));  // October 2 is the next saved day; both days booked
    const Bytes saved = g.save_bytes();
    const HokFile stored = decode_hok(saved);
    CHECK(int32_t(le32(stored.state, 0)) == g.budget().accrued);
    CHECK(le32(stored.state, 0xACFC) == 90000 && stored.state[0xAD01] == 2);
    CHECK(stored.state[0xAD00] == 7 && stored.state[0xAD02] == 12 && stored.state[0xAD03] == 8 &&
          le16(stored.state, 0xAD04) == 70);
    // At least one generated offer differs from the original, and all survive save/load/save.
    CHECK(!std::equal(h.state.begin() + 0xA52C, h.state.begin() + 0xA720, stored.state.begin() + 0xA52C));
    Game back(exe, saved, 6);
    CHECK(back.budget().accrued == g.budget().accrued && back.budget().adverts[0].months_left == 2);
    const HokFile again = decode_hok(back.save_bytes());
    CHECK(std::equal(stored.state.begin() + 0xA52C, stored.state.begin() + 0xA720, again.state.begin() + 0xA52C));
    CHECK(std::equal(h.state.begin() + 0xA720, h.state.begin() + 0xACFC, stored.state.begin() + 0xA720));
    CHECK(std::equal(h.state.begin() + 0xAD6A, h.state.begin() + 0xAEB4, stored.state.begin() + 0xAD6A));
    // Saving and loading cannot change the month-end calculation.
    ManagerBudget a = g.budget(), b = back.budget();
    const FinanceTables tables = load_finance_tables(exe);
    for (int day = 3; day <= 31; day++) {  // October 2 is already booked in both menu saves
        finance_day(tables, a, day, 31);
        finance_day(tables, b, day, 31);
    }
    CHECK(a.balance == b.balance && a.accrued == 0 && b.accrued == 0);
    // DS:A07E is now located through the complete writer block map. Its three
    // signed cost components must contribute without rewriting their native split.
    const ManagerBudget no_other = budget_from_save(h, 0);
    put(0x06C1, 10000);
    put(0x06C5, 20000);
    put(0x06C9, uint32_t(-15000));
    put(0x06D6, 7000);  // manager 1 has its own 0x15-byte record
    Game costs(exe, encode_hok(h), 6);
    CHECK(costs.budget().open_a07e == 15000 && budget_from_save(h, 1).open_a07e == 7000);
    CHECK(monthly_expenses(tables, costs.budget(), false, false) == monthly_expenses(tables, no_other, false, false) + 15000);
    const HokFile cost_saved = decode_hok(costs.save_bytes());
    CHECK(std::equal(h.state.begin() + 0x06B8, h.state.begin() + 0x070C, cost_saved.state.begin() + 0x06B8));
    std::printf("finance save: contracts, sponsor offers and accrued interest preserved\n");
}

// Saving immediately after a cup date must neither replay that date nor skip the next round.
static void cup_save_dates(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const SeasonCalendar calendar = load_season_calendar(exe);
    for (int round = 1; round <= 3; round++) {
        Game g(exe, read_file(ref_dir / "START.HOK"), 6);
        const int day = calendar.nth_day(kEventCup, round);
        while (g.season_day() < day) g.advance_day();
        Game back(exe, g.save_bytes(), 6);
        for (int d = day + 1; d <= calendar.nth_day(kEventCup, 4); d++) {
            g.advance_day();
            back.advance_day();
            CHECK(decode_hok(g.save_bytes()).cup_draw.size() == decode_hok(back.save_bytes()).cup_draw.size());
        }
    }
    std::printf("cup save: all three first-round date boundaries continue without replay or skipped rounds\n");
}

static void cup_series_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), start = read_file(ref_dir / "START.HOK");
    const SeasonCalendar calendar = load_season_calendar(exe);
    auto check = [](Game& game, int days) {
        const HokFile h = decode_hok(game.save_bytes());
        // the third final day closes the final (sub_42756 at game index 2, REAL-CODE period 57): winner and runner-up
        // booked, wins 0, game index stays 2, DS:D0E4 = 1, the grid pair keeps both finalists; nothing is booked earlier
        if (days == 15 && game.cup_winner() >= 0) {
            CHECK(h.cup_round == 5 && h.state[0x031B] == 2 && h.state[0x037C] == 0 && h.state[0x037D] == 0 && h.state[0x077C] == 1);
            CHECK(h.state[0x0DC7] == game.cup_winner() && h.state[0x0DCC] != game.cup_winner() && h.state[0x8E51] != h.state[0x8E52]);
            CHECK(h.state[0x8E51] == game.cup_winner() || h.state[0x8E52] == game.cup_winner());
            return;
        }
        if (days < 15) CHECK(game.cup_winner() < 0);
        const int round = std::min(days / 3 + 1, 5), played = days == 15 ? 2 : days % 3;
        CHECK(h.cup_round == round && h.state[0x031B] == played && h.state[0x8F84] == round);
        CHECK(h.cup_draw.size() == size_t(16 >> std::min(days / 3, 4)));
        bool human_in_draw = false;
        for (const auto& pair : h.cup_draw)
            human_in_draw |= pair.home == game.human_team() || pair.away == game.human_team();
        const int reached = h.state[0x0DCD + 0xE6];
        if (human_in_draw)
            CHECK(reached == round + (days == 15 && game.cup_winner() == game.human_team()));
        else CHECK(reached >= 1 && reached < round);
        const auto& series = game.cup_series();
        if (days % 3 == 0 && days < 15) {
            CHECK(series.empty());
            return;
        }
        CHECK(series.size() == h.cup_draw.size());
        for (size_t pair = 0; pair < series.size(); pair++) {
            const Series& s = series[pair];
            CHECK(s.wins_higher <= 2 && s.wins_lower <= 2 && s.games.size() <= 3);
            CHECK(s.games.size() == size_t(s.wins_higher + s.wins_lower));
            const int first = h.cup_draw[pair].home;
            int first_wins = 0, second_wins = 0;
            for (size_t match = 0; match < s.games.size(); match++) {
                const size_t o = 0x031C + match * 32 + pair * 2;
                const int a = h.state[o] % 30, b = h.state[o + 1] % 30;
                CHECK(a != b && h.state[o] != 100 && h.state[o + 1] != 100);
                first_wins += a > b; second_wins += a < b;
                const MatchResult& result = s.games[match];
                CHECK((h.state[o] / 30 + h.state[o + 1] / 30) * 30 == result.decision_marker);
                CHECK(((a > b) == (first == s.higher)) == ((result.home > result.away) == (match % 2 == 0)));
            }
            CHECK(first_wins == (first == s.higher ? s.wins_higher : s.wins_lower));
            CHECK(second_wins == (first == s.higher ? s.wins_lower : s.wins_higher));
            CHECK(h.state[0x037C + pair * 2] == first_wins && h.state[0x037D + pair * 2] == second_wins);
        }
        if (days == 15) CHECK(game.cup_winner() == series[0].winner() && h.state[0x0DC7] == game.cup_winner());
    };
    for (uint32_t seed : {2u, 9u})
        for (int checkpoint = 1; checkpoint <= 15; checkpoint++) {
            Game game(exe, start, seed);
            while (game.season_day() < calendar.nth_day(kEventCup, checkpoint)) game.advance_day();
            check(game, checkpoint);
            const Bytes bytes = game.save_bytes();
            Game resumed(exe, bytes, seed);
            check(resumed, checkpoint);
            // Loading recomputes the club record from the squad (162c:146c, as the original after loading). After a cup day
            // without a game that was also a training day the saved record still holds the weekly drift (REAL-CODE period 49),
            // so only skill1/skill2/form of the own club may change; everything else must survive the round trip.
            {
                HokFile x = decode_hok(bytes), y = decode_hok(resumed.save_bytes());
                const int own = x.state.at(0x0DCD + 0x14);
                // Since Köln period 94 every cup day of the own club that does not end the step skips the evening refresh, so the
                // skater count +0x32 of the record (recounted by 146c) may change on loading too.
                if (own >= 0 && own < kHokTeams)
                    for (size_t f : {0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x32}) x.state.at(0x17E7 + size_t(own) * 61 + f) = y.state.at(0x17E7 + size_t(own) * 61 + f);
                bool same = x.state == y.state;
                for (size_t i = 0; i < x.state.size() && !same; i++)
                    if (x.state[i] != y.state[i]) { std::fprintf(stderr, "cup resume diff at state %zx: %02x vs %02x (checkpoint %d)\n", i, x.state[i], y.state[i], checkpoint); break; }
                CHECK(same);
            }
            if (checkpoint < 15) {
                while (resumed.season_day() < calendar.nth_day(kEventCup, checkpoint + 1)) resumed.advance_day();
                check(resumed, checkpoint + 1);
            }
        }
    if (fs::exists(ref_dir / "CUPPOST.HOK")) {
        Game native(exe, read_file(ref_dir / "CUPPOST.HOK"), 9);
        check(native, 2);
        bool own_found = false;
        for (const Series& s : native.cup_series())
            if (s.higher == 7 || s.lower == 7) {
                CHECK(s.wins_higher == 1 && s.wins_lower == 1 && s.games.size() == 2);
                own_found = true;
            }
        CHECK(own_found);
        while (native.season_day() < calendar.nth_day(kEventCup, 3)) native.advance_day();
        check(native, 3);
    }
    if (fs::exists(ref_dir / "CUPSHOT.HOK") && fs::exists(ref_dir / "CUPSHRP.HOK")) {
        const HokFile before = decode_hok(read_file(ref_dir / "CUPSHOT.HOK"));
        const HokFile after = decode_hok(read_file(ref_dir / "CUPSHRP.HOK"));
        CHECK(after.cup_round == 2 && after.cup_draw.size() == 8 && after.state[0x031B] == 0);
        CHECK(after.state[0x0DCD + 0xE6] == 1);
        for (const auto& pair : after.cup_draw) {
            CHECK(pair.home != 7 && pair.away != 7);
            CHECK(pair.home / 12 >= pair.away / 12);
        }
        auto unchanged_cup_players = [&](const HokFile& h) {
            for (size_t slot = 0; slot < before.squad.size(); slot++) {
                const size_t at = 0x696D + slot * 52;
                CHECK(h.state[at + 4] == before.state[at + 4]);
                CHECK(h.state[at + 7] == before.state[at + 7]);
                CHECK(le16(h.state, at + 0x1E) == le16(before.state, at + 0x1E));
                CHECK(le16(h.state, at + 0x24) == le16(before.state, at + 0x24));
            }
        };
        unchanged_cup_players(after);
        Game native(exe, read_file(ref_dir / "CUPSHRP.HOK"), 6);
        const HokFile saved = decode_hok(native.save_bytes());
        CHECK(std::equal(after.state.begin() + 0x8E51, after.state.begin() + 0x8E71, saved.state.begin() + 0x8E51));
        while (native.season_day() < calendar.nth_day(kEventCup, 4)) native.advance_day();
        CHECK(native.save().state[0x0DCD + 0xE6] == 1);
        unchanged_cup_players(decode_hok(native.save_bytes()));
    }
    std::printf("Cup save: best of three, 15 boundaries x 2 seeds, winner and single results checked\n");
}

// Original re-entry into the manager menu automatically changes three lineup
// bytes in FINMRW. Compare every selected role and line with a guest-RAM capture.
static void original_club_lineup(const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "FINMRW.HOK") || !fs::exists(ref_dir / "FINEVLOD.HOK")) return;
    HokFile h = decode_hok(read_file(ref_dir / "FINMRW.HOK"));
    const HokFile native = decode_hok(read_file(ref_dir / "FINEVLOD.HOK"));
    CHECK(penalty_effort_adjustment(native, 7) == -20);
    // Verify the unusual one-based owner address, including national owner 6.
    h.state[0x0DCD + 0x1AF + 0xF0] = 47;
    CHECK(penalty_effort_adjustment(h, 7) == 27);
    h.state[0x0DCD + 0x1AF + 0xF0] = native.state[0x0DCD + 0x1AF + 0xF0];
    const uint8_t owner = h.state[0x17E7 + 7 * 61 + 0x16];
    h.state[0x17E7 + 7 * 61 + 0x16] = 6;
    h.state[0x0830] = 4;
    CHECK(penalty_effort_adjustment(h, 7) == -16);
    h.state[0x17E7 + 7 * 61 + 0x16] = owner;
    h.state[0x0830] = native.state[0x0830];
    CHECK(auto_lineup(h, 0));
    int selected = 0;
    for (size_t slot = 0; slot < 29; slot++) {
        const size_t at = 0x696D + slot * 52;
        CHECK(h.state[at + 10] == native.state[at + 10]);
        CHECK(h.state[at + 21] == native.state[at + 21]);
        selected += h.state[at + 10] != 0;
        if (slot < h.squad.size() && h.squad[slot].player >= 0) {
            CHECK(h.squad[slot].role == native.squad[slot].role);
            CHECK(h.squad[slot].line == native.squad[slot].line);
        }
    }
    CHECK(selected == 22);
    const Bytes once = h.state;
    CHECK(auto_lineup(h, 0) && h.state == once);  // no random tie drift
    h.state[0x078A] = 0;
    const Bytes manual = h.state;
    CHECK(!auto_lineup(h, 0) && h.state == manual);
    // An injured first-choice player must be replaced, even with maximum skills.
    h.state[0x078A] = 1;
    const size_t injured = 0x696D + 17 * 52;
    h.state[injured + 9] |= 2;
    h.squad[17].skill_a = h.squad[17].skill_b = h.squad[17].form = 255;
    CHECK(auto_lineup(h, 0) && h.squad[17].role == 0);
    CHECK(h.state[injured + 9] & 2);
    std::puts("original club lineup: all 29 roles/lines match DOSBox; manual mode and injury replacement retained");
}

// Native DOSBox continuation of FINMRW: Sep 27 -> cup Sep 29 -> Oct 1, saved as FINMONP.
// The monthly snapshot had team strength 320 (stored income 130000 raw), while the live
// post-save team is 321. Check the settlement using the actual snapshot used by the original.
static void original_finance_month(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "FINMRW.HOK") || !fs::exists(ref_dir / "FINMONP.HOK")) return;
    const FinanceTables t = load_finance_tables(read_file(em_dir / "EM.EXE"));
    const HokFile before = decode_hok(read_file(ref_dir / "FINMRW.HOK"));
    const HokFile after = decode_hok(read_file(ref_dir / "FINMONP.HOK"));
    ManagerBudget b = budget_from_save(before, 0);
    const ManagerBudget actual = budget_from_save(after, 0);
    CHECK(b.balance == 2024355 && b.accrued == -12345 && b.bank == 402000);
    CHECK(actual.stored_income == 130000 && actual.balance == 694697 && actual.accrued == 125);
    CHECK(le16(after.state, 0xB953) == 22 && after.date.day_index == 22);
    b.team_strength = 320;
    for (int day = 28; day <= 30; day++) finance_day(t, b, day, 30);
    finance_day(t, b, 1, 31);
    CHECK(b.balance == actual.balance && b.accrued == actual.accrued && b.bank == actual.bank);
    CHECK(b.adverts[0].amount == actual.adverts[0].amount && actual.adverts[0].amount == 90000);
    CHECK(b.adverts[0].months_left == actual.adverts[0].months_left && actual.adverts[0].months_left == 2);
    CHECK(actual.adverts[0].flags == 12);
    // The original's automatic lineup changes strength 321 -> 320 immediately after
    // loading FINMRW (read-only DOSBox capture FINEV_loaded, 2026-11-02). The whole
    // day loop must use that lineup and export Oct 1's already-booked finances.
    Game resumed(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "FINMRW.HOK"), 6);
    CHECK(resumed.budget().team_strength == 320);
    for (int day = 18; day <= 21; day++) resumed.advance_day();
    const HokFile exported = decode_hok(resumed.save_bytes());
    const ManagerBudget model = budget_from_save(exported, 0);
    CHECK(exported.date.day_index == 22 && model.accrued == actual.accrued);
    CHECK(model.adverts[0].months_left == actual.adverts[0].months_left);
    CHECK(model.balance == actual.balance && model.bank == actual.bank);
    CHECK(model.stored_income == actual.stored_income);
    std::printf("original finance month: settlement %ld raw, accrued %ld raw, contract %d months as in DOSBox\n",
                actual.balance, actual.accrued, actual.adverts[0].months_left);
}

// EuroCup native tables and results must survive saves in either phase, including the
// transition to the final group and resuming an actual original mid-tournament reference.
static void euro_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), start = read_file(ref_dir / "START.HOK");
    const SeasonCalendar calendar = load_season_calendar(exe);
    auto points = [](const HokFile& h, int kind, int group) {
        int total = 0;
        for (int slot = 0; slot < 4; slot++) total += h.state[0x0628 + size_t(kind * 12 + group * 6 + slot)];
        return total;
    };
    auto check_results = [](const HokFile& h, int kind, int group, int played) {
        std::map<int, std::array<int, 3>> rebuilt;  // goals for, against, points, keyed by team
        for (size_t i = 0; i < 4; i++) {
            const int team = kind == 2 ? h.euro_groups[size_t(group)][i] : h.state[0x03AC + i];
            rebuilt[team] = {};
        }
        for (int round = 0; round < played; round++)
            for (int pair = 0; pair < 2; pair++) {
                const size_t slot = size_t(kind * 0x48 + group * 0x24 + round * 6 + pair * 2);
                const int home = h.state[0x03B8 + slot], away = h.state[0x03B8 + slot + 1];
                const int hg = h.state[0x04D8 + slot], ag = h.state[0x04D8 + slot + 1];
                CHECK(rebuilt.count(home) == 1 && rebuilt.count(away) == 1 && home != away);
                rebuilt[home][0] += hg;
                rebuilt[home][1] += ag;
                rebuilt[away][0] += ag;
                rebuilt[away][1] += hg;
                rebuilt[home][2] += hg > ag ? 2 : hg == ag ? 1 : 0;
                rebuilt[away][2] += ag > hg ? 2 : hg == ag ? 1 : 0;
            }
        for (size_t i = 0; i < 4; i++) {
            const int team = kind == 2 ? h.euro_groups[size_t(group)][i] : h.state[0x03AC + i];
            const size_t slot = size_t(kind * 12 + group * 6) + i;
            CHECK(rebuilt[team][0] == h.state[0x02BB + slot]);
            CHECK(rebuilt[team][1] == h.state[0x02EB + slot]);
            CHECK(rebuilt[team][2] == h.state[0x0628 + slot]);
        }
    };
    auto check = [&](Game& g, int round) {
        const HokFile h = decode_hok(g.save_bytes());
        const auto live_image = image_competition_factors(g.save(), 0);
        const auto exported_image = image_competition_factors(h, 0);
        CHECK(live_image.playoffs == exported_image.playoffs && live_image.cup == exported_image.cup &&
              live_image.euro == exported_image.euro);
        CHECK(h.state[0x8F86] == round);
        for (int group = 0; group < 2; group++) {
            CHECK(points(h, 2, group) == std::min(round, 6) * 4);
            check_results(h, 2, group, std::min(round, 6));
        }
        if (round >= 6) {
            CHECK(points(h, 3, 0) == (round - 6) * 4);
            check_results(h, 3, 0, round - 6);
            std::vector<int> teams(h.state.begin() + 0x03AC, h.state.begin() + 0x03B0);
            std::sort(teams.begin(), teams.end());
            CHECK(std::adjacent_find(teams.begin(), teams.end()) == teams.end());
            for (int team : teams) {
                bool found = false;
                for (const auto& group : h.euro_groups)
                    found |= std::find(group.begin(), group.end(), team) != group.end();
                CHECK(found);
            }
        }
        if (round == 12) CHECK(g.euro_champion() >= 0 && h.state[0x0DC9] == g.euro_champion());
    };
    for (int checkpoint : {1, 5, 6, 7, 11}) {
        Game g(exe, start, 6);
        while (g.season_day() < calendar.nth_day(kEventEuroCup, checkpoint)) g.advance_day();
        check(g, checkpoint);
        Game back(exe, g.save_bytes(), 6);
        while (back.season_day() < calendar.nth_day(kEventEuroCup, checkpoint + 1)) back.advance_day();
        check(back, checkpoint + 1);
    }
    if (fs::exists(ref_dir / "MONAT2.HOK")) {
        const Bytes native = read_file(ref_dir / "MONAT2.HOK");
        const HokFile original = decode_hok(native);
        Game g(exe, native, 6);
        const int round = original.state[0x8F86];
        CHECK(round == 7 && points(original, 2, 0) == 24 && points(original, 3, 0) == 4);
        while (g.season_day() < calendar.nth_day(kEventEuroCup, round + 1)) g.advance_day();
        check(g, round + 1);
        const HokFile h = decode_hok(g.save_bytes());
        for (size_t i = 0; i < 4; i++) CHECK(h.state[0x03AC + i] == original.state[0x03AC + i]);
    }
    if (fs::exists(ref_dir / "SAISEND.HOK")) {
        Game g(exe, read_file(ref_dir / "SAISEND.HOK"), 6);
        CHECK(g.euro_champion() == 83);  // Slovan Bratislava, 10:2 points in the native final group
    }
    if (fs::exists(ref_dir / "EUPOST.HOK")) {
        // EUCHECK (OpenEM's re-export of MONAT2) was loaded and continued in DOSBox.
        // The original played four further EuroCup dates before this Nov 29 menu save.
        Game g(exe, read_file(ref_dir / "EUPOST.HOK"), 6);
        check(g, 11);
        while (g.season_day() < calendar.nth_day(kEventEuroCup, 12)) g.advance_day();
        check(g, 12);
    }
    if (fs::exists(ref_dir / "EUOWN.HOK") && fs::exists(ref_dir / "EUOWNR.HOK")) {
        const Bytes input = read_file(ref_dir / "EUOWN.HOK");
        const HokFile actual = decode_hok(read_file(ref_dir / "EUOWNR.HOK"));
        Game own(exe, input, 6);
        CHECK(own.save().state[0x0DCD + 0xE7] == 30);
        const int round = own.save().state[0x8F86];
        while (own.season_day() < calendar.nth_day(kEventEuroCup, round + 1)) own.advance_day();
        CHECK(actual.state[0x0DCD + 0xE7] == 4);
        CHECK(own.save().state[0x0DCD + 0xE7] == actual.state[0x0DCD + 0xE7]);
        Game restored(exe, own.checkpoint_bytes(), 999);
        CHECK(restored.save_bytes() == own.save_bytes());
        own.advance_day(); restored.advance_day();
        CHECK(restored.save_bytes() == own.save_bytes());
    }
    // A controlled first-group entrant records 2 only after actually playing.
    HokFile group_source = decode_hok(start);
    const int human = group_source.state[0x0DCD + 0x14];
    const int replaced = group_source.euro_groups[0][0];
    group_source.euro_groups[0][0] = human;
    for (auto& round : group_source.euro_rounds[0])
        for (auto& pair : round) {
            if (pair.home == replaced) pair.home = human;
            if (pair.away == replaced) pair.away = human;
        }
    group_source.state[0x0DCD + 0xE7] = 30;
    store_hok(group_source);
    Game first_group(exe, encode_hok(group_source), 6);
    CHECK(first_group.save().state[0x0DCD + 0xE7] == 30);
    while (first_group.season_day() < calendar.nth_day(kEventEuroCup, 1)) first_group.advance_day();
    CHECK(first_group.save().state[0x0DCD + 0xE7] == 2);
    if (fs::exists(ref_dir / "LICCHK22.HOK")) {
        for (int phase : {0, 30, 4}) {
            HokFile winner = decode_hok(read_file(ref_dir / "LICCHK22.HOK"));
            const int own_team = winner.state[0x0DCD + 0x14];
            const int top = winner.state[0x05F8 + 3 * 12];
            winner.state[0x03AC + size_t(top)] = uint8_t(own_team);
            winner.state[0x0DCD + 0xE7] = uint8_t(phase);
            const int old_titles = winner.state[0x0DCD + 0x17];
            Game title(exe, encode_hok(winner), 6);
            CHECK(title.euro_champion() == own_team);
            while (title.seasons_completed() == 0) title.advance_day();
            const HokFile saved = decode_hok(title.save_bytes());
            CHECK(saved.state[0x0DCD + 0x17] == old_titles + (phase == 4));
            CHECK(saved.state[0x0DCD + 0xF3] == (phase == 4 ? 0x20 : 0x04));
            CHECK(saved.state[0x0DE5 + 3] == (phase == 4 ? 0x8D : winner.state[0x0DE5 + 3]));
            CHECK(title.human_team() / 12 == (phase == 4 ? 0 : 1));
            for (size_t manager = 0; manager < 6; manager++)
                CHECK(saved.state[0x0DCD + manager * 0x1AF + 0xE7] == 30);
        }
    }
    std::printf("EuroCup save: group/final phase, five save boundaries and native references checked\n");
}

// Native advertising deals: capacity, whole-record placement and options across reloads.
static void sponsor_contracts(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    HokFile h = decode_hok(read_file(ref_dir / "START.HOK"));
    ManagerBudget budget = budget_from_save(h, 0);
    budget.adverts = {};
    for (size_t i = 0; i < 5; i++) budget.adverts[i] = {long(10000 * (i + 1)), 3, 0, int(i), int(i), 20};
    budget.adverts[7] = {60000, 3, 0, 1, 2, 40};
    store_budget(h, budget, 0);
    h.state[0x01F2] = 5;  // five existing board deals consumed ids 0..4
    SponsorOffers offers{};
    offers[0][0] = {75000, 3, 24, 12, 7, 50};  // insufficient room, then two boards fit
    offers[0][1] = {75000, 2, 24, 12, 7, 50};
    offers[1][0] = {90000, 1, 12, 0, 3, 30};
    offers[2][0] = {120000, 1, 12, 0, 4, 60};
    offers[3][0] = offers[4][0] = {150000, 1, 12, 0, 5, 60};
    store_sponsor_offers(h.state, offers, 0);
    // These two extension levels deliberately do not qualify for kinds 3/4.
    h.state[0x0DCD + 0x116] = h.state[0x0DCD + 0x11A] = 0;
    Game game(exe, encode_hok(h), 12);
    const Bytes before = game.save_bytes();
    CHECK(!game.accept_sponsor_offer(-1, 0) && !game.accept_sponsor_offer(5, 0));
    CHECK(!game.accept_sponsor_offer(0, 10) && !game.accept_sponsor_offer(0, 2));
    CHECK(!game.accept_sponsor_offer(0, 0) && !game.accept_sponsor_offer(1, 0));
    CHECK(!game.accept_sponsor_offer(3, 0) && !game.accept_sponsor_offer(4, 0));
    CHECK(game.save_bytes() == before);
    const long balance = game.budget().balance, income = monthly_income(game.budget(), false);
    CHECK(game.accept_sponsor_offer(0, 1));
    CHECK(game.budget().balance == balance && monthly_income(game.budget(), false) == income + 150000);
    CHECK(!game.accept_sponsor_offer(0, 1));
    const auto& ads = game.budget().adverts;
    CHECK(ads[5].amount == 75000 && ads[6].amount == 75000);
    CHECK(ads[5].board_owner == ads[6].board_owner && ads[5].board_owner != 4);
    CHECK(game.save().state[0x01F2] == 6);
    CHECK(ads[5].sponsor == 7 && ads[5].months_left == 24 && ads[5].flags == 12 && ads[5].min_image == 50);
    CHECK(game.sponsor_offers()[0][1].amount == 0 && game.sponsor_offers()[0][1].boards == 2);
    CHECK(game.accept_sponsor_offer(2, 0) && game.budget().adverts[8].amount == 120000);
    const Bytes accepted = game.save_bytes();
    Game loaded(exe, accepted, 12);
    CHECK(loaded.save_bytes() == accepted);
    // Other managers' complete offer/contract blocks remain untouched.
    const HokFile saved = decode_hok(accepted);
    CHECK(std::equal(h.state.begin() + 0xAD6A, h.state.begin() + 0xAEB4, saved.state.begin() + 0xAD6A));
    CHECK(std::equal(h.state.begin() + 0xA720, h.state.begin() + 0xACFC, saved.state.begin() + 0xA720));
    HokFile facilities = decode_hok(before);
    facilities.state[0x0DCD + 0x116] = facilities.state[0x0DCD + 0x11A] = 2;
    Game qualified(exe, encode_hok(facilities), 12);
    CHECK(qualified.accept_sponsor_offer(3, 0) && qualified.accept_sponsor_offer(4, 0));
    // A saved option must resolve all boards of the same deal exactly once. With
    // required image 0 acceptance is certain; 65535 guarantees sponsor rejection.
    for (int action = 0; action < 3; action++) {
        HokFile option = decode_hok(accepted);
        ManagerBudget b = budget_from_save(option, 0);
        for (size_t i : {size_t(5), size_t(6)}) {
            b.adverts[i].months_left = 0;
            b.adverts[i].min_image = action == 2 ? 65535 : 0;
        }
        store_budget(option, b, 0);
        Game renewing(exe, encode_hok(option), 12);
        CHECK(renewing.renew_advert_contract(5, action != 1) == (action == 0));
        CHECK(!renewing.renew_advert_contract(5) && !renewing.renew_advert_contract(99));
        for (size_t i : {size_t(5), size_t(6)}) {
            const auto& a = renewing.budget().adverts[i];
            CHECK(a.flags == 0 && a.amount == (action == 0 ? 75000 : 0));
            CHECK(a.months_left == (action == 0 ? 12 : 0));
        }
        CHECK(renewing.budget().adverts[4].amount == 50000);
        Game reload(exe, renewing.save_bytes(), 12);
        CHECK(reload.budget().adverts[5].months_left == (action == 0 ? 12 : 0));
        if (action == 0) {
            Game expired(exe, encode_hok(option), 12);
            expired.advance_day();
            CHECK(expired.budget().adverts[5].amount == 0 && expired.budget().adverts[6].amount == 0);
            CHECK(expired.budget().adverts[5].board_owner == 255);
        }
    }
    if (fs::exists(ref_dir / "ADSCHECK.HOK") && fs::exists(ref_dir / "ADSPOST.HOK")) {
        // Original UI: two JWE-Service boards at 30000 DM each, 24 months plus
        // 12-month option. ADSPOST was written by the native save dialog.
        Game native_case(exe, read_file(ref_dir / "ADSCHECK.HOK"), 12);
        CHECK(native_case.accept_sponsor_offer(0, 0));
        const HokFile model = decode_hok(native_case.save_bytes());
        const HokFile actual = decode_hok(read_file(ref_dir / "ADSPOST.HOK"));
        CHECK(std::equal(model.state.begin() + 0xACFC, model.state.begin() + 0xAD6A,
                         actual.state.begin() + 0xACFC));
        CHECK(std::equal(model.state.begin() + 0xA52C, model.state.begin() + 0xA720,
                         actual.state.begin() + 0xA52C));
        CHECK(model.state[0x01F2] == 2 && actual.state[0x01F2] == 2);
        CHECK(le32(model.state, 0x0F64) == 220125 && le32(actual.state, 0x0F64) == 220125);
        CHECK(native_case.budget().balance == 1500000);
        Game imported(exe, read_file(ref_dir / "ADSPOST.HOK"), 12);
        CHECK(imported.budget().adverts[5].board_owner == imported.budget().adverts[6].board_owner);
        CHECK(imported.budget().adverts[5].months_left == 24);
    }
    if (fs::exists(ref_dir / "ADRENEW.HOK") && fs::exists(ref_dir / "ADRENOK.HOK")) {
        Game native_option(exe, read_file(ref_dir / "ADRENEW.HOK"), 12);
        const int stored_image = native_option.image();
        CHECK(native_option.renew_advert_contract(5));
        CHECK(native_option.image() == stored_image);  // a negotiation calculates image without storing it
        const HokFile model = decode_hok(native_option.save_bytes());
        const HokFile actual = decode_hok(read_file(ref_dir / "ADRENOK.HOK"));
        CHECK(std::equal(model.state.begin() + 0xACFC, model.state.begin() + 0xAD6A,
                         actual.state.begin() + 0xACFC));
        CHECK(le16(model.state, 0x0F40) == le16(actual.state, 0x0F40));
        CHECK(model.state[0xAD33] == 12 && model.state[0xAD34] == 0);
        CHECK(model.state[0xAD3D] == 12 && model.state[0xAD3E] == 0);
    }
    {  // Month-start expiry clears the amount but retains native metadata (ovl08_1d61).
        ManagerBudget b = budget;
        b.adverts[0] = {90000, 1, 0, 8, 9, 70};
        finance_day(load_finance_tables(exe), b, 1, 30);
        CHECK(b.adverts[0].amount == 0 && b.adverts[0].months_left == 0);
        CHECK(b.adverts[0].board_owner == 8 && b.adverts[0].sponsor == 9 && b.adverts[0].min_image == 70);
    }
    std::printf("sponsor contracts: capacity, facilities, grouped renewal/expiry and save/load checked\n");
}

// A loaded WM retains its group results and winner/loser bracket through all placements.
static void national_squads(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "NATAUTO.HOK")) return;
    const Bytes native_bytes = read_file(ref_dir / "NATAUTO.HOK");
    HokFile native = decode_hok(native_bytes);
    CHECK(native.squad.size() == 25);  // four called club players still occupy their club slots
    const auto squad = hok_squad(native, 5);
    CHECK(squad.size() == 29 && native.state[0x0788] == 5);
    std::array<int, 3> counts{};
    int club_called = 0;
    for (const auto& sp : native.squad) if (native.players[size_t(sp.player)].team == 56) club_called++;
    CHECK(club_called == 4);
    for (size_t slot = 0; slot < squad.size(); slot++) {
        const size_t at = 0x696D + 5 * 0x618 + slot * 52;
        const int id = native.state[at + 15];
        const auto& player = native.players[size_t(squad[slot].player)];
        CHECK(player.raw == native.state.data() + 0x4FA5 + size_t(id) * 33);
        CHECK(player.team == 56 && !player.foreigner);
        counts[size_t(player.position == 0 ? 0 : player.position < 60 ? 1 : 2)]++;
    }
    CHECK(counts == (std::array<int, 3>{3, 10, 16}));
    {
        HokFile grants = decode_hok(native_bytes);
        const size_t history = 0x3BD9 + 56 * 0x50;
        std::fill_n(grants.state.begin() + long(history), 16, uint8_t(0));
        CHECK(national_team_grant(grants) == 1500000);
        for (int place = 1; place <= 8; place++) {
            grants.state[history] = uint8_t(place);
            CHECK(national_team_grant(grants) == 900000 + (8 - place) * 300000);
        }
        grants.state[history] = 1;
        grants.state[history + 1] = 11;
        CHECK(national_team_grant(grants) == 300000);
    }
    store_hok(native);
    CHECK(encode_hok(native) == native_bytes);
    const Bytes clubs_before(native.state.begin() + 0x696D, native.state.begin() + 0x696D + 5 * 0x618);
    std::vector<int> old_teams;
    for (size_t slot = 0; slot < squad.size(); slot++) old_teams.push_back(native.state[0x696D + 5 * 0x618 + slot * 52 + 12] & 127);
    release_national_squad(native);
    CHECK(hok_squad(native, 5).empty() && native.squad.size() == 25);
    for (size_t i = 0; i < squad.size(); i++) CHECK(native.players[size_t(squad[i].player)].team == old_teams[i]);
    CHECK(std::equal(clubs_before.begin(), clubs_before.end(), native.state.begin() + 0x696D));
    const auto tables = load_national_team_tables(read_file(em_dir / "EM.EXE"));
    const auto contracts = load_contract_tables(read_file(em_dir / "EM.EXE"));
    BorlandRandom rng(12);
    auto_national_squad(native, tables, contracts, rng);
    const auto selected = hok_squad(native, 5);
    CHECK(selected.size() == squad.size());
    const HokFile original = decode_hok(native_bytes);
    for (size_t slot = 0; slot < selected.size(); slot++) {
        CHECK(selected[slot].player == squad[slot].player);
        const size_t at = 0x696D + 5 * 0x618 + slot * 52;
        for (size_t field = 0; field < 52; field++) {
            if (field == 0x0E || field == 0x13 || field == 0x14 || (field >= 0x28 && field < 0x2C)) continue;
            if (native.state[at + field] != original.state[at + field])
                std::fprintf(stderr, "national slot %zu field %02zx: model %u, native %u\n", slot, field,
                             unsigned(native.state[at + field]), unsigned(original.state[at + field]));
            CHECK(native.state[at + field] == original.state[at + field]);
        }
    }
    CHECK(std::equal(clubs_before.begin(), clubs_before.end(), native.state.begin() + 0x696D));
    const Bytes full = encode_hok(native);
    CHECK(!call_up_national_player(native, native.players.size(), contracts, rng));
    for (size_t p = 0; p < native.players.size(); p++)
        if (native.players[p].foreigner) { CHECK(!call_up_national_player(native, p, contracts, rng)); break; }
    CHECK(!call_up_national_player(native, size_t(selected[0].player), contracts, rng));
    CHECK(encode_hok(native) == full);
    const size_t removed = size_t(selected[3].player);
    CHECK(release_national_player(native, removed) && hok_squad(native, 5).size() == 28);
    CHECK(!release_national_player(native, removed));
    CHECK(call_up_national_player(native, removed, contracts, rng) && hok_squad(native, 5).size() == 29);
    {
        const Bytes exe = read_file(em_dir / "EM.EXE");
        Game match(exe, native_bytes, 12);
        const HokFile before = decode_hok(match.save_bytes());
        const SeasonCalendar calendar = load_season_calendar(exe);
        while (match.season_day() < calendar.nth_day(kEventWorldChampionship, 1)) match.advance_day();
        const HokFile after = decode_hok(match.save_bytes());
        int goals = 0, credited = 0, assists = 0;
        for (size_t group = 0; group < 2; group++)
            for (size_t pair = 0; pair < 3; pair++) {
                const auto p = after.wm_rounds[group][0][pair];
                if (p.home == 56) goals = after.state[0x04D8 + group * 36 + pair * 2];
                if (p.away == 56) goals = after.state[0x04D9 + group * 36 + pair * 2];
            }
        for (size_t slot = 0; slot < squad.size(); slot++) {
            const size_t at = 0x696D + 5 * 0x618 + slot * 52;
            const size_t player = size_t(before.players[size_t(squad[slot].player)].raw - before.state.data());
            CHECK(after.state[player + 0x19] == uint8_t(before.state[player + 0x19] + 1));
            CHECK(after.state[player + 0x1D] == 56);
            const int played = squad[slot].role != 0 && squad[slot].line != 0 ? 1 : 0;
            CHECK(after.state[at + 8] == uint8_t(before.state[at + 8] + played));
            CHECK(le16(after.state, at + 0x20) == le16(before.state, at + 0x20) + played);
            CHECK(after.state[at + 0x13] >= before.state[at + 0x13]);
            CHECK(after.state[at + 0x1B] <= 90);
            if (!played) CHECK(after.state[at + 0x1B] == 90);
            credited += le16(after.state, at + 0x22) - le16(before.state, at + 0x22);
            assists += le16(after.state, player + 0x1F) - le16(before.state, player + 0x1F);
        }
        CHECK(credited == goals && assists == goals);
        Game reloaded(exe, match.save_bytes(), 12);
        CHECK(reloaded.save_bytes() == match.save_bytes());
        HokFile capped = decode_hok(native_bytes);
        for (size_t slot = 0; slot < squad.size(); slot++) {
            const size_t at = 0x696D + 5 * 0x618 + slot * 52;
            const size_t player = size_t(capped.players[size_t(squad[slot].player)].raw - capped.state.data());
            capped.state[player + 0x19] = capped.state[player + 0x1B] = 255;
            capped.state[at + 3] = capped.state[at + 0x22] = 255;
            capped.state[at + 0x23] = 0;
        }
        Game saturated(exe, encode_hok(capped), 12);
        while (saturated.season_day() < calendar.nth_day(kEventWorldChampionship, 1)) saturated.advance_day();
        const HokFile result = decode_hok(saturated.save_bytes());
        for (size_t slot = 0; slot < squad.size(); slot++) {
            const size_t at = 0x696D + 5 * 0x618 + slot * 52;
            const size_t player = size_t(capped.players[size_t(squad[slot].player)].raw - capped.state.data());
            CHECK(result.state[player + 0x19] == 255 && result.state[player + 0x1B] == 255);
            CHECK(result.state[at + 3] == 255 && le16(result.state, at + 0x22) == 255);
        }
    }
    if (fs::exists(ref_dir / "NATLIVQ.HOK") && fs::exists(ref_dir / "NATLIVP.HOK")) {
        const HokFile before = decode_hok(read_file(ref_dir / "NATLIVQ.HOK"));
        const Bytes bytes = read_file(ref_dir / "NATLIVP.HOK");
        HokFile after = decode_hok(bytes);
        CHECK(std::equal(before.state.begin() + 0x04D8, before.state.begin() + 0x0520, after.state.begin() + 0x04D8));
        CHECK(std::equal(before.state.begin() + 0x8F60, before.state.begin() + 0x8F68, after.state.begin() + 0x8F60));
        for (const auto& sp : hok_squad(after, 5)) CHECK(sp.player >= 0 && after.players[size_t(sp.player)].raw[0x19] == 7);
        CHECK(after.state[0x8F85] == 7 && after.state[0x03B7] == 16);
        store_hok(after);
        CHECK(encode_hok(after) == bytes);
        // A nation out in the groups still gets a nomination cap on subsequent WM days.
        for (size_t at = 0x8EB1 + 16; at < 0x8EB1 + 24; at++)
            if (after.state[at] == 56) after.state[at] = 60;
        Game spectating(read_file(em_dir / "EM.EXE"), encode_hok(after), 1);
        const auto calendar = load_season_calendar(read_file(em_dir / "EM.EXE"));
        while (spectating.season_day() < calendar.nth_day(kEventWorldChampionship, 8)) spectating.advance_day();
        const HokFile done = decode_hok(spectating.save_bytes());
        for (const auto& sp : hok_squad(after, 5)) {
            const size_t at = size_t(after.players[size_t(sp.player)].raw - after.state.data());
            CHECK(done.state[at + 0x19] == 8);
        }
        CHECK(spectating.national_squad().empty());
    }
    if (fs::exists(ref_dir / "NATENGQ.HOK") && fs::exists(ref_dir / "NATENGP.HOK")) {
        const Bytes exe = read_file(em_dir / "EM.EXE");
        const HokFile before = decode_hok(read_file(ref_dir / "NATENGQ.HOK"));
        const Bytes energy_bytes = read_file(ref_dir / "NATENGP.HOK");
        HokFile native_energy = decode_hok(energy_bytes);
        Game game(exe, read_file(ref_dir / "NATENGQ.HOK"), 1);
        const auto calendar = load_season_calendar(exe);
        while (game.season_day() < calendar.nth_day(kEventWorldChampionship, 7)) game.advance_day();
        const HokFile cpp = decode_hok(game.save_bytes());
        CHECK(native_energy.state[0x8F85] == 7 && cpp.state[0x8F85] == 7);
        for (const HokFile* after : std::array<const HokFile*, 2>{&native_energy, &cpp}) {
            CHECK(std::equal(before.state.begin() + 0x04D8, before.state.begin() + 0x0520, after->state.begin() + 0x04D8));
            CHECK(std::equal(before.state.begin() + 0x8F60, before.state.begin() + 0x8F68, after->state.begin() + 0x8F60));
            for (size_t slot = 0; slot < 29; slot++) {
                const size_t at = 0x696D + 5 * 0x618 + slot * 52;
                const int id = before.state[at + 0x0F];
                const size_t player = 0x4FC6 + size_t(id - 1) * 33;
                CHECK(after->state[player + 0x19] == before.state[player + 0x19] + 1);
                CHECK(after->state[at + 0x13] == before.state[at + 0x13]);
                CHECK(after->state[at + 0x1B] == std::min(90, 100 - before.state[at + 0x13] / 8));
                CHECK(after->state[at + 8] == before.state[at + 8]);
                CHECK(le16(after->state, at + 0x20) == le16(before.state, at + 0x20));
            }
        }
        store_hok(native_energy);
        CHECK(encode_hok(native_energy) == energy_bytes);
    }
    if (fs::exists(ref_dir / "NATFULLQ.HOK") && fs::exists(ref_dir / "NATRETR.HOK")) {
        const HokFile input = decode_hok(read_file(ref_dir / "NATFULLQ.HOK"));
        const Bytes returned_bytes = read_file(ref_dir / "NATRETR.HOK");
        HokFile returned = decode_hok(returned_bytes);
        CHECK(std::equal(input.state.begin() + 0x04D8, input.state.begin() + 0x0520, returned.state.begin() + 0x04D8));
        CHECK(std::equal(input.state.begin() + 0x8F60, input.state.begin() + 0x8F68, returned.state.begin() + 0x8F60));
        CHECK(returned.state[0x164C] == 0xFD && hok_squad(returned, 5).empty());
        CHECK(national_team_grant(returned) == 1200000);
        CHECK(le32(returned.state, 0x17C7) == le32(input.state, 0x17C7));
        for (const auto& sp : hok_squad(input, 5)) {
            const size_t at = size_t(input.players[size_t(sp.player)].raw - input.state.data());
            CHECK(returned.state[at + 0x19] == uint8_t(input.state[at + 0x19] + 2));
            CHECK(returned.state[at + 0x1D] != 56);
        }
        store_hok(returned);
        CHECK(encode_hok(returned) == returned_bytes);
    }
    if (fs::exists(ref_dir / "NATPRE1.HOK")) {
        const Bytes exe = read_file(em_dir / "EM.EXE");
        Game qualifying(exe, read_file(ref_dir / "NATPRE1.HOK"), 1);
        CHECK(qualifying.national_coach() == -1);
        {
            HokFile higher = decode_hok(read_file(ref_dir / "NATPRE1.HOK"));
            const int leader = higher.table_order[0][0];
            const size_t team = size_t(higher.teams[size_t(leader)].raw - higher.state.data());
            higher.state[team + 0x16] = 2;  // a better placed second human manager
            Game other_coach(exe, encode_hok(higher), 1);
            other_coach.advance_day();
            CHECK(other_coach.national_coach() == -1 && other_coach.national_squad().empty());
            higher.state[team + 0x16] = 0;
            higher.state[0x025A] = 2;  // league only
            Game league_only(exe, encode_hok(higher), 1);
            league_only.advance_day();
            CHECK(league_only.national_coach() == -1 && league_only.national_squad().empty());
            const auto calendar = load_season_calendar(exe);
            while (league_only.season_day() < calendar.nth_day(kEventWorldChampionship, 8)) league_only.advance_day();
            CHECK(league_only.world_champion() == -1 && league_only.world_ranking().empty());
            CHECK(decode_hok(league_only.save_bytes()).state[0x8F85] == 0);
        }
        qualifying.advance_day();  // section end -> national manager's preparation menu
        CHECK(qualifying.national_coach() == 0 && qualifying.national_team() == 56);
        CHECK(qualifying.national_squad().size() == 29);
        CHECK(qualifying.save().squad.size() == 25);
        const HokFile prepared = decode_hok(qualifying.save_bytes());
        CHECK(prepared.state[0x0788] == 5 && prepared.state[0x0789] == 5);
        CHECK(le32(prepared.state, 0x1638 + 0x18F) == 1500000);
        CHECK(prepared.state[0x164C] == 56 && prepared.state[0x1723] == 0);
        CHECK(prepared.state[0x0EC0] & 0x40);
        Game restored(exe, qualifying.save_bytes(), 1);
        CHECK(restored.national_coach() == 0 && restored.national_squad().size() == 29);
        const int chosen = restored.national_squad()[0].player;
        CHECK(restored.select_national_player(size_t(chosen), false));
        CHECK(restored.national_squad().size() == 28);
        CHECK(restored.select_national_player(size_t(chosen), true));
        CHECK(restored.national_squad().size() == 29);
        restored.advance_day();
        CHECK(!restored.select_national_player(size_t(chosen), false));
        CHECK(!restored.auto_select_national_squad());
        CHECK(le32(decode_hok(restored.save_bytes()).state, 0x1638 + 0x18F) == 1500000);
        const SeasonCalendar calendar = load_season_calendar(exe);
        while (restored.season_day() < calendar.nth_day(kEventWorldChampionship, 8)) restored.advance_day();
        CHECK(restored.world_ranking().size() == 8 && restored.national_coach() == -1);
        CHECK(restored.national_squad().empty());
        const HokFile ended = decode_hok(restored.save_bytes());
        CHECK(ended.state[0x164C] == 0xFD && le32(ended.state, 0x17C7) == 1500000);
        CHECK(ended.state[0x0788] == 0 && ended.state[0x0789] == 0);
        CHECK(!restored.auto_select_national_squad());
        restored.advance_day();  // after the WM final (day 231) the next call changes the season (REAL-CODE period 30)
        CHECK(restored.seasons_completed() == 1 && restored.national_coach() == -1);
        CHECK(restored.national_squad().empty());
        CHECK((decode_hok(restored.save_bytes()).state[0x0DCD + 0xF3] & 0x40) != 0);
        CHECK(restored.save().teams[restored.save().state[0x068C]].name == "DEUTSCHLAND");
    }
    std::printf("national squads: native 3/10/16 selection, explicit ids, club preservation and release checked\n");
}

static void world_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "SAISEND.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), start = read_file(ref_dir / "SAISEND.HOK");
    const SeasonCalendar calendar = load_season_calendar(exe);
    auto check = [](Game& game, int rounds) {
        const HokFile h = decode_hok(game.save_bytes());
        CHECK(h.state[0x8F85] == std::min(rounds, 7));
        CHECK(h.state[0x03B7] == std::clamp(rounds - 5, 0, 2) * 8);
        for (int group = 0; group < 2; group++) {
            std::array<int, 6> gf{}, ga{}, pts{};
            for (int round = 0; round < std::min(rounds, 5); round++)
                for (int pair = 0; pair < 3; pair++) {
                    const auto& p = h.wm_rounds[size_t(group)][size_t(round)][size_t(pair)];
                    const int hi = p.home - 52 - group * 6, ai = p.away - 52 - group * 6;
                    CHECK(hi >= 0 && hi < 6 && ai >= 0 && ai < 6 && hi != ai);
                    const size_t o = 0x04D8 + size_t(group * 36 + round * 6 + pair * 2);
                    const int hg = h.state[o], ag = h.state[o + 1];
                    gf[size_t(hi)] += hg; ga[size_t(hi)] += ag;
                    gf[size_t(ai)] += ag; ga[size_t(ai)] += hg;
                    const int hp = hg > ag ? 2 : hg == ag ? 1 : 0;
                    pts[size_t(hi)] += hp; pts[size_t(ai)] += 2 - hp;
                }
            const auto& t = game.world_group(group);
            CHECK(t.points == pts && t.goals_for == gf && t.goals_against == ga);
            for (size_t i = 0; i < 6; i++) {
                const size_t slot = size_t(group * 6) + i;
                CHECK(h.state[0x0628 + slot] == pts[i]);
                CHECK(h.state[0x02BB + slot] == gf[i] && h.state[0x02EB + slot] == ga[i]);
            }
        }
        if (rounds >= 5) {
            // Every formed bracket has eight distinct national teams; completed pairs
            // have a strictly better winner score, with native shoot-out markers stripped.
            for (int stage = 0; stage <= std::min(rounds - 5, 2); stage++) {
                std::vector<int> teams;
                for (int slot = 0; slot < 8; slot++) teams.push_back(h.state[0x8EB1 + size_t(stage * 8 + slot)]);
                std::sort(teams.begin(), teams.end());
                CHECK(teams.front() >= 52 && teams.back() < 64);
                CHECK(std::adjacent_find(teams.begin(), teams.end()) == teams.end());
                if (rounds >= stage + 6)
                    for (int pair = 0; pair < 4; pair++) {
                        const size_t o = 0x8F60 + size_t(stage * 8 + pair * 2);
                        // the quick shoot-out of the original can leave a level score (the away team wins, REAL-CODE
                        // periods 28 and 30)
                        CHECK(h.state[o] % 30 >= h.state[o + 1] % 30);
                    }
            }
        }
        if (rounds == 8) {
            CHECK(game.world_ranking().size() == 8 && game.world_champion() == h.state[0x8EC1]);
            for (size_t i = 0; i < 8; i++) CHECK(game.world_ranking()[i] == h.state[0x8EC1 + i]);
        }
    };
    for (int checkpoint = 1; checkpoint <= 8; checkpoint++) {
        Game game(exe, start, 12);
        while (game.season_day() < calendar.nth_day(kEventWorldChampionship, checkpoint)) game.advance_day();
        check(game, checkpoint);
        const Bytes bytes = game.save_bytes();
        Game resumed(exe, bytes, 12);
        check(resumed, checkpoint);
        CHECK(resumed.save_bytes() == bytes);
        if (checkpoint < 8) {
            while (resumed.season_day() < calendar.nth_day(kEventWorldChampionship, checkpoint + 1)) resumed.advance_day();
            check(resumed, checkpoint + 1);
        }
    }
    if (fs::exists(ref_dir / "WMPOST.HOK")) {
        // The original moved its club menu to Apr 30 without playing the remaining
        // WM rounds. Trust the stored counter rather than inventing a completed final.
        Game pending(exe, read_file(ref_dir / "WMPOST.HOK"), 12);
        check(pending, 5);
        CHECK(pending.world_champion() == -1 && pending.world_ranking().empty());
        const int menu_day = pending.season_day();
        const long balance = pending.budget().balance, accrued = pending.budget().accrued;
        for (int round = 6; round <= 8; round++) {
            pending.advance_day();
            CHECK(pending.season_day() == menu_day && pending.seasons_completed() == 0);
            CHECK(pending.budget().balance == balance && pending.budget().accrued == accrued);
            check(pending, round);
        }
        pending.advance_day();
        CHECK(pending.seasons_completed() == 1);
    }
    if (fs::exists(ref_dir / "WMNATIVE.HOK")) {
        // Read-only RAM reference from the original at its world-champion screen;
        // all 94 native state blocks, with SAISEND's header/key (see probe provenance).
        Game native(exe, read_file(ref_dir / "WMNATIVE.HOK"), 12);
        check(native, 8);
        CHECK(native.world_champion() == 63);
        CHECK(native.save().teams[63].name == "TSCHECHEI");
        const std::vector<int> ranking{63, 62, 61, 59, 52, 56, 57, 55};
        CHECK(native.world_ranking() == ranking);
        for (size_t nation = 52; nation < 64; nation++) {
            const auto place = std::find(ranking.begin(), ranking.end(), int(nation));
            CHECK(native.save().state[0x3BD9 + nation * 0x50] ==
                  (place == ranking.end() ? 11 : int(place - ranking.begin()) + 1));
        }
        const Bytes bytes = native.save_bytes();
        Game reloaded(exe, bytes, 12);
        CHECK(reloaded.save_bytes() == bytes && reloaded.world_ranking() == ranking);
    }
    // Empty, nearly full and full native history columns. Prefixes must survive;
    // the seventeenth tournament discards only the oldest entry for every nation.
    for (const int years : {0, 15, 16}) {
        HokFile h = decode_hok(start);
        for (size_t nation = 52; nation < 64; nation++)
            for (size_t year = 0; year < 16; year++)
                h.state[0x3BD9 + nation * 0x50 + year] = year < size_t(years) ? uint8_t(year % 11 + 1) : 0;
        Game history(exe, encode_hok(h), 12);
        while (history.season_day() < calendar.nth_day(kEventWorldChampionship, 8)) history.advance_day();
        const HokFile complete = decode_hok(history.save_bytes());
        const auto& ranking = history.world_ranking();
        for (size_t nation = 52; nation < 64; nation++) {
            const size_t o = 0x3BD9 + nation * 0x50;
            const size_t column = size_t(std::min(years, 15));
            for (size_t year = 0; year < column; year++)
                CHECK(complete.state[o + year] == h.state[o + year + (years == 16)]);
            const auto place = std::find(ranking.begin(), ranking.end(), int(nation));
            CHECK(complete.state[o + column] == (place == ranking.end() ? 11 : int(place - ranking.begin()) + 1));
        }
        Game reload(exe, history.save_bytes(), 12);
        CHECK(reload.save_bytes() == history.save_bytes());
    }
    std::printf("WM save: all eight boundaries, group results and winner/loser bracket checked\n");
}

static void league_results_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const ScheduleTables schedule = load_schedule_tables(exe);
    auto check = [&](const HokFile& h) {
        // The pair-table error of the original (DEVIATIONS.md) books Oberliga results to team 0, so those rows
        // are checked only through the totals over all leagues.
        std::array<int, 10> total{}, booked{};
        for (size_t league = 0; league < 3; league++) {
            std::array<std::array<int, 2>, 12> gf{}, ga{}, games{}, wins{}, losses{};
            for (int day = 0; day < h.matchday[league] - 1; day++) {
                const auto pairs = round_robin_pairings(schedule.teams12, day);
                for (size_t pair = 0; pair < pairs.size(); pair++) {
                    const size_t o = 0x0796 + league * 0x210 + size_t(day) * 12 + pair * 2;
                    const int hg = h.state[o], ag = h.state[o + 1];
                    const size_t home = size_t(pairs[pair].home), away = size_t(pairs[pair].away);
                    gf[home][0] += hg; ga[home][0] += ag; games[home][0]++;
                    gf[away][1] += ag; ga[away][1] += hg; games[away][1]++;
                    wins[home][0] += hg > ag; losses[home][0] += hg < ag;
                    wins[away][1] += ag > hg; losses[away][1] += ag < hg;
                }
            }
            for (size_t team = 0; team < 12; team++) {
                const HokTableRow& row = h.table[league * 12 + team];
                for (size_t side = 0; side < 2; side++) {
                    const int want[5] = {gf[team][side], ga[team][side], games[team][side], wins[team][side], losses[team][side]};
                    const int have[5] = {row.goals_for[side], row.goals_against[side], row.games[side], row.wins[side], row.losses[side]};
                    for (size_t k = 0; k < 5; k++) { total[side * 5 + k] += want[k]; booked[side * 5 + k] += have[k]; }
                    if (league == 2 || (league == 0 && team == 0)) continue;
                    CHECK(row.goals_for[side] == gf[team][side] && row.goals_against[side] == ga[team][side]);
                    CHECK(row.games[side] == games[team][side] && row.wins[side] == wins[team][side]);
                    CHECK(row.losses[side] == losses[team][side]);
                }
            }
        }
        CHECK(total == booked);
    };
    if (fs::exists(ref_dir / "MONAT1.HOK")) check(decode_hok(read_file(ref_dir / "MONAT1.HOK")));
    Game game(exe, read_file(ref_dir / "START.HOK"), 9);
    for (int day = 0; day < 70; day++) game.advance_day();
    const HokFile before = decode_hok(game.save_bytes());
    check(before);
    Game resumed(exe, game.save_bytes(), 9);
    for (int day = 0; day < 75; day++) resumed.advance_day();
    const HokFile after = decode_hok(resumed.save_bytes());
    check(after);
    for (size_t league = 0; league < 3; league++)
        for (int day = 0; day < before.matchday[league] - 1; day++)
            for (size_t score = 0; score < 12; score++) {
                const size_t o = 0x0796 + league * 0x210 + size_t(day) * 12 + score;
                CHECK(after.state[o] == before.state[o]);
            }
    while (resumed.seasons_completed() == 0) resumed.advance_day();
    const HokFile fresh = decode_hok(resumed.save_bytes());
    CHECK(fresh.cup_draw.size() == 16 && fresh.state[0x0DCD + 0xE6] == 1);
    int human_cup_slots = 0;
    std::vector<int> cup_teams;
    for (const auto& pair : fresh.cup_draw) {
        human_cup_slots += pair.home == resumed.human_team();
        human_cup_slots += pair.away == resumed.human_team();
        CHECK(pair.home / 12 >= pair.away / 12);
        cup_teams.push_back(pair.home); cup_teams.push_back(pair.away);
    }
    std::sort(cup_teams.begin(), cup_teams.end());
    CHECK(human_cup_slots == 1 && std::adjacent_find(cup_teams.begin(), cup_teams.end()) == cup_teams.end());
    for (size_t league = 0; league < 3; league++)
        for (int day = fresh.matchday[league] - 1; day < 44; day++)
            for (size_t score = 0; score < 12; score++)
                CHECK(fresh.state[0x0796 + league * 0x210 + size_t(day) * 12 + score] == 0);
    std::printf("League result save: native MONAT1, three league tables, resume and new-season clearing checked\n");
}

static void playoff_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), start = read_file(ref_dir / "START.HOK");
    const SeasonCalendar calendar = load_season_calendar(exe);
    auto check = [](Game& game, int days) {
        const HokFile h = decode_hok(game.save_bytes());
        const int stage = std::min((days - 1) / 5, 2), base = stage == 0 ? 0 : stage == 1 ? 8 : 12;
        const auto& series = game.playoff_series();
        CHECK(series.size() == size_t(4 >> stage));
        const int group_rounds = days / 5 * 2 + (days % 5 >= 1) + (days % 5 >= 3);
        for (int group = 0; group < 2; group++) {
            const auto& t = game.relegation_group(group);
            std::array<int, 4> gf{}, ga{}, points{};
            for (int round = 0; round < group_rounds; round++)
                for (size_t pair = 0; pair < 2; pair++) {
                    const size_t o = 0x0400 + size_t(group * 36 + round * 6) + pair * 2;
                    const int home = h.state[o], away = h.state[o + 1];
                    const size_t hi = size_t(std::find(t.teams.begin(), t.teams.end(), home) - t.teams.begin());
                    const size_t ai = size_t(std::find(t.teams.begin(), t.teams.end(), away) - t.teams.begin());
                    CHECK(hi < 4 && ai < 4 && hi != ai);
                    const int hg = h.state[o + 0x120], ag = h.state[o + 0x121];
                    gf[hi] += hg; ga[hi] += ag; gf[ai] += ag; ga[ai] += hg;
                    const int hp = hg > ag ? 2 : hg == ag ? 1 : 0;
                    points[hi] += hp; points[ai] += 2 - hp;
                }
            CHECK(t.points == points && t.goals_for == gf && t.goals_against == ga);
            int total = 0;
            for (int p : points) total += p;
            CHECK(total == group_rounds * 4);
            if (days >= 13) {
                const auto order = t.order();
                for (size_t i = 0; i < 4; i++) CHECK(game.relegation_order(group)[i] == t.teams[size_t(order[i])]);
            }
        }
        for (size_t pair = 0; pair < series.size(); pair++) {
            const Series& s = series[pair];
            CHECK(s.higher != s.lower && s.wins_higher <= 3 && s.wins_lower <= 3);
            CHECK(s.games.size() == size_t(s.wins_higher + s.wins_lower) && s.games.size() <= 5);
            const size_t slot = size_t(base) + pair * 2;
            const int first = h.state[0x8E91 + slot];
            CHECK(first == s.higher || first == s.lower);
            CHECK(std::min(int(h.state[0x024B + slot]), 3) == (first == s.higher ? s.wins_higher : s.wins_lower));  // 0x14 marks a lower-seed winner
            int higher_wins = 0, lower_wins = 0;
            for (size_t match = 0; match < s.games.size(); match++) {
                const size_t o = 0x0205 + match * 14 + slot;
                const int hg = h.state[o] % 30, ag = h.state[o + 1] % 30;
                const auto& r = s.games[match];
                // a quick shoot-out after a real draw can end level (the scan already marked +30); the away team wins then
                const bool higher_won = (r.home > r.away) == (match % 2 == 0);
                if (hg != ag) CHECK((hg > ag) == (first == s.higher) == higher_won);
                higher_wins += higher_won; lower_wins += !higher_won;
                CHECK((h.state[o] / 30 + h.state[o + 1] / 30) * 30 == r.decision_marker);
            }
            CHECK(higher_wins == s.wins_higher && lower_wins == s.wins_lower);
        }
        const int next_stage = std::min(days / 5, 2), next_base = next_stage == 0 ? 0 : next_stage == 1 ? 8 : 12;
        CHECK(h.state[0x8F87] == next_stage && h.state[0x03B6] == next_base);
        CHECK(h.state[0x0259] == (days == 15 ? 5 : days % 5));
        if (days == 15) CHECK(game.champion() == series[0].winner() && h.state[0x0DC8] == game.champion());
    };
    for (uint32_t seed : {2u, 9u})
        for (int checkpoint = 1; checkpoint <= 15; checkpoint++) {
            Game game(exe, start, seed);
            while (game.season_day() < calendar.nth_day(kEventPlayoff, checkpoint)) game.advance_day();
            check(game, checkpoint);
            const Bytes bytes = game.save_bytes();
            Game resumed(exe, bytes, seed);
            check(resumed, checkpoint);
            CHECK(resumed.save_bytes() == bytes);
            CHECK(resumed.champion() == game.champion() && resumed.cup_winner() == game.cup_winner());
            if (checkpoint < 15) {
                while (resumed.season_day() < calendar.nth_day(kEventPlayoff, checkpoint + 1)) resumed.advance_day();
                check(resumed, checkpoint + 1);
            }
        }
    if (fs::exists(ref_dir / "SAISEND.HOK")) {
        const HokFile original = decode_hok(read_file(ref_dir / "SAISEND.HOK"));
        Game game(exe, read_file(ref_dir / "SAISEND.HOK"), 6);
        CHECK(game.champion() == 0 && game.cup_winner() == 4);
        CHECK(game.relegation_order(0) == (std::array<int, 4>{12, 19, 11, 9}));
        CHECK(game.relegation_order(1) == (std::array<int, 4>{28, 15, 24, 16}));
        const HokFile back = decode_hok(game.save_bytes());
        for (size_t i = 0x0205; i <= 0x0259; i++) CHECK(back.state[i] == original.state[i]);
        for (size_t i = 0x8E91; i < 0x8EB1; i++) CHECK(back.state[i] == original.state[i]);
        CHECK(game.playoff_series()[0].games.size() == 5);
    }
    if (fs::exists(ref_dir / "POPOST.HOK")) {
        Game game(exe, read_file(ref_dir / "POPOST.HOK"), 9);
        check(game, 3);
        const auto& series = game.playoff_series();
        bool found = false;
        for (const Series& s : series)
            if (s.higher == 7 || s.lower == 7) {
                CHECK((s.higher == 7 ? s.wins_higher : s.wins_lower) == 2);
                CHECK(s.games.size() == 3);
                found = true;
            }
        CHECK(found);
        while (game.season_day() < calendar.nth_day(kEventPlayoff, 4)) game.advance_day();
        check(game, 4);
    }
    if (fs::exists(ref_dir / "POSEMI1.HOK") && fs::exists(ref_dir / "POSEMIP.HOK")) {
        const HokFile input = decode_hok(read_file(ref_dir / "POSEMI1.HOK"));
        const HokFile actual = decode_hok(read_file(ref_dir / "POSEMIP.HOK"));
        const std::array<int, 4> bracket{3, 7, 0, 10};
        for (size_t i = 0; i < bracket.size(); i++) CHECK(input.state[0x8E99 + i] == bracket[i]);
        // Native UI accepted the two semifinal pairings and played their first game.
        // Current teams/history are reversed for game 2: Düsseldorf beat Köln 5:2.
        CHECK(actual.state[0x8F87] == 1 && actual.state[0x0259] == 1 && actual.state[0x03B6] == 8);
        Game resumed(exe, read_file(ref_dir / "POSEMIP.HOK"), 12);
        check(resumed, 6);
        CHECK(resumed.playoff_series().size() == 2);
        const Series& koeln = resumed.playoff_series()[0];
        CHECK(koeln.higher == 3 && koeln.lower == 7 && koeln.wins_higher == 1 && koeln.wins_lower == 0);
        CHECK(koeln.games.size() == 1 && koeln.games[0].home == 5 && koeln.games[0].away == 2);
        const HokFile saved = decode_hok(resumed.save_bytes());
        for (size_t i = 0x0205; i <= 0x0259; i++) CHECK(saved.state[i] == actual.state[i]);
        // All five earlier quarter-final rows survive the semifinal and its export.
        for (size_t row = 0; row < 5; row++)
            for (size_t slot = 0; slot < 8; slot++)
                CHECK(saved.state[0x0205 + row * 14 + slot] == input.state[0x0205 + row * 14 + slot]);
        while (resumed.season_day() < calendar.nth_day(kEventPlayoff, 7)) resumed.advance_day();
        check(resumed, 7);
    }
    std::printf("Play-off/relegation save: 15 boundaries x 2 seeds, original final and title holders checked\n");
}

// The human club's team record (162c:146c): line strengths of lines 0..2 / 3 per kind give the
// stored values of the reference saves.
static void human_team_record(const fs::path& ref_dir) {
    int checked = 0, exact = 0;
    for (const char* name : {"START.HOK", "MONAT1.HOK", "MONAT2.HOK", "MONAT3.HOK"}) {
        if (!fs::exists(ref_dir / name)) continue;
        HokFile h = decode_hok(read_file(ref_dir / name));
        HumanSquad human{&h.squad, &h.players};
        const HokTeam& team = h.teams[7];
        for (int kind = 0; kind < 2; kind++) {
            int s1 = 0, s2 = 0, f = 0;
            for (int line = 0; line < 3; line++) {
                const LineValues v = line_strength(team, human, line, kind, false, 0);
                s1 += v.skill1;
                s2 += v.skill2;
                f += v.form;
            }
            exact += (s1 / 3 == team.skill1[kind]) + (s2 / 3 == team.skill2[kind]) + (f / 3 == team.form[kind]);
            checked += 3;
        }
    }
    std::printf("human team record: %d of %d values as stored\n", exact, checked);
    CHECK(checked == 0 || exact * 10 >= checked * 8);
}

// Knock-out competitions with the clubs of MONAT1: the stored Achtelfinale draw gives 8 winners,
// favourites go through more often, a best-of-five ends at 3 wins, the play-offs produce a champion
// from the first eight (the leader more often than the eighth) and the relegation two survivors.
static void cup_draws() {
    auto flatten = [](const std::vector<HokPairing>& draw) {
        std::vector<int> result;
        for (const auto& pair : draw) { result.push_back(pair.home); result.push_back(pair.away); }
        return result;
    };
    // Independently calculated from the extracted native loops and Borland LCG.
    BorlandRandom first(1), next(3);
    CHECK(flatten(draw_initial_cup({7}, first)) == (std::vector<int>{
        22,2,28,10,25,27,16,4,14,19,24,12,35,9,13,11,26,17,5,3,31,0,21,20,30,15,34,7,33,18,29,1}));
    CHECK(first.rand() == 2285);
    std::vector<int> winners;
    for (int i = 0; i < 16; i++) winners.push_back(i);
    CHECK(flatten(draw_cup_round(winners, next)) == (std::vector<int>{14,0,2,9,8,4,1,6,7,5,15,3,13,10,12,11}));
    CHECK(next.rand() == 23283);
    for (int seed = 1; seed <= 100; seed++) {
        BorlandRandom rng{uint32_t(seed)};
        const std::vector<int> humans = {0, 11, 23, 35};
        const auto draw = draw_initial_cup(humans, rng);
        auto teams = flatten(draw);
        std::sort(teams.begin(), teams.end());
        CHECK(teams.size() == 32 && std::adjacent_find(teams.begin(), teams.end()) == teams.end());
        CHECK(teams.front() >= 0 && teams.back() <= 35);
        for (int human : humans) CHECK(std::binary_search(teams.begin(), teams.end(), human));
        for (const auto& pair : draw) CHECK(pair.home / 12 >= pair.away / 12);
    }
    BorlandRandom empty(1);
    CHECK(draw_cup_round({}, empty).empty());
}

static void competitions(const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    GameRandom rng(2024);
    BorlandRandom brng(3);
    std::vector<Tie> round = play_cup_round(h.teams, h.cup_draw, rng);
    CHECK(round.size() == 8);
    std::vector<int> winners;
    for (const Tie& t : round) {
        CHECK(t.result.home != t.result.away);
        winners.push_back(t.winner());
    }
    CHECK(draw_cup_round(winners, brng).size() == 4);
    int drawn_after_sixty = 0;
    for (uint32_t seed = 1; seed <= 100; seed++) {
        GameRandom regular_rng(seed), cup_rng(seed);
        MatchStats regular_stats, cup_stats;
        const MatchResult regular = league_match(h.teams[7], h.teams[10], regular_rng, 0, nullptr, nullptr, &regular_stats);
        MatchOptions options; options.knockout = true;
        const MatchResult cup = league_match(h.teams[7], h.teams[10], cup_rng, 0, nullptr, nullptr, &cup_stats, options);
        int played = 0;
        for (int line = 0; line < 4; line++) played += cup_stats.line_minutes[0][line];
        if (regular.home == regular.away) {
            drawn_after_sixty++;
            CHECK(played > 60 && played <= 70 && (cup.shootout ? played == 70 : true) && cup.home != cup.away);  // sudden death: the first goal of the overtime decides
            CHECK(cup.decision_marker == (cup.shootout ? 60 : 30));
            if (cup.shootout)
                for (int side = 0; side < 2; side++) CHECK(cup.shootout_goals[side] >= 2 && cup.shootout_goals[side] <= 5);
        } else {
            CHECK(played == 60 && !cup.shootout);
            CHECK(cup.decision_marker == 0);
            CHECK(cup.home == regular.home && cup.away == regular.away);
        }
    }
    CHECK(drawn_after_sixty > 0 && drawn_after_sixty < 100);
    // Augsburg (2nd division) - Preussen Berlin (first division leader): Preussen should mostly win
    int preussen = 0, shootouts = 0;
    for (int i = 0; i < 400; i++) {
        MatchResult r = knockout_quick(h.teams[14], h.teams[10], true, true, rng);
        preussen += r.away > r.home;
        shootouts += r.shootout;
        CHECK(r.decision_marker == (r.shootout ? 30 : 0));
        for (int side = 0; side < 2; side++) CHECK(r.shootout_goals[side] >= 0 && r.shootout_goals[side] <= 2);
    }
    std::vector<int> order(h.table_order[0].begin(), h.table_order[0].end());
    int leader_titles = 0, eighth_titles = 0, rounds_ok = 1;
    for (int i = 0; i < 100; i++) {
        PlayoffResult p = play_playoffs(h.teams, order, rng);
        leader_titles += p.champion() == order[0];
        eighth_titles += p.champion() == order[7];
        for (const Series& s : p.quarter) rounds_ok &= (s.wins_higher == 3) != (s.wins_lower == 3) && s.games.size() <= 5;
    }
    std::vector<int> second(h.table_order[1].begin(), h.table_order[1].end());
    std::vector<RelegationTie> rel = play_relegation(h.teams, order, second, rng);
    std::printf("cup: Preussen beats Augsburg %d / 400 (%d shoot-outs); play-offs: leader %d, eighth %d titles / 100\n",
                preussen, shootouts, leader_titles, eighth_titles);
    CHECK(preussen > 240 && rounds_ok && leader_titles > eighth_titles);
    CHECK(rel.size() == 2 && (rel[0].stays_up() == rel[0].upper || rel[0].stays_up() == rel[0].lower));
}

// Scorer choice (ovl06_3191): Köln's 27 league goals of the first 9 matchdays went 24 to forwards
// and 3 to defenders (MONAT1). Credit 27 goals to the START squad many times and compare.
static void match_energy() {
    HokTeam team;
    std::vector<HokSquadPlayer> squad(5);
    const int raw[] = {50, 80, 120, 150, 255};
    const int prepared[] = {90, 90, 85, 82, 69};  // 162c:05f2-062e
    for (size_t i = 0; i < squad.size(); i++) {
        squad[i].player = int(i);
        squad[i].raw_exhaustion = raw[i];
    }
    prepare_match_energy(team, &squad);
    for (size_t i = 0; i < squad.size(); i++) {
        CHECK(squad[i].fitness == prepared[i]);
        CHECK(squad[i].raw_exhaustion == std::min(raw[i], 150));
    }
    const int drained[] = {0, 1, 70, 71, 90}, rested[] = {19, 20, 89, 90, 90};
    for (size_t i = 0; i < squad.size(); i++) squad[i].fitness = drained[i];
    team.energy[0][0] = 0;
    prepare_match_energy(team, &squad, true);
    CHECK(team.energy[0][0] == 19);
    for (size_t i = 0; i < squad.size(); i++) CHECK(squad[i].fitness == rested[i]);
    // Exhaustion during play must reduce strength instead of turning a zero back into 90.
    std::vector<HokPlayer> players(5);
    for (auto& p : players) p.position = 75;
    for (auto& p : squad) {
        p.role = 1; p.line = 0x81; p.skill_a = p.skill_b = p.form = 90; p.fitness = 90;
    }
    HumanSquad human{&squad, &players};
    const int fresh = line_strength(team, human, 0, 1, true, 0, 60).skill2;
    for (auto& p : squad) p.fitness = 0;
    CHECK(line_strength(team, human, 0, 1, true, 0, 60).skill2 < fresh);
    HokTeam cpu, other;
    cpu.current_line[0] = cpu.current_line[1] = 3;
    other.current_line[0] = other.current_line[1] = 3;
    cpu.flags = other.flags = 2;  // preparation disables CPU changes, not their saved lines
    GameRandom rng(6);
    MatchStats empty_fourth;
    const MatchResult score = league_match(cpu, other, rng, 0, nullptr, nullptr, &empty_fourth);
    CHECK(score.home == 0 && score.away == 0);
    CHECK(empty_fourth.final_teams[0].current_line[0] == 3 && empty_fourth.final_teams[0].current_line[1] == 3);
    CHECK(empty_fourth.line_minutes[0][3] == 60 && empty_fourth.line_minutes[1][3] == 60);
}

static void tactic_controls() {
    std::vector<HokSquadPlayer> squad;
    std::vector<HokPlayer> players;
    HumanSquad human{&squad, &players};
    human.effort_control = human.offence_control = 1;
    HokTeam t, opponent;
    t.effort_base = 59; t.effort_current = 79;
    const int effort[] = {0, 25, 20, 30, 0}, offence[] = {4, 5, 7, 7, 4};
    for (int i = 0; i < kTacticSituations; i++) { t.effort[i] = effort[i]; t.offence[i] = offence[i]; }
    for (auto& kind : t.energy) for (int& e : kind) e = 90;
    setup_computer_team(t, opponent, &human);
    CHECK(t.effort_base == 59 && t.effort_current == 79 && t.offence[kDraw] == 7);
    apply_effort(t, 0, 0, nullptr, 1, 1);
    CHECK(t.effort_current == 79 && ((t.flags >> 4) & 7) == 7);
    apply_effort(t, 1, 0, nullptr, 1, 1);
    CHECK(t.effort_current == 84 && ((t.flags >> 4) & 7) == 5);
    t.effort_current = 37;
    apply_effort(t, 0, 5, nullptr, 2, 2);
    CHECK(t.effort_current == 37 && ((t.flags >> 4) & 7) == 5);
    human.effort_control = 0;
    setup_computer_team(t, opponent, &human);
    CHECK((t.effort_base == 40 || t.effort_base == 60) && t.offence[kDraw] == 7);
    human.effort_control = 1; human.offence_control = 0;
    t.effort_base = 59; t.effort_current = 79;
    setup_computer_team(t, opponent, &human);
    CHECK(t.effort_base == 59 && t.effort_current == 79 && t.offence[kDraw] != 7);
}

static void human_line_energy(const fs::path& ref_dir) {
    // Native live capture, stopped at minute 60 of the second uninterrupted
    // LINEMN22 match. This is RAM evidence, not a menu-loadable reference save.
    const fs::path capture = ref_dir.parent_path() / "exp/evening_20261102/LINE_second_60.hok";
    if (!fs::exists(capture)) return;
    const HokFile native = decode_hok(read_file(capture));
    HumanSquad human{&native.squad, &native.players};
    HokTeam recalculated = native.teams[7];
    const int expected[2][4] = {{85, 66, 62, 30}, {81, 46, 49, 0}};
    refresh_line_energy(recalculated, human);
    for (int kind = 0; kind < 2; kind++)
        for (int line = 0; line < 4; line++) {
            CHECK(native.teams[7].energy[kind][line] == expected[kind][line]);
            CHECK(recalculated.energy[kind][line] == expected[kind][line]);
        }
    CHECK(native.teams[7].current_line[0] == 2 && native.teams[7].current_line[1] == 2);
    // The natural goalkeeper's fitness is excluded from every cached line.
    auto squad = native.squad;
    human.squad = &squad;
    for (auto& p : squad) if (p.player >= 0 && native.players[size_t(p.player)].position == 0) p.fitness = 0;
    refresh_line_energy(recalculated, human);
    for (int kind = 0; kind < 2; kind++)
        for (int line = 0; line < 4; line++) CHECK(recalculated.energy[kind][line] == expected[kind][line]);
}

static void line_controls() {
    std::vector<HokSquadPlayer> squad;
    std::vector<HokPlayer> players;
    HumanSquad human{&squad, &players};
    human.line_control = 4;
    human.line_quotas = {{{2, 3, 3, 0}, {2, 3, 3, 0}}};
    LineRotation rotation;
    human.rotation = &rotation;
    HokTeam team, opponent;
    team.flags = 2;  // stored control byte overrides the previous automatic team mode
    GameRandom rng(1);
    MatchStats first;
    league_match(team, opponent, rng, 0, &human, nullptr, &first);
    for (size_t kind = 0; kind < 2; kind++) {
        CHECK(first.final_teams[0].current_line[kind] == 1 && rotation.used[kind][1] == 2);
        CHECK(first.line_minutes[kind][0] == 17 && first.line_minutes[kind][1] == 23);
        CHECK(first.line_minutes[kind][2] == 20 && first.line_minutes[kind][3] == 0);
    }
    MatchStats second;
    league_match(first.final_teams[0], opponent, rng, 0, &human, nullptr, &second);
    for (size_t kind = 0; kind < 2; kind++)
        CHECK(second.final_teams[0].current_line[kind] == 2 && rotation.used[kind][2] == 3);
    // Manual control retains the selected lines, including nonzero initial lines.
    human.line_control = 5;
    team.current_line[0] = 2; team.current_line[1] = 1;
    MatchStats manual;
    league_match(team, opponent, rng, 0, &human, nullptr, &manual);
    CHECK(manual.line_minutes[0][2] == 60 && manual.line_minutes[1][1] == 60);
    // A fourth line is eligible only while a matching assigned player exists.
    squad.resize(2); players.resize(2);
    for (int i = 0; i < 2; i++) { squad[size_t(i)].player = i; squad[size_t(i)].role = 1;
        squad[size_t(i)].line = 4 + i * 128; players[size_t(i)].position = i ? 75 : 25; }
    human.line_control = 4;
    human.line_quotas = {{{0, 0, 0, 1}, {0, 0, 0, 1}}};
    rotation = LineRotation{};
    team.current_line[0] = team.current_line[1] = 0;
    MatchStats fourth;
    league_match(team, opponent, rng, 0, &human, nullptr, &fourth);
    CHECK(fourth.line_minutes[0][3] == 60 && fourth.line_minutes[1][3] == 60);
    // An assigned manual line without a player on the ice does not age its
    // shift. A depleted two-player lineup cannot receive further penalties.
    human.line_control = 5;
    team.current_line[0] = team.current_line[1] = 0;
    squad[0].line = 1; squad[1].line = 129;
    MatchStats exhausted;
    league_match(team, opponent, rng, 0, &human, nullptr, &exhausted);
    for (const auto& p : exhausted.final_squads[0]) CHECK(p.fitness >= 1);
    CHECK(exhausted.final_squads[0][0].fitness == 1 && exhausted.final_squads[0][1].fitness == 1);
    squad.clear();
    MatchStats empty;
    league_match(team, opponent, rng, 0, &human, nullptr, &empty);
    CHECK(empty.final_teams[0].shift[0] == 0 && empty.final_teams[0].shift[1] == 0);
}

static void line_control_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "LINEPS22.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), source = read_file(ref_dir / "LINEPS22.HOK");
    const HokFile native = decode_hok(source);
    HumanSquad human{&native.squad, &native.players};
    configure_human_tactics(human, native, 0);
    CHECK(human.line_control == 4 && native.teams[7].current_line[0] == 2 && native.teams[7].current_line[1] == 2);
    CHECK((human.line_quotas == std::array<std::array<int, 4>, 2>{{{2, 3, 3, 0}, {2, 3, 3, 0}}}));
    Game game(exe, source, 6);
    game.advance_day();  // native 22 October cup fixture; transient rotation starts at zero
    const HokFile saved = decode_hok(game.save_bytes());
    CHECK(saved.state[0x01F9] == 4);
    CHECK(std::equal(native.state.begin() + 0x025B, native.state.begin() + 0x0263, saved.state.begin() + 0x025B));
    CHECK(saved.teams[7].current_line[0] == 0 && saved.teams[7].current_line[1] == 0);
    Game reloaded(exe, game.save_bytes(), 6);
    CHECK(reloaded.save().teams[7].current_line[0] == 0 && reloaded.save().teams[7].current_line[1] == 0);
}

static void checkpoint_format() {
    GameCheckpoint source;
    source.hok = {1, 2, 3};
    source.exe_fingerprint = 0xFEDCBA9876543210ull;
    source.game_random = 3999999; source.borland_random = 0xFFFFFFFFu;
    source.club_rotation.used[1][2] = 255;
    source.national_rotation.used[0][3] = 17;
    source.injured_roles[29] = 11;
    source.squad_status[29] = uint8_t(SquadStatus::Team);
    source.team_strength = 321; source.players = 29;
    source.squad_costs = 123456; source.pool_costs = 456;
    source.offers.push_back({198, 149, true, 12345678});
    source.line_counts[0]={4,1};source.line_counts[5]={0,255};
    source.stadium_refusals[0][4]=1;source.stadium_refusals[3][10]=1;
    source.save_history.remember(0xFFFF1234u);source.save_history.remember(0xFFFF5678u);
    source.annual_distribution_cursors={16,5,11};
    source.last_scorer_slot=17;
    source.message_step_pending=false;
    source.table_copies_frozen=true;source.league_since_refresh=true;
    source.club_timers.role[0]=3;source.club_timers.slot[1]=17;source.national_timers.role[1]=5;
    const Bytes bytes = encode_game_checkpoint(source);
    const auto decoded = decode_game_checkpoint(bytes);
    CHECK(decoded.hok == source.hok && decoded.exe_fingerprint == source.exe_fingerprint);
    CHECK(decoded.game_random == source.game_random && decoded.borland_random == source.borland_random);
    CHECK(decoded.club_rotation.used == source.club_rotation.used);
    CHECK(decoded.national_rotation.used == source.national_rotation.used);
    CHECK(decoded.injured_roles == source.injured_roles && decoded.offers[0].amount == 12345678);
    CHECK(decoded.squad_status == source.squad_status);
    CHECK(decoded.line_counts==source.line_counts);
    CHECK(decoded.stadium_refusals==source.stadium_refusals);
    CHECK(decoded.save_history==source.save_history);
    CHECK(decoded.annual_distribution_cursors==source.annual_distribution_cursors);
    CHECK(decoded.last_scorer_slot==source.last_scorer_slot);
    CHECK(decoded.message_step_pending==source.message_step_pending);
    CHECK(decoded.table_copies_frozen&&decoded.league_since_refresh);
    CHECK(decoded.club_timers.role[0]==3&&decoded.club_timers.slot[1]==17&&decoded.club_timers.slot[0]==-1&&decoded.national_timers.role[1]==5);
    CHECK(encode_game_checkpoint(decoded) == bytes);
    // A genuine version1 layout ends directly after its offers. It remains
    // readable and gets the explicitly historical2/3 default without RNG.
    Bytes legacy(bytes.begin(),bytes.end()-272); // v2 counters12 + v3 gates44 + v4 history201 + v5 cursors3 + v6 scorer slot1 + v7 message flag1 + v8 table-copy flags2 + v9 penalty timers8
    const auto patch32=[&](size_t at,uint32_t value){for(int b=0;b<4;b++)legacy[at+size_t(b)]=uint8_t(value>>(8*b));};
    patch32(8,1);patch32(12,uint32_t(legacy.size()-20));
    uint32_t crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);
    const auto v1=decode_game_checkpoint(legacy);
    CHECK(v1.line_counts[0]==(std::array<uint8_t,2>{2,3})&&v1.line_counts[5]==v1.line_counts[0]);
    CHECK(v1.borland_random==source.borland_random&&v1.game_random==source.game_random);
    CHECK(v1.stadium_refusals==(std::array<std::array<uint8_t,11>,4>{}));
    // Version2 includes row counters, but predates process-local stadium gates.
    legacy.assign(bytes.begin(),bytes.end()-260);patch32(8,2);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v2=decode_game_checkpoint(legacy);
    CHECK(v2.line_counts==source.line_counts&&v2.stadium_refusals==v1.stadium_refusals);
    CHECK(v1.save_history==NativeSaveHistory{}&&v2.save_history==NativeSaveHistory{});
    // Version3 contains the construction gates, but has no identity history.
    legacy.assign(bytes.begin(),bytes.end()-216);patch32(8,3);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v3=decode_game_checkpoint(legacy);
    CHECK(v3.line_counts==source.line_counts&&v3.stadium_refusals==source.stadium_refusals&&v3.save_history==NativeSaveHistory{});
    CHECK(v1.annual_distribution_cursors==(std::array<uint8_t,3>{})&&v2.annual_distribution_cursors==v1.annual_distribution_cursors&&v3.annual_distribution_cursors==v1.annual_distribution_cursors);
    // Version4 retains history but predates the three session cursors.
    legacy.assign(bytes.begin(),bytes.end()-15);patch32(8,4);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v4=decode_game_checkpoint(legacy);
    CHECK(v4.save_history==source.save_history&&v4.annual_distribution_cursors==v1.annual_distribution_cursors);
    // Version5 has the cursors but not the scorer slot of the last pick.
    legacy.assign(bytes.begin(),bytes.end()-12);patch32(8,5);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v5=decode_game_checkpoint(legacy);
    CHECK(v5.annual_distribution_cursors==source.annual_distribution_cursors&&v5.last_scorer_slot==0);
    // Version6 has the scorer slot but no message step flag (loads as due).
    legacy.assign(bytes.begin(),bytes.end()-11);patch32(8,6);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v6=decode_game_checkpoint(legacy);
    CHECK(v6.last_scorer_slot==source.last_scorer_slot&&v6.message_step_pending);
    // Version7 has the message step flag but no table-copy flags (the copies follow the table).
    legacy.assign(bytes.begin(),bytes.end()-10);patch32(8,7);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v7=decode_game_checkpoint(legacy);
    CHECK(v7.message_step_pending==source.message_step_pending&&!v7.table_copies_frozen&&!v7.league_since_refresh);
    // Version8 has the table-copy flags but no penalty timers (they load cleared, as after a native load).
    legacy.assign(bytes.begin(),bytes.end()-8);patch32(8,8);patch32(12,uint32_t(legacy.size()-20));
    crc=0xFFFFFFFFu;
    for(size_t i=20;i<legacy.size();i++){crc^=legacy[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    patch32(16,~crc);const auto v8=decode_game_checkpoint(legacy);
    CHECK(v8.table_copies_frozen&&v8.club_timers.role[0]==0&&v8.club_timers.slot[1]==-1);
    const auto rejects = [](const Bytes& bad) {
        try { (void)decode_game_checkpoint(bad); return false; } catch (const FormatError&) { return true; }
    };
    for (size_t n : {size_t(0), size_t(7), size_t(8), size_t(19), bytes.size() - 1})
        CHECK(rejects(Bytes(bytes.begin(), bytes.begin() + long(n))));
    for (size_t i : {size_t(8), size_t(12), size_t(16), size_t(20), bytes.size() - 1}) {
        Bytes bad = bytes; bad[i] ^= 1; CHECK(rejects(bad));
    }
    Bytes extra = bytes; extra.push_back(0); CHECK(rejects(extra));
    source.annual_distribution_cursors[2]=17;bool bad_cursor=false;
    try{(void)encode_game_checkpoint(source);}catch(const FormatError&){bad_cursor=true;}
    CHECK(bad_cursor);source.annual_distribution_cursors[2]=11;
    source.game_random = 4000000;
    bool range_rejected = false;
    try { (void)encode_game_checkpoint(source); } catch (const FormatError&) { range_rejected = true; }
    CHECK(range_rejected);
}

static void table_ordering() {
    HokTableRow history;
    history.position_history[0] = 10;
    history.position_history[1] = 3;
    history.position_history[43] = 8;
    history.penalty_minutes = 0x012D;
    CHECK(fever_curve_position(history, 0) == 10 && fever_curve_position(history, 1) == 3);
    CHECK(fever_curve_position(history, 43) == 8 && fever_curve_position(history, 44) == 13);
    bool bad_history = false;
    try { (void)fever_curve_position(history, 45); } catch (const FormatError&) { bad_history = true; }
    CHECK(bad_history);
    League league;
    for (int team = 0; team < kHokLeagueSize; team++) {
        league.rows[size_t(team)].position = kHokLeagueSize - 1 - team;
        league.ranking[size_t(team)] = kHokLeagueSize - 1 - team;
    }
    sort_league(league);
    for (int team = 0; team < kHokLeagueSize; team++)
        CHECK(league.rows[size_t(team)].position == kHokLeagueSize - 1 - team);
    // Normal league sort prefers fewer played games before goal difference.
    league.rows[0].games[0] = 4; league.rows[0].wins[0] = 2; league.rows[0].losses[0] = 2;
    league.rows[0].goals_for[0] = 30;
    league.rows[1].games[0] = 3; league.rows[1].wins[0] = 2; league.rows[1].losses[0] = 1;
    league.rows[1].goals_against[0] = 20;
    sort_league(league);
    CHECK(league.rows[1].position == 0 && league.rows[0].position == 1);
    // Native non-adjacent swaps can displace equally ranked entries.
    league = League{};
    for (int team = 0; team < kHokLeagueSize; team++) league.rows[size_t(team)].position = team;
    league.rows[2].games[0] = league.rows[2].wins[0] = 1;
    sort_league(league);
    CHECK(league.rows[2].position == 0 && league.rows[1].position == 1 && league.rows[0].position == 2);
    GroupTable group{{10, 11, 12, 13}};
    group.book(10, 11, {}); group.book(12, 13, {}); group.finish_round();
    CHECK((group.order() == std::array<int, 4>{3, 2, 1, 0}));
    CHECK(group.order() == group.order());  // reading does not repeat tie swaps
    group.book(10, 12, {}); group.book(11, 13, {}); group.finish_round();
    CHECK((group.order() == std::array<int, 4>{0, 1, 2, 3}));
    group.points = {2, 2, 0, 0}; group.goals_for = {200, 0, 0, 0}; group.goals_against = {};
    group.ranking_dirty = true; group.finish_round();
    CHECK(group.order()[0] == 1);  // byte difference 200 wraps to signed -56
    HokTeam zero;
    zero.current_line[0] = zero.current_line[1] = 3;
    int draws = 0, added_away_goals = 0;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        GameRandom rng(seed);
        const MatchResult result = quick_competition_match(zero, zero, false, false, rng, 0, false);
        CHECK(result.home == 0 && result.away >= 0 && result.away <= 4 && !result.shootout);
        draws += result.home == result.away; added_away_goals += result.away > 0;
    }
    CHECK(draws > 0 && added_away_goals > 0);
    League prolific;
    prolific.rows[0].goals_for[0] = prolific.rows[1].goals_against[1] = 250;
    prolific.rows[0].games[0] = prolific.rows[0].wins[0] = 21;
    prolific.rows[1].games[1] = prolific.rows[1].losses[1] = 21;
    book_result(prolific, 0, 1, {10, 0});
    CHECK(prolific.rows[0].goals_for[0] == 4 && prolific.rows[1].goals_against[1] == 4);
    CHECK(prolific.rows[0].games[0] == 22 && prolific.rows[0].wins[0] == 22);
}

static void native_night_preparation(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "NIGHTCP1.HOK") || !fs::exists(ref_dir / "NITLOAD1.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const HokFile actual = decode_hok(read_file(ref_dir / "NITLOAD1.HOK"));  // read-only native RAM alias
    Game game(exe, read_file(ref_dir / "NIGHTCP1.HOK"), 6);
    game.advance_day(); game.advance_day();  // Sep 11 -> prepared Sep 13, before its match
    const HokFile saved = decode_hok(game.save_bytes());
    const ManagerBudget native = budget_from_save(actual, 0);
    CHECK(saved.date.day_index == actual.date.day_index && saved.date.day_index == 4);
    CHECK(game.budget().balance == native.balance && native.balance == -12064731);
    CHECK(game.budget().accrued == native.accrued && native.accrued == -515064);
    CHECK(game.image() == le16(actual.state, 0x0F40) && game.image() == 50);
    CHECK(saved.state[0x0DE1] == actual.state[0x0DE1] && game.human_team() == 20);
    CHECK(saved.state[0x0EB8] == actual.state[0x0EB8] && game.budget().league == 1);
    CHECK(saved.state[0x0EC0] == actual.state[0x0EC0] && saved.state[0x0EC0] == 4);
    CHECK(le16(saved.state, 0xB951) == le16(actual.state, 0xB951) && le16(saved.state, 0xB951) == 1);
    CHECK(std::equal(saved.state.begin() + 0x0DE5, saved.state.begin() + 0x0DE9,
                     actual.state.begin() + 0x0DE5));
}

static void native_game_noop(const fs::path& em_dir, const fs::path& ref_dir) {
    const Bytes exe = read_file(em_dir / "EM.EXE");
    for (const char* name : {"FINMONP", "ADSPOST", "ADRENOK", "PENPST22", "TACMAN22", "TACPOS22",
                            "LINEPS22", "CPPOST22", "TIEPOST2", "CUPPOST", "EUPOST", "POPOST", "POSEMIP",
                            "NATAUTO", "NATQPOST", "NATZEAR", "NATLIVP", "NATENGP", "LNGPOST2", "LICPOST2",
                            "RCHPOST1", "CUPPOS02", "OLPOST01", "PROMPOS1", "NGHTPOS1", "KAUFPOS1", "INSPOST1", "INSPOS01", "INS64POS", "FORFPOS1", "CNTPOS01", "CNTREF01", "LISTPOS1", "RECLPOS1", "BANKLN01", "BANKPAY1", "TRNPOS01", "TRNPOS02", "POOLPOS1"}) {
        const fs::path path = ref_dir / (std::string(name) + ".HOK");
        if (!fs::exists(path)) continue;
        const Bytes source = read_file(path);
        Game game(exe, source, 6);
        if (game.save_bytes() != source) std::fprintf(stderr, "native no-op mismatch: %s\n", name);
        CHECK(game.save_bytes() == source);
    }
}

static void checkpoint_continuation(const fs::path& em_dir, const fs::path& ref_dir) {
    const Bytes exe = read_file(em_dir / "EM.EXE");
    const Database db = decode_db(read_file(em_dir / "KB/23.vga"));
    { // The exported round44 preview and the retained native menu cache
      // differ. At round45 the cache becomes visible again; OEM must retain
      // that phase instead of retaining its round44 export as a new cache.
        Game direct(exe,read_file(ref_dir/"MONAT4.HOK"),6);
        direct.set_database(db);
        for(int day=0;day<20&&decode_hok(direct.save_bytes()).matchday[0]<44;++day)direct.advance_day();
        CHECK(decode_hok(direct.save_bytes()).matchday[0]==44);
        const auto checkpoint=direct.checkpoint_bytes();Game restored(exe,checkpoint,999);restored.set_database(db);
        for(int day=0;day<20&&decode_hok(direct.save_bytes()).matchday[0]<45;++day){direct.advance_day();restored.advance_day();}
        CHECK(decode_hok(direct.save_bytes()).matchday[0]==45);
        CHECK(direct.checkpoint_bytes()==restored.checkpoint_bytes());
        // After the last league day the original sets up the play-off phase at once: the pair table DS:5D7B then holds
        // the relegation round, not round44 (REAL-CODE Köln period 33). Leagues 1 and 2 keep their round44 pairs behind it.
        const auto last=round_robin_pairings(load_schedule_tables(exe).teams12,43);
        for(size_t league=1;league<3;++league)for(size_t pair=0;pair<6;++pair){
            const size_t at=0x8dfd+league*12+pair*2;
            CHECK(direct.save().state.at(at)==int(league)*12+last[pair].home);
            CHECK(direct.save().state.at(at+1)==int(league)*12+last[pair].away);
        }
        // Independently distinct export-preview bytes must not replace the
        // native working cache when an OEM is restored at the no-write round.
        auto phase=decode_game_checkpoint(direct.checkpoint_bytes());auto preview=decode_hok(phase.hok);
        for(size_t league=0;league<3;++league)for(size_t pair=0;pair<6;++pair)
            preview.pairings[league][pair]={int(league*12+pair*2),int(league*12+pair*2+1)};
        store_hok(preview);phase.hok=encode_hok(preview);
        Game cache_restore(exe,encode_game_checkpoint(phase),999);cache_restore.set_database(db);
        CHECK(cache_restore.checkpoint_bytes()==direct.checkpoint_bytes());
    }
    struct Scenario { const char* name; int days; };
    for (const Scenario scenario : {Scenario{"START", 480}, Scenario{"LINEMN22", 25},
                                    Scenario{"NATAUTO", 20}, Scenario{"POSEMI1", 30}, Scenario{"EUOWN", 45}}) {
      for (uint32_t seed : {1u, 6u, 2024u}) {
        const fs::path path = ref_dir / (std::string(scenario.name) + ".HOK");
        if (!fs::exists(path)) continue;
        const Bytes source = read_file(path);
        Game continuous(exe, source, seed);
        auto resumed = std::make_unique<Game>(exe, source, seed);
        continuous.set_database(db); resumed->set_database(db);
        continuous.clear_log(); resumed->clear_log();
        for (int day = 0; day < scenario.days; day++) {
            continuous.advance_day(); resumed->advance_day();
            // Never export the baseline until the end. Earlier tests exported
            // both sides daily and could conceal export-induced mutations.
            if (continuous.log() != resumed->log()) {
                std::fprintf(stderr, "checkpoint event mismatch: %s, day %d\n", scenario.name, day + 1);
                for (size_t i = 0; i < std::max(continuous.log().size(), resumed->log().size()); i++) {
                    const std::string a = i < continuous.log().size() ? continuous.log()[i] : "-", b = i < resumed->log().size() ? resumed->log()[i] : "-";
                    if (a != b) { std::fprintf(stderr, "  continuous: %s\n  resumed:    %s\n", a.c_str(), b.c_str()); break; }
                }
                CHECK(continuous.log() == resumed->log());
                break;
            }
            CHECK(continuous.seasons_completed() == resumed->seasons_completed());
            CHECK(continuous.offers().size() == resumed->offers().size());
            for (size_t i = 0; i < continuous.offers().size(); i++)
                CHECK(continuous.offers()[i].amount == resumed->offers()[i].amount);
            if (day % 37 == 11 && !continuous.offers().empty())
                CHECK(continuous.accept_offer(0) == resumed->accept_offer(0));
            if (day % 31 == 15)
                CHECK(continuous.accept_sponsor_offer(0, 0) == resumed->accept_sponsor_offer(0, 0));
            continuous.clear_log(); resumed->clear_log();
            const Bytes checkpoint = resumed->checkpoint_bytes();
            resumed = std::make_unique<Game>(exe, checkpoint, 999999);  // caller seed must be ignored
            resumed->set_database(db);
            const Bytes rewritten = resumed->checkpoint_bytes();
            if (rewritten != checkpoint) {
                const auto a = decode_game_checkpoint(checkpoint), b = decode_game_checkpoint(rewritten);
                size_t diff = 0;
                while (diff < std::min(a.hok.size(), b.hok.size()) && a.hok[diff] == b.hok[diff]) diff++;
                std::fprintf(stderr, "checkpoint rewrite: %s day %d HOK offset %zx, working=%d\n",
                             scenario.name, day + 1, diff, int(a.working_state == b.working_state));
            }
            CHECK(rewritten == checkpoint);
        }
        const Bytes original_working = continuous.save().state;
        const Bytes expected = continuous.save_bytes(), actual = resumed->save_bytes();
        CHECK(continuous.save().state == original_working);  // export is observational
        if (expected != actual) {
            std::fprintf(stderr, "checkpoint HOK mismatch after %s\n", scenario.name);
            const HokFile x = decode_hok(expected), y = decode_hok(actual);
            for (size_t i = 0; i < x.state.size(); i++)
                if (x.state[i] != y.state[i]) std::fprintf(stderr, "  state %zx: %02x vs %02x\n", i, x.state[i], y.state[i]);
        }
        CHECK(expected == actual);
        std::printf("checkpoint continuation: %s seed %u, %d days, save/reload after every day\n",
                    scenario.name, unsigned(seed), scenario.days);
      }
    }
    if (fs::exists(ref_dir / "START.HOK")) {
        Game game(exe, read_file(ref_dir / "START.HOK"), 6);
        HokFile clone = clone_hok(game.save());
        CHECK(clone.teams[0].raw != game.save().teams[0].raw && clone.players[0].raw != game.save().players[0].raw);
        const Bytes original_state = game.save().state;
        clone.teams[0].effort_current ^= 1;
        store_hok(clone);
        CHECK(game.save().state == original_state);
        const Bytes checkpoint = game.checkpoint_bytes();
        Bytes different = exe; different[0] ^= 1;
        bool rejected = false;
        try { Game invalid(different, checkpoint); } catch (const FormatError&) { rejected = true; }
        CHECK(rejected);
        auto broken = decode_game_checkpoint(checkpoint);
        const size_t player_id = 0x696D + 0x0F;
        broken.working_state[player_id] = broken.working_state[player_id] == 1 ? 2 : 1;
        const Bytes inconsistent = encode_game_checkpoint(broken);  // valid checksum, inconsistent identities
        rejected = false;
        try { Game invalid(exe, inconsistent); } catch (const FormatError&) { rejected = true; }
        CHECK(rejected);
    }
}

static void native_checkpoint_export(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "CPPOST22.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), source = read_file(ref_dir / "CPPOST22.HOK");
    const HokFile native = decode_hok(source);
    Game imported(exe, source, 6);
    const HokFile again = decode_hok(imported.save_bytes());
    CHECK(again.finance.balance == native.finance.balance);
    CHECK(again.state[0x8F86] == 4 && again.state[0x8F86] == native.state[0x8F86]);
    CHECK(std::equal(native.state.begin() + 0x0796, native.state.begin() + 0x0DC6, again.state.begin() + 0x0796));
    for (size_t slot = 0; slot < native.squad.size(); slot++) {
        CHECK(again.squad[slot].major_penalties == native.squad[slot].major_penalties);
        CHECK(again.squad[slot].minor_penalties == native.squad[slot].minor_penalties);
    }
    imported.advance_day();
    Game resumed(exe, imported.checkpoint_bytes(), 999);
    CHECK(resumed.checkpoint_bytes() == imported.checkpoint_bytes());
}

static void native_group_ties(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "TIELIVE2.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    Game game(exe, read_file(ref_dir / "TIELIVE2.HOK"), 6);
    for (int day = 0; day < 3 && game.euro_champion() < 0; day++) game.advance_day();
    const HokFile after = decode_hok(game.save_bytes());
    CHECK(after.state[0x8F86] == 12 && game.euro_champion() == 81);
    for (size_t i = 0; i < 4; i++) {
        CHECK(after.state[0x061C + i] == 3 - i);
        CHECK(after.state[0x064C + i] == 6);
        CHECK(after.state[0x02DF + i] == 0 && after.state[0x030F + i] == 0);
    }
    if (fs::exists(ref_dir / "TIEPOST2.HOK")) {
        const HokFile native = decode_hok(read_file(ref_dir / "TIEPOST2.HOK"));
        CHECK(native.state[0x0DC9] == 81 && native.state[0x8F86] == 12);
        for (size_t i = 0; i < 4; i++)
            for (size_t base : {size_t(0x061C), size_t(0x064C), size_t(0x02DF), size_t(0x030F)})
                CHECK(native.state[base + i] == after.state[base + i]);
    }
}

static void tactic_save(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "TACMAN22.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE"), source = read_file(ref_dir / "TACMAN22.HOK");
    const HokFile native = decode_hok(source);
    CHECK(native.state[0x01FF] == 1 && native.state[0x01F3] == 1);
    CHECK(native.teams[7].effort_base == 59 && native.teams[7].effort_current == 79);
    CHECK(native.teams[7].offence[kDraw] == 7);
    HumanSquad human{&native.squad, &native.players};
    configure_human_tactics(human, native, 0);
    CHECK(human.effort_control == 1 && human.offence_control == 1);
    HokTeam prepared = native.teams[7];
    setup_computer_team(prepared, native.teams[0], &human);
    CHECK(prepared.effort_base == 59 && prepared.offence[kDraw] == 7);
    Game g(exe, source, 6);
    CHECK(g.save_bytes() == source);  // native SEMI reference round-trips through the full Game API
    g.advance_day();
    const Bytes saved = g.save_bytes();
    const HokFile after = decode_hok(saved);
    CHECK(after.teams[7].effort_base == 59 && after.teams[7].offence[kDraw] == 7);
    CHECK(after.state[0x01FF] == 1 && after.state[0x01F3] == 1);
    CHECK(std::equal(native.state.begin() + 0xB96B, native.state.begin() + 0xBA91,
                     after.state.begin() + 0xB96B));
    Game resumed(exe, saved, 6);
    resumed.advance_day();
    CHECK(resumed.save().teams[7].effort_base == 59 && resumed.save().teams[7].offence[kDraw] == 7);
    // Native mode 2 keeps the live value fixed instead of applying score/fatigue
    // changes. This is a controlled derivative, not an original UI observation.
    HokFile fixed = decode_hok(source);
    fixed.state[0x01FF] = fixed.state[0x01F3] = 2;
    fixed.teams[7].effort_current = 37;
    fixed.teams[7].flags = (fixed.teams[7].flags & 0x8F) | 0x50;
    store_hok(fixed);
    Game manual(exe, encode_hok(fixed), 6);
    manual.advance_day();
    const HokFile manual_after = decode_hok(manual.save_bytes());
    CHECK(manual_after.teams[7].effort_current == 37);
    CHECK(((manual_after.teams[7].flags >> 4) & 7) == 5);
    CHECK(manual_after.state[0x01FF] == 2 && manual_after.state[0x01F3] == 2);
    std::puts("tactics save: native SEMI 59/7 retained; independent controls and fixed live values checked");
}

static void personal_penalties() {
    PenaltyOpportunities opportunities;
    const int pp = opportunities.record(1, 4, 5, false, true);
    CHECK(pp == 0 && opportunities.power_plays[0] == 1);
    opportunities.rollback(pp);  // native paired call failed
    CHECK(opportunities.power_plays[0] == 0);
    const int pk = opportunities.record(0, 4, 5, true, false);
    CHECK(pk == 2 && opportunities.penalty_kills[0] == 1);
    CHECK(opportunities.record(0, 3, 5, true, false) == -1);  // two-skater deficit
    CHECK(opportunities.record(1, 4, 4, false, true) == -1);  // equal strength
    // With two human managers the original remembers only the last touched
    // counter: rollback of the kill leaves the other manager's power play intact.
    const int both = opportunities.record(1, 4, 5, true, true);
    opportunities.rollback(both);
    CHECK(opportunities.power_plays[0] == 1 && opportunities.penalty_kills[1] == 0);
    opportunities.after_goal(0, true, 4, 5, true, true);
    CHECK(opportunities.penalty_kills[0] == 2 && opportunities.power_plays[1] == 1);
    opportunities.after_goal(0, false, 4, 5, true, true);
    opportunities.after_goal(1, true, 5, 5, true, true);
    CHECK(opportunities.penalty_kills[0] == 2 && opportunities.power_plays[1] == 1);
    HokTeam team;
    team.skaters = 5;
    std::vector<HokSquadPlayer> squad(8);
    std::vector<HokPlayer> players(8);
    for (size_t i = 0; i < squad.size(); i++) {
        squad[i].player = int(i);
        squad[i].role = int(i) + 1;
        squad[i].line = 0x81;
        squad[i].skill_a = 80; squad[i].skill_b = 60;
        players[i].position = 75;
    }
    squad[1].minor_penalties = 255;
    PenaltyBox box;
    CHECK(box.start(team, true, 0, &squad));
    CHECK(box.start(team, false, 1, &squad));
    CHECK(!box.start(team, false, 2, &squad));
    CHECK(team.skaters == 3 && squad[0].role == 0 && squad[1].role == 0);
    CHECK(squad[0].major_penalties == 1 && squad[1].minor_penalties == 0);  // native byte wrap
    box.goal_against(team, &squad);  // native quirk: upper timer ends, first queued player returns
    CHECK(team.skaters == 4 && squad[0].role == 1 && squad[1].role == 0);
    for (int minute = 0; minute < 4; minute++) CHECK(box.tick(team, &squad) == 1);
    CHECK(box.tick(team, &squad) == 0 && team.skaters == 5 && squad[1].role == 2);
    CHECK(box.start(team, false, 1, &squad));
    CHECK(box.tick(team, &squad) == 1);
    CHECK(box.start(team, false, 2, &squad));
    box.goal_against(team, &squad);
    CHECK(team.skaters == 4 && squad[1].role == 2 && squad[2].role == 0);
    CHECK(box.tick(team, &squad) == 0 && squad[2].role == 3);  // second player inherits earlier timer
    CHECK(box.start(team, false, 1, &squad) && box.start(team, true, 2, &squad));
    box.finish(team, &squad);
    CHECK(team.skaters == 5 && box.running() == 0 && squad[1].role == 2 && squad[2].role == 3);
    CHECK(box.started() == 6 && box.minutes_assessed() == 18);
    // Only slot 3 is on the current forward line and has a role. Selection uses
    // the separate Borland stream, with bounded rejection instead of an endless loop.
    for (auto& p : squad) p.role = 0;
    squad[3].role = 1;
    BorlandRandom selected(1);
    CHECK(pick_penalized_player(team, squad, players, selected) == 3);
    squad[3].line = 0x82;
    CHECK(pick_penalized_player(team, squad, players, selected) == -1);
    CHECK(pick_penalized_player(team, squad, players, selected, false) == 3);
    squad.resize(5);
    CHECK(pick_penalized_player(team, squad, players, selected) == -1);
    squad.resize(8);
    for (size_t i = 0; i < squad.size(); i++) {
        squad[i].player = int(i); squad[i].line = 0x81;
        squad[i].role = i < 7 ? 1 : 0;
    }
    HumanSquad depleted{&squad, &players};
    MatchStats stats;
    GameRandom match_rng(1);
    for (int match = 0; match < 20; match++) league_match(team, team, match_rng, 0, &depleted, nullptr, &stats);
    CHECK(stats.penalties[0] == 0);  // native minimum of eight assigned players
    for (size_t i = 0; i < squad.size(); i++) CHECK(stats.final_squads[0][i].role == squad[i].role);
}

static void personal_penalty_saves(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    Game g(exe, read_file(ref_dir / "START.HOK"), 6);
    for (int day = 0; day < 30; day++) g.advance_day();
    const HokFile stored = decode_hok(g.save_bytes());
    int personal = 0;
    for (size_t slot = 0; slot < stored.squad.size(); slot++) {
        const auto& p = stored.squad[slot];
        if (p.player < 0) continue;
        CHECK(p.major_penalties == g.save().squad[slot].major_penalties);
        CHECK(p.minor_penalties == g.save().squad[slot].minor_penalties);
        personal += p.major_penalties + p.minor_penalties;
    }
    CHECK(personal > 0);
    for (size_t team = 0; team < 36; team++) CHECK(stored.table[team].penalty_minutes > 0);
    Game resumed(exe, g.save_bytes(), 6);
    const HokFile again = decode_hok(resumed.save_bytes());
    for (size_t team = 0; team < 36; team++)
        CHECK(again.table[team].penalty_minutes == stored.table[team].penalty_minutes);
    for (size_t slot = 0; slot < stored.squad.size(); slot++) {
        CHECK(again.squad[slot].major_penalties == stored.squad[slot].major_penalties);
        CHECK(again.squad[slot].minor_penalties == stored.squad[slot].minor_penalties);
    }
    if (fs::exists(ref_dir / "FINEVMON.HOK")) {
        const HokFile native = decode_hok(read_file(ref_dir / "FINEVMON.HOK"));
        int major = 0, minor = 0;
        for (const auto& p : native.squad) { major += p.major_penalties; minor += p.minor_penalties; }
        CHECK(major == 2 && minor == 9);  // original league + cup, individually observed in guest RAM
    }
    if (fs::exists(ref_dir / "PENCHK22.HOK") && fs::exists(ref_dir / "PENPST22.HOK")) {
        // C++ counters 7/43 were retained by the original; its next league match
        // added one major and two minors. PENPST22 is a real native dialog save.
        const HokFile input = decode_hok(read_file(ref_dir / "PENCHK22.HOK"));
        const HokFile native = decode_hok(read_file(ref_dir / "PENPST22.HOK"));
        int major_delta = 0, minor_delta = 0;
        for (size_t slot = 0; slot < input.squad.size(); slot++) {
            CHECK(input.squad[slot].player == native.squad[slot].player);
            major_delta += native.squad[slot].major_penalties - input.squad[slot].major_penalties;
            minor_delta += native.squad[slot].minor_penalties - input.squad[slot].minor_penalties;
        }
        CHECK(major_delta == 1 && minor_delta == 2);
        CHECK(native.table[7].penalty_minutes - input.table[7].penalty_minutes == 9);
        Game imported(exe, read_file(ref_dir / "PENPST22.HOK"), 6);
        const HokFile exported = decode_hok(imported.save_bytes());
        CHECK(exported.table[7].penalty_minutes == native.table[7].penalty_minutes);
        for (size_t slot = 0; slot < native.squad.size(); slot++) {
            CHECK(exported.squad[slot].major_penalties == native.squad[slot].major_penalties);
            CHECK(exported.squad[slot].minor_penalties == native.squad[slot].minor_penalties);
        }
        imported.advance_day();
        const HokFile continued = decode_hok(imported.save_bytes());
        CHECK(continued.table[7].penalty_minutes >= native.table[7].penalty_minutes);
        CHECK(continued.date.day_index == native.date.day_index + 1);
        for (size_t league = 0; league < 3; league++) {
            const size_t at = 0x0796 + league * 0x210;
            CHECK(std::equal(native.state.begin() + at, native.state.begin() + at + 10 * 12,
                             continued.state.begin() + at));
        }
    }
    if (fs::exists(ref_dir / "SAISEND.HOK") && fs::exists(ref_dir / "NEUSAIS.HOK")) {
        const HokFile native = decode_hok(read_file(ref_dir / "NEUSAIS.HOK"));
        for (const auto& p : native.squad) CHECK(p.major_penalties == 0 && p.minor_penalties == 0);
        HokFile marked = decode_hok(read_file(ref_dir / "SAISEND.HOK"));
        for (auto& p : marked.squad) p.major_penalties = p.minor_penalties = 254;
        store_hok(marked);
        Game rollover(exe, encode_hok(marked), 6);
        for (int day = 0; day < 20 && rollover.seasons_completed() == 0; day++) rollover.advance_day();
        CHECK(rollover.seasons_completed() == 1 && !rollover.save().squad.empty());
        // The first new-season match may already be played by advance_day();
        // none of the prior season's marked counters may remain.
        for (const auto& p : rollover.save().squad) CHECK(p.major_penalties < 254 && p.minor_penalties < 254);
    }
    std::printf("personal penalties: %d assessed player counters survive save/load; native major/minor fields checked\n", personal);
}

static void scorer_choice(const fs::path& ref_dir) {
    {
        std::vector<HokPlayer> players(2);
        players[1].position = 75;
        std::vector<HokSquadPlayer> depleted(4);  // includes native empty slots (player == -1)
        depleted[0].player = 0;
        depleted[0].role = 1;
        depleted[3].player = 1;
        depleted[3].role = 1;
        BorlandRandom rng(1);
        const GoalCredit only = credit_goal(depleted, players, rng);
        CHECK(only.scorer == 3 && only.assist == -1 && depleted[3].league_goals == 1);
        depleted[3].role = 0;
        const GoalCredit none = credit_goal(depleted, players, rng);
        CHECK(none.scorer == -1 && none.assist == -1);
    }
    if (!fs::exists(ref_dir / "START.HOK")) return;
    HokFile h = decode_hok(read_file(ref_dir / "START.HOK"));
    BorlandRandom rng(1);
    long forwards = 0, total = 0, goalies = 0;
    for (int run = 0; run < 300; run++) {
        std::vector<HokSquadPlayer> squad = h.squad;
        for (int g = 0; g < 27; g++) {
            GoalCredit c = credit_goal(squad, h.players, rng);
            CHECK(c.scorer >= 0 && c.assist >= 0 && c.scorer != c.assist);
            const HokSquadPlayer& s = squad[size_t(c.scorer)];
            forwards += (s.line & 0x80) != 0;
            goalies += h.players[size_t(s.player)].position == 0;
            total++;
        }
    }
    std::printf("scorers: forwards %.0f%% of goals (original Köln 89%%), goalkeepers %ld\n",
                100.0 * double(forwards) / double(total), goalies);
    CHECK(goalies == 0);
    CHECK(double(forwards) / double(total) > 0.75 && double(forwards) / double(total) < 0.97);
    BorlandRandom a(5);
    CHECK(a.rand() == int(((5u * 22695477u + 1u) >> 16) & 0x7FFF));
}

// Human team (FUN_162c_16bf, squad branch) with Kölner EC in MONAT1: the strengths from the squad
// are of the same order as the computer-team formula from the team record, the empty net costs
// the defence everything, and a season of matches against the league gives a plausible record.
static void human_team(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    HumanSquad hs{&h.squad, &h.players};
    const HokTeam& koeln = h.teams[7];
    for (StrengthMode m : {StrengthMode::Defensive, StrengthMode::Neutral, StrengthMode::Offensive}) {
        int squad_s = playing_strength(koeln, &hs, m, 0, 0);
        int record_s = playing_strength(koeln, nullptr, m, 0, 0);
        std::printf("Köln strength mode %d: squad %d, team record %d\n", int(m), squad_s, record_s);
        CHECK(squad_s > record_s * 6 / 10 && squad_s < record_s * 14 / 10);
    }
    if (fs::exists(ref_dir / "START.HOK")) {
        HokFile s = decode_hok(read_file(ref_dir / "START.HOK"));
        HumanSquad ss{&s.squad, &s.players};
        for (StrengthMode m : {StrengthMode::Defensive, StrengthMode::Offensive})
            std::printf("Köln START strength mode %d: squad %d, team record %d, effort %d\n", int(m),
                        playing_strength(s.teams[7], &ss, m, 0, 0), playing_strength(s.teams[7], nullptr, m, 0, 0),
                        s.teams[7].effort_current);
        for (int kind = 0; kind < 2; kind++) {
            LineValues v = line_strength(s.teams[7], ss, 0, kind, true, 0);
            std::printf("  START kind %d line 0: s1 %d s2 %d f %d\n", kind, v.skill1, v.skill2, v.form);
        }
    }
    // without a goalkeeper role the defence collapses
    std::vector<HokSquadPlayer> no_goalie = h.squad;
    for (auto& p : no_goalie)
        if (h.players[size_t(p.player)].position == 0) p.role = 0;
    HumanSquad empty{&no_goalie, &h.players};
    CHECK(line_strength(koeln, empty, 0, 0, true, 0).skill1 == 0);
    // Köln with its squad against the rest of the league, home and away, many times
    ScheduleTables tables = load_schedule_tables(read_file(em_dir / "EM.EXE"));
    GameRandom rng(99);
    int w = 0, d = 0, l = 0, gf = 0, ga = 0;
    for (int run = 0; run < 100; run++)
        for (size_t o = 0; o < 12; o++) {
            if (o == 7) continue;
            MatchResult r1 = league_match(koeln, h.teams[o], rng, 0, &hs, nullptr);
            MatchResult r2 = league_match(h.teams[o], koeln, rng, 0, nullptr, &hs);
            for (auto [f, a] : {std::pair{r1.home, r1.away}, std::pair{r2.away, r2.home}}) {
                gf += f;
                ga += a;
                w += f > a;
                d += f == a;
                l += f < a;
            }
        }
    std::printf("Köln (squad) vs league: %d-%d-%d, goals %d:%d per 2200 games\n", w, d, l, gf, ga);
    CHECK(w > l / 2 && w < l * 4);  // a mid-to-upper table team: neither hopeless nor unbeatable
    // Penalties and lines (ovl06_02d8, FUN_162c_0e32): Köln's "Totale" screen after 11 games shows
    // 36 power plays and 39 penalty kills (about 3.5 penalties per side and game) and 6 power-play
    // goals (16 %). Köln plays in tactics mode 2 (automatic line changes).
    MatchStats st;
    int games = 0;
    for (int run = 0; run < 50; run++)
        for (size_t o = 0; o < 12; o++) {
            if (o == 7) continue;
            league_match(koeln, h.teams[o], rng, 0, &hs, nullptr, &st);
            games++;
        }
    const double pen_own = double(st.penalties[0]) / games, pen_opp = double(st.penalties[1]) / games;
    const double pp_rate = st.power_plays[0] ? double(st.power_play_goals[0]) / st.power_plays[0] : 0;
    CHECK(st.power_plays[0] > 0 && st.penalty_kills[0] > 0);
    CHECK(st.power_plays[0] <= st.penalties[1] && st.penalty_kills[0] <= st.penalties[0]);
    std::printf("Köln live: %.1f own / %.1f opponent penalties per game, power-play goals %.0f%%, "
                "defence line minutes %d/%d/%d, forward %d/%d/%d\n",
                pen_own, pen_opp, 100 * pp_rate, st.line_minutes[0][0], st.line_minutes[0][1], st.line_minutes[0][2],
                st.line_minutes[1][0], st.line_minutes[1][1], st.line_minutes[1][2]);
    CHECK(pen_own > 1.5 && pen_own < 7 && pen_opp > 1.5 && pen_opp < 7);
    CHECK(st.line_minutes[1][1] > 0 && st.line_minutes[1][2] > 0);  // automatic mode rotates the lines
}

// Matchday simulation: re-sorting the stored tables of TAG1/MONAT1 must give the stored positions,
// and playing matchdays 1-9 from START (many seeds) must give statistics close to the original
// season in MONAT1 per league (goals per match, draws, home wins).
static void season_regression(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK") || !fs::exists(ref_dir / "MONAT1.HOK")) return;
    ScheduleTables tables = load_schedule_tables(read_file(em_dir / "EM.EXE"));
    HokFile start = decode_hok(read_file(ref_dir / "START.HOK"));
    HokFile month = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    for (const HokFile* save : {&month}) {
        for (int l = 0; l < 3; l++) {
            League league = league_from_save(*save, l);
            League sorted = league;
            sort_league(sorted);
            for (int s = 0; s < kHokLeagueSize; s++)
                CHECK(sorted.rows[size_t(s)].position == league.rows[size_t(s)].position);
        }
    }
    for (int l = 0; l < 3; l++) {
        // original after 9 matchdays
        League orig = league_from_save(month, l);
        double og = 0, od = 0, oh = 0, games = 0;
        for (const HokTableRow& r : orig.rows) {
            og += r.total(r.goals_for);
            games += r.games[0];
            oh += r.wins[0];
            od += r.draws(0);
        }
        // simulated: 200 seasons x 9 matchdays from the START tables
        GameRandom rng(uint32_t(1000 + l));
        double sg = 0, sd = 0, sh = 0, sn = 0;
        for (int run = 0; run < 200; run++) {
            League league = league_from_save(start, l);
            for (int md = 0; md < 9; md++)
                for (const MatchResult& r : play_matchday(league, start.teams, tables, md, rng)) {
                    sg += r.home + r.away;
                    sd += r.home == r.away;
                    sh += r.home > r.away;
                    sn++;
                }
        }
        std::printf("league %d: original %.2f goals, %.0f%% draws, %.0f%% home wins; simulated %.2f, %.0f%%, %.0f%%\n",
                    l, og / games, 100 * od / games, 100 * oh / games, sg / sn, 100 * sd / sn, 100 * sh / sn);
        CHECK(std::abs(sg / sn - og / games) < 1.0);
        CHECK(sd / sn > 0.03 && sd / sn < 0.25);
        CHECK(std::abs(sh / sn - oh / games) < 0.1);
    }
}

// Quick match simulation (FUN_162c_1e9c) with the first-division clubs of MONAT1: the goal average
// must be close to the 6.9 goals per match the original produced in the first 9 matchdays
// (375 goals in 54 matches), and the strongest club must beat the weakest most of the time.
static void match_plausibility(const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "MONAT1.HOK")) return;
    HokFile h = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    GameRandom rng(12345);
    long goals = 0, games = 0;
    for (int round = 0; round < 50; round++)
        for (size_t i = 0; i < 12; i++)
            for (size_t j = 0; j < 12; j++) {
                if (i == j) continue;
                MatchResult r = quick_match(h.teams[i], h.teams[j], rng);
                goals += r.home + r.away;
                games++;
            }
    double avg = double(goals) / double(games);
    int strong_wins = 0;
    for (int n = 0; n < 1000; n++) {
        MatchResult r = quick_match(h.teams[10], h.teams[11], rng);  // Preussen - Landshut
        strong_wins += r.home > r.away;
    }
    std::printf("match: %.2f goals per match (original 6.94), Preussen beats Landshut %d / 1000\n", avg, strong_wins);
    CHECK(avg > 5.5 && avg < 8.5);
    CHECK(strong_wins > 600);
    GameRandom a(7), b(7);
    CHECK(a.range(0, 99) == b.range(0, 99));
    LineValues lv = line_strength(h.teams[10], 0, 0, false, 0);  // Preussen defence, first line
    CHECK(lv.skill1 == std::min(100, h.teams[10].skill1[0] + 4));
    CHECK(lv.form == std::min(100, h.teams[10].form[0] + 4));
    HokTeam extreme;
    extreme.skill1[0] = extreme.skill2[0] = extreme.form[0] = 255;
    const LineValues capped = line_strength(extreme, 0, 0, false, 0);
    CHECK(capped.skill1 == 100 && capped.skill2 == 100 && capped.form == 100);  // native 1bce..1c1b
}

// Schedule tables from EM.EXE reproduce every pairing list stored in the reference saves: the
// next league matchday (all three leagues) and the full WM and European cup group schedules.
static void schedule_vs_saves(const fs::path& em_dir, const fs::path& ref_dir) {
    ScheduleTables tables = load_schedule_tables(read_file(em_dir / "EM.EXE"));
    CHECK(tables.teams12[1] == "dafchejglibk");
    int checked = 0;
    for (const char* name : {"START.HOK", "TAG1.HOK", "MONAT1.HOK"}) {
        if (!fs::exists(ref_dir / name)) continue;
        HokFile h = decode_hok(read_file(ref_dir / name));
        for (size_t l = 0; l < 3; l++) {
            auto gen = round_robin_pairings(tables.teams12, h.matchday[l] - 1);
            for (size_t k = 0; k < gen.size(); k++)
                CHECK(gen[k].home + int(l) * 12 == h.pairings[l][k].home &&
                      gen[k].away + int(l) * 12 == h.pairings[l][k].away);
        }
        for (size_t g = 0; g < 2; g++) {
            // round 1 lists the group, so slot s of the group is wm_rounds[g][0][s/2].home/.away
            auto slot = [&](int s) { return s % 2 ? h.wm_rounds[g][0][size_t(s / 2)].away : h.wm_rounds[g][0][size_t(s / 2)].home; };
            for (int r = 0; r < 5; r++) {
                auto gen = round_robin_pairings(tables.teams6, r);
                for (size_t k = 0; k < gen.size(); k++)
                    CHECK(slot(gen[k].home) == h.wm_rounds[g][size_t(r)][k].home &&
                          slot(gen[k].away) == h.wm_rounds[g][size_t(r)][k].away);
            }
            for (int r = 0; r < 6; r++) {
                auto gen = round_robin_pairings(tables.teams4, r);
                for (size_t k = 0; k < gen.size(); k++)
                    CHECK(h.euro_groups[g][size_t(gen[k].home)] == h.euro_rounds[g][size_t(r)][k].home &&
                          h.euro_groups[g][size_t(gen[k].away)] == h.euro_rounds[g][size_t(r)][k].away);
            }
        }
        checked++;
    }
    std::printf("schedule: %d saves checked\n", checked);
}

// Reference saves made in DOSBox-X (work\EM, Kölner EC): team table and player records.
static void reference_hok(const fs::path& dir) {
    for (const char* name : {"START.HOK", "TAG1.HOK", "MONAT1.HOK"}) {
        if (!fs::exists(dir / name)) continue;
        HokFile h = decode_hok(read_file(dir / name));
        CHECK(h.club == "KÖLNER EC" && h.teams.size() == 150);
        CHECK(h.teams[0].name == "EC HEDOS MÜNCHEN" && h.teams[7].name == "KÖLNER EC");
        CHECK(h.teams[56].name == "DEUTSCHLAND" && h.teams[149].name == "VSV VILLACH");
        int own = 0;
        bool ages_ok = true;
        for (auto& p : h.players) {
            own += p.team == 7;
            ages_ok = ages_ok && p.age >= 18 && p.age <= 40;
        }
        CHECK(h.players.size() >= 190 && own == 25 && ages_ok);
        std::printf("%s: %zu players, %d at the human club\n", name, h.players.size(), own);
    }
    // Date, matchday and league table against the in-game screens (work\ref\*.png).
    auto row_is = [](const HokTableRow& r, int pos, int w, int d, int l, int pp, int pm, int gf, int ga) {
        return r.position + 1 == pos && r.total(r.wins) == w && r.draws(0) + r.draws(1) == d &&
               r.total(r.losses) == l && r.points_plus() == pp && r.points_minus() == pm &&
               r.total(r.goals_for) == gf && r.total(r.goals_against) == ga;
    };
    if (fs::exists(dir / "START.HOK")) {
        HokFile h = decode_hok(read_file(dir / "START.HOK"));
        // "Sonntag, 10. Sept. 1993", before the first matchday
        CHECK(h.date.day == 10 && h.date.month == 9 && h.date.year == 1993 && h.date.weekday() == 0);
        CHECK(h.matchday[0] == 1 && h.table[7].form == "--------" && h.table[7].games[0] == 0);
        CHECK(h.finance.balance == 500000 && h.finance.bank == 0);  // "500.000 DM"
        CHECK(h.finance.attendance_min == 99999 && h.finance.shares[0] == 0);
        // first matchday: EHC Freiburg - Kölner EC; cup round 1 with 32 teams
        CHECK(h.pairings[0][3].home == 6 && h.pairings[0][3].away == 7);
        CHECK(h.cup_round == 1 && h.cup_draw.size() == 16);
    }
    if (fs::exists(dir / "TAG1.HOK")) {
        HokFile h = decode_hok(read_file(dir / "TAG1.HOK"));
        // "Mittwoch, 13. Sept. 1993", table after matchday 1
        CHECK(h.date.day == 13 && h.date.month == 9 && h.date.weekday() == 3 && h.matchday[0] == 2);
        CHECK(row_is(h.table[10], 1, 1, 0, 0, 2, 0, 9, 4));    // SC PREUSSEN BERLIN
        CHECK(row_is(h.table[7], 11, 0, 0, 1, 0, 2, 1, 5));    // KÖLNER EC
        CHECK(row_is(h.table[11], 12, 0, 0, 1, 0, 2, 4, 9));   // EV LANDSHUT
        CHECK(h.pairings[0][2].home == 7 && h.pairings[0][2].away == 4);  // Köln - Kaufbeuren
        // table order = the in-game table after matchday 1
        const std::array<int, 12> order = {10, 6, 0, 4, 3, 8, 9, 5, 2, 1, 7, 11};
        CHECK(h.table_order[0] == order);
    }
    if (fs::exists(dir / "MONAT1.HOK")) {
        HokFile h = decode_hok(read_file(dir / "MONAT1.HOK"));
        // "Mittwoch, 11. Okt. 1993", table after matchday 9
        CHECK(h.date.day == 11 && h.date.month == 10 && h.date.weekday() == 3 && h.matchday[0] == 10);
        CHECK(row_is(h.table[10], 1, 8, 0, 1, 16, 2, 53, 18));  // SC PREUSSEN BERLIN
        CHECK(row_is(h.table[3], 2, 8, 0, 1, 16, 2, 40, 16));   // DÜSSELDORFER EG
        CHECK(row_is(h.table[7], 4, 5, 2, 2, 12, 6, 27, 23));   // KÖLNER EC
        CHECK(row_is(h.table[9], 12, 0, 1, 8, 1, 17, 18, 39));  // EISBÄREN BERLIN
        CHECK(h.table[10].form == "SssSssNS" && h.table[7].form == "sUsUNssS");
        CHECK(h.table[7].position_history[0] == 10);  // 11th after matchday 1, as in TAG1
        const HokFinance& f = h.finance;
        CHECK(f.balance == 518266 && f.bank == 0);     // "518.266 DM"
        CHECK(h.pairings[0][1].home == 7 && h.pairings[0][1].away == 1);  // Köln - Mannheim
        // "DEB-Pokalspiele Achtelfinale": 8 pairings, among them Freiburg - Köln, Kempten - Landshut
        CHECK(h.cup_round == 2 && h.cup_draw.size() == 8);
        bool fk = false, kl = false;
        for (const HokPairing& p : h.cup_draw) {
            fk = fk || (p.home == 6 && p.away == 7);
            kl = kl || (h.teams[size_t(p.home)].name == "EA KEMPTEN" && p.away == 11);
        }
        CHECK(fk && kl);
        // "Verträge" screen (work\ref\MONAT1_vertraege.png): SP, ST, TO, STATUS, E, years, salary
        CHECK(h.squad.size() == 25);
        struct Row { const char* name; int sp, st, to; SquadStatus status; int e; long salary; };
        const Row rows[] = {
            {"HEISS", 0, 72, 0, SquadStatus::Reserve, 75, 17100},
            {"CISSEWSKI", 11, 72, 0, SquadStatus::Team, 36, 18000},
            {"J.SCHULZ", 11, 74, 2, SquadStatus::Team, 37, 12100},
            {"KÜHN", 0, 67, 0, SquadStatus::None, 78, 11000},
            {"SIKORA", 11, 70, 4, SquadStatus::Team, 72, 12100},
            {"HANEMANN", 11, 69, 4, SquadStatus::Team, 72, 10400},
            {"SCHAEDLER", 11, 73, 7, SquadStatus::Team, 82, 11500},
        };
        for (const Row& row : rows) {
            bool found = false;
            for (const HokSquadPlayer& s : h.squad) {
                if (h.players[size_t(s.player)].name != row.name) continue;
                found = true;
                CHECK(s.league_games + s.cup_games == row.sp && s.strength() == row.st);
                CHECK(s.league_goals + s.cup_goals == row.to && s.status == row.status);
                CHECK(s.exhaustion == row.e && s.salary == row.salary && s.contract_years == 2);
            }
            CHECK(found);
        }
        CHECK(h.squad[1].goals_against == 23);  // CISSEWSKI: Köln conceded 23 league goals
        // "STÄRKEN Bundesliga - Gesamt" (MONAT1_staerken1.png): columns 1-3 of all 12 clubs
        const int strength[12][3] = {{82, 85, 50}, {81, 75, 48}, {72, 76, 48}, {89, 89, 52}, {79, 90, 54},
                                     {75, 80, 49}, {71, 77, 51}, {80, 80, 52}, {76, 75, 51}, {68, 69, 53},
                                     {96, 91, 47}, {70, 67, 46}};
        for (size_t t = 0; t < 12; t++)
            CHECK(h.teams[t].skill1_mean() == strength[t][0] && h.teams[t].skill2_mean() == strength[t][1] &&
                  h.teams[t].form_mean() == strength[t][2]);
        CHECK(h.teams[52].energy[0][1] == 90);  // computer teams keep fresh lines
        // "Taktik" of Kölner EC (MONAT1_taktik.png): effort 40 / 20 20 15 20 0, offence 0 / 4 5 6 7 4
        const HokTeam& koeln = h.teams[7];
        CHECK(koeln.effort_base == 40 && koeln.effort[kHighLead] == 20 && koeln.effort[kDraw] == 15 &&
              koeln.effort[kHighDeficit] == 0);
        CHECK(koeln.offence_base == 0 && koeln.offence[kHighLead] == 4 && koeln.offence[kNarrowDeficit] == 7);
        // "Europacup Gruppe 1/2" and WM screens (work\ref\MONAT1_eurocup*.png, MONAT1_wm*.png)
        CHECK(h.teams[size_t(h.euro_groups[0][0])].name == "DS OLMUETZ" && h.euro_groups[1][3] == 3);
        CHECK(h.euro_rounds[0][1][0].home == 96 && h.euro_rounds[0][1][0].away == 82);  // Chamonix - Olmütz
        CHECK(h.euro_rounds[1][3][1].home == 3);  // return game round 4: Düsseldorf at home
        CHECK(h.teams[size_t(h.wm_rounds[0][0][0].home)].name == "RUSSLAND" &&
              h.teams[size_t(h.wm_rounds[1][0][2].away)].name == "TSCHECHEI");
        // "Totale" screen: attendance 7.500 average/record/lowest, power play 6 of 36 (16 %),
        // 11 conceded short-handed in 39 penalty kills (72 %); share prices 70/83/87/102 DM
        CHECK(f.attendance_total == 30000 && f.attendance_record == 7500 && f.attendance_min == 7500);
        CHECK(f.pp_goals == 6 && f.sh_against == 11 && f.power_plays == 36 && f.penalty_kills == 39);
        CHECK(f.share_price[0][35] == 70 && f.share_price[1][35] == 83 && f.share_price[2][35] == 87 &&
              f.share_price[3][35] == 102);
    }
    if (fs::exists(dir / "AKTIE.HOK")) {
        // MONAT1 + 134.000 DM on the savings book + 2.088 Herrenhäuser shares for 148.352 DM
        HokFile h = decode_hok(read_file(dir / "AKTIE.HOK"));
        const HokFinance& f = h.finance;
        CHECK(f.balance == 235914 && f.bank == 134000 && f.month_income == 14715);
        CHECK(f.shares[0] == 2088 && f.share_cost[0] == 148352 && f.shares[1] == 0);
    }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // progress lines in order with the CHECK messages on stderr
    try {
        unit_tests();
        cup_draws();
        match_energy();
        tactic_controls();
        line_controls();
        checkpoint_format();
        table_ordering();
        personal_penalties();
        season_end();
        if (argc > 1) {
            original_kb(argv[1]);
            original_hok(argv[1]);
            original_db(argv[1]);
            original_tore(argv[1]);
        }
        if (argc > 2) reference_hok(argv[2]);
        if (argc > 2) hok_round_trip(argv[1], argv[2]);
        if (argc > 2) schedule_vs_saves(argv[1], argv[2]);
        if (argc > 2) calendar_vs_saves(argv[1], argv[2]);
        if (argc > 2) match_plausibility(argv[2]);
        if (argc > 2) season_regression(argv[1], argv[2]);
        if (argc > 2) human_team(argv[1], argv[2]);
        if (argc > 2) scorer_choice(argv[2]);
        if (argc > 2) competitions(argv[2]);
        if (argc > 2) finances(argv[1], argv[2]);
        if (argc > 2) attendance(argv[1], argv[2]);
        if (argc > 2) contracts(argv[1], argv[2]);
        if (argc > 2) transfers(argv[1], argv[2]);
        if (argc > 2) sponsors(argv[1], argv[2]);
        if (argc > 2) image_competitions(argv[2]);
        if (argc > 2) training(argv[1], argv[2]);
        if (argc > 2) human_team_record(argv[2]);
        if (argc > 2) day_loop(argv[1], argv[2]);
        if (argc > 2) attendance_history(argv[1], argv[2]);
        if (argc > 2) full_season(argv[1], argv[2]);
        if (argc > 2) season_change(argv[1], argv[2]);
        if (argc > 2) regional_manager_image(argv[1], argv[2]);
        if (argc > 2) financial_license_save(argv[1], argv[2]);
        if (argc > 2) human_promotion(argv[1], argv[2]);
        if (argc > 2) human_oberliga_relegation(argv[1], argv[2]);
        if (argc > 2) native_summer_finances(argv[1], argv[2]);
        if (argc > 2) summer_healing_and_construction(argv[1], argv[2]);
        if (argc > 2) february_sponsor_boundary(argv[1], argv[2]);
        if (argc > 2) saved_economy(argv[1], argv[2]);
        if (argc > 2) game_save(argv[1], argv[2]);
        if (argc > 2) finance_save(argv[1], argv[2]);
        if (argc > 2) sponsor_contracts(argv[1], argv[2]);
        if (argc > 2) cup_save_dates(argv[1], argv[2]);
        if (argc > 2) cup_series_save(argv[1], argv[2]);
        if (argc > 2) original_finance_month(argv[1], argv[2]);
        if (argc > 2) original_club_lineup(argv[2]);
        if (argc > 2) tactic_save(argv[1], argv[2]);
        if (argc > 2) human_line_energy(argv[2]);
        if (argc > 2) line_control_save(argv[1], argv[2]);
        if (argc > 2) checkpoint_continuation(argv[1], argv[2]);
        if (argc > 2) native_checkpoint_export(argv[1], argv[2]);
        if (argc > 2) native_group_ties(argv[1], argv[2]);
        if (argc > 2) native_game_noop(argv[1], argv[2]);
        if (argc > 2) native_night_preparation(argv[1], argv[2]);
        if (argc > 2) personal_penalty_saves(argv[1], argv[2]);
        if (argc > 2) euro_save(argv[1], argv[2]);
        if (argc > 2) world_save(argv[1], argv[2]);
        if (argc > 2) national_squads(argv[1], argv[2]);
        if (argc > 2) playoff_save(argv[1], argv[2]);
        if (argc > 2) league_results_save(argv[1], argv[2]);
        if (argc > 2) transfer_list(argv[1], argv[2]);
        if (argc > 2) owned_transfers(argv[1], argv[2]);
        if (argc > 2) owned_transfer_seasons(argv[1], argv[2]);
        if (argc > 2) bank_actions(argv[1], argv[2]);
        if (argc > 2) training_actions(argv[1], argv[2]);
        if (argc > 2) transfer_purchases(argv[1], argv[2]);
        if (argc > 2) contract_cooldowns(argv[1], argv[2]);
        if (argc > 2) forfeits(argv[1], argv[2]);
        if (argc > 2) contract_negotiations(argv[1], argv[2]);
        if (argc > 2) player_insurance(argv[1], argv[2]);
        if (argc > 2) injuries(argv[1], argv[2]);
        if (argc > 2) manager_offers(argv[1], argv[2]);
        if (argc > 2) contract_expiry(argv[1], argv[2]);
        if (argc > 2) save_years(argv[1], argv[2]);
        if (argc > 2) club_scorers(argv[1], argv[2]);
        if (argc > 2) human_match_statistics(argv[1], argv[2]);
        if (argc > 2) head_to_head(argv[1], argv[2]);
        if (argc > 1) {
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
