// SPDX-License-Identifier: GPL-3.0-only
// Tests of the asset editor logic (no window): drawing tools, undo/redo, saving with backup.
#include <cstdio>
#include <filesystem>

#include "editor/palette_doc.hpp"
#include "editor/map_doc.hpp"
#include "editor/pixel_editor.hpp"
#include "editor/team_doc.hpp"
#include "formats/tore.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::editor;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                                \
        }                                                                              \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("editor_tests: no data folder, skipped\n");
        return 0;
    }
    const fs::path src = argv[1], tmp = fs::temp_directory_path() / "openem_editor_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "KB");
    for (const char* n : {"21.vga", "80.vga", "5.vga", "231.vga", "233.vga", "235.vga", "246.vga", "247.vga", "23.vga", "8.vga", "14.vga", "232.vga", "236.vga", "242.vga"}) fs::copy_file(src / "KB" / n, tmp / "KB" / n);

    PixelEditor ed(tmp);
    CHECK(ed.load(80));
    const Image before = ed.image();
    CHECK(before.width == 38 && before.height == 38);

    ed.set_tool(PixelEditor::Tool::Pencil);
    ed.set_color(2);
    ed.stroke_begin(0, 0);
    ed.stroke_move(5, 0);
    ed.stroke_end();
    CHECK(ed.dirty());
    for (int x = 0; x <= 5; x++) CHECK(ed.image().pixels[size_t(x)] == 2);

    ed.undo();
    CHECK(ed.image().pixels == before.pixels);
    ed.redo();
    CHECK(ed.image().pixels[0] == 2);

    ed.set_tool(PixelEditor::Tool::FilledRect);
    ed.set_color(9);
    ed.stroke_begin(0, 10);
    ed.stroke_move(3, 12);
    ed.stroke_end();
    CHECK(ed.image().pixels[size_t(11) * 38 + 2] == 9 && ed.image().pixels[size_t(12) * 38 + 3] == 9);

    ed.set_tool(PixelEditor::Tool::Fill);
    ed.set_color(3);
    ed.stroke_begin(37, 37);  // fill the background
    CHECK(ed.image().pixels[size_t(38) * 38 - 1] == 3);

    const Image edited = ed.image();
    CHECK(ed.save());
    CHECK(!ed.dirty());
    CHECK(fs::exists(tmp / "KB" / "80.vga.bak"));
    CHECK(ed.load(5) && ed.load(80));
    CHECK(ed.image().pixels == edited.pixels);

    // a picture with its own palette keeps it
    CHECK(ed.load(5));
    const Image screen = ed.image();
    CHECK(ed.save() == true);
    CHECK(ed.load(5) && ed.image().pixels == screen.pixels);

    CHECK(ed.export_bmp(tmp / "x.bmp") && fs::file_size(tmp / "x.bmp") == 14 + 40 + 1024 + size_t((screen.width + 3) / 4 * 4) * screen.height);

    // tile / sprite sheets and the panel: unchanged save keeps every pixel, an edit lands in the right cell
    {
        using Kind = PixelEditor::Kind;
        CHECK(ed.load(231));
        CHECK(ed.kind() == Kind::Tiles && ed.cell_size() == 16 && ed.cell_columns() == 16);
        CHECK(ed.image().width == 256 && ed.image().height == 256);
        const Bytes tiles_before = unpack_kb_block(read_file(tmp / "KB" / "231.vga"));
        ed.set_tool(PixelEditor::Tool::Pencil);
        ed.set_color(5);
        ed.stroke_begin(16 * 3 + 2, 16 * 2 + 1);  // tile 2 * 16 + 3 = 35, pixel (2, 1)
        ed.stroke_end();
        CHECK(ed.dirty() && ed.save());
        const Bytes tiles_after = unpack_kb_block(read_file(tmp / "KB" / "231.vga"));
        CHECK(tiles_after.size() == tiles_before.size());
        size_t changed = 0;
        for (size_t i = 0; i < tiles_after.size(); i++) changed += tiles_after[i] != tiles_before[i];
        CHECK(tiles_after[35 * 256 + 1 * 16 + 2] == 5 && changed <= 1);

        CHECK(ed.load(233) && ed.kind() == Kind::Sprites && ed.cell_size() == 32);
        const Image sp = ed.image();
        CHECK(ed.save() && ed.load(233) && ed.image().pixels == sp.pixels);
        CHECK(decode_tor_sprites(read_file(tmp / "KB" / "233.vga")).size() == 40);
        CHECK(ed.load(235) && ed.image().width == 256 && ed.image().height == 64);
        CHECK(ed.save() && decode_tor_sprites(read_file(tmp / "KB" / "235.vga")).size() == 12);

        CHECK(ed.load(246) && ed.kind() == Kind::Panel);
        const Image panel = ed.image();
        CHECK(panel.width == 320 && panel.height == 40);
        CHECK(ed.save() && ed.load(246) && ed.image().pixels == panel.pixels);
        ed.close();
        CHECK(ed.current() < 0);
    }

    // palette editor: only the edited colour changes, the unused high bits stay
    {
        PaletteDoc pd(tmp);
        const Bytes orig = read_file(tmp / "KB" / "21.vga");
        CHECK(pd.load(21) && !pd.dirty());
        const auto c = pd.get(7);
        CHECK(c[0] == (orig[21] & 63));
        pd.set(7, 63, 0, 10);
        CHECK(pd.dirty() && pd.get(7)[0] == 63 && pd.get(7)[2] == 10);
        pd.set(255, 1, 2, 3);  // not stored: ignored
        CHECK(pd.save() && !pd.dirty());
        const Bytes now = read_file(tmp / "KB" / "21.vga");
        CHECK(now.size() == 765);
        size_t diff = 0;
        for (size_t i = 0; i < now.size(); i++) diff += now[i] != orig[i];
        CHECK(diff <= 3 && (now[21] & 0xC0) == (orig[21] & 0xC0) && (now[21] & 63) == 63);
        CHECK(fs::exists(tmp / "KB" / "21.vga.bak"));
        pd.set(7, 0, 0, 0);
        pd.revert();
        CHECK(!pd.dirty() && pd.get(7)[0] == 63);
        CHECK(pd.load(247) && pd.palette().rgb[1] == decode_palette(read_file(tmp / "KB" / "247.vga")).rgb[1]);
    }

    // palette source: KB8 is drawn by the game over the palette of KB14; images with their own palette keep it
    {
        CHECK(ed.load(8) && ed.palette_source() == 14);
        const auto p14 = ed.palette().rgb[40];
        CHECK(ed.set_palette_source(21) && ed.palette_source() == 21 && ed.palette().rgb[40] != p14);
        CHECK(ed.set_palette_source(14) && ed.palette().rgb[40] == p14);
        CHECK(ed.load(80) && ed.palette_source() == 21);
        CHECK(ed.load(14) && ed.has_own_palette() && !ed.set_palette_source(21));
        CHECK(ed.load(231) && ed.palette_source() == 247);
    }

    // PNG-style import: exact palette colours map back to their (first matching) index, near colours snap
    {
        CHECK(ed.load(80));
        const Image before = ed.image();
        std::vector<uint32_t> px(before.pixels.size());
        for (size_t i = 0; i < px.size(); i++) {
            const auto& c = ed.palette().rgb[before.pixels[i]];
            px[i] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        int approx = -1;
        CHECK(ed.import_argb(px, before.width, before.height, false, &approx) && approx == 0);
        // duplicate palette colours may map to another index with the same colour: compare as colours
        bool same = true;
        for (size_t i = 0; i < px.size(); i++) {
            const auto& a2 = ed.palette().rgb[ed.image().pixels[i]];
            const auto& b2 = ed.palette().rgb[before.pixels[i]];
            same = same && a2 == b2;
        }
        CHECK(same);
        ed.undo();
        CHECK(ed.image().pixels == before.pixels);
        // a colour that is not in the palette snaps to the nearest one; transparent -> 0
        std::vector<uint32_t> two(4, 0xFF000000u);
        two[0] = 0xFFFE0101u;
        two[1] = 0x00123456u;
        CHECK(ed.import_argb(two, 2, 2, true, &approx) && approx >= 1 && ed.image().width == 2 && ed.image().height == 2);
        CHECK(ed.image().pixels[1] == 0 && !ed.can_undo());
        CHECK(ed.load(80) && !ed.import_argb(two, 2, 2, false));  // other size without permission
        CHECK(ed.load(233) && !ed.import_argb(two, 2, 2, true));  // sheets must match exactly
    }

    // hall map editor
    {
        MapDoc md(tmp);
        CHECK(md.load(236) && md.current() == 236 && md.tile_count() == 500);
        const Image ref = decode_rink(read_file(tmp / "KB" / "231.vga"), read_file(tmp / "KB" / "232.vga"), read_file(tmp / "KB" / "236.vga"));
        CHECK(md.rink().width == ref.width && md.rink().height == ref.height && md.rink().pixels == ref.pixels);
        const Bytes before = read_file(tmp / "KB" / "236.vga");

        md.stroke_begin();
        md.set(3, 5, 7, false);
        md.set(4, 5, 7, true);
        md.stroke_end();
        CHECK(md.dirty() && md.tile(3, 5) == 7 && !md.mirrored(3, 5) && md.mirrored(4, 5));
        CHECK(md.rink().pixels[size_t(5 * 16) * md.rink().width + size_t(3 * 16)] == md.tile_image(7).pixels[0]);
        // mirrored cell shows the tile flipped top to bottom
        CHECK(md.rink().pixels[size_t(5 * 16) * md.rink().width + size_t(4 * 16)] == md.tile_image(7).pixels[size_t(15) * 16]);
        md.undo();
        CHECK(md.rink().pixels == ref.pixels && md.can_redo());
        md.redo();
        CHECK(md.tile(3, 5) == 7);

        md.fill(0, 0, 9, false);  // the whole connected region of the corner cell
        CHECK(md.tile(0, 0) == 9);
        md.undo();
        CHECK(md.save() && !md.dirty() && fs::exists(tmp / "KB" / "236.vga.bak"));
        const Bytes after = read_file(tmp / "KB" / "236.vga");
        CHECK(after.size() == before.size());
        size_t diff = 0;
        for (size_t i = 0; i < after.size(); i++) diff += after[i] != before[i];
        CHECK(diff > 0 && diff <= 4);  // two cells of two bytes
        MapDoc re(tmp);
        CHECK(re.load(236) && re.tile(3, 5) == 7 && re.mirrored(4, 5));
        CHECK(re.load(242) && re.current() == 242);
        md.stroke_begin();
        md.stroke_end();  // empty stroke leaves no undo step
        const bool had = md.can_undo();
        md.undo();
        (void)had;
    }

    // team editor
    {
        TeamDoc td(tmp);
        const Bytes orig = read_file(tmp / "KB" / "23.vga");
        CHECK(td.load() && td.club_name(8) == "MANNHEIMER ERC" && td.player_name(8, 0) == "SCHLICKENRIED");
        // foreigner byte = byte behind the name: KRENTZ (18) and LALA (19) of Mannheim, not FRITZ (20)
        CHECK(td.foreign(8, 18) && td.foreign(8, 19) && !td.foreign(8, 20));
        CHECK(!td.dirty() && TeamDoc::editable(47) && !TeamDoc::editable(48) && TeamDoc::editable(52));

        CHECK(td.save());  // unchanged: identical file
        CHECK(read_file(tmp / "KB" / "23.vga") == orig);

        CHECK(td.set_club_name(8, "mannheimer sc") && td.club_name(8) == "MANNHEIMER SC");
        CHECK(!td.set_club_name(8, "ZU LANGER VEREINSNAME MIT VIELEN ZEICHEN"));
        CHECK(td.set_player_name(8, 0, "Müller") && td.player_name(8, 0) == "MüLLER");
        CHECK(!td.set_player_name(8, 1, "ÅKE"));
        td.set_player_name(8, 0, "MÜLLER");
        td.set_foreign(8, 20, true);
        td.set_crest(8, 5);
        td.set_jersey(8, 15, 0, 3);
        td.set_strength(8, 0, 77);
        td.set_place(8, 4);
        CHECK(td.dirty() && td.save() && !td.dirty());
        TeamDoc re(tmp);
        CHECK(re.load() && re.club_name(8) == "MANNHEIMER SC" && re.player_name(8, 0) == "MÜLLER" && re.foreign(8, 20));
        CHECK(re.data(8).crest == 5 && re.data(8).strength[0] == 77 && re.data(8).place == 4 && re.data(8).jersey == 0x0F03);
        CHECK(re.foreign(8, 18) && re.player_name(8, 2) == td.player_name(8, 2));  // neighbours untouched

        const std::string n0 = re.club_name(0), n1 = re.club_name(1);
        const auto d0 = re.data(0), d1 = re.data(1);
        re.swap_teams(0, 1);
        CHECK(re.club_name(0) == n1 && re.club_name(1) == n0 && re.data(0).strength == d1.strength && re.data(1).crest == d0.crest);
        CHECK(re.save());
        TeamDoc re2(tmp);
        CHECK(re2.load() && re2.club_name(0) == n1 && re2.player_name(0, 0) == re.player_name(0, 0));
        const size_t dups = re2.duplicate_names().size();  // the original data has some already
        re2.set_player_name(2, 0, re2.player_name(3, 0));
        CHECK(re2.duplicate_names().size() == dups + 1 || re2.duplicate_names().size() == dups);
        re2.revert();
        CHECK(!re2.dirty() && re2.duplicate_names().size() == dups);
    }
// European clubs (teams 64..149): name, jersey and strengths        TeamDoc eu(tmp);        CHECK(eu.load() && eu.club_name(64) == "KLAGENFURTER AC" && !TeamDoc::has_players(64) && TeamDoc::editable(149));        CHECK(eu.set_club_name(64, "hc test") && eu.club_name(64) == "HC TEST");        CHECK(!eu.set_club_name(64, "EIN VIEL ZU LANGER EUROPAEISCHER NAME"));        eu.set_strength(64, 0, 55);        eu.set_jersey(64, 1, 2, 3);        CHECK(eu.save());        TeamDoc eu2(tmp);        CHECK(eu2.load() && eu2.club_name(64) == "HC TEST" && eu2.club_name(65) == eu.club_name(65) && eu2.data(64).strength[0] == 55 && eu2.data(64).jersey == 0x0123);        eu2.swap_teams(64, 65);        CHECK(eu2.club_name(65) == "HC TEST" && eu2.data(65).strength[0] == 55);        eu2.swap_teams(0, 64);  // different groups: ignored        CHECK(eu2.club_name(64) != "EISBÄREN BERLIN");

    fs::remove_all(tmp);
    if (failures) {
        std::printf("FAILED (%d failures)\n", failures);
        return 1;
    }
    std::printf("editor tests OK\n");
    return 0;
}
