// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor, portable front end: message boxes (Save / Discard / Cancel and the other questions of the Win32
// editor), the in-app file and folder browser that replaces the system file dialogs, the start screen shown when no
// game folder was found, and the upper-case text field of the team tab.
#include <algorithm>
#include <cctype>
#include <system_error>

#include "editor/sdl_app.hpp"

namespace em::editor::sdl {

namespace {

constexpr ImVec4 kErrorColor(0.80f, 0.10f, 0.10f, 1.0f);
constexpr ImVec4 kOkColor(0.10f, 0.55f, 0.15f, 1.0f);

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

template <size_t N>
void copy_to(std::array<char, N>& buf, const std::string& s) {  // truncating copy into an edit buffer
    const size_t n = std::min(s.size(), N - 1);
    std::copy_n(s.begin(), n, buf.begin());
    buf[n] = '\0';
}

// Shows folder `d` (made absolute) and refreshes the path field; the entries are listed on the next draw.
void set_dir(FileBrowser& b, const fs::path& d) {
    std::error_code ec;
    fs::path p = fs::absolute(d, ec);
    if (ec) p = d;
    p = p.lexically_normal();
    if (p.has_relative_path() && !p.has_filename()) p = p.parent_path();  // drop a trailing separator ("C:/x/" -> "C:/x")
    b.dir = p;
    copy_to(b.path, utf8(p));
    b.listed = false;
    b.error.clear();
}

// Lists the visible entries of b.dir: folders, and in Open/Save mode the files with an accepted extension.
void list_dir(FileBrowser& b) {
    b.entries.clear();
    b.listed = true;
    std::error_code ec;
    fs::directory_iterator it(b.dir, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) {
        b.error = "Ordner kann nicht gelesen werden";
        return;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = utf8(it->path().filename());
        if (name.empty() || name[0] == '.') continue;  // hidden files (Linux, macOS)
        std::error_code e2;
        const bool folder = it->is_directory(e2);
        if (!folder) {
            if (b.mode == FileBrowser::Mode::Folder) continue;
            const std::string ext = lower(utf8(it->path().extension()));
            if (!b.exts.empty() && std::find(b.exts.begin(), b.exts.end(), ext) == b.exts.end()) continue;
        }
        b.entries.push_back({name, folder});
    }
    std::sort(b.entries.begin(), b.entries.end(), [](const FileBrowser::Entry& x, const FileBrowser::Entry& y) {
        if (x.folder != y.folder) return x.folder;
        return lower(x.name) < lower(y.name);
    });
}

enum class BrowseResult { None, Accept, Cancel };

// The browser controls: "up" button (and drive choice on Windows), path field, entry list of height `list_h`, name
// field (Open/Save) and the OK / cancel buttons. A click on a folder opens it; a double click on a file accepts it.
BrowseResult browser_body(App& a, FileBrowser& b, float list_h, const char* ok_label, const char* cancel_label) {
    if (!b.listed) list_dir(b);
    BrowseResult result = BrowseResult::None;
    if (ImGui::Button("Hoch") && b.dir != b.dir.root_path() && b.dir.has_parent_path()) set_dir(b, b.dir.parent_path());
    ImGui::SameLine();
#ifdef _WIN32
    {   // drive letters
        const std::string root = utf8(b.dir.root_name());
        ImGui::SetNextItemWidth(a.px(64));
        if (ImGui::BeginCombo("##drive", root.c_str())) {
            for (char d = 'A'; d <= 'Z'; d++) {
                const std::string r = std::string(1, d) + ":";
                std::error_code ec;
                if (!fs::exists(fs::path(r + "/"), ec)) continue;
                if (ImGui::Selectable(r.c_str(), r == root)) set_dir(b, fs::path(r + "/"));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
#endif
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", b.path.data(), b.path.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const fs::path p = path_from_utf8(b.path.data());
        std::error_code ec;
        if (fs::is_directory(p, ec)) set_dir(b, p);
        else b.error = "Ordner nicht gefunden";
    }
    ImGui::BeginChild("entries", ImVec2(0, std::max(a.px(80), list_h)), ImGuiChildFlags_Borders);
    fs::path enter;
    for (const auto& e : b.entries) {
        const std::string label = e.folder ? e.name + "/" : e.name;
        if (ImGui::Selectable(label.c_str(), !e.folder && e.name == b.name.data(), ImGuiSelectableFlags_AllowDoubleClick)) {
            if (e.folder) enter = b.dir / path_from_utf8(e.name);
            else {
                copy_to(b.name, e.name);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) result = BrowseResult::Accept;
            }
        }
    }
    if (b.entries.empty() && b.error.empty()) ImGui::TextDisabled("(leer)");
    ImGui::EndChild();
    if (!enter.empty()) set_dir(b, enter);
    if (!b.error.empty()) ImGui::TextColored(kErrorColor, "%s", b.error.c_str());
    if (b.mode != FileBrowser::Mode::Folder) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Dateiname");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", b.name.data(), b.name.size(), ImGuiInputTextFlags_EnterReturnsTrue)) result = BrowseResult::Accept;
    }
    ImGui::Spacing();
    if (ImGui::Button(ok_label, ImVec2(a.px(180), 0))) result = BrowseResult::Accept;
    ImGui::SameLine();
    if (ImGui::Button(cancel_label, ImVec2(a.px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) result = BrowseResult::Cancel;
    return result;
}

// The modal browser opened by browse(). The callback runs after the popup has been closed, so it may open a
// message box (overwrite question, import size question).
void draw_browser(App& a) {
    FileBrowser& b = a.browser;
    if (!b.active) return;
    const std::string id = b.title + "###browser";
    if (!b.popup_open) {
        ImGui::OpenPopup(id.c_str());
        b.popup_open = true;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x / 2, vp->WorkPos.y + vp->WorkSize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(a.px(640), a.px(460)), ImGuiCond_Appearing);
    std::function<void()> after;
    if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        const float footer = ImGui::GetFrameHeightWithSpacing() * (b.mode == FileBrowser::Mode::Folder ? 2.5f : 3.5f) + a.px(10);
        const char* ok = b.mode == FileBrowser::Mode::Save ? "Speichern" : b.mode == FileBrowser::Mode::Open ? "Öffnen" : "Ordner wählen";
        const BrowseResult r = browser_body(a, b, ImGui::GetContentRegionAvail().y - footer, ok, "Abbrechen");
        if (r == BrowseResult::Accept) {
            fs::path p = b.dir;
            std::error_code ec;
            bool ok_path = true, exists = false;
            if (b.mode != FileBrowser::Mode::Folder) {
                if (!b.name[0]) {
                    b.error = "Bitte einen Dateinamen angeben";
                    ok_path = false;
                } else {
                    p /= path_from_utf8(b.name.data());
                    if (b.mode == FileBrowser::Mode::Save && !p.has_extension() && !b.exts.empty()) p += b.exts.front();
                    exists = fs::exists(p, ec);
                    if (b.mode == FileBrowser::Mode::Open && !fs::is_regular_file(p, ec)) {
                        b.error = "Datei nicht gefunden";
                        ok_path = false;
                    }
                }
            }
            if (ok_path) {
                auto done = b.done;
                if (b.mode == FileBrowser::Mode::Save && exists) {
                    after = [&a, done, p] {
                        ask(a, {utf8(p.filename()) + " ist schon vorhanden.", "Soll die Datei ersetzt werden?", {"Ersetzen", "Abbrechen"}, [done, p](int btn) {
                                    if (btn == 0 && done) done(p);
                                }});
                    };
                } else {
                    after = [done, p] {
                        if (done) done(p);
                    };
                }
                b.active = false;
                ImGui::CloseCurrentPopup();
            }
        } else if (r == BrowseResult::Cancel) {
            b.active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        b.active = false;  // closed some other way
    }
    if (!b.active) b.popup_open = false;
    if (after) after();
}

// Upper-cases letters as they are typed (ES_UPPERCASE of the Win32 edit fields) and cuts the text at the character
// limit passed in UserData (EM_LIMITTEXT).
int upper_callback(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        const ImWchar c = d->EventChar;
        if (c >= 'a' && c <= 'z') d->EventChar = ImWchar(c - 32);
        else if (c == 0xE4 || c == 0xF6 || c == 0xFC) d->EventChar = ImWchar(c - 0x20);  // ä ö ü -> Ä Ö Ü
    } else if (d->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
        const int limit = *static_cast<const int*>(d->UserData);
        int chars = 0;
        for (int i = 0; i < d->BufTextLen; i++) {
            if ((static_cast<unsigned char>(d->Buf[i]) & 0xC0) == 0x80) continue;  // UTF-8 continuation byte
            if (++chars > limit) {
                d->DeleteChars(i, d->BufTextLen - i);
                break;
            }
        }
    }
    return 0;
}

}  // namespace

