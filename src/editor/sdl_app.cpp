// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor, portable front end: the window frame around the tabs - menu bar, toolbar, tab strip, file list,
// status bar - plus the commands behind them (save, undo/redo, tools, zoom, import/export), the keyboard shortcuts and
// the "unsaved changes" guard. The tab contents are in sdl_tabs.cpp. Behaviour follows win_main.cpp.
#include <algorithm>
#include <cctype>
#include <system_error>

#include "editor/sdl_app.hpp"
#include "editor/sdl_image_io.hpp"

namespace em::editor::sdl {

namespace {

using Tool = PixelEditor::Tool;

bool pal_tab(const App& a) { return a.tab == kTabPalettes; }
bool team_tab(const App& a) { return a.tab == kTabTeams; }
bool map_tab(const App& a) { return a.tab == kTabMaps; }
bool picture_tab(const App& a) { return a.tab == kTabPictures || a.tab == kTabSheets; }

// KB number of the document of the current tab (23 on the team tab), -1 when none is open.
int current_number(const App& a) {
    return map_tab(a) ? a.map->current() : team_tab(a) ? 23 : pal_tab(a) ? a.pal->current() : a.ed->current();
}

// Unsaved changes in the document of the current tab (team tab: teams or crest).
bool current_dirty(const App& a) {
    return map_tab(a) ? a.map->dirty() : team_tab(a) ? a.team->dirty() || a.crest->dirty() : pal_tab(a) ? a.pal->dirty() : a.ed->dirty();
}

// Undo on the palette and team tabs discards all changes, so it is offered while there are changes.
bool can_undo(const App& a) { return map_tab(a) ? a.map->can_undo() : picture_tab(a) ? a.ed->can_undo() : current_dirty(a); }
bool can_redo(const App& a) { return map_tab(a) ? a.map->can_redo() : picture_tab(a) && a.ed->can_redo(); }
// Hall maps use pencil (paint), fill and picker; the palette and team tabs have no tools.
bool tool_enabled(const App& a, Tool t) {
    return picture_tab(a) || (map_tab(a) && (t == Tool::Pencil || t == Tool::Fill || t == Tool::Picker));
}
bool can_save(const App& a) { return current_number(a) >= 0 && current_dirty(a); }
bool can_convert(const App& a) { return picture_tab(a) && a.ed->current() >= 0; }  // picture import/export

const char* status_text(const App& a) {  // left part of the status bar
    if (!a.hover_left.empty()) return a.hover_left.c_str();
    return (map_tab(a) ? a.map->status() : team_tab(a) ? a.team->status() : pal_tab(a) ? a.pal->status() : a.ed->status()).c_str();
}

// Saves KB\23.vga (and the crest if changed); warns about duplicate names first, because the game treats equal
// names as the same person. Runs `then` after a successful save.
void team_save(App& a, std::function<void()> then) {
    if (a.crest->dirty() && !a.crest->save()) return;
    if (!a.team->dirty()) {
        if (then) then();
        return;
    }
    const auto dups = a.team->duplicate_names();
    auto save = [&a, then] {
        if (a.team->save() && then) then();
    };
    if (dups.empty()) {
        save();
        return;
    }
    std::string list;
    for (size_t i = 0; i < dups.size() && i < 8; i++) list += (i ? ", " : "") + dups[i];
    if (dups.size() > 8) list += ", …";
    ask(a, {"Doppelte Namen", "Diese Namen kommen mehrfach vor: " + list + ".\nDas Spiel behandelt gleiche Namen als dieselbe Person. Trotzdem speichern?",
            {"Speichern", "Abbrechen"}, [save](int b) {
                if (b == 0) save();
            }});
}

// Saves the document of the current tab; `then` runs after success.
void save_current(App& a, std::function<void()> then) {
    if (team_tab(a)) {
        team_save(a, std::move(then));
        return;
    }
    const bool ok = map_tab(a) ? a.map->save() : pal_tab(a) ? a.pal->save() : a.ed->save();
    if (ok && then) then();
}

// Throws away the unsaved changes of the current tab by reloading the document from disk.
void discard_current(App& a) {
    if (map_tab(a)) a.map->load(a.map->current());
    else if (team_tab(a)) {
        a.team->revert();
        load_team_fields(a);
    } else if (pal_tab(a)) a.pal->revert();
    else if (a.ed->current() >= 0) a.ed->load(a.ed->current());
}

// Opens list entry `number` (KB number, or team index on the team tab) after asking about unsaved changes.
void open_item(App& a, int number) {
    if (team_tab(a)) {  // team edits apply live; only the crest pixels need a decision
        if (number == a.team_sel) return;
        ensure_crest_clean(a, [&a, number] {
            a.team_sel = number;
            load_team_fields(a);
        });
        return;
    }
    if (number == current_number(a)) return;
    ensure_clean(a, [&a, number] {
        if (map_tab(a)) open_map(a, number);
        else if (pal_tab(a)) {
            if (a.pal->load(number)) a.pal_sel = std::min(a.pal_sel, PaletteDoc::kColors);
        } else if (a.ed->load(number)) fit_zoom(a);
        a.list_scroll = true;
    });
}

void set_zoom(App& a, int z) {
    if (map_tab(a)) a.map_zoom = std::clamp(z, 1, 4);
    else a.zoom = std::clamp(z, 1, 32);
}

// The commands of menu, toolbar and shortcuts. Save, undo and redo act on the document of the current tab; on the
// palette and team tabs undo discards all unsaved changes. Import and export work on the picture tabs only.
enum class Cmd { Save, Import, Export, Exit, Undo, Redo, Grid, Fit, ZoomIn, ZoomOut, About };

void do_export(App& a) {
    PixelEditor& e = *a.ed;
    const int number = e.current();
    browse(a, FileBrowser::Mode::Save, "Bild exportieren", a.last_dir, "kb" + std::to_string(number) + ".png", {".png", ".bmp"},
           [&a](const fs::path& p) {
               a.last_dir = p.parent_path();
               PixelEditor& e = *a.ed;
               std::string ext = utf8(p.extension());
               for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
               if (ext == ".bmp") e.export_bmp(p);
               else e.set_status(export_png(p, e.image(), e.palette()) ? "Exportiert: " + utf8(p.filename()) : "Export fehlgeschlagen");
           });
}

void do_import(App& a) {
    browse(a, FileBrowser::Mode::Open, "Bild importieren", a.last_dir, "", {".png", ".bmp", ".gif", ".jpg", ".jpeg"}, [&a](const fs::path& p) {
        a.last_dir = p.parent_path();
        PixelEditor& e = *a.ed;
        auto px = std::make_shared<std::vector<uint32_t>>();
        int w = 0, h = 0;
        if (e.current() < 0) return;
        if (!load_argb(p, *px, w, h)) {
            e.set_status("Import: Datei konnte nicht gelesen werden");
            return;
        }
        if ((w != e.image().width || h != e.image().height) && e.picture_kind() == PixelEditor::Kind::Image) {
            const std::string head = "Die Datei ist " + std::to_string(w) + " × " + std::to_string(h) + ", das Bild " + std::to_string(e.image().width) +
                                     " × " + std::to_string(e.image().height) + ".";
            ask(a, {head, "Das Spiel erwartet für viele Bilder feste Größen. Rückgängig ist danach für dieses Bild nicht mehr möglich.",
                    {"Größe des Bildes ändern", "Abbrechen"}, [&a, px, w, h](int b) {
                        if (b == 0 && a.ed->import_argb(*px, w, h, true)) fit_zoom(a);
                    }});
            return;
        }
        if (e.import_argb(*px, w, h, false)) fit_zoom(a);
    });
}

void command(App& a, Cmd c) {
    PixelEditor& e = *a.ed;
    switch (c) {
        case Cmd::Save:
            if (can_save(a)) save_current(a, nullptr);
            break;
        case Cmd::Export:
            if (can_convert(a)) do_export(a);
            break;
        case Cmd::Import:
            if (can_convert(a)) do_import(a);
            break;
        case Cmd::Exit: request_quit(a); break;
        case Cmd::Undo:
            if (map_tab(a)) a.map->undo();
            else if (team_tab(a)) {  // teams: discard all changes
                a.team->revert();
                load_team_fields(a);
            } else if (pal_tab(a)) a.pal->revert();  // palettes: discard all changes
            else e.undo();
            break;
        case Cmd::Redo:
            if (map_tab(a)) a.map->redo();
            else if (picture_tab(a)) e.redo();
            break;
        case Cmd::Grid:
            if (picture_tab(a) || map_tab(a)) a.grid = !a.grid;
            break;
        case Cmd::Fit:
            if (picture_tab(a)) fit_zoom(a);
            break;
        case Cmd::ZoomIn: set_zoom(a, (map_tab(a) ? a.map_zoom : a.zoom) + 1); break;
        case Cmd::ZoomOut: set_zoom(a, (map_tab(a) ? a.map_zoom : a.zoom) - 1); break;
        case Cmd::About: a.show_about = true; break;
    }
}

const char* const kToolNames[] = {"Stift", "Linie", "Rechteck", "Gefülltes Rechteck", "Füllen", "Pipette"};
const char* const kToolShort[] = {"Stift", "Linie", "Rechteck", "Gefüllt", "Füllen", "Pipette"};
const char* const kToolKeys[] = {"P", "L", "R", "B", "F", "I"};

// Menu bar: Datei, Bearbeiten, Werkzeug, Ansicht, Hilfe (as in the Win32 editor).
void draw_menu(App& a) {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("Datei")) {
        if (ImGui::MenuItem("Speichern", "Strg+S", false, can_save(a))) command(a, Cmd::Save);
        if (ImGui::MenuItem("Bild importieren…", "Strg+I", false, can_convert(a))) command(a, Cmd::Import);
        if (ImGui::MenuItem("Bild exportieren… (PNG, BMP)", "Strg+E", false, can_convert(a))) command(a, Cmd::Export);
        ImGui::Separator();
        if (ImGui::MenuItem("Beenden")) command(a, Cmd::Exit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Bearbeiten")) {
        if (ImGui::MenuItem("Rückgängig", "Strg+Z", false, can_undo(a))) command(a, Cmd::Undo);
        if (ImGui::MenuItem("Wiederholen", "Strg+Y", false, can_redo(a))) command(a, Cmd::Redo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Werkzeug")) {
        for (int i = 0; i < 6; i++)
            if (ImGui::MenuItem(kToolNames[i], kToolKeys[i], int(a.ed->tool()) == i, tool_enabled(a, Tool(i)))) a.ed->set_tool(Tool(i));
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ansicht")) {
        if (ImGui::MenuItem("Raster", "G", a.grid, picture_tab(a) || map_tab(a))) command(a, Cmd::Grid);
        if (ImGui::MenuItem("Größe anpassen", nullptr, false, picture_tab(a))) command(a, Cmd::Fit);
        if (ImGui::MenuItem("Vergrößern", "+", false, picture_tab(a) || map_tab(a))) command(a, Cmd::ZoomIn);
        if (ImGui::MenuItem("Verkleinern", "-", false, picture_tab(a) || map_tab(a))) command(a, Cmd::ZoomOut);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Hilfe")) {
        if (ImGui::MenuItem("Info")) command(a, Cmd::About);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// A toolbar button; `checked` draws it pressed (tools, grid). Returns true when clicked.
bool tool_button(const char* label, bool checked, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    if (checked) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool clicked = ImGui::Button(label);
    if (checked) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    return clicked;
}

void toolbar_separator() {
    ImGui::SameLine(0, 0);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
}

// Toolbar: the tools as a radio group, undo/redo, save, grid and fit.
void draw_toolbar(App& a) {
    for (int i = 0; i < 6; i++)
        if (tool_button(kToolShort[i], int(a.ed->tool()) == i, tool_enabled(a, Tool(i)))) a.ed->set_tool(Tool(i));
    toolbar_separator();
    if (tool_button("Rückgängig", false, can_undo(a))) command(a, Cmd::Undo);
    if (tool_button("Wiederholen", false, can_redo(a))) command(a, Cmd::Redo);
    toolbar_separator();
    if (tool_button("Speichern", false, can_save(a))) command(a, Cmd::Save);
    toolbar_separator();
    if (tool_button("Raster", a.grid, picture_tab(a) || map_tab(a))) command(a, Cmd::Grid);
    if (tool_button("Anpassen", false, picture_tab(a))) command(a, Cmd::Fit);
    ImGui::NewLine();
}

// Tab strip. The tab shown is always a.tab; a click on another tab asks about unsaved changes and switches only then.
void draw_tabs(App& a) {
    static const char* const names[kTabCount] = {"Bilder", "Torszenen", "Paletten", "Mannschaften", "Hallenkarten"};
    if (!ImGui::BeginTabBar("tabs")) return;
    for (int i = 0; i < kTabCount; i++) {
        const bool open = ImGui::BeginTabItem(names[i], nullptr, i == a.tab ? ImGuiTabItemFlags_SetSelected : 0);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && i != a.tab) ensure_clean(a, [&a, i] { switch_tab(a, i); });
        if (open) ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

// List text and group of a team: 1-based number (European clubs counted from 1 again) and club name.
std::string team_label(const App& a, int t) { return std::to_string(t < 64 ? t + 1 : t - 63) + "  " + a.team->club_name(t); }
int team_group(int t) { return t < 12 ? 0 : t < 24 ? 1 : t < 36 ? 2 : t < 48 ? 3 : t < 64 ? 4 : 5; }

// File list on the left: pictures, sheets, palettes, teams (in groups) or hall maps, with a second column for the
// size (teams: league place). A click opens the entry.
void draw_list(App& a) {
    const bool teams = team_tab(a);
    const int selected = teams ? a.team_sel : current_number(a);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("list", 2, flags, ImGui::GetContentRegionAvail())) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(teams ? "Mannschaft" : "Datei", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(teams ? "Platz" : "Größe", ImGuiTableColumnFlags_WidthFixed, a.px(teams ? 44.0f : 80.0f));
    ImGui::TableHeadersRow();
    int clicked = -1;
    auto row = [&](int number, const std::string& label, const std::string& size) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(number);
        const bool sel = number == selected;
        if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) clicked = number;
        if (sel && a.list_scroll) ImGui::SetScrollHereY(0.3f);
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(size.c_str());
    };
    if (teams) {
        static const char* const heads[] = {"Liga 1 (Teams 1–12)", "Liga 2 (Teams 13–24)", "Liga 3 (Teams 25–36)", "Weitere Vereine (Teams 37–48)",
                                            "Nationalmannschaften (Teams 53–64)", "Europapokal-Vereine"};
        int group = -1;
        for (int t = 0; t < TeamDoc::kAllTeams; t++) {
            if (!TeamDoc::editable(t)) continue;
            if (team_group(t) != group) {
                group = team_group(t);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SeparatorText(heads[group]);
            }
            const int place = a.team->data(t).place;
            row(t, team_label(a, t), place >= 0 ? std::to_string(place) : "–");
        }
    } else if (map_tab(a)) {
        static const char* const names[] = {"KB236  Standardhalle", "KB242  Halle 1", "KB243  Halle 2", "KB244  Halle 3", "KB245  Halle 4"};
        for (size_t i = 0; i < MapDoc::numbers().size(); i++) row(MapDoc::numbers()[i], names[i], "42 × 72");
    } else if (pal_tab(a)) {
        row(21, "KB21  Spielpalette", "255 Farben");
        row(247, "KB247  Torszenen", "255 Farben");
    } else {
        // pictures with their own palette are marked with a dot (an asterisk if the font has no dot)
        const char* mark = ImGui::GetFont()->FindGlyphNoFallback(0x25CF) ? " \xE2\x97\x8F" : " *";
        for (const auto& f : a.ed->files()) {
            if ((f.kind == PixelEditor::Kind::Image) != (a.tab == kTabPictures)) continue;
            row(f.number, f.label + (f.embedded ? mark : ""), std::to_string(f.w) + " × " + std::to_string(f.h));
        }
    }
    a.list_scroll = false;
    ImGui::EndTable();
    if (clicked >= 0) open_item(a, clicked);
}

// Status bar: document status (or hover text) on the left; position, colour, cell and zoom on the right.
void draw_status(App& a) {
    ImGui::Separator();
    const float edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(status_text(a));
    std::string right;
    if (map_tab(a)) {
        if (a.map_hc >= 0) {
            right = "Spalte " + std::to_string(a.map_hc) + "   Zeile " + std::to_string(a.map_hr) + "   Kachel " + std::to_string(a.map->tile(a.map_hc, a.map_hr)) +
                    (a.map->mirrored(a.map_hc, a.map_hr) ? " (gespiegelt)" : "") + "   ";
        }
        right += "Zoom " + std::to_string(a.map_zoom) + "×";
    } else if (picture_tab(a)) {
        if (a.hover_x >= 0) {
            right = "x " + std::to_string(a.hover_x) + "   y " + std::to_string(a.hover_y) + "   Farbe " + std::to_string(a.hover_color) + "   ";
            if (a.hover_cell >= 0) right = (a.ed->kind() == PixelEditor::Kind::Tiles ? "Kachel " : "Sprite ") + std::to_string(a.hover_cell) + "   " + right;
        }
        right += "Zoom " + std::to_string(a.zoom) + "×";
    }
    if (!right.empty()) {
        const float w = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + a.px(20), edge - w));
        ImGui::TextUnformatted(right.c_str());
    }
}

// Keyboard shortcuts of the Win32 editor. Letter keys and undo/redo are ignored while a text field has the focus.
void shortcuts(App& a) {
    if (!a.dialogs.empty() || a.browser.active || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const bool typing = ImGui::GetIO().WantTextInput;
    auto chord = [](ImGuiKeyChord k) { return ImGui::IsKeyChordPressed(k); };
    if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) command(a, Cmd::Save);
    if (chord(ImGuiMod_Ctrl | ImGuiKey_E)) command(a, Cmd::Export);
    if (chord(ImGuiMod_Ctrl | ImGuiKey_I)) command(a, Cmd::Import);
    if (typing) return;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) command(a, Cmd::Undo);
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Y)) command(a, Cmd::Redo);
    static const ImGuiKey tool_keys[] = {ImGuiKey_P, ImGuiKey_L, ImGuiKey_R, ImGuiKey_B, ImGuiKey_F, ImGuiKey_I};
    for (int i = 0; i < 6; i++)
        if (chord(tool_keys[i]) && tool_enabled(a, Tool(i))) a.ed->set_tool(Tool(i));
    if (chord(ImGuiKey_G)) command(a, Cmd::Grid);
    // + and - as typed characters, so they work with every keyboard layout and on the keypad
    for (const ImWchar c : ImGui::GetIO().InputQueueCharacters) {
        if (c == '+') command(a, Cmd::ZoomIn);
        else if (c == '-') command(a, Cmd::ZoomOut);
    }
}

