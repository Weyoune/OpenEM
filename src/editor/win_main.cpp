// SPDX-License-Identifier: GPL-3.0-only
// OpenEM Editor - Windows front end (Win32 with Common Controls 6: menu, toolbar, list view, status bar,
// task and file dialogs). Document logic: pixel_editor.cpp.
//
//   em_editor [--data <EM folder>] [--file <KB number>] [--shot out.bmp]
// --shot opens the window, saves a picture of it and exits (for checks).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "editor/palette_doc.hpp"
#include "editor/pixel_editor.hpp"
#include "editor/team_doc.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace fs = std::filesystem;
using em::editor::PaletteDoc;
using em::editor::PixelEditor;
using em::editor::TeamDoc;

namespace {

enum Cmd : UINT {
    ID_FILE_SAVE = 101, ID_FILE_EXPORT, ID_FILE_EXIT,
    ID_EDIT_UNDO = 110, ID_EDIT_REDO,
    ID_TOOL_PENCIL = 120, ID_TOOL_LINE, ID_TOOL_RECT, ID_TOOL_FILLRECT, ID_TOOL_FILL, ID_TOOL_PICKER,
    ID_VIEW_GRID = 130, ID_VIEW_FIT, ID_VIEW_ZOOMIN, ID_VIEW_ZOOMOUT,
    ID_HELP_ABOUT = 140,
    IDC_TOOLBAR = 201, IDC_LIST, IDC_STATUS, IDC_TAB,
    IDC_TRACK_R = 210, IDC_TRACK_G, IDC_TRACK_B,
    IDC_PALSEL = 400,
    IDC_CLUB = 300, IDC_NAME0 = 310, IDC_FOR0 = 340, IDC_CREST = 370, IDC_PLACE, IDC_STR0 = 372, IDC_JER0 = 380, IDC_SWAP_BTN = 390, IDC_SWAP_COMBO,
};

const wchar_t kMainClass[] = L"OpenEMEditorMain", kCanvasClass[] = L"OpenEMEditorCanvas", kPaletteClass[] = L"OpenEMEditorPalette", kPalGridClass[] = L"OpenEMEditorPalGrid";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct TeamUi {
    HWND club = nullptr, name[25] = {}, foreign[25] = {}, crest = nullptr, crest_ud = nullptr, place = nullptr, place_ud = nullptr;
    HWND str[3] = {}, str_ud[3] = {}, jer[3] = {}, jer_lbl[3] = {}, preview = nullptr, swap_combo = nullptr, swap_btn = nullptr;
    HWND lbl_club = nullptr, lbl_crest = nullptr, lbl_place = nullptr, lbl_str[3] = {}, lbl_jersey = nullptr, lbl_swap = nullptr, lbl_players = nullptr;
    std::vector<HWND> all;  // every control of the page, for show/hide and fonts
};

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, toolbar = nullptr, list = nullptr, status = nullptr, canvas = nullptr, palette = nullptr;
    HWND psel = nullptr;
    HWND tab = nullptr, pgrid = nullptr, track[3] = {}, tlabel[3] = {}, pinfo = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    std::unique_ptr<PixelEditor> ed;
    std::unique_ptr<PaletteDoc> pal;
    int tab_index = 0;        // 0 Bilder, 1 Torszenen, 2 Paletten
    int pal_sel = 0;          // selected colour in the palette editor
    bool setting_sliders = false;
    int hover_cell = -1;
    std::vector<int> list_numbers;  // list row -> KB number or team index
    std::unique_ptr<TeamDoc> team;
    TeamUi tu;
    int team_sel = 0;
    bool loading_team = false;
    em::Image crest_img;
    int crest_no = -1;
    em::Palette crest_pal;
    bool crest_pal_ok = false;
    fs::path data;

    int zoom = 4;
    bool grid = true;
    int scroll_x = 0, scroll_y = 0;
    int hover_x = -1, hover_y = -1, hover_color = -1;
    bool selecting = false;  // guard against list-view feedback
    unsigned shown_revision = ~0u;
    std::vector<uint32_t> bits;  // canvas picture as 32-bit pixels
    bool shot_mode = false;

    int px(int v) const { return MulDiv(v, int(dpi), 96); }
};

App* g_app = nullptr;

// ------------------------------------------------------------------------------------------- helpers

void make_font(App& a) {
    NONCLIENTMETRICSW m{sizeof m};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, a.dpi);
    if (a.font) DeleteObject(a.font);
    a.font = CreateFontIndirectW(&m.lfMessageFont);
}

std::wstring dialog_path(const wchar_t* title, bool folder, bool save, const std::wstring& name, HWND owner) {
    std::wstring result;
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&d))))
        return result;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    d->SetTitle(title);
    if (save) {
        static const COMDLG_FILTERSPEC spec[] = {{L"Bitmap (*.bmp)", L"*.bmp"}};
        d->SetFileTypes(1, spec);
        d->SetDefaultExtension(L"bmp");
        d->SetFileName(name.c_str());
    }
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    d->Release();
    return result;
}

// ------------------------------------------------------------------------------------------- UI state

void update_scroll(App& a);
void sync_sliders(App& a);

bool pal_tab(const App& a) { return a.tab_index == 2; }
bool team_tab(const App& a) { return a.tab_index == 3; }

