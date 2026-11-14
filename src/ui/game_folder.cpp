// SPDX-License-Identifier: GPL-3.0-only
// Startup search for the original game and the native "open file" dialog (see game_folder.hpp).
#include "ui/game_folder.hpp"

#include <SDL.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <nfd.hpp>

#include "ui/session.hpp"

namespace em::ui {
namespace fs = std::filesystem;

namespace {
// The text file in the user's settings folder that holds the chosen game folder (UTF-8, one line).
fs::path remembered_file() {
    char* pref = SDL_GetPrefPath("OpenEM", "OpenEM");
    if (!pref) return {};
    const fs::path file = path_from_text(pref) / "game_folder.txt";
    SDL_free(pref);
    return file;
}
}  // namespace

bool is_game_folder(const fs::path& p) {
    std::error_code ec;
    // any spelling: DOS copies come as EM.EXE, EM.exe, kb/, ... (resolve_case)
    return !p.empty() && fs::is_regular_file(resolve_case(p / "EM.EXE"), ec) && fs::is_directory(resolve_case(p / "KB"), ec);
}

fs::path find_game_folder(const fs::path& exe_dir, const fs::path& remembered) {
    std::vector<fs::path> candidates;
    if (const char* env = std::getenv("OPENEM_DATA")) candidates.push_back(path_from_text(env));
    if (!remembered.empty()) candidates.push_back(remembered);
    for (const fs::path& base : {exe_dir, fs::current_path()})
        for (const char* sub : {"", "EM", "data"}) candidates.push_back(*sub ? base / sub : base);
    for (const auto& c : candidates)
        if (is_game_folder(c)) return c;
    return {};
}

fs::path remembered_game_folder() {
    const fs::path file = remembered_file();
    if (file.empty()) return {};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text.empty() ? fs::path() : path_from_text(text);
}

void remember_game_folder(const fs::path& folder) {
    const fs::path file = remembered_file();
    if (file.empty()) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out << path_text(fs::absolute(folder)) << '\n';
}

std::optional<fs::path> choose_game_exe(const std::string& title) {
    (void)title;  // nativefiledialog-extended 1.2 has no title parameter; the window title is the system's
    NFD::Guard guard;
    NFD::UniquePathU8 path;
    // Linux filters are case sensitive, so both spellings of the extension are listed.
    const nfdu8filteritem_t filters[] = {{"Eishockey Manager (EM.EXE)", "exe,EXE,Exe"}};
    const nfdresult_t result = NFD::OpenDialog(path, filters, 1);
    if (result == NFD_CANCEL) return std::nullopt;
    if (result != NFD_OKAY) throw std::runtime_error(std::string("Kein Dateidialog verfügbar: ") + NFD::GetError());
    return path_from_text(path.get());
}

}  // namespace em::ui