bool input_upper(const char* id, char* buf, size_t size, int limit) {
    return ImGui::InputText(id, buf, size, ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit, upper_callback, &limit);
}

void ask(App& a, Dialog d) { a.dialogs.push_back(std::move(d)); }

void browse(App& a, FileBrowser::Mode mode, std::string title, const fs::path& start, const std::string& name, std::vector<std::string> exts,
            std::function<void(const fs::path&)> done) {
    FileBrowser& b = a.browser;
    b.active = true;
    b.popup_open = false;
    b.mode = mode;
    b.title = std::move(title);
    b.exts = std::move(exts);
    b.done = std::move(done);
    copy_to(b.name, name);
    std::error_code ec;
    set_dir(b, !start.empty() && fs::is_directory(start, ec) ? start : fs::current_path(ec));
}

void draw_dialogs(App& a) {
    draw_browser(a);
    if (a.dialogs.empty() || a.browser.active) return;
    const char* id = "OpenEM Editor###dialog";
    if (!a.dialog_open) {
        ImGui::OpenPopup(id);
        a.dialog_open = true;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x / 2, vp->WorkPos.y + vp->WorkSize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    int result = -2;  // -2: still open
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const Dialog& d = a.dialogs.front();
        ImGui::PushTextWrapPos(a.px(440));
        ImGui::TextUnformatted(d.head.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", d.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Spacing();
        for (size_t i = 0; i < d.buttons.size(); i++) {
            if (i) ImGui::SameLine();
            if (ImGui::Button(d.buttons[i].c_str(), ImVec2(std::max(a.px(100), ImGui::CalcTextSize(d.buttons[i].c_str()).x + a.px(20)), 0))) result = int(i);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) result = -1;
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) result = 0;  // default button
        if (result != -2) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    } else {
        result = -1;
    }
    if (result != -2) {
        Dialog d = std::move(a.dialogs.front());
        a.dialogs.pop_front();
        a.dialog_open = false;
        if (d.done) d.done(result);
    }
}

