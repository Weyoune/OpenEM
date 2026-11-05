// SPDX-License-Identifier: GPL-3.0-only
// Headless original-pixel goldens. No arguments: palette/invalid-input checks.
// With argv[1] = original EM directory: fonts and the actual KB21 palette.
// Expected values were independently extracted from original bytes/assembly;
// provenance: work/exp/day_20261103/font/NOTES.md and the native palette PNG.
#include "ui/classic_font.hpp"
#include "ui/classic_graph.hpp"
#include "ui/classic_preview.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

using namespace em;
using em::ui::ClassicFont;
using em::ui::ClassicFontId;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

template<class Action>
static bool throws_exception(Action action) {
    try { action(); }
    catch (const std::exception&) { return true; }
    return false;
}

static size_t count_color(const Image& image, uint8_t color) {
    return size_t(std::count(image.pixels.begin(), image.pixels.end(), color));
}

static void unit_tests() {
    // Original KB21 index1 carries E4 EC F0. Runtime native PNG establishes
    // RGB 92 B2 C3: this distinguishes DAC masking from 8-bit RGB and <<2-only.
    Bytes palette(765, 0);
    palette[3] = 228; palette[4] = 236; palette[5] = 240;
    CHECK((decode_palette(palette).rgb[1] == std::array<uint8_t, 3>{146, 178, 195}));
    palette[6] = 63; palette[7] = 64; palette[8] = 255;
    CHECK((decode_palette(palette).rgb[2] == std::array<uint8_t, 3>{255, 0, 255}));
    CHECK(throws_exception([] { decode_palette(Bytes(764, 0)); }));

    CHECK(throws_exception([] { ClassicFont font(Bytes{}); }));
    CHECK(throws_exception([] { ClassicFont font(Bytes{0x4d, 0x5a}); }));
    // Large enough to reach an actual header check, but not a DOS executable.
    CHECK(throws_exception([] { ClassicFont font(Bytes(0x10000, 0)); }));
}

struct UmlautGolden {
    std::string_view utf8;
    uint8_t cp437;
    std::array<int, 3> advances;
    std::array<size_t, 3> ink;
};

