// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Scripted headless runs of the classic UI: a text script of mouse clicks, keys and text (the lines written by
// `openem --input-log` can be pasted in unchanged) plus a few commands that let a simple player click through
// whole stretches of a career. The UI clock is virtual: waiting costs no wall time and every run is deterministic
// (the session seed is fixed). The player only sends the inputs a person would send: Weiter in the main menu, a
// click on notices, reviews and cup tables, a key on the message field, Ja/Nein on questions, a right click
// to leave a page. Script commands (one per line, '#' starts a comment):
//   mouse=1 x=X y=Y | mouse=3 x=X y=Y   left/right click (input-log form), followed by the button release
//   768 key=K ...                        key press with SDL key code K (input-log form); 769 lines are ignored
//   text=T                               text input (input-log form)
//   click X Y | rclick X Y | key NAME | type TEXT     the same in short form (NAME: escape, return, backspace,
//                                        space or a single letter)
//   wait MS                              let the virtual clock run
//   play until COND                      the player clicks on until COND holds (fails when the UI stops moving)
//   main                                 the player leaves pages and dialogs until the plain main menu shows
//   tour                                 opens every item of every main-menu category once and goes back
//   expect COND                          fails unless COND holds
//   contracts yes|no|no N                the answer to the contract questions of the season change (no N: the next N)
//   known-gap TEXT                       a notice containing TEXT is a known UI gap (reported, not failed)
//   restart [FILE]                       a program restart that starts with FILE of the game folder (as --save)
//   render FILE                          writes the screen as BMP into the output folder
//   echo TEXT                            writes TEXT into the log
// COND: date>=DD.MM.YYYY | date=DD.MM.YYYY | modal=NAME | screen=NAME | notice~TEXT | news~TEXT (game log) | idle |
//       days>=N (days played by the script)
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/classic_app.hpp"

namespace em::ui {

// Outcome of a script run.
struct ClassicScriptReport {
    bool ok = true;
    std::vector<std::string> failures;     // with script line
    std::vector<std::string> gaps;         // notices of unfinished UI parts (known gaps included)
    std::vector<std::string> log;          // dated log lines
    std::map<std::string, int> modals;     // how often each dialog kind was answered
    std::map<std::string, int> screens;    // frames shown per screen
    std::map<std::string, int> notices;    // distinct notice texts
    long inputs = 0, ticks = 0;            // inputs sent and frames drawn
    int days = 0;                          // days played
};

// English name of a screen as used in `screen=NAME` conditions and logs ("?" for an unknown value).
std::string classic_screen_name(ClassicScreen screen);
// A program restart for the script command `restart [FILE]`: a new application that starts with FILE of the game
// folder (empty: the start menu of a new game), as `openem --save` would.
using ClassicRestart = std::function<std::unique_ptr<ClassicApplication>(const std::string& file)>;
// Runs `script` against `app`; `output` receives rendered pictures. `trace` (may be null) gets the log as it grows.
ClassicScriptReport run_classic_script(ClassicApplication& app, std::istream& script, const std::filesystem::path& output,
                                       std::ostream* trace = nullptr, ClassicRestart restart = {});

}  // namespace em::ui
