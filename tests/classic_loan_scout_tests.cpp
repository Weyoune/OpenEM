// SPDX-License-Identifier: GPL-3.0-only
// Mouse/keyboard flows of the transfer page's loan editor and talent question (M0 reference save).
#include "ui/classic_app.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;
static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
// Pixels that differ from a native DOSBox capture (320 x 240 RGB, work/exp/transfer_20261110), with their box.
static size_t differences(const ClassicCanvas& canvas, const fs::path& golden) {
    const auto expected = read_file(golden);
    require(expected.size() == 320 * 240 * 3, "native RGB golden size");
    const auto actual = canvas.argb();
    size_t count = 0;
    int left = 320, top = 240, right = -1, bottom = -1;
    for (size_t pixel = 0; pixel < actual.size(); pixel++) {
        const uint32_t rgb = uint32_t(expected[pixel * 3]) << 16 | uint32_t(expected[pixel * 3 + 1]) << 8 | expected[pixel * 3 + 2];
        if ((actual[pixel] & 0xffffff) == rgb) continue;
        count++;
        const int x = int(pixel % 320), y = int(pixel / 320);
        left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y);
    }
    if (count && std::getenv("EM_DUMP_DIR")) {
        std::string rgb;
        for (const uint32_t c : actual) { rgb.push_back(char(c >> 16)); rgb.push_back(char(c >> 8)); rgb.push_back(char(c)); }
        std::FILE* f = std::fopen((fs::path(std::getenv("EM_DUMP_DIR")) / golden.filename()).string().c_str(), "wb");
        if (f) { std::fwrite(rgb.data(), 1, rgb.size(), f); std::fclose(f); }
    }
    if (count) std::fprintf(stderr, "%s: %zu pixels differ in %d,%d..%d,%d\n", golden.filename().string().c_str(), count, left, top, right, bottom);
    return count;
}
constexpr size_t kPool = 0x81CD, kSquad = 0x696D, kRecord = 52;

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("classic_loan_scout_tests DATA REFERENCE GOLDENS OUTPUT");
        const fs::path data = path_from_text(argv[1]), refs = path_from_text(argv[2]);
        const fs::path goldens = path_from_text(argv[3]);
        const fs::path output = path_from_text(argv[4]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path save = refs / "M0.HOK";

        // The loan editor shares the purchase editor; only its title row differs.
        {
            ClassicApplication purchase(data, output, save), loan(data, output, save);
            purchase.motion(187, 219); loan.motion(187, 219);
            purchase.render_screen("purchase-fee"); loan.render_screen("loan-fee");
            const auto a = purchase.canvas().argb(), b = loan.canvas().argb();
            size_t title = 0;
            for (size_t n = 0; n < a.size(); n++) {
                if (a[n] == b[n]) continue;
                const int y = int(n / 320);
                require(y >= 31 && y <= 41, "loan editor differs outside the title at y=" + std::to_string(y));
                title++;
            }
            require(title > 0, "loan editor title says 'leihen'");
        }

        // Native DOSBox captures from M0 (EM.EXE V1.00): the talent question after a click on "Talent", the loan
        // editor after a click on Frosch's "(L)" price and the sponsor question of a 130.000 DM loan of Frosch
        // (57.200 DM offer = 171600 raw), drawn over the page as it was before the loan. The software cursor
        // stays where the click was.
        {
            ClassicApplication app(data, output, save);
            app.motion(290, 231); app.render_screen("talent-question");
            require(differences(app.canvas(), goldens / "talent-question.rgb") == 0, "talent question matches DOSBox");
        }
        {
            ClassicApplication app(data, output, save);
            // DOSBox moved the pointer from "Lieber nicht." (193,172) to (260,91) through pool rows 7..4.
            app.render_screen("transfer");
            for (const auto& [x, y] : {std::pair{230, 155}, {240, 140}, {245, 125}, {250, 110}, {255, 100}, {260, 91}}) app.motion(x, y);
            app.render_screen("loan-fee");
            require(differences(app.canvas(), goldens / "loan-fee.rgb") == 0, "loan editor matches DOSBox");
        }
        {
            ClassicApplication app(data, output, save);
            app.render_screen("transfer");
            for (const auto& [x, y] : {std::pair{230, 155}, {240, 140}, {245, 125}, {250, 110}, {255, 100}, {260, 91}}) app.motion(x, y);
            app.render_screen("subsidy-question:171600");
            require(differences(app.canvas(), goldens / "sponsor-question.rgb") == 0, "sponsor question matches DOSBox");
        }

        // Sponsor question: "Lieber nicht." changes nothing, "Aber immer!" books the offer.
        {
            ClassicApplication app(data, output, save);
            const auto before = app.session().game().checkpoint_bytes();
            const long balance = app.session().game().budget().balance;
            app.render_screen("subsidy-question:171600");
            app.click(190, 172, false);
            require(app.session().game().checkpoint_bytes() == before, "declined offer is neutral");
            app.render_screen("subsidy-question:171600");
            app.click(120, 172, false);
            require(app.session().game().budget().balance == balance + 171600, "accepted offer booked");
        }

        // Click on the "(L)" price of pool row 3, type a bid, Enter: the player joins on loan.
        {
            ClassicApplication app(data, output, save);
            app.render_screen("transfer");
            const HokFile& h = app.session().game().save();
            const size_t pool = kPool + 3 * kRecord;
            const int id = h.state.at(pool + 15);
            require(id > 0 && h.state.at(0x4FC6 + size_t(id - 1) * 33 + 0x1A) == 4, "computer player at pool row 3");
            const long value = long(le32(h.state, pool + 0x2C) & 0x7FFFFFFFu);
            const long bid_dm = value * 2 / 9 + 1;  // twice the loan price (value / 3 raw = value / 9 DM)
            const long balance = app.session().game().budget().balance;
            require(balance >= bid_dm * 3, "M0 can afford the loan");
            const auto before = app.session().game().checkpoint_bytes();
            app.click(260, 92, false);
            require(app.session().game().checkpoint_bytes() == before, "opening the editor changes nothing");
            app.text_input(std::to_string(bid_dm));
            app.key(SDLK_RETURN);
            const HokFile& after = app.session().game().save();
            int row = -1;
            for (int s = 0; s < 30; s++) if (after.state.at(kSquad + size_t(s) * kRecord + 15) == id) row = s;
            require(row >= 0, "loaned player in the squad");
            require(after.state.at(kSquad + size_t(row) * kRecord + 0x0C) >= 0x80, "loan marker set");
            require(app.session().game().budget().balance == balance - bid_dm * 3, "bid paid");
            // A click on his own row now reports the loan instead of listing him.
            const auto loaned = app.session().game().checkpoint_bytes();
            app.click(40, 29 + 6 * row + 2, false);
            require(app.session().game().checkpoint_bytes() == loaned, "loaned player is not listed");
        }

        // Talent button: "Lieber nicht." is neutral, "Aber immer!" pays and starts the scout.
        {
            ClassicApplication app(data, output, save);
            app.render_screen("transfer");
            require(app.session().game().talent_search_days() == 0, "M0 has no running search");
            const auto before = app.session().game().checkpoint_bytes();
            app.click(290, 232, false);
            app.click(190, 172, false);
            require(app.session().game().checkpoint_bytes() == before, "declined question is neutral");
            const long balance = app.session().game().budget().balance, price = app.session().game().talent_search_price();
            app.click(290, 232, false);
            app.click(120, 172, false);
            require(app.session().game().talent_search_days() >= 3, "search running");
            require(app.session().game().budget().balance == balance - price, "price paid");
            const auto running = app.session().game().checkpoint_bytes();
            app.click(290, 232, false);  // the button is gone while a search runs: no question opens
            app.click(190, 172, false);  // would be "Lieber nicht."; here an empty roster area
            require(app.session().game().checkpoint_bytes() == running, "no second search");
        }
        std::puts("Loan editor and talent question flows passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "Loan/scout UI: %s\n", e.what()); return 1; }
}