void refresh(App& a) {
    PixelEditor& e = *a.ed;
    const bool tt = team_tab(a), pt = pal_tab(a) || tt;  // pt: a page without the pixel tools
    const int cur = tt ? 23 : pt ? a.pal->current() : e.current();
    const bool dirty = tt ? a.team->dirty() : pt ? a.pal->dirty() : e.dirty();
    std::wstring title = cur < 0 ? L"OpenEM Editor" : L"KB" + std::to_wstring(cur) + (dirty ? L" *" : L"") + L" – OpenEM Editor";
    SetWindowTextW(a.main, title.c_str());
    // toolbar
    SendMessage(a.toolbar, TB_CHECKBUTTON, ID_TOOL_PENCIL + int(e.tool()), TRUE);
    SendMessage(a.toolbar, TB_CHECKBUTTON, ID_VIEW_GRID, a.grid);
    for (int id = ID_TOOL_PENCIL; id <= ID_TOOL_PICKER; id++) SendMessage(a.toolbar, TB_ENABLEBUTTON, id, !pt);
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_EDIT_UNDO, pt ? dirty : e.can_undo());
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_EDIT_REDO, !pt && e.can_redo());
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_FILE_SAVE, cur >= 0 && dirty);
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_VIEW_GRID, !pt);
    SendMessage(a.toolbar, TB_ENABLEBUTTON, ID_VIEW_FIT, !pt);
    // menu
    HMENU m = GetMenu(a.main);
    EnableMenuItem(m, ID_FILE_SAVE, MF_BYCOMMAND | (cur >= 0 && dirty ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_FILE_EXPORT, MF_BYCOMMAND | (!pt && e.current() >= 0 ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_EDIT_UNDO, MF_BYCOMMAND | ((pt ? dirty : e.can_undo()) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, ID_EDIT_REDO, MF_BYCOMMAND | (!pt && e.can_redo() ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(m, ID_VIEW_GRID, MF_BYCOMMAND | (a.grid ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(m, ID_TOOL_PENCIL, ID_TOOL_PICKER, ID_TOOL_PENCIL + int(e.tool()), MF_BYCOMMAND);
    // status bar
    SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)widen(tt ? a.team->status() : pt ? a.pal->status() : e.status()).c_str());
    std::wstring right;
    if (!pt) {
        if (a.hover_x >= 0) {
            right = L"x " + std::to_wstring(a.hover_x) + L"   y " + std::to_wstring(a.hover_y) + L"   Farbe " + std::to_wstring(a.hover_color);
            if (a.hover_cell >= 0) right = (e.kind() == PixelEditor::Kind::Tiles ? L"Kachel " : L"Sprite ") + std::to_wstring(a.hover_cell) + L"   " + right;
        }
        right += L"   Zoom " + std::to_wstring(a.zoom) + L"×";
    }
    SendMessage(a.status, SB_SETTEXT, 1, (LPARAM)right.c_str());
    for (int i = 0, n = int(SendMessage(a.psel, CB_GETCOUNT, 0, 0)); i < n; i++)
        if (int(SendMessage(a.psel, CB_GETITEMDATA, WPARAM(i), 0)) == e.palette_source()) SendMessage(a.psel, CB_SETCURSEL, WPARAM(i), 0);
    EnableWindow(a.psel, e.current() >= 0 && !e.has_own_palette());
    InvalidateRect(a.palette, nullptr, FALSE);
    InvalidateRect(a.pgrid, nullptr, FALSE);
    if (a.shown_revision != e.revision()) {
        a.shown_revision = e.revision();
        InvalidateRect(a.canvas, nullptr, FALSE);
    }
}

bool team_save(App& a);
void load_team_controls(App& a);
void layout_team(App& a, int x, int y, int w, int h, int rx, int rw);
void show_team(App& a, bool on);

bool confirm_discard(App& a) {
    const bool pt = pal_tab(a), tt = team_tab(a);
    const bool dirty = tt ? a.team->dirty() : pt ? a.pal->dirty() : a.ed->dirty();
    if (!dirty) return true;
    const int cur = tt ? 23 : pt ? a.pal->current() : a.ed->current();
    std::wstring head = L"KB" + std::to_wstring(cur) + L" wurde geändert.";
    TASKDIALOG_BUTTON buttons[] = {{101, L"Speichern"}, {102, L"Verwerfen"}};
    TASKDIALOGCONFIG cfg{sizeof cfg};
    cfg.hwndParent = a.main;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    cfg.pszWindowTitle = L"OpenEM Editor";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = head.c_str();
    cfg.pszContent = L"Sollen die Änderungen gespeichert werden?";
    cfg.pButtons = buttons;
    cfg.cButtons = 2;
    cfg.nDefaultButton = 101;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) return false;
    if (pressed == 101) return tt ? team_save(a) : pt ? a.pal->save() : a.ed->save();
    if (pressed == 102 && tt) a.team->revert();
    else if (pressed == 102 && pt) a.pal->revert();
    return pressed == 102;
}

int current_number(const App& a) { return pal_tab(a) ? a.pal->current() : a.ed->current(); }

void select_in_list(App& a, int number) {
    int idx = -1;
    for (size_t i = 0; i < a.list_numbers.size(); i++)
        if (a.list_numbers[i] == number) idx = int(i);
    a.selecting = true;
    ListView_SetItemState(a.list, -1, 0, LVIS_SELECTED);
    if (idx >= 0) {
        ListView_SetItemState(a.list, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(a.list, idx, FALSE);
    }
    a.selecting = false;
}

void fill_list(App& a) {
    a.selecting = true;
    ListView_DeleteAllItems(a.list);
    a.list_numbers.clear();
    auto add = [&](int number, const std::wstring& label, const std::wstring& size) {
        const int i = int(a.list_numbers.size());
        LVITEMW it{LVIF_TEXT};
        it.iItem = i;
        it.pszText = const_cast<wchar_t*>(label.c_str());
        ListView_InsertItem(a.list, &it);
        ListView_SetItemText(a.list, i, 1, const_cast<wchar_t*>(size.c_str()));
        a.list_numbers.push_back(number);
    };
    LVCOLUMNW hc{LVCF_TEXT};
    hc.pszText = const_cast<wchar_t*>(team_tab(a) ? L"Mannschaft" : L"Datei");
    ListView_SetColumn(a.list, 0, &hc);
    hc.pszText = const_cast<wchar_t*>(team_tab(a) ? L"Platz" : L"Größe");
    ListView_SetColumn(a.list, 1, &hc);
    if (team_tab(a)) {
        for (int t = 0; t < TeamDoc::kTeams; t++) {
            if (!TeamDoc::editable(t)) continue;
            const int place = a.team->data(t).place;
            add(t, std::to_wstring(t + 1) + L"  " + widen(a.team->club_name(t)), place >= 0 ? std::to_wstring(place) : L"–");
        }
    } else if (pal_tab(a)) {
        add(21, L"KB21  Spielpalette", L"255 Farben");
        add(247, L"KB247  Torszenen", L"255 Farben");
    } else {
        for (const auto& f : a.ed->files()) {
            const bool image = f.kind == PixelEditor::Kind::Image;
            if (image != (a.tab_index == 0)) continue;
            add(f.number, widen(f.label) + (f.embedded ? L" ●" : L""), std::to_wstring(f.w) + L" × " + std::to_wstring(f.h));
        }
    }
    a.selecting = false;
}

void fit_zoom(App& a) {
    RECT r;
    GetClientRect(a.canvas, &r);
    const auto& img = a.ed->image();
    if (a.ed->current() < 0) return;
    const int m = a.px(24);
    a.zoom = std::clamp(std::min(int(r.right - m) / std::max(1, img.width), int(r.bottom - m) / std::max(1, img.height)), 1, 32);
    a.scroll_x = a.scroll_y = 0;
    update_scroll(a);
}

void open_image(App& a, int number) {
    if (team_tab(a)) {  // the number is a team index; edits are applied live, nothing to confirm
        a.team_sel = number;
        load_team_controls(a);
        refresh(a);
        return;
    }
    if (pal_tab(a)) {
        if (number == a.pal->current()) return;
        if (!confirm_discard(a)) {
            select_in_list(a, a.pal->current());
            return;
        }
        if (a.pal->load(number)) {
            a.pal_sel = std::min(a.pal_sel, PaletteDoc::kColors);
            sync_sliders(a);
        }
        select_in_list(a, a.pal->current());
        refresh(a);
        return;
    }
    if (number == a.ed->current()) return;
    if (!confirm_discard(a)) {
        select_in_list(a, a.ed->current());
        return;
    }
    if (a.ed->load(number)) fit_zoom(a);
    select_in_list(a, a.ed->current());
    refresh(a);
}

void set_zoom(App& a, int z) {
    a.zoom = std::clamp(z, 1, 32);
    update_scroll(a);
    InvalidateRect(a.canvas, nullptr, FALSE);
    refresh(a);
}

void sync_sliders(App& a) {
    const auto c = a.pal->get(a.pal_sel);
    const bool on = a.pal->current() >= 0 && a.pal_sel < PaletteDoc::kColors;
    a.setting_sliders = true;
    for (int i = 0; i < 3; i++) {
        SendMessage(a.track[i], TBM_SETPOS, TRUE, c[size_t(i)]);
        EnableWindow(a.track[i], on);
        const wchar_t* names[] = {L"Rot", L"Grün", L"Blau"};
        std::wstring t = std::wstring(names[i]) + L"  " + std::to_wstring(c[size_t(i)]);
        SetWindowTextW(a.tlabel[i], t.c_str());
    }
    std::wstring info = a.pal->current() < 0 ? L"Palette links wählen"
                        : a.pal_sel >= PaletteDoc::kColors ? L"Farbe 255 wird nicht gespeichert"
                                                           : L"Farbe " + std::to_wstring(a.pal_sel);
    SetWindowTextW(a.pinfo, info.c_str());
    a.setting_sliders = false;
}

void show_tab_controls(App& a) {
    const bool pt = pal_tab(a), tt = team_tab(a);
    ShowWindow(a.canvas, pt || tt ? SW_HIDE : SW_SHOW);
    ShowWindow(a.palette, pt || tt ? SW_HIDE : SW_SHOW);
    ShowWindow(a.psel, pt || tt ? SW_HIDE : SW_SHOW);
    show_team(a, tt);
    ShowWindow(a.pgrid, pt ? SW_SHOW : SW_HIDE);
    ShowWindow(a.pinfo, pt ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 3; i++) {
        ShowWindow(a.track[i], pt ? SW_SHOW : SW_HIDE);
        ShowWindow(a.tlabel[i], pt ? SW_SHOW : SW_HIDE);
    }
}

void layout(App& a) {
    RECT r;
    GetClientRect(a.main, &r);
    SendMessage(a.toolbar, TB_AUTOSIZE, 0, 0);
    SendMessage(a.status, WM_SIZE, 0, 0);
    RECT tr, sr;
    GetWindowRect(a.toolbar, &tr);
    GetWindowRect(a.status, &sr);
    const int tab_h = a.px(30);
    const int top0 = tr.bottom - tr.top, bottom = r.bottom - (sr.bottom - sr.top);
    MoveWindow(a.tab, 0, top0, r.right, tab_h, TRUE);
    const int top = top0 + tab_h;
    const int lw = a.px(270), pw = a.px(250);
    MoveWindow(a.list, 0, top, lw, bottom - top, TRUE);
    MoveWindow(a.canvas, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, TRUE);
    MoveWindow(a.psel, r.right - pw + a.px(10), top + a.px(8), pw - a.px(20), a.px(300), TRUE);
    MoveWindow(a.palette, r.right - pw, top + a.px(40), pw, bottom - top - a.px(40), TRUE);
    // palette page: grid in the middle, sliders on the right
    MoveWindow(a.pgrid, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, TRUE);
    const int cx = r.right - pw + a.px(12), cw = pw - a.px(24);
    int y = top + a.px(14);
    MoveWindow(a.pinfo, cx, y, cw, a.px(20), TRUE);
    y += a.px(34);
    for (int i = 0; i < 3; i++) {
        MoveWindow(a.tlabel[i], cx, y, cw, a.px(20), TRUE);
        MoveWindow(a.track[i], cx, y + a.px(20), cw, a.px(32), TRUE);
        y += a.px(64);
    }
    layout_team(a, lw, top, std::max(10, int(r.right) - lw - pw), bottom - top, r.right - pw, pw);
    const int w1 = a.px(260);
    int parts[2] = {std::max(w1, int(r.right) - w1), -1};
    SendMessage(a.status, SB_SETPARTS, 2, (LPARAM)parts);
    update_scroll(a);
}

void switch_tab(App& a, int index) {
    a.tab_index = index;
    a.hover_x = -1;
    fill_list(a);
    if (team_tab(a)) {
        select_in_list(a, a.team_sel);
        load_team_controls(a);
    } else if (pal_tab(a)) {
        if (a.pal->current() < 0) a.pal->load(21);
        sync_sliders(a);
        select_in_list(a, a.pal->current());
    } else {
        if (a.ed->current() >= 0 && (a.ed->kind() == PixelEditor::Kind::Image) != (a.tab_index == 0)) a.ed->close();
        select_in_list(a, a.ed->current());
    }
    show_tab_controls(a);
    refresh(a);
}

// ---------------------------------------------------------------------------------------------- canvas

struct Geo {
    int ox, oy, iw, ih, total_w, total_h, cw, ch;
};

Geo geometry(App& a) {
    RECT r;
    GetClientRect(a.canvas, &r);
    Geo g{};
    const auto& img = a.ed->image();
    g.cw = r.right;
    g.ch = r.bottom;
    g.iw = img.width * a.zoom;
    g.ih = img.height * a.zoom;
    const int m = a.px(12);
    g.total_w = g.iw + 2 * m;
    g.total_h = g.ih + 2 * m;
    g.ox = g.total_w <= g.cw ? (g.cw - g.iw) / 2 : m - a.scroll_x;
    g.oy = g.total_h <= g.ch ? (g.ch - g.ih) / 2 : m - a.scroll_y;
    return g;
}

void update_scroll(App& a) {
    if (!a.canvas) return;
    if (a.ed->current() < 0) {
        ShowScrollBar(a.canvas, SB_BOTH, FALSE);
        return;
    }
    for (int pass = 0; pass < 2; pass++) {  // adding one scroll bar may make the other necessary
        Geo g = geometry(a);
        a.scroll_x = std::clamp(a.scroll_x, 0, std::max(0, g.total_w - g.cw));
        a.scroll_y = std::clamp(a.scroll_y, 0, std::max(0, g.total_h - g.ch));
        SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0;
        si.nMax = g.total_w - 1;
        si.nPage = UINT(g.cw);
        si.nPos = a.scroll_x;
        SetScrollInfo(a.canvas, SB_HORZ, &si, TRUE);
        si.nMax = g.total_h - 1;
        si.nPage = UINT(g.ch);
        si.nPos = a.scroll_y;
        SetScrollInfo(a.canvas, SB_VERT, &si, TRUE);
    }
}

void paint_canvas(App& a, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr;
    GetClientRect(hwnd, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(58, 60, 64));
    FillRect(mem, &cr, bg);
    DeleteObject(bg);
    PixelEditor& e = *a.ed;
    if (e.current() >= 0) {
        const auto& img = e.image();
        const auto& pal = e.palette();
        a.bits.resize(img.pixels.size());
        for (size_t i = 0; i < a.bits.size(); i++) {
            const auto& c = pal.rgb[img.pixels[i]];
            a.bits[i] = uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = img.width;
        bi.bmiHeader.biHeight = -img.height;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        Geo g = geometry(a);
        SetStretchBltMode(mem, COLORONCOLOR);
        StretchDIBits(mem, g.ox, g.oy, g.iw, g.ih, 0, 0, img.width, img.height, a.bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        if (a.grid && a.zoom >= 6) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(110, 110, 118));
            HGDIOBJ op = SelectObject(mem, pen);
            for (int x = 0; x <= img.width; x++) {
                MoveToEx(mem, g.ox + x * a.zoom, g.oy, nullptr);
                LineTo(mem, g.ox + x * a.zoom, g.oy + g.ih);
            }
            for (int y = 0; y <= img.height; y++) {
                MoveToEx(mem, g.ox, g.oy + y * a.zoom, nullptr);
                LineTo(mem, g.ox + g.iw, g.oy + y * a.zoom);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        if (const int cell = e.cell_size()) {  // tile or sprite boundaries
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(232, 160, 40));
            HGDIOBJ op = SelectObject(mem, pen);
            for (int x = 0; x <= img.width; x += cell) {
                MoveToEx(mem, g.ox + x * a.zoom, g.oy, nullptr);
                LineTo(mem, g.ox + x * a.zoom, g.oy + g.ih);
            }
            for (int y = 0; y <= img.height; y += cell) {
                MoveToEx(mem, g.ox, g.oy + y * a.zoom, nullptr);
                LineTo(mem, g.ox + g.iw, g.oy + y * a.zoom);
            }
            SelectObject(mem, op);
            DeleteObject(pen);
        }
        RECT fr{g.ox - 1, g.oy - 1, g.ox + g.iw + 1, g.oy + g.ih + 1};
        FrameRect(mem, &fr, (HBRUSH)GetStockObject(BLACK_BRUSH));
    } else {
        SelectObject(mem, a.font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(190, 190, 195));
        DrawTextW(mem, L"Links ein Bild wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

bool to_image(App& a, int mx, int my, int& ix, int& iy) {
    Geo g = geometry(a);
    ix = floordiv(mx - g.ox, a.zoom);
    iy = floordiv(my - g.oy, a.zoom);
    const auto& img = a.ed->image();
    return a.ed->current() >= 0 && ix >= 0 && iy >= 0 && ix < img.width && iy < img.height;
}

LRESULT CALLBACK canvas_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_canvas(a, h); return 0;
        case WM_SIZE: update_scroll(a); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            int ix, iy;
            if (to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy)) {
                SetCapture(h);
                a.ed->stroke_begin(ix, iy);
                refresh(a);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int ix, iy;
            const bool in = to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy);
            a.hover_x = in ? ix : -1;
            a.hover_y = in ? iy : -1;
            a.hover_color = in ? a.ed->pick(ix, iy) : -1;
            a.hover_cell = in && a.ed->cell_size() ? iy / a.ed->cell_size() * a.ed->cell_columns() + ix / a.ed->cell_size() : -1;
            if (a.ed->drawing() && (wp & MK_LBUTTON)) {
                Geo g = geometry(a);
                a.ed->stroke_move(floordiv(GET_X_LPARAM(lp) - g.ox, a.zoom), floordiv(GET_Y_LPARAM(lp) - g.oy, a.zoom));
            }
            refresh(a);
            return 0;
        }
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            a.ed->stroke_end();
            refresh(a);
            return 0;
        case WM_RBUTTONDOWN: {
            int ix, iy;
            if (to_image(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ix, iy)) {
                a.ed->set_color(a.ed->pick(ix, iy));
                refresh(a);
            }
            return 0;
        }
        case WM_MOUSELEAVE: a.hover_x = -1; refresh(a); return 0;
        case WM_MOUSEWHEEL: {
            const int d = GET_WHEEL_DELTA_WPARAM(wp);
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                set_zoom(a, a.zoom + (d > 0 ? 1 : -1));
            } else if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) {
                a.scroll_x -= d / 2;
                update_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            } else {
                a.scroll_y -= d / 2;
                update_scroll(a);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            const bool horz = msg == WM_HSCROLL;
            SCROLLINFO si{sizeof si, SIF_ALL};
            GetScrollInfo(h, horz ? SB_HORZ : SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= a.px(16); break;
                case SB_LINEDOWN: pos += a.px(16); break;
                case SB_PAGEUP: pos -= int(si.nPage); break;
                case SB_PAGEDOWN: pos += int(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            (horz ? a.scroll_x : a.scroll_y) = pos;
            update_scroll(a);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// --------------------------------------------------------------------------------------------- palette

struct PaletteGeo {
    int x, y, cell;
};

PaletteGeo palette_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    const int m = a.px(10), text = a.px(52);
    const int size = std::max(16, std::min(int(r.right) - 2 * m, int(r.bottom) - 2 * m - text));
    return {m, m, size / 16};
}

void paint_palette(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_BTNFACE));
    PixelEditor& e = *a.ed;
    PaletteGeo g = palette_geo(a, h);
    const auto& pal = e.palette();
    for (int i = 0; i < 256; i++) {
        const auto& c = pal.rgb[size_t(i)];
        HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
        RECT r{g.x + (i % 16) * g.cell, g.y + (i / 16) * g.cell, g.x + (i % 16 + 1) * g.cell, g.y + (i / 16 + 1) * g.cell};
        FillRect(mem, &r, b);
        DeleteObject(b);
    }
    // selection frame: black outside, white inside
    const int s = e.color();
    RECT sr{g.x + (s % 16) * g.cell, g.y + (s / 16) * g.cell, g.x + (s % 16 + 1) * g.cell, g.y + (s / 16 + 1) * g.cell};
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
    InflateRect(&sr, 1, 1);
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
    InflateRect(&sr, -2, -2);
    FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
    // current colour
    const int ty = g.y + g.cell * 16 + a.px(12);
    const auto& c = pal.rgb[size_t(s)];
    HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
    RECT sw{g.x, ty, g.x + a.px(36), ty + a.px(36)};
    FillRect(mem, &sw, b);
    DeleteObject(b);
    FrameRect(mem, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
    std::wstring t1 = L"Farbe " + std::to_wstring(s), t2 = L"RGB " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]);
    TextOutW(mem, g.x + a.px(46), ty, t1.c_str(), int(t1.size()));
    TextOutW(mem, g.x + a.px(46), ty + a.px(18), t2.c_str(), int(t2.size()));
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK palette_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_palette(a, h); return 0;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            PaletteGeo g = palette_geo(a, h);
            const int cx = (GET_X_LPARAM(lp) - g.x) / g.cell, cy = (GET_Y_LPARAM(lp) - g.y) / g.cell;
            if (GET_X_LPARAM(lp) >= g.x && GET_Y_LPARAM(lp) >= g.y && cx >= 0 && cy >= 0 && cx < 16 && cy < 16) {
                if (msg == WM_LBUTTONDOWN) {
                    a.ed->set_color(cy * 16 + cx);
                    refresh(a);
                } else {
                    const auto& c = a.ed->palette().rgb[size_t(cy * 16 + cx)];
                    std::wstring s = L"Farbe " + std::to_wstring(cy * 16 + cx) + L"   RGB " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]);
                    SendMessage(a.status, SB_SETTEXT, 0, (LPARAM)s.c_str());
                }
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// ------------------------------------------------------------------------------------------- team editor

std::string narrow(const wchar_t* w, int n = -1) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(0, len)), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::wstring window_text(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(size_t(n));
    return s;
}

COLORREF jersey_color(int r, int g, int b) {  // same scaling as the DAC: six bits, widened to eight
    auto c = [](int n) { const int v = n * 4; return (v << 2) | (v >> 4); };
    return RGB(c(r), c(g), c(b));
}

void create_team_controls(App& a) {
    TeamUi& u = a.tu;
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id) {
        HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | style, 0, 0, 0, 0, a.main, (HMENU)UINT_PTR(id), a.inst, nullptr);
        u.all.push_back(w);
        return w;
    };
    const DWORD edit = ES_AUTOHSCROLL | WS_TABSTOP | ES_UPPERCASE;
    u.lbl_club = make(L"STATIC", L"Verein", 0, 0, 0);
    u.club = make(L"EDIT", L"", edit, WS_EX_CLIENTEDGE, IDC_CLUB);
    SendMessage(u.club, EM_LIMITTEXT, TeamDoc::kClubLimit, 0);
    u.lbl_players = make(L"STATIC", L"Spieler (Ausländer = Haken)", 0, 0, 0);
    for (int i = 0; i < 25; i++) {
        u.name[i] = make(L"EDIT", L"", edit, WS_EX_CLIENTEDGE, IDC_NAME0 + i);
        SendMessage(u.name[i], EM_LIMITTEXT, TeamDoc::kNameLimit, 0);
        u.foreign[i] = make(L"BUTTON", L"Ausl.", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_FOR0 + i);
    }
    auto numeric = [&](HWND& e, HWND& ud, int id, int lo, int hi) {
        e = make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, id);
        ud = make(UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0);
        SendMessage(ud, UDM_SETBUDDY, (WPARAM)e, 0);
        SendMessage(ud, UDM_SETRANGE32, lo, hi);
    };
    u.lbl_crest = make(L"STATIC", L"Wappen (0–63)", 0, 0, 0);
    numeric(u.crest, u.crest_ud, IDC_CREST, 0, 63);
    u.lbl_place = make(L"STATIC", L"Platz in der Liga (1–12)", 0, 0, 0);
    numeric(u.place, u.place_ud, IDC_PLACE, 0, 12);
    const wchar_t* sn[] = {L"Stärke 1", L"Stärke 2", L"Stärke 3"};
    for (int i = 0; i < 3; i++) {
        u.lbl_str[i] = make(L"STATIC", sn[i], 0, 0, 0);
        numeric(u.str[i], u.str_ud[i], IDC_STR0 + i, 1, 99);
    }
    u.lbl_jersey = make(L"STATIC", L"Trikotfarbe der Torszenen", 0, 0, 0);
    for (int i = 0; i < 3; i++) {
        u.jer_lbl[i] = make(L"STATIC", L"", 0, 0, 0);
        u.jer[i] = make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, IDC_JER0 + i);
        SendMessage(u.jer[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 15));
    }
    u.preview = make(L"STATIC", L"", SS_OWNERDRAW, WS_EX_CLIENTEDGE, 0);
    u.lbl_swap = make(L"STATIC", L"Mannschaft tauschen mit", 0, 0, 0);
    u.swap_combo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_SWAP_COMBO);
    u.swap_btn = make(L"BUTTON", L"Tauschen", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_SWAP_BTN);
    for (HWND w : u.all) SendMessage(w, WM_SETFONT, (WPARAM)a.font, TRUE);
}