static void original_fonts(const fs::path& original_dir) {
    const Bytes exe = read_file(original_dir / "EM.EXE");
    const ClassicFont font(exe);
    constexpr std::array<ClassicFontId, 3> ids{
        ClassicFontId::Mini, ClassicFontId::Flow, ClassicFontId::NewBold};
    constexpr std::array<int, 3> heights{6, 8, 20};
    constexpr std::array<int, 3> descenders{1, 1, 4};
    constexpr std::array<int, 3> baselines{4, 6, 15};
    constexpr std::array<int, 3> a_advances{5, 8, 12};
    constexpr std::array<int, 3> space_advances{2, 4, 7};
    constexpr std::array<size_t, 3> a_ink{12, 26, 85};
    constexpr std::array<int, 3> word_advances{36, 51, 78}; // "Finanzen"
    const std::array<UmlautGolden, 7> umlauts{{
        {"\xc3\x84", 0x8e, {5, 8, 18}, {12, 30, 137}}, // Ä
        {"\xc3\x96", 0x99, {5, 7, 16}, {10, 28, 139}}, // Ö
        {"\xc3\x9c", 0x9a, {5, 7, 14}, {8, 24, 127}},  // Ü
        {"\xc3\xa4", 0x84, {5, 7, 12}, {10, 23, 74}},  // ä
        {"\xc3\xb6", 0x94, {5, 7, 12}, {8, 24, 70}},   // ö
        {"\xc3\xbc", 0x81, {5, 7, 10}, {9, 24, 62}},   // ü
        {"\xc3\x9f", 0xe1, {4, 8, 12}, {10, 28, 145}}  // ß
    }};

    CHECK(!font.available(ClassicFontId::Normal));
    CHECK(font.metrics(ClassicFontId::Normal).height == 16);
    CHECK(font.metrics(ClassicFontId::Normal).descender == 4);
    CHECK(throws_exception([&] { font.measure(ClassicFontId::Normal, "A"); }));
    Image untouched(8, 8, 7);
    CHECK(throws_exception([&] { font.draw(untouched, ClassicFontId::Normal, 0, 5, "A", 9); }));
    CHECK(count_color(untouched, 7) == 64);

    for (size_t f = 0; f < ids.size(); ++f) {
        const auto id = ids[f];
        CHECK(font.available(id));
        CHECK(font.metrics(id).height == heights[f]);
        CHECK(font.metrics(id).descender == descenders[f]);
        CHECK(font.metrics(id).baseline_offset() == baselines[f]);
        CHECK(font.measure(id, "A") == a_advances[f]);
        CHECK(font.measure(id, " ") == space_advances[f]);
        CHECK(font.measure(id, "Finanzen") == word_advances[f]);

        Image a(32, 32, 7);
        CHECK(font.draw(a, id, 1, baselines[f] + 1, "A", 9) == a_advances[f]);
        CHECK(count_color(a, 9) == a_ink[f]);
        CHECK(count_color(a, 7) == 1024 - a_ink[f]);
        Image space(32, 32, 7);
        CHECK(font.draw(space, id, 1, baselines[f] + 1, " ", 9) == space_advances[f]);
        CHECK(count_color(space, 7) == 1024);

        // CP437 aliases and UTF-8 must choose the very same native glyphs.
        // Each independent original glyph has a fixed width and ink count.
        for (const auto& golden : umlauts) {
            const std::string native(1, char(golden.cp437));
            CHECK(font.measure(id, golden.utf8) == golden.advances[f]);
            CHECK(font.measure_native(id, native) == golden.advances[f]);
            Image unicode_image(32, 32, 7), native_image(32, 32, 7);
            CHECK(font.draw(unicode_image, id, 1, baselines[f] + 1, golden.utf8, 9) == golden.advances[f]);
            CHECK(font.draw_native(native_image, id, 1, baselines[f] + 1, native, 9) == golden.advances[f]);
            CHECK(unicode_image.pixels == native_image.pixels);
            CHECK(count_color(unicode_image, 9) == golden.ink[f]);
        }
    }

    // Mini A's source rows are 60 90 90 F0 90 00. These points plus ink
    // count verify row order, bit order and baseline without a second decoder.
    Image transparent(8, 8, 7);
    CHECK(font.draw(transparent, ClassicFontId::Mini, 1, 5, "A", 9) == 5);
    CHECK(count_color(transparent, 9) == 12);
    CHECK(count_color(transparent, 7) == 52);
    CHECK(transparent.at(2, 1) == 9 && transparent.at(3, 1) == 9);
    CHECK(transparent.at(1, 1) == 7 && transparent.at(4, 1) == 7);
    CHECK(transparent.at(1, 4) == 9 && transparent.at(4, 4) == 9);
    CHECK(transparent.at(4, 5) == 9 && transparent.at(4, 6) == 7);

    Image opaque(8, 8, 7);
    CHECK(font.draw(opaque, ClassicFontId::Mini, 1, 5, "A", 9, true, 3) == 5);
    CHECK(count_color(opaque, 9) == 12);
    CHECK(count_color(opaque, 3) == 18);
    CHECK(count_color(opaque, 7) == 34);
    CHECK(opaque.at(1, 1) == 3 && opaque.at(5, 6) == 3 && opaque.at(6, 6) == 7);

    Image zero_ink(8, 8, 7);
    font.draw(zero_ink, ClassicFontId::Mini, 1, 5, "A", 0);
    CHECK(count_color(zero_ink, 7) == 64); // native transparent blit skips index0
    Image opaque_zero(8, 8, 7);
    font.draw(opaque_zero, ClassicFontId::Mini, 1, 5, "A", 0, true, 3);
    CHECK(count_color(opaque_zero, 0) == 12 && count_color(opaque_zero, 3) == 18);

    Image clipped(4, 2, 7);
    CHECK(font.draw(clipped, ClassicFontId::Mini, -2, 0, "A", 9) == 5);
    CHECK((clipped.pixels == std::vector<uint8_t>{7, 9, 7, 7, 7, 7, 7, 7}));
    Image beyond_right(4, 2, 7);
    CHECK(font.draw(beyond_right, ClassicFontId::Mini, 4, 0, "A", 9) == 5);
    CHECK(count_color(beyond_right, 7) == 8);

    // Original numeric alignment uses an empty digit-width percent glyph.
    Image numeric_pad(8, 8, 7);
    CHECK(font.draw_native(numeric_pad, ClassicFontId::Mini, 1, 5, "%", 9) == 5);
    CHECK(count_color(numeric_pad, 7) == 64);
    CHECK(font.measure_native(ClassicFontId::Mini, "%1") == 10);

    // Literal backslash maps to the backslash shape at5F, whereas native99
    // chooses Ö at5C. This catches accidental double ISO-646 conversion.
    CHECK(font.measure_native(ClassicFontId::Mini, "\\") == 4);
    CHECK(font.measure(ClassicFontId::Mini, "\xc3\x96") == 5);
    Image sharp_s(16, 12, 7), tilde_alias(16, 12, 7);
    font.draw(sharp_s, ClassicFontId::Mini, 1, 5, "\xc3\x9f", 9);
    font.draw_native(tilde_alias, ClassicFontId::Mini, 1, 5, "~", 9);
    CHECK(sharp_s.pixels == tilde_alias.pixels);

    // Truncation and wrong DOS header must fail through an ordinary C++
    // exception, before drawing. These are input-boundary checks, not fonts.
    Bytes truncated(exe.begin(), exe.begin() + std::min<size_t>(exe.size(), 0x1e00));
    CHECK(throws_exception([&] { ClassicFont invalid(truncated); }));
    // Original Flow glyphs7E/7F share file FB08: width + eight rows ends
    // at FB11 exclusive. Cutting at FB10 removes its final required row.
    Bytes missing_font_tail(exe.begin(), exe.begin() + std::min<size_t>(exe.size(), 0xfb10));
    CHECK(throws_exception([&] { ClassicFont invalid(missing_font_tail); }));
    Bytes bad_header = exe;
    bad_header.at(0) = 0;
    CHECK(throws_exception([&] { ClassicFont invalid(bad_header); }));
}

