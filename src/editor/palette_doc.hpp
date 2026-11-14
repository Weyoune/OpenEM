// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the palette editor for KB\21.vga (game palette) and KB\247.vga (goal scene palette).
// 255 colours of three bytes; the VGA DAC uses the low six bits, so the editor edits 0..63 per channel and keeps
// the unused high bits of the file untouched.
#include <array>
#include <filesystem>
#include <string>

#include "formats/vga.hpp"

namespace em::editor {

// One open palette file, held as its raw bytes so that everything the editor does not touch is written back
// unchanged.
class PaletteDoc {
public:
    static constexpr int kColors = 255;  // colour 255 is not stored
    // `data` is the game directory; the files are read from its KB subdirectory.
    explicit PaletteDoc(const std::filesystem::path& data) : kb_(resolve_case(data / "KB")) {}

    // Loads KB\<number>.vga (21 or 247). Fails (false, message in status()) when the file is missing or not a
    // palette file.
    bool load(int number);
    void close();           // forgets the open palette (current() becomes -1)
    bool save();            // writes KB\<n>.vga, first time with a .bak copy
    void revert();          // reload from disk

    int current() const { return current_; }   // number of the open file, -1 when none
    bool dirty() const { return dirty_; }      // unsaved changes exist
    std::array<int, 3> get(int index) const;  // six-bit R, G, B
    // Sets colour `index` to six-bit R, G, B (each clamped to 0..63); the two high bits of every byte are kept.
    void set(int index, int r, int g, int b);
    Palette palette() const;                  // expanded to eight bits (as the game shows it)
    unsigned revision() const { return revision_; }        // bumped on every change
    const std::string& status() const { return status_; }  // last status or error message (German, shown in the UI)

private:
    std::filesystem::path kb_;  // <game>/KB
    int current_ = -1;
    Bytes raw_;                 // the whole file, 3 bytes per colour
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;
};

}  // namespace em::editor