void show_team(App& a, bool on) {
    for (HWND w : a.tu.all) ShowWindow(w, on ? SW_SHOW : SW_HIDE);
}

void layout_team(App& a, int x, int y, int w, int h, int rx, int rw) {
    TeamUi& u = a.tu;
    (void)h;
    if (!u.club) return;
    const int m = a.px(14), rowh = a.px(28), eh = a.px(23);
    MoveWindow(u.lbl_club, x + m, y + m + a.px(3), a.px(50), eh, TRUE);
    MoveWindow(u.club, x + m + a.px(54), y + m, a.px(280), eh, TRUE);
    MoveWindow(u.lbl_players, x + m, y + m + rowh + a.px(8), a.px(300), eh, TRUE);
    const int colw = (w - 3 * m) / 2, top = y + m + 2 * rowh + a.px(8);
    for (int i = 0; i < 25; i++) {
        const int col = i < 13 ? 0 : 1, row = i < 13 ? i : i - 13;
        const int cx = x + m + col * (colw + m), cy = top + row * rowh;
        MoveWindow(u.name[i], cx, cy, colw - a.px(64), eh, TRUE);
        MoveWindow(u.foreign[i], cx + colw - a.px(58), cy, a.px(58), eh, TRUE);
    }
    // right column
    const int cx = rx + a.px(12), cw = rw - a.px(24), nw = a.px(70);
    int cy = y + m;
    MoveWindow(u.preview, cx, cy, cw, a.px(130), TRUE);
    cy += a.px(142);
    auto row = [&](HWND lbl, HWND e, HWND ud) {
        MoveWindow(lbl, cx, cy + a.px(3), cw - nw - a.px(8), eh, TRUE);
        MoveWindow(e, cx + cw - nw, cy, nw, eh, TRUE);
        SendMessage(ud, UDM_SETBUDDY, (WPARAM)e, 0);  // re-attach: the arrows follow the edit
        ShowWindow(ud, team_tab(a) ? SW_SHOW : SW_HIDE);
        cy += rowh;
    };
    row(u.lbl_crest, u.crest, u.crest_ud);
    row(u.lbl_place, u.place, u.place_ud);
    for (int i = 0; i < 3; i++) row(u.lbl_str[i], u.str[i], u.str_ud[i]);
    cy += a.px(6);
    MoveWindow(u.lbl_jersey, cx, cy, cw, eh, TRUE);
    cy += a.px(22);
    for (int i = 0; i < 3; i++) {
        MoveWindow(u.jer_lbl[i], cx, cy + a.px(4), a.px(60), eh, TRUE);
        MoveWindow(u.jer[i], cx + a.px(60), cy, cw - a.px(60), a.px(28), TRUE);
        cy += a.px(30);
    }
    cy += a.px(10);
    MoveWindow(u.lbl_swap, cx, cy, cw, eh, TRUE);
    cy += a.px(22);
    MoveWindow(u.swap_combo, cx, cy, cw - a.px(86), a.px(300), TRUE);
    MoveWindow(u.swap_btn, cx + cw - a.px(80), cy - 1, a.px(80), eh + 2, TRUE);
}