void draw_start_screen(App& a) {
    FileBrowser& b = a.browser;
    if (b.mode != FileBrowser::Mode::Folder || b.dir.empty()) {
        b = FileBrowser{};
        b.mode = FileBrowser::Mode::Folder;
        std::error_code ec;
        set_dir(b, fs::current_path(ec));
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("OpenEM Editor##start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("OpenEM Editor");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("Der OpenEM Editor braucht deine Kopie des Originalspiels. Bitte den Ordner mit EM.EXE und KB wählen.");
    ImGui::Spacing();
    const bool game = is_game_folder(b.dir);
    const float footer = ImGui::GetFrameHeightWithSpacing() * 3 + a.px(10);
    const BrowseResult r = browser_body(a, b, ImGui::GetContentRegionAvail().y - footer, "Diesen Ordner verwenden", "Beenden");
    if (game) ImGui::TextColored(kOkColor, "Spielordner erkannt (EM.EXE und KB)");
    else ImGui::TextDisabled("Kein Spielordner: hier fehlt EM.EXE oder der Ordner KB");
    if (!a.start_error.empty()) ImGui::TextColored(kErrorColor, "%s", a.start_error.c_str());
    ImGui::End();
    if (r == BrowseResult::Accept) {
        std::string err;
        if (open_game_folder(a, b.dir, err)) {
            a.start_error.clear();
            b = FileBrowser{};
        } else {
            a.start_error = err;
        }
    } else if (r == BrowseResult::Cancel) {
        a.quit = true;
    }
}

}  // namespace em::editor::sdl
