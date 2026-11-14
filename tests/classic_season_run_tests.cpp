// SPDX-License-Identifier: GPL-3.0-only
// Acceptance run of version 1.0: a new game played through the classic UI, headless, from a script
// (tests/scripts/classic_season_run.txt, ui/classic_script.hpp): a whole season with cup, play-offs and the
// season change, saving and loading in between, into the next season. Prints the run's log and the list of UI gaps.
//
// Usage: classic_season_run_tests DATA SCRIPT OUTPUT [SAVE] — without SAVE the run starts at the start menu of a new
// game, with SAVE from that savegame. EM_SEASON_RUN_TRACE set prints every step while it runs.
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "ui/classic_app.hpp"
#include "ui/classic_script.hpp"

namespace fs = std::filesystem;
using namespace em::ui;

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5) throw std::runtime_error("classic_season_run_tests DATA SCRIPT OUTPUT [SAVE]");
        const fs::path data = path_from_text(argv[1]), script_path = path_from_text(argv[2]);
        const fs::path output = fs::absolute(path_from_text(argv[3])) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(output);
        std::ifstream script(script_path);
        if (!script) throw std::runtime_error("Skript fehlt: " + path_text(script_path));
        const auto started = std::chrono::steady_clock::now();
        ClassicApplication app(data, output, argc == 5 ? path_from_text(argv[4]) : fs::path());  // no save: the start menu
        const bool verbose = std::getenv("EM_SEASON_RUN_TRACE") != nullptr;
        const ClassicRestart restart = [&](const std::string& file) {
            return std::make_unique<ClassicApplication>(data, output, file.empty() ? fs::path() : data / path_from_text(file));
        };
        const auto report = run_classic_script(app, script, output, verbose ? &std::cout : nullptr, restart);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (!verbose) for (const auto& line : report.log) std::cout << line << '\n';
        std::cout << "\nTage: " << report.days << ", Eingaben: " << report.inputs << ", Takte: " << report.ticks << ", " << seconds << " s\n";
        std::cout << "Dialoge:";
        for (const auto& [name, count] : report.modals) std::cout << ' ' << name << '=' << count;
        std::cout << "\nBildschirme:";
        for (const auto& [name, count] : report.screens) std::cout << ' ' << name;
        std::cout << "\nLücken der Oberfläche (" << report.gaps.size() << "):\n";
        for (const auto& gap : report.gaps) std::cout << "  " << gap << '\n';
        for (const auto& failure : report.failures) std::cerr << "FAIL: " << failure << '\n';
        return report.ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
