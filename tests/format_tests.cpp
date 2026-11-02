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
#include <map>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "formats/tore.hpp"
#include "formats/vga.hpp"
#include "game/calendar.hpp"
#include "game/competitions.hpp"
#include "game/contracts.hpp"
#include "game/finances.hpp"
#include "game/game.hpp"
#include "game/injuries.hpp"
#include "game/match.hpp"
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
            complete += decode_vga(d).complete;
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
    CHECK(monthly_income(b, true) == b.stored_income && b.stored_income / 3 == 14715);
    CHECK(monthly_expenses(t, b, true, true) / 3 == 482566);
    HokFile m = decode_hok(read_file(ref_dir / "MONAT1.HOK"));
    ManagerBudget mb = budget_from_save(m, 0);
    CHECK(mb.stored_income == 40000 && monthly_income(mb, true) == 40125);
    CHECK(monthly_expenses(t, mb, false, false) == 1447700);
    // a 31-day month: interest accrues daily and is booked with income and expenses at the end
    ManagerBudget day = b;
    const long start = day.balance;
    for (int d = 1; d <= 31; d++) finance_day(t, day, d, 31);
    CHECK(day.accrued == 0 && day.balance == start + 40125 - 1447700 + 31 * (4020 / 32));
    // a bank loan costs 4 % a month, an overdraft 10 %
    ManagerBudget loan = b;
    loan.bank = -300000;
    loan.balance = -90000;
    CHECK(loan_interest(loan) == 12000 && monthly_income(loan, true) == 40125);
    CHECK(monthly_expenses(t, loan, true, true) == 1447700 + 12000 + 9000);
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

// Sponsors (ovl08_2893 / 2ba7): MONAT1 stores image 70 for Köln (manager block +0x173); with the
// save's stadium, squad values and table the formula lands there. Generated offers stay inside
// the decoded ranges.
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
        in.recent_results.push_back(j % 2 ? (b >> 4) & 15 : b);
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
    CHECK(lo <= stored + 2 && hi >= stored - 2);
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
}

// Season end (ovl02_0000): prize money, retirement chances and the replacement players.
static void season_end() {
    CHECK(season_prizes(true, 0, true, false, false) / 3 == 600000);
    CHECK(season_prizes(true, 1, false, false, false) / 3 == 300000);
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
        for (const HokTableRow& r : g.league(l).rows) CHECK(r.total(r.games) == 19);
    }
    CHECK(home_games >= 9 && home_games <= 11);
    CHECK(g.budget().balance != start && g.image() > 0 && g.image() <= 100);
    std::printf("day loop: 70 days, %d home games, balance %ld -> %ld DM, image %d\n", home_games, start / 3,
                g.budget().balance / 3, g.image());
}

