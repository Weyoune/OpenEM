// SPDX-License-Identifier: GPL-3.0-only
// Mouse/keyboard flows of the transfer page's loan editor and talent question (M0 reference save).
#include "ui/classic_app.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;
static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
constexpr size_t kPool = 0x81CD, kSquad = 0x696D, kRecord = 52;

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("classic_loan_scout_tests DATA REFERENCE OUTPUT");
        const fs::path data = path_from_text(argv[1]), refs = path_from_text(argv[2]);
        const fs::path output = path_from_text(argv[3]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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