void fill_swap_combo(App& a) {
    HWND c = a.tu.swap_combo;
    SendMessage(c, CB_RESETCONTENT, 0, 0);
    for (int t = 0; t < TeamDoc::kTeams; t++) {
        if (!TeamDoc::editable(t) || t == a.team_sel) continue;
        const std::wstring s = std::to_wstring(t + 1) + L"  " + widen(a.team->club_name(t));
        const LRESULT i = SendMessage(c, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessage(c, CB_SETITEMDATA, WPARAM(i), t);
    }
    SendMessage(c, CB_SETCURSEL, 0, 0);
}

void update_team_labels(App& a) {
    const wchar_t* n[] = {L"Rot", L"Grün", L"Blau"};
    for (int i = 0; i < 3; i++) {
        const int v = int(SendMessage(a.tu.jer[i], TBM_GETPOS, 0, 0));
        SetWindowTextW(a.tu.jer_lbl[i], (std::wstring(n[i]) + L" " + std::to_wstring(v)).c_str());
    }
    InvalidateRect(a.tu.preview, nullptr, FALSE);
}

void load_team_controls(App& a) {
    if (!a.team || !a.tu.club) return;
    TeamUi& u = a.tu;
    const int t = a.team_sel;
    const em::TeamData& d = a.team->data(t);
    a.loading_team = true;
    SetWindowTextW(u.club, widen(a.team->club_name(t)).c_str());
    for (int i = 0; i < 25; i++) {
        SetWindowTextW(u.name[i], widen(a.team->player_name(t, i)).c_str());
        SendMessage(u.foreign[i], BM_SETCHECK, a.team->foreign(t, i) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    SetWindowTextW(u.crest, std::to_wstring(std::max(0, d.crest)).c_str());
    SetWindowTextW(u.place, std::to_wstring(std::max(0, d.place)).c_str());
    EnableWindow(u.place, d.place >= 0);
    EnableWindow(u.place_ud, d.place >= 0);
    for (int i = 0; i < 3; i++) SetWindowTextW(u.str[i], std::to_wstring(d.strength[size_t(i)]).c_str());
    const auto rgb = TeamDoc::jersey_rgb(d);
    for (int i = 0; i < 3; i++) SendMessage(u.jer[i], TBM_SETPOS, TRUE, rgb[size_t(i)]);
    update_team_labels(a);
    fill_swap_combo(a);
    a.loading_team = false;
}

bool team_save(App& a) {
    const auto dups = a.team->duplicate_names();
    if (!dups.empty()) {
        std::wstring list;
        for (size_t i = 0; i < dups.size() && i < 8; i++) list += (i ? L", " : L"") + widen(dups[i]);
        if (dups.size() > 8) list += L", …";
        std::wstring text = L"Diese Namen kommen mehrfach vor: " + list + L".\nDas Spiel behandelt gleiche Namen als dieselbe Person. Trotzdem speichern?";
        if (MessageBoxW(a.main, text.c_str(), L"Doppelte Namen", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    }
    return a.team->save();
}

int list_row_of(const App& a, int team) {
    return int(std::find(a.list_numbers.begin(), a.list_numbers.end(), team) - a.list_numbers.begin());
}

void team_edit_changed(App& a, int id) {
    if (a.loading_team || !a.team) return;
    TeamUi& u = a.tu;
    const int t = a.team_sel;
    auto number = [&](HWND e, int dflt) {
        const std::wstring s = window_text(e);
        return s.empty() ? dflt : _wtoi(s.c_str());
    };
    if (id == IDC_CLUB) {
        if (a.team->set_club_name(t, narrow(window_text(u.club).c_str()))) {
            const std::wstring label = std::to_wstring(t + 1) + L"  " + widen(a.team->club_name(t));
            ListView_SetItemText(a.list, list_row_of(a, t), 0, const_cast<wchar_t*>(label.c_str()));
        }
    } else if (id >= IDC_NAME0 && id < IDC_NAME0 + 25) {
        a.team->set_player_name(t, id - IDC_NAME0, narrow(window_text(u.name[id - IDC_NAME0]).c_str()));
    } else if (id == IDC_CREST) {
        a.team->set_crest(t, number(u.crest, a.team->data(t).crest));
        InvalidateRect(u.preview, nullptr, FALSE);
    } else if (id == IDC_PLACE) {
        a.team->set_place(t, number(u.place, 0));
        const int p = a.team->data(t).place;
        const std::wstring text = p >= 0 ? std::to_wstring(p) : L"–";
        ListView_SetItemText(a.list, list_row_of(a, t), 1, const_cast<wchar_t*>(text.c_str()));
    } else if (id >= IDC_STR0 && id < IDC_STR0 + 3) {
        a.team->set_strength(t, id - IDC_STR0, number(u.str[id - IDC_STR0], a.team->data(t).strength[size_t(id - IDC_STR0)]));
    }
    refresh(a);
}

void team_jersey_changed(App& a) {
    if (a.loading_team || !a.team) return;
    const int r = int(SendMessage(a.tu.jer[0], TBM_GETPOS, 0, 0)), g = int(SendMessage(a.tu.jer[1], TBM_GETPOS, 0, 0)),
              b = int(SendMessage(a.tu.jer[2], TBM_GETPOS, 0, 0));
    a.team->set_jersey(a.team_sel, r, g, b);
    update_team_labels(a);
    refresh(a);
}

void team_swap(App& a) {
    const LRESULT i = SendMessage(a.tu.swap_combo, CB_GETCURSEL, 0, 0);
    if (i < 0) return;
    const int other = int(SendMessage(a.tu.swap_combo, CB_GETITEMDATA, WPARAM(i), 0));
    a.team->swap_teams(a.team_sel, other);
    fill_list(a);
    select_in_list(a, a.team_sel);
    load_team_controls(a);
    refresh(a);
}

void paint_team_preview(App& a, DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
    if (!a.team) return;
    const em::TeamData& d = a.team->data(a.team_sel);
    if (!a.crest_pal_ok) {
        try {
            a.crest_pal = em::decode_palette(em::read_file(a.data / "KB" / "21.vga"));
            a.crest_pal_ok = true;
        } catch (const std::exception&) {
        }
    }
    if (d.crest >= 0 && a.crest_pal_ok && a.crest_no != d.crest) {
        try {
            a.crest_img = em::decode_vga(em::read_file(a.data / "KB" / (std::to_string(80 + d.crest) + ".vga")));
            a.crest_no = d.crest;
        } catch (const std::exception&) {
            a.crest_no = -1;
            a.crest_img = em::Image();
        }
    }
    if (a.crest_no >= 0 && a.crest_pal_ok && a.crest_no == d.crest) {
        const int s = std::max(1, std::min(int(r.bottom - r.top - a.px(16)) / std::max(1, a.crest_img.height), 4));
        std::vector<uint32_t> bits(a.crest_img.pixels.size());
        for (size_t i = 0; i < bits.size(); i++) {
            const auto& c = a.crest_pal.rgb[a.crest_img.pixels[i]];
            bits[i] = uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = a.crest_img.width;
        bi.bmiHeader.biHeight = -a.crest_img.height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, r.left + a.px(10), r.top + a.px(8), a.crest_img.width * s, a.crest_img.height * s, 0, 0, a.crest_img.width,
                      a.crest_img.height, bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    // jersey colour as the goal scenes show it
    const auto rgb = TeamDoc::jersey_rgb(d);
    HBRUSH b = CreateSolidBrush(jersey_color(rgb[0], rgb[1], rgb[2]));
    RECT sw{r.right - a.px(64), r.top + a.px(8), r.right - a.px(10), r.top + a.px(62)};
    FillRect(dc, &sw, b);
    DeleteObject(b);
    FrameRect(dc, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SelectObject(dc, a.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    TextOutW(dc, sw.left, sw.bottom + a.px(4), L"Trikot", 6);
}

// ------------------------------------------------------------------------------------------ palette editor

struct GridGeo {
    int x, y, cell;
};

GridGeo grid_geo(App& a, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    const int m = a.px(16), text = a.px(50);
    const int size = std::max(32, std::min(int(r.right) - 2 * m, int(r.bottom) - 2 * m - text));
    const int cell = size / 16;
    return {(int(r.right) - cell * 16) / 2, m, cell};
}

void paint_pgrid(App& a, HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &cr, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(mem, a.font);
    SetBkMode(mem, TRANSPARENT);
    if (a.pal->current() >= 0) {
        GridGeo g = grid_geo(a, h);
        const em::Palette p = a.pal->palette();
        for (int i = 0; i < 256; i++) {
            const auto& c = p.rgb[size_t(i)];
            HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
            RECT r{g.x + (i % 16) * g.cell, g.y + (i / 16) * g.cell, g.x + (i % 16 + 1) * g.cell, g.y + (i / 16 + 1) * g.cell};
            FillRect(mem, &r, b);
            DeleteObject(b);
        }
        const int s = a.pal_sel;
        RECT sr{g.x + (s % 16) * g.cell, g.y + (s / 16) * g.cell, g.x + (s % 16 + 1) * g.cell, g.y + (s / 16 + 1) * g.cell};
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(WHITE_BRUSH));
        InflateRect(&sr, 1, 1);
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        InflateRect(&sr, -2, -2);
        FrameRect(mem, &sr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        const auto& c = p.rgb[size_t(s)];
        const auto six = a.pal->get(s);
        const int ty = g.y + g.cell * 16 + a.px(12);
        HBRUSH b = CreateSolidBrush(RGB(c[0], c[1], c[2]));
        RECT sw{g.x, ty, g.x + a.px(40), ty + a.px(32)};
        FillRect(mem, &sw, b);
        DeleteObject(b);
        FrameRect(mem, &sw, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring t1 = L"Farbe " + std::to_wstring(s) + L"   DAC " + std::to_wstring(six[0]) + L", " + std::to_wstring(six[1]) + L", " + std::to_wstring(six[2]) +
                          L"   (Anzeige " + std::to_wstring(c[0]) + L", " + std::to_wstring(c[1]) + L", " + std::to_wstring(c[2]) + L")";
        TextOutW(mem, g.x + a.px(50), ty + a.px(7), t1.c_str(), int(t1.size()));
    } else {
        SetTextColor(mem, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(mem, L"Palette links wählen", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

LRESULT CALLBACK pgrid_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_pgrid(a, h); return 0;
        case WM_LBUTTONDOWN: {
            if (a.pal->current() < 0) return 0;
            GridGeo g = grid_geo(a, h);
            const int x = GET_X_LPARAM(lp) - g.x, y = GET_Y_LPARAM(lp) - g.y;
            if (x >= 0 && y >= 0 && x / g.cell < 16 && y / g.cell < 16) {
                a.pal_sel = y / g.cell * 16 + x / g.cell;
                sync_sliders(a);
                refresh(a);
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void slider_changed(App& a) {
    if (a.setting_sliders || a.pal->current() < 0) return;
    const int r = int(SendMessage(a.track[0], TBM_GETPOS, 0, 0)), g = int(SendMessage(a.track[1], TBM_GETPOS, 0, 0)),
              b = int(SendMessage(a.track[2], TBM_GETPOS, 0, 0));
    a.pal->set(a.pal_sel, r, g, b);
    sync_sliders(a);
    refresh(a);
}

// ------------------------------------------------------------------------------------------ main window

void build_menu(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu(), edit = CreatePopupMenu(), tool = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE, L"&Speichern\tStrg+S");
    AppendMenuW(file, MF_STRING, ID_FILE_EXPORT, L"Als &BMP exportieren…\tStrg+E");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"&Beenden\tAlt+F4");
    AppendMenuW(edit, MF_STRING, ID_EDIT_UNDO, L"&Rückgängig\tStrg+Z");
    AppendMenuW(edit, MF_STRING, ID_EDIT_REDO, L"&Wiederholen\tStrg+Y");
    AppendMenuW(tool, MF_STRING, ID_TOOL_PENCIL, L"&Stift\tP");
    AppendMenuW(tool, MF_STRING, ID_TOOL_LINE, L"&Linie\tL");
    AppendMenuW(tool, MF_STRING, ID_TOOL_RECT, L"&Rechteck\tR");
    AppendMenuW(tool, MF_STRING, ID_TOOL_FILLRECT, L"&Gefülltes Rechteck\tB");
    AppendMenuW(tool, MF_STRING, ID_TOOL_FILL, L"&Füllen\tF");
    AppendMenuW(tool, MF_STRING, ID_TOOL_PICKER, L"&Pipette\tI");
    AppendMenuW(view, MF_STRING, ID_VIEW_GRID, L"&Raster\tG");
    AppendMenuW(view, MF_STRING, ID_VIEW_FIT, L"Größe &anpassen");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMIN, L"&Vergrößern\t+");
    AppendMenuW(view, MF_STRING, ID_VIEW_ZOOMOUT, L"&Verkleinern\t-");
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&Info");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&Datei");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Bearbeiten");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tool, L"&Werkzeug");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&Ansicht");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Hilfe");
    SetMenu(hwnd, bar);
}

void build_toolbar(App& a) {
    a.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER,
                                0, 0, 0, 0, a.main, (HMENU)IDC_TOOLBAR, a.inst, nullptr);
    SendMessage(a.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(a.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    auto btn = [](int id, const wchar_t* text, BYTE style) {
        TBBUTTON b{};
        b.iBitmap = I_IMAGENONE;
        b.idCommand = id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BYTE(style | BTNS_SHOWTEXT | BTNS_AUTOSIZE);
        b.iString = (INT_PTR)text;
        return b;
    };
    auto sep = [] { TBBUTTON b{}; b.fsStyle = BTNS_SEP; return b; };
    TBBUTTON bs[] = {
        btn(ID_TOOL_PENCIL, L"Stift", BTNS_CHECKGROUP),      btn(ID_TOOL_LINE, L"Linie", BTNS_CHECKGROUP),
        btn(ID_TOOL_RECT, L"Rechteck", BTNS_CHECKGROUP),     btn(ID_TOOL_FILLRECT, L"Gefüllt", BTNS_CHECKGROUP),
        btn(ID_TOOL_FILL, L"Füllen", BTNS_CHECKGROUP),       btn(ID_TOOL_PICKER, L"Pipette", BTNS_CHECKGROUP),
        sep(),
        btn(ID_EDIT_UNDO, L"Rückgängig", BTNS_BUTTON),       btn(ID_EDIT_REDO, L"Wiederholen", BTNS_BUTTON),
        sep(),
        btn(ID_FILE_SAVE, L"Speichern", BTNS_BUTTON),
        sep(),
        btn(ID_VIEW_GRID, L"Raster", BTNS_CHECK),            btn(ID_VIEW_FIT, L"Anpassen", BTNS_BUTTON),
    };
    SendMessage(a.toolbar, TB_ADDBUTTONS, UINT(sizeof bs / sizeof bs[0]), (LPARAM)bs);
}

void init_list(App& a) {
    ListView_SetExtendedListViewStyle(a.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(a.list, L"Explorer", nullptr);  // modern list look
    LVCOLUMNW col{LVCF_TEXT | LVCF_WIDTH};
    col.pszText = const_cast<wchar_t*>(L"Datei");
    col.cx = a.px(160);
    ListView_InsertColumn(a.list, 0, &col);
    col.pszText = const_cast<wchar_t*>(L"Größe");
    col.cx = a.px(90);
    ListView_InsertColumn(a.list, 1, &col);
    fill_list(a);
}

void command(App& a, UINT id) {
    PixelEditor& e = *a.ed;
    switch (id) {
        case ID_FILE_SAVE:
            if (team_tab(a)) team_save(a);
            else if (pal_tab(a)) a.pal->save();
            else e.save();
            break;
        case ID_FILE_EXPORT: {
            if (e.current() < 0) break;
            std::wstring p = dialog_path(L"Als BMP exportieren", false, true, L"kb" + std::to_wstring(e.current()) + L".bmp", a.main);
            if (!p.empty()) e.export_bmp(fs::path(p));
            break;
        }
        case ID_FILE_EXIT: SendMessage(a.main, WM_CLOSE, 0, 0); return;
        case ID_EDIT_UNDO:
            if (team_tab(a)) { a.team->revert(); fill_list(a); select_in_list(a, a.team_sel); load_team_controls(a); }  // teams: discard all changes
            else if (pal_tab(a)) { a.pal->revert(); sync_sliders(a); }  // palettes: discard all changes
            else e.undo();
            break;
        case ID_EDIT_REDO: e.redo(); break;
        case ID_TOOL_PENCIL: case ID_TOOL_LINE: case ID_TOOL_RECT: case ID_TOOL_FILLRECT: case ID_TOOL_FILL: case ID_TOOL_PICKER:
            e.set_tool(PixelEditor::Tool(id - ID_TOOL_PENCIL));
            break;
        case ID_VIEW_GRID: a.grid = !a.grid; InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_VIEW_FIT: fit_zoom(a); InvalidateRect(a.canvas, nullptr, FALSE); break;
        case ID_VIEW_ZOOMIN: set_zoom(a, a.zoom + 1); return;
        case ID_VIEW_ZOOMOUT: set_zoom(a, a.zoom - 1); return;
        case ID_HELP_ABOUT:
            MessageBoxW(a.main, L"OpenEM Editor\nBilder des Eishockey Manager (1993) bearbeiten.\n\nTeil von OpenEM (GPL-3.0).", L"Info", MB_OK | MB_ICONINFORMATION);
            break;
        default: break;
    }
    refresh(a);
}

LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = *g_app;
    switch (msg) {
        case WM_CREATE: {
            a.main = h;
            a.dpi = GetDpiForWindow(h);
            make_font(a);
            build_menu(h);
            build_toolbar(a);
            a.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h, (HMENU)IDC_STATUS, a.inst, nullptr);
            a.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, h,
                                     (HMENU)IDC_LIST, a.inst, nullptr);
            a.canvas = CreateWindowExW(WS_EX_CLIENTEDGE, kCanvasClass, nullptr, WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.palette = CreateWindowExW(0, kPaletteClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.psel = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, h, (HMENU)IDC_PALSEL, a.inst, nullptr);
            for (int n : a.ed->palette_choices()) {
                std::wstring label = n == 21 ? L"Palette: Spiel (KB21)" : n == 247 ? L"Palette: Torszenen (KB247)" : L"Palette: wie KB" + std::to_wstring(n);
                const LRESULT i = SendMessage(a.psel, CB_ADDSTRING, 0, (LPARAM)label.c_str());
                SendMessage(a.psel, CB_SETITEMDATA, WPARAM(i), n);
            }
            a.tab = CreateWindowExW(0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH, 0, 0, 0, 0, h, (HMENU)IDC_TAB, a.inst, nullptr);
            const wchar_t* tabs[] = {L"Bilder", L"Torszenen", L"Paletten", L"Mannschaften"};
            for (int i = 0; i < 4; i++) {
                TCITEMW ti{TCIF_TEXT};
                ti.pszText = const_cast<wchar_t*>(tabs[i]);
                TabCtrl_InsertItem(a.tab, i, &ti);
            }
            TabCtrl_SetItemSize(a.tab, a.px(120), a.px(26));
            a.pgrid = CreateWindowExW(WS_EX_CLIENTEDGE, kPalGridClass, nullptr, WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            a.pinfo = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
            for (int i = 0; i < 3; i++) {
                a.tlabel[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h, nullptr, a.inst, nullptr);
                a.track[i] = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, 0, 0, 0, h, (HMENU)UINT_PTR(IDC_TRACK_R + i), a.inst, nullptr);
                SendMessage(a.track[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 63));
                SendMessage(a.track[i], TBM_SETTICFREQ, 8, 0);
                SendMessage(a.track[i], TBM_SETPAGESIZE, 0, 4);
            }
            for (HWND c : {a.toolbar, a.status, a.list, a.tab, a.pinfo, a.psel, a.tlabel[0], a.tlabel[1], a.tlabel[2]}) SendMessage(c, WM_SETFONT, (WPARAM)a.font, TRUE);
            create_team_controls(a);
            init_list(a);
            return 0;
        }
        case WM_SIZE:
            if (a.canvas) layout(a);
            return 0;
        case WM_DPICHANGED: {
            a.dpi = HIWORD(wp);
            make_font(a);
            for (HWND c : {a.toolbar, a.status, a.list, a.tab, a.pinfo, a.psel, a.tlabel[0], a.tlabel[1], a.tlabel[2]}) SendMessage(c, WM_SETFONT, (WPARAM)a.font, TRUE);
            TabCtrl_SetItemSize(a.tab, a.px(110), a.px(26));
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {a.px(760), a.px(480)};
            return 0;
        }
        case WM_COMMAND: {
            const UINT id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDC_LIST) return 0;
            if (id == IDC_CLUB || (id >= IDC_NAME0 && id < IDC_NAME0 + 25) || id == IDC_CREST || id == IDC_PLACE || (id >= IDC_STR0 && id < IDC_STR0 + 3)) {
                if (code == EN_CHANGE) team_edit_changed(a, int(id));
                return 0;
            }
            if (id >= IDC_FOR0 && id < IDC_FOR0 + 25) {
                if (code == BN_CLICKED && !a.loading_team) {
                    a.team->set_foreign(a.team_sel, int(id) - IDC_FOR0, SendMessage(a.tu.foreign[id - IDC_FOR0], BM_GETCHECK, 0, 0) == BST_CHECKED);
                    refresh(a);
                }
                return 0;
            }
            if (id == IDC_SWAP_BTN) { team_swap(a); return 0; }
            if (id == IDC_SWAP_COMBO) return 0;
            if (id == IDC_PALSEL) {
                if (code == CBN_SELCHANGE) {
                    const LRESULT i = SendMessage(a.psel, CB_GETCURSEL, 0, 0);
                    if (i >= 0) a.ed->set_palette_source(int(SendMessage(a.psel, CB_GETITEMDATA, WPARAM(i), 0)));
                    refresh(a);
                }
                return 0;
            }
            command(a, id);
            return 0;
        }
        case WM_DRAWITEM:
            if (reinterpret_cast<DRAWITEMSTRUCT*>(lp)->hwndItem == a.tu.preview) paint_team_preview(a, reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED && !a.selecting) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED))
                    open_image(a, a.list_numbers[size_t(lv->iItem)]);
            }
            if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGING) return !confirm_discard(a);  // TRUE vetoes
            if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) switch_tab(a, TabCtrl_GetCurSel(a.tab));
            return 0;
        }
        case WM_HSCROLL:
            for (int i = 0; i < 3; i++) {
                if (HWND(lp) == a.track[i]) slider_changed(a);
                if (HWND(lp) == a.tu.jer[i]) team_jersey_changed(a);
            }
            return 0;
        case WM_SETFOCUS: SetFocus(pal_tab(a) ? a.pgrid : a.canvas); return 0;
        case WM_CLOSE:
            if (confirm_discard(a)) DestroyWindow(h);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

// ------------------------------------------------------------------------------------------- start-up

bool is_game_folder(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p / "EM.EXE", ec) && fs::is_directory(p / "KB", ec);
}

fs::path find_game_folder(const fs::path& exe_dir) {
    std::vector<fs::path> c;
    wchar_t env[MAX_PATH];
    if (GetEnvironmentVariableW(L"OPENEM_DATA", env, MAX_PATH)) c.push_back(env);
    for (fs::path up = exe_dir; !up.empty(); up = up.parent_path()) {
        c.push_back(up / "work" / "EM");
        if (up == up.parent_path()) break;
    }
    for (const fs::path& base : {exe_dir, fs::current_path()})
        for (const wchar_t* sub : {L"", L"EM", L"data"}) c.push_back(*sub ? base / sub : base);
    for (const auto& p : c)
        if (is_game_folder(p)) return p;
    return {};
}

void save_shot(HWND h, const wchar_t* path) {
    RECT r;
    GetWindowRect(h, &r);
    const int w = r.right - r.left, ht = r.bottom - r.top;
    HDC dc = GetDC(h);
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof bi.bmiHeader, w, ht, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(h, mem, PW_RENDERFULLCONTENT);
    BITMAPFILEHEADER fh{0x4D42, uint32_t(sizeof fh + sizeof bi.bmiHeader + size_t(w) * ht * 4), 0, 0, uint32_t(sizeof fh + sizeof bi.bmiHeader)};
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD n;
    WriteFile(f, &fh, sizeof fh, &n, nullptr);
    WriteFile(f, &bi.bmiHeader, sizeof bi.bmiHeader, &n, nullptr);
    WriteFile(f, bits, DWORD(size_t(w) * ht * 4), &n, nullptr);
    CloseHandle(f);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(h, dc);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

    App app;
    g_app = &app;
    app.inst = inst;
    fs::path data;
    int file = -1, tab_arg = -1;
    std::wstring shot;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring o = argv[i];
        if (o == L"--data") data = argv[i + 1];
        else if (o == L"--file") file = _wtoi(argv[i + 1]);
        else if (o == L"--shot") shot = argv[i + 1];
        else if (o == L"--tab") tab_arg = _wtoi(argv[i + 1]);
    }
    LocalFree(argv);
    if (data.empty()) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        data = find_game_folder(fs::path(exe).parent_path());
    }
    while (!is_game_folder(data)) {
        MessageBoxW(nullptr, L"Der OpenEM Editor braucht deine Kopie des Originalspiels.\nBitte den Ordner mit EM.EXE und KB wählen.", L"OpenEM Editor", MB_OK | MB_ICONINFORMATION);
        std::wstring p = dialog_path(L"Ordner des Eishockey Managers wählen", true, false, L"", nullptr);
        if (p.empty()) return 1;
        data = p;
    }
    try {
        app.ed = std::make_unique<PixelEditor>(data);
        app.pal = std::make_unique<PaletteDoc>(data);
        app.team = std::make_unique<TeamDoc>(data);
        app.team->load();
        app.data = data;
    } catch (const std::exception& e) {
        MessageBoxW(nullptr, widen(e.what()).c_str(), L"OpenEM Editor", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainClass;
    wc.lpfnWndProc = main_proc;
    RegisterClassExW(&wc);
    wc.hIcon = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCanvasClass;
    wc.lpfnWndProc = canvas_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kPaletteClass;
    wc.lpfnWndProc = palette_proc;
    RegisterClassExW(&wc);
    wc.lpszClassName = kPalGridClass;
    wc.lpfnWndProc = pgrid_proc;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kMainClass, L"OpenEM Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // size for the real DPI
    const UINT dpi = GetDpiForWindow(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, MulDiv(1180, int(dpi), 96), MulDiv(760, int(dpi), 96), SWP_NOMOVE | SWP_NOZORDER);
    layout(app);
    int start_tab = 0;
    if (tab_arg >= 0) start_tab = tab_arg;
    for (const auto& f : app.ed->files())
        if (f.number == file && f.kind != PixelEditor::Kind::Image) start_tab = 1;
    app.tab_index = start_tab;
    TabCtrl_SetCurSel(app.tab, start_tab);
    switch_tab(app, start_tab);
    if (file >= 0 && start_tab < 2) {
        app.ed->load(file);
        fit_zoom(app);
        select_in_list(app, file);
    } else if (file >= 0) {
        app.pal->load(file);
        sync_sliders(app);
        select_in_list(app, file);
    }
    refresh(app);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'S', ID_FILE_SAVE},   {FCONTROL | FVIRTKEY, 'E', ID_FILE_EXPORT}, {FCONTROL | FVIRTKEY, 'Z', ID_EDIT_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_EDIT_REDO},   {FVIRTKEY, 'P', ID_TOOL_PENCIL},           {FVIRTKEY, 'L', ID_TOOL_LINE},
        {FVIRTKEY, 'R', ID_TOOL_RECT},              {FVIRTKEY, 'B', ID_TOOL_FILLRECT},          {FVIRTKEY, 'F', ID_TOOL_FILL},
        {FVIRTKEY, 'I', ID_TOOL_PICKER},            {FVIRTKEY, 'G', ID_VIEW_GRID},              {FVIRTKEY, VK_ADD, ID_VIEW_ZOOMIN},
        {FVIRTKEY, VK_OEM_PLUS, ID_VIEW_ZOOMIN},    {FVIRTKEY, VK_SUBTRACT, ID_VIEW_ZOOMOUT},   {FVIRTKEY, VK_OEM_MINUS, ID_VIEW_ZOOMOUT},
    };
    HACCEL accel = CreateAcceleratorTableW(keys, int(sizeof keys / sizeof keys[0]));

    if (!shot.empty()) {
        for (int i = 0; i < 20; i++) {
            MSG m;
            while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
            Sleep(20);
        }
        save_shot(hwnd, shot.c_str());
        return 0;
    }
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAccelerator(hwnd, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return int(msg.wParam);
}
