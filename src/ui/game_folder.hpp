// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Finding the player's copy of the original game at startup. OpenEM looks in fixed places first (the folder named by
// OPENEM_DATA, the folder chosen last time, the program's folder and the current folder, each also with the
// subfolders EM and data). When none holds the game, the interactive program asks with the system's "open file"
// dialog for EM.EXE (Windows, macOS, and Linux through GTK, via nativefiledialog-extended) and remembers the folder
// for the next start.
#include <filesystem>
#include <optional>
#include <string>

namespace em::ui {

// True when p holds the original game: EM.EXE (any case) plus the picture folder KB.
bool is_game_folder(const std::filesystem::path& p);

// The first folder of the fixed search order that holds the game; empty when none does. exe_dir is the program's
// folder, remembered the folder stored by remember_game_folder (may be empty).
std::filesystem::path find_game_folder(const std::filesystem::path& exe_dir, const std::filesystem::path& remembered);

// The folder remembered from the last start (empty when there is none or it cannot be read). The file lives in the
// user's settings folder (SDL_GetPrefPath).
std::filesystem::path remembered_game_folder();

// Stores folder for the next start; failures are ignored (OpenEM then simply asks again).
void remember_game_folder(const std::filesystem::path& folder);

// Shows the system's "open file" dialog for EM.EXE. Returns the chosen file, nullopt when the player cancels; throws
// std::runtime_error when no dialog can be shown (for example without a desktop session on Linux).
std::optional<std::filesystem::path> choose_game_exe(const std::string& title);

}  // namespace em::ui