static void original_palette(const fs::path& original_dir) {
    const Bytes bytes = read_file(original_dir / "KB" / "21.vga");
    CHECK(bytes.size() == 765);
    CHECK((decode_palette(bytes).rgb[0] == std::array<uint8_t, 3>{0, 0, 0}));
    CHECK((decode_palette(bytes).rgb[1] == std::array<uint8_t, 3>{146, 178, 195}));
}

static bool same_region(const Image& actual, const Image& expected,
                        int left, int top, int right, int bottom) {
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            if (actual.pixels[size_t(y) * actual.width + x] !=
                expected.pixels[size_t(y) * expected.width + x]) return false;
    return true;
}

static void ranking_graph(const fs::path& original_dir) {
    using em::ui::ClassicCanvas;
    using em::ui::draw_classic_ranking_graph;
    ClassicCanvas canvas(original_dir);
    HokFile h;
    h.state.resize(0x5000, 0);
    h.matchday[0] = h.matchday[1] = h.matchday[2] = 2;
    const auto clear = [&] { canvas.rect(0, 0, 319, 239, 2); };

    //5cb4:1529..15c2: first completed round uses step85, ending at x90;
    // the labels use the terminated loop index n, placing them at x92.
    h.state[0x3bd9] = 2;          // own third place => y51
    h.state[0x3bd9 + 80] = 6;     // opponent seventh place => y59
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 1, true);
    CHECK(canvas.pixels().at(5, 46) == 6 && canvas.pixels().at(5, 69) == 6);
    CHECK(canvas.pixels().at(6, 70) == 8 && canvas.pixels().at(101, 70) == 8);
    CHECK(canvas.pixels().at(6, 51) == 24 && canvas.pixels().at(90, 51) == 24);
    CHECK(canvas.pixels().at(6, 59) == 7 && canvas.pixels().at(90, 59) == 7);
    CHECK(canvas.pixels().at(91, 51) == 2);
    Image labels(320, 240, 2);
    canvas.font().draw(labels, ClassicFontId::Mini, 92, 51, "3.", 24);
    canvas.font().draw(labels, ClassicFontId::Mini, 92, 59, "7.", 7);
    CHECK(same_region(canvas.pixels(), labels, 92, 42, 109, 69));
    Image caption(320, 240, 2);
    // Original Mini width41: native separate-halves centering yields38.
    canvas.font().draw(caption, ClassicFontId::Mini, 38, 39, "2.Spieltag", 7);
    CHECK(same_region(canvas.pixels(), caption, 6, 34, 110, 40));

    // Two rounds: packed A1 means ranks1 then10, not two low-nibble reads.
    h.matchday[0] = 3;
    h.state[0x3bd9] = 0xa1;
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 255, false);
    CHECK(canvas.pixels().at(6, 49) == 24 && canvas.pixels().at(47, 49) == 24);
    CHECK(canvas.pixels().at(89, 67) == 24);
    Image packed_label(320, 240, 2);
    canvas.font().draw(packed_label, ClassicFontId::Mini, 91, 67, "11.", 24);
    CHECK(same_region(canvas.pixels(), packed_label, 91, 60, 109, 69));

    //16b1..1741: overlapping sixth-place labels separate by moving only the
    // own label seven pixels upward. Opponent curve is drawn second.
    h.matchday[0] = 2;
    h.state[0x3bd9] = h.state[0x3bd9 + 80] = 5;
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 1, false);
    Image overlap(320, 240, 2);
    canvas.font().draw(overlap, ClassicFontId::Mini, 92, 50, "6.", 24);
    canvas.font().draw(overlap, ClassicFontId::Mini, 92, 57, "6.", 7);
    CHECK(same_region(canvas.pixels(), overlap, 92, 42, 109, 69));
    CHECK(canvas.pixels().at(90, 57) == 7);

    // End of44 regular rounds: step1 is promoted to2, and a rank10 label
    // moves left/up one pixel only after the manager's regular-season limit.
    std::fill_n(h.state.begin() + 0x3bd9, 22, uint8_t(0x99));
    h.matchday[0] = 45;
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 255, true);
    CHECK(canvas.pixels().at(93, 65) == 24);
    Image late_label(320, 240, 2);
    canvas.font().draw(late_label, ClassicFontId::Mini, 94, 64, "10.", 24);
    CHECK(same_region(canvas.pixels(), late_label, 94, 58, 109, 69));
    Image blank(320, 240, 2);
    CHECK(same_region(canvas.pixels(), blank, 6, 34, 110, 40));

    // National-team classification suppresses the whole graph, including
    // axes. FF still permits the exact pre-season sentence; FE suppresses it.
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 52, true);
    CHECK(count_color(canvas.pixels(), 2) == 320 * 240);
    h.matchday[0] = 1;
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 255, false);
    Image before_first(320, 240, 2);
    canvas.font().draw(before_first, ClassicFontId::Mini, 5, 39, "Vor dem ersten Spieltag.", 7);
    CHECK(same_region(canvas.pixels(), before_first, 1, 34, 110, 40));
    clear();
    draw_classic_ranking_graph(canvas, h, 0, 254, false);
    CHECK(same_region(canvas.pixels(), blank, 1, 34, 110, 40));

    h.matchday[0] = 2;
    h.state.clear();
    CHECK(throws_exception([&] { draw_classic_ranking_graph(canvas, h, 0, 255, false); }));
}

