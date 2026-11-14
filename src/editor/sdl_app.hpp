// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// OpenEM Editor - portable front end (SDL2 + Dear ImGui with the SDL_Renderer backend), shared declarations.
// The front end is the Linux/macOS counterpart of win_main.cpp and drives the same document classes
// (PixelEditor, PaletteDoc, TeamDoc, MapDoc). Files:
//   sdl_main.cpp     entry point: options, game folder lookup, SDL/ImGui set-up, main loop, --shot
//   sdl_app.cpp      window frame: menu, toolbar, tabs, file list, status bar, commands, shortcuts, unsaved-changes guard
//   sdl_tabs.cpp     the work area and side panel of the five tabs (pictures, goal scene sheets, palettes, teams, maps)
//   sdl_dialogs.cpp  message boxes, the in-app file/folder browser and the start screen without game folder
//   sdl_image_io.cpp picture import/export (stb_image, indexed PNG writer)
// ImGui is an immediate-mode UI: every frame draws the whole window from App and the documents, so there is no
// "refresh" step as in the Win32 version; dialogs are asynchronous and run a callback when answered.
#include <SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editor/map_doc.hpp"
#include "editor/palette_doc.hpp"
#include "editor/pixel_editor.hpp"
#include "editor/team_doc.hpp"
#include "imgui.h"

namespace em::editor::sdl {

namespace fs = std::filesystem;

// Tab indices; the same numbers as the --tab option and the Win32 editor.
enum Tab : int { kTabPictures = 0, kTabSheets = 1, kTabPalettes = 2, kTabTeams = 3, kTabMaps = 4, kTabCount = 5 };

// GPU copy of a picture for drawing with ImGui (streaming ARGB8888 texture, nearest-neighbour scaling).
class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    ~Texture() { reset(); }
    // Uploads an indexed picture through `pal`; (re)creates the texture when the size changed.
    void upload(SDL_Renderer* r, const Image& img, const Palette& pal);
    // Uploads 0xAARRGGBB pixels (row by row, w * h values).
    void upload_argb(SDL_Renderer* r, const uint32_t* px, int w, int h);
    void reset();  // frees the texture
    ImTextureID id() const { return ImTextureID(reinterpret_cast<std::uintptr_t>(tex_)); }
    bool valid() const { return tex_ != nullptr; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<uint32_t> scratch_;  // conversion buffer of upload()
};

// A message box: headline, text and buttons. `done` receives the index of the pressed button, or -1 when the box
// was closed with Escape (treated like the last button, which is always the cancel button).
struct Dialog {
    std::string head, text;
    std::vector<std::string> buttons;
    std::function<void(int)> done;
};

// The in-app file and folder browser (ImGui has no native dialogs): a path field, the entries of the current folder
// and, for files, a name field. Used as a modal window and, in Folder mode, on the start screen.
struct FileBrowser {
    enum class Mode { Folder, Open, Save };
    bool active = false;      // the modal browser is shown (or about to be opened)
    bool popup_open = false;  // OpenPopup has been called
    Mode mode = Mode::Open;
    std::string title;
    fs::path dir;                     // folder being shown
    std::array<char, 1024> path{};    // edit buffer of the path field
    std::array<char, 260> name{};     // edit buffer of the file name (Open/Save)
    std::vector<std::string> exts;    // accepted extensions in lower case with dot; the first is the default (Save)
    std::function<void(const fs::path&)> done;  // called with the chosen file or folder
    struct Entry {
        std::string name;  // UTF-8
        bool folder;
    };
    std::vector<Entry> entries;  // current folder, folders first
    bool listed = false;         // entries belong to `dir`
    std::string error;           // shown under the list (e.g. folder not readable)
};

// The whole state of the editor: SDL objects, the documents, the view state of every tab and the open dialogs.
struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    float scale = 1.0f;   // UI scale (display DPI / 96 where the system does not scale itself)
    bool shot = false;    // --shot: no interaction, one frame is saved
    bool quit = false;

    fs::path data;  // game folder
    bool ready = false;  // documents exist (else the start screen asks for the folder)
    std::string start_error;  // why the last chosen folder could not be used
    std::unique_ptr<PixelEditor> ed;     // pictures and goal scene sheets
    std::unique_ptr<PixelEditor> crest;  // crest of the selected team (team tab)
    std::unique_ptr<PaletteDoc> pal;
    std::unique_ptr<MapDoc> map;
    std::unique_ptr<TeamDoc> team;
    std::vector<int> palette_choices;  // display palettes offered for pictures without own palette