void draw_about(App& a) {
    if (a.show_about) {
        ImGui::OpenPopup("Info");
        a.show_about = false;
    }
    if (ImGui::BeginPopupModal("Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("OpenEM Editor " OPENEM_VERSION);
        ImGui::TextUnformatted("Bilder des Eishockey Manager (1993) bearbeiten.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Teil von OpenEM (GPL-3.0). Oberfläche: Dear ImGui, SDL2.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(a.px(100), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path path_from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool is_game_folder(const fs::path& p) {
    std::error_code ec;
    if (p.empty() || !fs::is_directory(p / "KB", ec)) return false;
    // EM.EXE in any case (the original file systems did not keep it; on Linux and macOS the name may be EM.exe)
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = utf8(it->path().filename());
        for (auto& ch : n) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (n == "EM.EXE") return true;
    }
    return false;
}

fs::path find_game_folder() {
    std::vector<fs::path> c;
    if (const char* env = SDL_getenv("OPENEM_DATA"); env && *env) c.push_back(path_from_utf8(env));
    std::vector<fs::path> bases;
    if (char* base = SDL_GetBasePath()) {
        bases.push_back(path_from_utf8(base));
        SDL_free(base);
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) bases.push_back(cwd);
    for (const fs::path& base : bases)
        for (const char* sub : {"", "EM", "data"}) c.push_back(*sub ? base / sub : base);
    for (const auto& p : c)
        if (is_game_folder(p)) return p;
    return {};
}

bool open_game_folder(App& a, const fs::path& data, std::string& error) {
    if (!is_game_folder(data)) {
        error = "Der Ordner enthält kein EM.EXE mit KB-Ordner.";
        return false;
    }
    try {
        a.ed = std::make_unique<PixelEditor>(data);
        a.pal = std::make_unique<PaletteDoc>(data);
        a.crest = std::make_unique<PixelEditor>(data);
        a.map = std::make_unique<MapDoc>(data);
        a.team = std::make_unique<TeamDoc>(data);
        a.team->load();
    } catch (const std::exception& e) {
        error = e.what();
        a.ed.reset();
        return false;
    }
    a.palette_choices = a.ed->palette_choices();
    a.data = data;
    a.ready = true;
    std::error_code ec;
    a.last_dir = fs::current_path(ec);
    return true;
}

void switch_tab(App& a, int index) {
    a.tab = std::clamp(index, 0, kTabCount - 1);
    a.hover_x = a.map_hc = -1;
    a.list_scroll = true;
    if (map_tab(a)) {
        if (a.map->current() < 0) open_map(a, MapDoc::numbers().front());
    } else if (team_tab(a)) {
        load_team_fields(a);
    } else if (pal_tab(a)) {
        if (a.pal->current() < 0) a.pal->load(21);
    } else if (a.ed->current() >= 0 && (a.ed->kind() == PixelEditor::Kind::Image) != (a.tab == kTabPictures)) {
        a.ed->close();
    }
}

void ensure_crest_clean(App& a, std::function<void()> then) {
    PixelEditor& e = *a.crest;
    if (!e.dirty()) {
        then();
        return;
    }
    ask(a, {"Wappen " + std::to_string(e.current() - 80) + " wurde geändert.", "Das Wappen gilt für alle Vereine, die es benutzen. Änderungen speichern?",
            {"Speichern", "Verwerfen", "Abbrechen"}, [&a, then](int b) {
                if (b == 0 && a.crest->save()) then();
                else if (b == 1) {
                    a.crest->load(a.crest->current());  // discard
                    then();
                }
            }});
}

void ensure_clean(App& a, std::function<void()> then) {
    auto doc_step = [&a, then] {
        const bool dirty = map_tab(a) ? a.map->dirty() : team_tab(a) ? a.team->dirty() : pal_tab(a) ? a.pal->dirty() : a.ed->dirty();
        if (!dirty) {
            then();
            return;
        }
        ask(a, {"KB" + std::to_string(current_number(a)) + " wurde geändert.", "Sollen die Änderungen gespeichert werden?", {"Speichern", "Verwerfen", "Abbrechen"},
                [&a, then](int b) {
                    if (b == 0) save_current(a, then);
                    else if (b == 1) {
                        discard_current(a);
                        then();
                    }
                }});
    };
    if (team_tab(a)) ensure_crest_clean(a, doc_step);
    else doc_step();
}

void request_quit(App& a) {
    ensure_clean(a, [&a] { a.quit = true; });
}

// Layout of the window: menu bar, toolbar, tab strip, then list (left), work area (middle) and side panel (right),
// status bar at the bottom.
void draw_frame(App& a) {
    a.hover_left.clear();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("OpenEM Editor", nullptr, wf);
    ImGui::PopStyleVar();
    draw_menu(a);
    draw_toolbar(a);
    draw_tabs(a);
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float status_h = ImGui::GetTextLineHeight() + st.ItemSpacing.y * 2 + 2;
    const float h = std::max(a.px(100), avail.y - status_h);
    const float list_w = a.px(270), side_w = a.px(250);
    const float work_w = std::max(a.px(100), avail.x - list_w - side_w - 2 * st.ItemSpacing.x);
    ImGui::BeginChild("left", ImVec2(list_w, h), ImGuiChildFlags_None);
    draw_list(a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("work", ImVec2(work_w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    tab_work(a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("side", ImVec2(side_w, h), ImGuiChildFlags_Borders);
    tab_side(a);
    ImGui::EndChild();
    draw_status(a);
    draw_about(a);
    ImGui::End();
    draw_dialogs(a);
    shortcuts(a);
    // window title like the Win32 editor
    const int cur = current_number(a);
    const std::string title = cur < 0 ? "OpenEM Editor" : "KB" + std::to_string(cur) + (current_dirty(a) ? " *" : "") + " – OpenEM Editor";
    if (a.window && title != SDL_GetWindowTitle(a.window)) SDL_SetWindowTitle(a.window, title.c_str());
}

}  // namespace em::editor::sdl