int main(int argc, char** argv) {
    try {
        Bytes prefix(0xB0,0xA5);
        std::copy_n("KRON_0.99",10,prefix.begin());
        prefix[0x0A]=0xC9;prefix[0x0B]=7;prefix[0x0C]=prefix[0x0D]=0;
        prefix[0x0E]=1;prefix[0x0F]=10;prefix[0x10]=0;prefix[0x11]=0;
        std::fill(prefix.begin()+0x12,prefix.begin()+0x38,uint8_t(0));
        std::copy_n("AGENT",5,prefix.begin()+0x12);
        const uint8_t club[]={0x4B,0x99,0x4C,0x4E,0x45,0x52,0x20,0x45,0x43};
        std::copy(std::begin(club),std::end(club),prefix.begin()+0x25);
        const auto preview=em::ui::decode_classic_preview(prefix);
        CHECK(preview.year==1993&&preview.matchday==10&&preview.managers.size()==1);
        CHECK(preview.managers[0].manager=="AGENT"&&preview.managers[0].club==std::string(reinterpret_cast<const char*>(club),9));
        auto malformed=prefix;malformed.resize(0xAF);
        CHECK(throws_exception([&]{em::ui::decode_classic_preview(malformed);}));
        malformed=prefix;malformed[0x0E]=5;
        CHECK(throws_exception([&]{em::ui::decode_classic_preview(malformed);}));
        malformed=prefix;malformed[0x10]=255;malformed[0x11]=1;
        CHECK(em::ui::decode_classic_preview(malformed).managers[0].world_cup);
        unit_tests();
        if (argc > 1) {
            original_fonts(argv[1]);
            original_palette(argv[1]);
            ranking_graph(argv[1]);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Classic tests: unexpected exception: %s\n", e.what());
        return 2;
    }
    if (failures != 0) {
        std::fprintf(stderr, "Classic tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("Classic font/palette goldens passed%s\n", argc > 1 ? " (original EM data)" : " (synthetic inputs)");
    return 0;
}