    int tab = kTabPictures;
    bool list_scroll = true;  // scroll the file list to the open document on the next frame
    std::string hover_left;   // status text set by hovering (palette swatch, tile); replaces the document status

    // pictures and sheets
    int zoom = 4;
    bool grid = true;
    bool fit_pending = false;  // choose the largest fitting zoom once the canvas size is known
    ImVec2 canvas{800, 600};   // size of the picture canvas in the last frame
    int hover_x = -1, hover_y = -1, hover_color = -1, hover_cell = -1;
    Texture pic_tex;

    // palettes
    int pal_sel = 0;  // colour being edited

    // teams
    int team_sel = 0;
    std::array<char, 64> club{};                    // edit buffer of the club name (UTF-8)
    std::array<std::array<char, 64>, 25> names{};   // edit buffers of the player names
    int swap_with = -1;                             // team chosen in the swap combo box
    int crest_zoom = 5;     // in 96-dpi pixels per crest pixel, 2..16
    bool crest_auto = true;  // zoom follows the available space until the user picks one
    Texture crest_tex;

    // hall maps
    int map_tile = 0, map_zoom = 1;
    bool map_mirror = false;
    int map_hc = -1, map_hr = -1;  // hovered cell
    bool map_painting = false;     // a paint stroke runs (left button held after a paint click)
    Texture rink_tex;
    unsigned rink_rev = ~0u;  // MapDoc revision rink_tex shows
    int rink_no = -1;         // map number rink_tex shows
    Texture atlas_tex;        // all 500 tiles, kAtlasCols per row
    static constexpr int kAtlasCols = 20;

    // dialogs
    std::deque<Dialog> dialogs;  // the front one is shown
    bool dialog_open = false;    // OpenPopup was called for the front dialog
    FileBrowser browser;
    fs::path last_dir;          // folder of the last import/export
    bool show_about = false;

    float px(float v) const { return v * scale; }  // 96-dpi pixels -> UI pixels
};

// ---- sdl_app.cpp
bool is_game_folder(const fs::path& p);  // contains EM.EXE (any case) and the KB folder
// OPENEM_DATA, then the program folder and the current folder with their EM and data subfolders; empty when none.
fs::path find_game_folder();
// Creates the documents for game folder `data`. False with a message in `error` when the folder is unusable.
bool open_game_folder(App& a, const fs::path& data, std::string& error);
void draw_frame(App& a);   // one frame of the editor window (after the documents exist)
void request_quit(App& a);  // asks about unsaved changes, then sets quit
void switch_tab(App& a, int index);  // switches without asking (the caller confirmed)
// Runs `then` once the document of the current tab has no unsaved changes: asks Save / Discard / Cancel when needed
// (on the team tab first for the crest). Cancel drops `then`.
void ensure_clean(App& a, std::function<void()> then);
// Team tab: the same for unsaved crest pixels only (switching teams, changing the crest number).
void ensure_crest_clean(App& a, std::function<void()> then);
std::string utf8(const fs::path& p);         // path -> UTF-8 text
fs::path path_from_utf8(const std::string& s);  // UTF-8 text -> path

// ---- sdl_tabs.cpp
void tab_work(App& a);  // middle area of the current tab
void tab_side(App& a);  // right column of the current tab
void fit_zoom(App& a);  // picture tabs: fit the picture into the canvas (applied in the next frame)
void load_team_fields(App& a);  // team tab: edit buffers, crest and swap choice from the selected team
void load_crest(App& a);        // opens the crest picture of the selected team (closes it for European clubs)
void open_map(App& a, int number);  // hall map tab: opens a map (the caller confirmed unsaved changes)

// ---- sdl_dialogs.cpp
void ask(App& a, Dialog d);  // queues a message box
// Opens the modal browser. `start` is the first folder shown, `name` the suggested file name (Save).
void browse(App& a, FileBrowser::Mode mode, std::string title, const fs::path& start, const std::string& name,
            std::vector<std::string> exts, std::function<void(const fs::path&)> done);
void draw_dialogs(App& a);  // the front message box and the browser, if any
// Start screen without a game folder: explanation, folder browser and buttons. Calls open_game_folder on accept.
void draw_start_screen(App& a);
// Text field that upper-cases letters (including German umlauts) and accepts at most `limit` characters.
bool input_upper(const char* id, char* buf, size_t size, int limit);

}  // namespace em::editor::sdl
