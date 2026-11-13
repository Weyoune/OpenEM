// SPDX-License-Identifier: GPL-3.0-only
// Held scroll arrows: on Statistiken -> Frueher the list keeps scrolling while the mouse button is held on an
// arrow (the original polls the button without waiting for its release) and stops at release(); a single click
// scrolls one step.
//
// Usage: classic_hold_tests DATA REFERENCE OUTPUT (REFERENCE holds M0.HOK)
#include "ui/classic_app.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;
// Throws with `message` when `ok` is false.
static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }

// Canvas of the Frueher page after `clicks` single presses on the lower arrow.
static std::vector<uint32_t> after_clicks(const fs::path& data, const fs::path& output, const fs::path& save, int clicks) {
    ClassicApplication app(data, output, save);
    app.render_screen("stats-history");
    app.motion(169, 42);
    for (int n = 0; n < clicks; n++) { app.click(169, 42, false); app.release(); app.tick(); }
    app.draw();
    return app.canvas().argb();
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("classic_hold_tests DATA REFERENCE OUTPUT");
        const fs::path data = path_from_text(argv[1]), refs = path_from_text(argv[2]);
        const fs::path output = path_from_text(argv[3]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path save = refs / "M0.HOK";
        const auto one = after_clicks(data, output, save, 1), two = after_clicks(data, output, save, 2);
        require(one != two, "each click scrolls one row");

        // Released at once: later ticks change nothing.
        {
            ClassicApplication app(data, output, save);
            app.render_screen("stats-history");
            app.motion(169, 42);
            app.click(169, 42, false); app.release();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            app.tick(); app.draw();
            require(app.canvas().argb() == one, "a released arrow does not repeat");
        }
        // Held for a while: the list keeps scrolling past the second row, then stops at the release.
        {
            ClassicApplication app(data, output, save);
            app.render_screen("stats-history");
            app.motion(169, 42);
            app.click(169, 42, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            app.tick(); app.draw();
            const auto held = app.canvas().argb();
            require(held != one && held != two, "a held arrow repeats");
            app.release();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            app.tick(); app.draw();
            require(app.canvas().argb() == held, "release stops the repeat");
        }
        std::puts("Held arrow repeat passed");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "Held arrows: %s\n", e.what()); return 1; }
}