// A whole season from START (232 days): 44 matchdays per league, a European cup winner from the final
// round of four, a German champion from the first eight after three best-of-five rounds.
static void full_season(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    Game g(read_file(em_dir / "EM.EXE"), read_file(ref_dir / "START.HOK"), 9);
    for (int i = 0; i < 232; i++) {  // day 232 (29.04.) is the last one
        g.advance_day();
        g.clear_log();
    }
    for (int l = 0; l < 3; l++)
        for (const HokTableRow& r : g.league(l).rows) CHECK(r.total(r.games) == 44);
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
    for (int i = 0; i < 232; i++) {
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
    const HokTableRow own = g.league(g.human_team() / 12).rows[size_t(g.human_team() % 12)];  // for the balance
    std::vector<std::array<int, 3>> before;  // age, skills of every player record
    for (const HokPlayer& pl : g.save().players) before.push_back({pl.age, pl.skill_a, pl.skill_b});
    std::vector<std::string> oberliga_down;  // 11th and 12th go to the regional league
    for (int s = 0; s < 12; s++)
        if (g.league(2).rows[size_t(s)].position >= 10) oberliga_down.push_back(g.save().teams[size_t(24 + s)].name);
    const int euro = g.euro_champion();  // a German European cup winner enters again (ovl05_51a5)
    const std::string euro_winner = euro >= 0 && euro < 64 ? g.save().teams[size_t(euro)].name : champion;
    g.advance_day();  // new season, day 1
    CHECK(g.seasons_completed() == 1 && g.save().year == 1994 && g.season_day() == 1);
    {  // skills move by rand(-3, 3) within 30..99 (players not replaced: one year older)
        int moved = 0, kept = 0;
        for (size_t i = 0; i < before.size() && i < g.save().players.size(); i++) {
            const HokPlayer& pl = g.save().players[i];
            if (pl.age != before[i][0] + 1) continue;
            kept++;
            const int d1 = pl.skill_a - before[i][1], d2 = pl.skill_b - before[i][2];
            CHECK(d1 >= -3 && d1 <= 3 && d2 >= -3 && d2 <= 3 && pl.skill_a >= 30 && pl.skill_b <= 99);
            moved += d1 != 0 || d2 != 0;
        }
        CHECK(kept > 150 && moved > kept * 3 / 4);
    }
    // ovl16_0480: free players (owner 6) of clubs 0-23 and the nations carry a name from their
    // team's roster in KB\23.vga; no name twice
    {
        const Database db = decode_db(read_file(em_dir / "KB" / "23.vga"));
        std::map<std::string, int> names;
        int free_players = 0, from_roster = 0;
        for (const HokPlayer& pl : g.save().players) {
            names[pl.name]++;
            const size_t rec = size_t(pl.raw - g.save().state.data() - 0x4FC6) / 0x21;
            if (pl.raw[0x1A] != 6 || pl.team == g.human_team()) continue;
            free_players++;
            const bool nation = rec >= 179;  // 178 is in both ranges; mode 1 runs last
            CHECK(nation ? pl.team >= 52 && pl.team <= 63 : pl.team >= 0 && pl.team <= 23);
            for (const DbClub& c : db.clubs)
                if (c.name == g.save().teams[size_t(pl.team)].name)
                    for (const DbPlayer& d : c.players)
                        if (d.name == pl.name) {
                            from_roster++;
                            CHECK(pl.raw[0x13] == d.copy[19]);
                        }
        }
        CHECK(free_players > 150 && from_roster == free_players);
        for (const auto& [name, n] : names) CHECK(n == 1);
        std::printf("season change: %d free players renamed from their new team's roster\n", from_roster);
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
    for (int i = 1; i < 232; i++) {
        g.advance_day();
        g.clear_log();
    }
    for (int l = 0; l < 3; l++)
        for (const HokTableRow& r : g.league(l).rows) CHECK(r.total(r.games) == 44);
    for (int t = 0; t < 36; t++) {  // new season values in the class bounds (162c:1c5b)
        if (t == g.human_team()) continue;  // follows its squad (162c:146c)
        const HokTeam& tm = g.save().teams[size_t(t)];
        const int lo = t < 12 ? 69 : t < 24 ? 39 : 15, hi = t < 12 ? 94 : t < 24 ? 73 : 43;
        for (int k = 0; k < 2; k++)
            CHECK(tm.skill1[k] >= lo && tm.skill1[k] <= hi && tm.skill2[k] >= lo && tm.skill2[k] <= hi);
    }
    std::printf("season change: %zu promoted, second season played to day %d\n", promoted.size(), g.season_day());
}

// HOK writer: decode -> store_hok -> encode_hok gives every reference save back byte for byte.
// Transfer list refresh in the day loop (ovl11_34ef): after 60 days the list holds only listed
// players (+0x1A = 4) of computer clubs, sorted by class, with value and salary from ovl11_3d36,
// and every listed player is on the list.
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
    CHECK(refreshes > 5 && refreshes < 30);  // chance 1/4 a day
    const HokFile h = decode_hok(g.save_bytes());
    const ContractTables t = load_contract_tables(exe);
    BorlandRandom rng(1);
    std::vector<int> listed;
    int last_class = 0;
    for (int slot = 0; slot < 10; slot++) {
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

// Offers to the manager (146e:0c89, 1000:3bb8, ovl11_05f1): with seed 1 a club bids for a squad
// player in the first weeks; accepting sells him at once (balance, squad, player record, save).
static void manager_offers(const fs::path& em_dir, const fs::path& ref_dir) {
    if (!fs::exists(ref_dir / "START.HOK")) return;
    const Bytes exe = read_file(em_dir / "EM.EXE");
    Game g(exe, read_file(ref_dir / "START.HOK"), 1);
    int days = 0;
    while (g.offers().empty() && days < 232) {
        g.advance_day();
        g.clear_log();
        days++;
    }
    CHECK(!g.offers().empty());
    if (g.offers().empty()) return;
    const ManagerOffer o = g.offers()[0];
    CHECK(!o.listed && o.bidder >= 0 && o.bidder < 43 && o.bidder != g.human_team() && o.amount > 0);
    const long before = g.budget().balance;
    const size_t squad = g.save().squad.size();
    const std::string name = g.save().players[size_t(o.player)].name;
    CHECK(g.accept_offer(0));
    CHECK(g.budget().balance == before + o.amount && g.save().squad.size() == squad - 1);
    CHECK(g.save().players[size_t(o.player)].team == o.bidder && g.save().players[size_t(o.player)].raw[0x1A] == 6);
    for (const HokSquadPlayer& sp : g.save().squad) CHECK(g.save().players[size_t(sp.player)].name != name);
    const HokFile again = decode_hok(g.save_bytes());
    CHECK(again.squad.size() == squad - 1);
    for (size_t i = 0; i < again.squad.size(); i++)  // slot k still holds the k-th player of the club
        CHECK(again.state[0x696D + i * 0x34 + 0x0F] == again.squad[i].player + 1);
    for (int i = 0; i < 30; i++) g.advance_day();
    std::printf("offers: day %d %s sold for %ld DM\n", days, name.c_str(), o.amount / 3);
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
    int signed_up = 0, left = 0;
    while (g.seasons_completed() < 2) {
        g.advance_day();
        for (const std::string& line : g.log()) {
            signed_up += line.find(" signs for 2 years") != std::string::npos;
            left += line.find(" leaves the club") != std::string::npos;
        }
        g.clear_log();
    }
    CHECK(asked == int(squad) && left >= 1 && signed_up + left == asked);
    CHECK(g.save().squad.size() == squad - size_t(left));
    for (size_t i = 0; i < g.save().squad.size(); i++) {
        const uint8_t* r = g.save().state.data() + 0x696D + i * 0x34;
        CHECK(r[0x0B] == 2 && r[0x0F] == g.save().squad[i].player + 1);
    }
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
    Game g(exe, read_file(ref_dir / "START.HOK"), 6);
    for (int i = 0; i < 30; i++) {
        g.advance_day();
        g.clear_log();
    }
    const Bytes saved = g.save_bytes();
    CHECK(saved.size() == read_file(ref_dir / "START.HOK").size());
    HokFile h = decode_hok(saved);
    CHECK(h.date.day_index == 31 && h.matchday[0] == g.next_matchday(0) && h.year == 1993);
    for (int t = 0; t < 36; t++) {  // table rows as EM.EXE expects them: points, copies, fever curve
        const size_t b = 0x3BA8 + size_t(t) * 0x50;
        const HokTableRow& r = h.table[size_t(t)];
        CHECK(h.state[b] == 2 * r.wins[0] + r.draws(0) && h.state[b + 1] == 2 * r.wins[1] + r.draws(1));
        CHECK(h.state[b + 2] == h.state[b] && h.state[b + 0x18] == h.state[b + 0x16] && h.state[b + 0x28] == r.wins[0]);
        CHECK(r.position_history[g.next_matchday(0) - 2] == r.position);
    }
    for (int t = 0; t < 12; t++) {  // series (state 0x9988): the current streaks follow the form string
        const std::string& form = h.table[size_t(t)].form;
        int won = 0, lost = 0;
        for (size_t i = form.size(); i-- > 0 && (form[i] == 's' || form[i] == 'S');) won++;
        for (size_t i = form.size(); i-- > 0 && (form[i] == 'n' || form[i] == 'N');) lost++;
        const size_t b = 0x9988 + size_t(t) * 0x15;
        CHECK((won < 8 ? h.state[b] == won : h.state[b] >= 8) && (lost < 8 ? h.state[b + 3] == lost : h.state[b + 3] >= 8));
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
    for (size_t i = 0; i < g.save().squad.size(); i++)
        CHECK(g.save().squad[i].skill_a == back.save().squad[i].skill_a &&
              g.save().squad[i].contract_years == back.save().squad[i].contract_years);
    back.advance_day();
    CHECK(back.season_day() == 31);
    std::printf("game save: saved on day 30, loaded and continued (balance %ld DM)\n", back.budget().balance / 3);
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
    // Augsburg (2nd division) - Preussen Berlin (first division leader): Preussen should mostly win
    int preussen = 0, shootouts = 0;
    for (int i = 0; i < 400; i++) {
        MatchResult r = knockout_quick(h.teams[14], h.teams[10], true, true, rng);
        preussen += r.away > r.home;
        shootouts += r.shootout;
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
static void scorer_choice(const fs::path& ref_dir) {
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
    const double pp_rate = st.penalties[1] ? double(st.power_play_goals[0]) / st.penalties[1] : 0;
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
    CHECK(lv.skill1 == h.teams[10].skill1[0] + 4 && lv.form == h.teams[10].form[0] + 4);
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
    try {
        unit_tests();
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
        if (argc > 2) training(argv[1], argv[2]);
        if (argc > 2) human_team_record(argv[2]);
        if (argc > 2) day_loop(argv[1], argv[2]);
        if (argc > 2) full_season(argv[1], argv[2]);
        if (argc > 2) season_change(argv[1], argv[2]);
        if (argc > 2) game_save(argv[1], argv[2]);
        if (argc > 2) transfer_list(argv[1], argv[2]);
        if (argc > 2) injuries(argv[1], argv[2]);
        if (argc > 2) manager_offers(argv[1], argv[2]);
        if (argc > 2) contract_expiry(argv[1], argv[2]);
        if (argc > 2) save_years(argv[1], argv[2]);
        if (argc > 2) club_scorers(argv[1], argv[2]);
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
