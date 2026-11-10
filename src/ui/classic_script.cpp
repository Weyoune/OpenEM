// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_script.hpp"

#include <SDL.h>

#include <algorithm>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace em::ui {
namespace {

uint64_t script_now = 1000;
uint64_t script_clock() { return script_now; }

struct ClockScope {
    ClockScope() { script_now = 1000; set_classic_clock(script_clock); }
    ~ClockScope() { set_classic_clock(nullptr); }
};

struct ScriptError : std::runtime_error { using std::runtime_error::runtime_error; };

std::string one_line(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

bool is_gap_notice(const std::string& text) {
    for (const char* pattern : {"noch nicht", "nicht unterst", "nicht nachgebaut", "Kein weiterer eigener Spieltermin"})
        if (text.find(pattern) != std::string::npos) return true;
    return false;
}

class Runner {
public:
    Runner(ClassicApplication& app, const fs::path& output, std::ostream* trace, ClassicRestart restart)
        : app_(&app), output_(output), trace_(trace), restart_(std::move(restart)) {}
    ClassicApplication& app() { return *app_; }

    ClassicScriptReport run(std::istream& script) {
        ClockScope clock;
        std::string line;
        try {
            settle_frame();
            while (std::getline(script, line)) {
                line_++;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto first = line.find_first_not_of(" \t");
                if (first == std::string::npos || line[first] == '#') continue;
                command(line.substr(first));
                if (app().finished()) { log("Das Programm wurde beendet."); break; }
            }
        } catch (const ScriptError& e) {
            fail(e.what());
        } catch (const std::exception& e) {
            fail(std::string("Absturz: ") + e.what() + " (" + describe() + ")");
        }
        for (const auto& gap : unknown_gaps_) fail("Unbekannte Lücke: " + gap);
        report_.ok = report_.failures.empty();
        return std::move(report_);
    }

private:
    // --- reading the screen --------------------------------------------------------------------------------
    std::string describe() {
        const auto p = app().probe();
        std::string s = classic_screen_name(app().screen());
        if (!p.modal.empty()) s += " / " + p.modal;
        if (p.category >= 0) s += " / Kategorie " + std::to_string(p.category);
        if (!p.text.empty()) s += " / \"" + one_line(p.text) + "\"";
        return s;
    }
    std::string stamp() {
        if (!app().session().loaded()) return "[--.--.----] ";
        return "[" + date_text(app().session().game().date()) + "] ";
    }
    void log(const std::string& text) {
        report_.log.push_back(stamp() + text);
        if (trace_) *trace_ << report_.log.back() << '\n' << std::flush;
    }
    void fail(const std::string& text) {
        report_.failures.push_back("Zeile " + std::to_string(line_) + ": " + text);
        log("FEHLER: " + text);
    }
    long day_key() {
        if (!app().session().loaded()) return -1;
        return long(app().session().game().save().year) * 1000 + app().session().game().season_day();
    }
    std::string progress_key() {
        const auto p = app().probe();
        return std::to_string(day_key()) + "|" + classic_screen_name(app().screen()) + "|" + p.modal + "|" + p.text + "|" +
               std::to_string(p.category) + "|" + std::to_string(p.deadline.value_or(0));
    }

    // --- driving the UI ------------------------------------------------------------------------------------
    void frame(uint64_t until) {
        script_now = std::max(script_now + 1, until);
        app().tick();
        app().draw();
        report_.ticks++;
        const long key = day_key();
        if (key != last_day_) {
            if (last_day_ >= 0 && key >= 0) report_.days++;
            if (last_day_ >= 0 && key / 1000 != last_day_ / 1000) log("Neues Spieljahr " + std::to_string(key / 1000));
            last_day_ = key;
        }
        observe();
    }
    void settle_frame() { frame(script_now + 16); }
    // Notices are collected as they appear, also when the script answers them itself.
    void observe() {
        report_.screens[classic_screen_name(app().screen())]++;  // frames per screen
        const auto p = app().probe();
        const std::string key = p.modal + "|" + p.text;
        if (key == last_seen_) return;
        last_seen_ = key;
        if ((p.modal == "notice" || p.modal == "timed-notice") && !p.text.empty()) {
            const std::string text = one_line(p.text);
            report_.notices[text]++;
            log("Meldung: " + text);
            if (is_gap_notice(text)) {
                const bool known = std::any_of(known_gaps_.begin(), known_gaps_.end(),
                                               [&](const std::string& k) { return text.find(k) != std::string::npos; });
                const std::string entry = text + " (" + classic_screen_name(app().screen()) + ")";
                if (std::find(report_.gaps.begin(), report_.gaps.end(), entry) == report_.gaps.end()) {
                    report_.gaps.push_back(entry);
                    if (!known) unknown_gaps_.push_back(entry);
                }
            }
        }
    }
    void click(int x, int y, bool right) {
        app().motion(x, y);
        app().click(x, y, right);
        app().release();
        report_.inputs++;
        settle_frame();
    }
    void key(int k) {
        app().key(k);
        report_.inputs++;
        settle_frame();
    }
    void type(const std::string& text) {
        app().text_input(text);
        report_.inputs++;
        settle_frame();
    }

    // One input or one wait of the player for whatever the screen shows. `weiter`: may start the next day.
    // Returns false when the plain main menu shows and `weiter` is off.
    bool respond(bool weiter) {
        const auto p = app().probe();
        const auto& m = p.modal;
        if (m == "match" || m == "banner" || m == "timed-notice") { frame(p.deadline.value_or(script_now + 16)); return true; }
        if (m == "advancing") { settle_frame(); return true; }
        if (!m.empty()) report_.modals[m]++;
        if (!m.empty() && m != "cup-table" && m != "notice" && m != "contract-question") log("Dialog " + m);
        if (m == "cup-table" || m == "review-overview" || m == "review") click(160, 120, false);
        else if (m == "notice") click(120, 171, false);
        else if (m == "messages") key(SDLK_SPACE);
        else if (m == "sale-offer") click(40, 89, false);  // keep the player
        else if (m == "talent-question" || m == "advert-question" || m == "stadium-question" || m == "quit-question")
            click(190, 171, false);
        else if (m == "contract-question") {
            const bool yes = contract_count_ >= contracts_no_;
            contract_count_++;
            log(std::string("Vertragsfrage ") + p.text + ": " + (yes ? "Mach' ich!" : "Abbruch"));
            click(yes ? 38 : 118, (p.slot < 12 ? 80 : 0) + 86, false);  // ovl11_41db sits low for the first twelve
        } else if (m == "player-detail") click(196, 214, false);
        else if (m == "club-info" || m == "team-schedule") click(160, 120, true);
        else if (m == "file-preview") { app().release(); settle_frame(); }
        else if (m == "fee-editor" || m == "contract-editor") key(SDLK_ESCAPE);
        else if (!m.empty()) throw ScriptError("Unbekannter Dialog " + m);
        else if (app().screen() == ClassicScreen::NewManager) click(280, 213, false);  // "Los jetzt!" leaves
        else if (app().screen() != ClassicScreen::Main) {
            // leave the page: a right click, every second try Escape
            if (back_tries_++ % 2 == 0) click(316, 4, true);  // outside lists and buttons
            else key(SDLK_ESCAPE);
            return true;
        } else if (p.category >= 0) click(160, 20, true);
        else if (!weiter) return false;
        else { report_.modals["weiter"]++; click(293, 52, false); }
        if (app().screen() == ClassicScreen::Main) back_tries_ = 0;
        return true;
    }

    // The player goes on until `done` holds; the UI must keep moving.
    template <class Done> void play(Done done, bool weiter, const std::string& what, long limit = 3000000) {
        std::string last = progress_key();
        int still = 0;
        for (long n = 0; !done(); n++) {
            if (n > limit) throw ScriptError("Kein Ende erreicht: " + what + " (" + describe() + ")");
            if (!respond(weiter)) throw ScriptError("Hauptmenü erreicht, aber nicht " + what);
            const std::string now = progress_key();
            if (now == last) {
                if (++still > 40) throw ScriptError("Die Oberfläche bewegt sich nicht mehr (" + describe() + ")");
            } else {
                still = 0;
                last = now;
            }
        }
    }
    bool idle() {
        const auto p = app().probe();
        return app().screen() == ClassicScreen::Main && p.modal.empty() && p.category < 0;
    }
    void to_main() { play([&] { return idle(); }, false, "das Hauptmenü"); }

    void tour() {
        constexpr int counts[] = {6, 5, 6, 6, 6, 4, 3};
        for (int category = 0; category < 7; category++)
            for (int item = 0; item < counts[category]; item++) {
                to_main();
                click(43 + 77 * (category % 4), 108 + 60 * (category / 4), false);
                click(26 + 53 * item, 220, false);
                for (int n = 0; n < 4; n++) settle_frame();
                log("Rundgang " + std::to_string(category) + "/" + std::to_string(item) + ": " + describe());
            }
        to_main();
    }

    // --- conditions ----------------------------------------------------------------------------------------
    bool condition(const std::string& c) {
        const auto p = app().probe();
        if (c == "idle") return idle();
        if (c.rfind("modal=", 0) == 0) return p.modal == c.substr(6);
        if (c.rfind("screen=", 0) == 0) return classic_screen_name(app().screen()) == c.substr(7);
        if (c.rfind("notice~", 0) == 0) return p.text.find(c.substr(7)) != std::string::npos;
        if (c.rfind("days>=", 0) == 0) return report_.days >= std::stoi(c.substr(6));
        if (c.rfind("news~", 0) == 0) {  // the game log of the session (Session::news)
            const auto& news = app().session().news();
            return std::any_of(news.begin(), news.end(), [&](const News& n) { return n.text.find(c.substr(5)) != std::string::npos; });
        }
        if (c.rfind("date>=", 0) == 0 || c.rfind("date=", 0) == 0) {
            const bool exact = c[4] == '=';
            int d = 0, m = 0, y = 0;
            if (std::sscanf(c.c_str() + (exact ? 5 : 6), "%d.%d.%d", &d, &m, &y) != 3) throw ScriptError("Datum: " + c);
            if (!app().session().loaded()) return false;
            const GameDate g = app().session().game().date();
            return exact ? std::tie(g.year, g.month, g.day) == std::tie(y, m, d) : std::tie(g.year, g.month, g.day) >= std::tie(y, m, d);
        }
        throw ScriptError("Unbekannte Bedingung: " + c);
    }

    // --- commands ------------------------------------------------------------------------------------------
    static int key_code(const std::string& name) {
        if (name == "escape") return SDLK_ESCAPE;
        if (name == "return") return SDLK_RETURN;
        if (name == "backspace") return SDLK_BACKSPACE;
        if (name == "space") return SDLK_SPACE;
        if (name.size() == 1) return std::tolower(static_cast<unsigned char>(name[0]));
        throw ScriptError("Unbekannte Taste: " + name);
    }
    static int field(const std::string& line, const std::string& name) {
        const auto at = line.find(name + "=");
        if (at == std::string::npos) throw ScriptError("Feld " + name + " fehlt: " + line);
        return std::stoi(line.substr(at + name.size() + 1));
    }
    void command(const std::string& line) {
        std::istringstream in(line);
        std::string word;
        in >> word;
        std::string rest;
        std::getline(in, rest);
        if (const auto f = rest.find_first_not_of(' '); f != std::string::npos) rest.erase(0, f); else rest.clear();
        if (line.rfind("mouse=", 0) == 0) { click(field(line, "x"), field(line, "y"), field(line, "mouse") == 3); return; }
        if (line.rfind("text=", 0) == 0) { type(line.substr(5)); return; }
        if (line.rfind("768 ", 0) == 0) { key(field(line, "key")); return; }
        if (line.rfind("769 ", 0) == 0 || line.rfind("windows=", 0) == 0) return;
        if (word == "click" || word == "rclick") {
            int x = 0, y = 0;
            std::istringstream(rest) >> x >> y;
            click(x, y, word == "rclick");
        } else if (word == "key") key(key_code(rest));
        else if (word == "type") type(rest);
        else if (word == "wait") {
            const uint64_t until = script_now + uint64_t(std::stoll(rest));
            while (script_now < until) frame(std::min(until, script_now + 16));
        } else if (word == "play") {
            if (rest.rfind("until ", 0) != 0) throw ScriptError("play until COND");
            const std::string c = rest.substr(6);
            log("Spiele bis " + c);
            play([&] { return condition(c); }, true, c);
            log("Erreicht: " + c + " (" + describe() + ")");
        } else if (word == "main") to_main();
        else if (word == "tour") tour();
        else if (word == "expect") {
            if (!condition(rest)) fail("Erwartet " + rest + ", aber " + describe());
        } else if (word == "contracts") contracts_no_ = rest == "yes" ? 0 : rest == "no" ? 1000000 : rest.rfind("no ", 0) == 0 ? contract_count_ + std::stoi(rest.substr(3)) : throw ScriptError("contracts yes|no|no N");
        else if (word == "known-gap") known_gaps_.push_back(rest);
        else if (word == "render") {
            const auto bytes = app().canvas().bmp_bytes();
            std::ofstream(output_ / rest, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        } else if (word == "restart") {
            if (!restart_) throw ScriptError("restart ist hier nicht möglich");
            owned_ = restart_(rest);
            app_ = owned_.get();
            last_seen_.clear();
            last_day_ = -1;
            settle_frame();
            log("Neustart" + (rest.empty() ? std::string() : " mit " + rest) + " (" + describe() + ")");
        } else if (word == "echo") log(rest + " (" + describe() + ")");
        else throw ScriptError("Unbekannter Befehl: " + line);
    }

    ClassicApplication* app_;
    std::unique_ptr<ClassicApplication> owned_;  // after a restart
    ClassicRestart restart_;
    fs::path output_;
    std::ostream* trace_;
    ClassicScriptReport report_;
    std::vector<std::string> known_gaps_, unknown_gaps_;
    std::string last_seen_;
    long last_day_ = -1;
    int line_ = 0, contracts_no_ = 0, contract_count_ = 0, back_tries_ = 0;
};

}  // namespace

std::string classic_screen_name(ClassicScreen screen) {
    static const char* const names[] = {"Main", "Files", "Table", "Totals", "Bank", "Contracts", "Training", "Transfer",
                                        "Lineup", "Tactics", "Strengths", "Cup", "EuroCup", "WorldChampionship",
                                        "LeagueFixtures", "LeagueCompetition", "Advertising", "Stadium", "Youth",
                                        "Settings", "Statistics", "Highscore", "NewManager"};
    const size_t index = size_t(screen);
    return index < std::size(names) ? names[index] : "?";
}

ClassicScriptReport run_classic_script(ClassicApplication& app, std::istream& script, const fs::path& output, std::ostream* trace,
                                       ClassicRestart restart) {
    return Runner(app, output, trace, std::move(restart)).run(script);
}

}  // namespace em::ui
