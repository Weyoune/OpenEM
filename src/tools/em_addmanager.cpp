// SPDX-License-Identifier: GPL-3.0-only
// em_addmanager: runs the "Manager -> Neuer" action (add_manager_to_game in
// game/world_generator.hpp) on a given save state and writes the result as a .HOK, so the
// addition of a manager to a running game can be inspected outside the UI.
//   em_addmanager <EM folder> <header.HOK> <state.bin> <borland state> <name> <face> <team> <out.HOK>
// The HOK file only lends its header; the state comes from the raw state file (as written by
// "em_dump hok <file> <out.bin>"). <borland state> is the initial 32-bit state of the Borland
// random generator (decimal or 0x hex). EM_WORLD_DRAWS=<file> logs the (lo, hi) argument of
// every Borland draw, one "B lo hi" line each. Prints the final generator state and draw count.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/world_generator.hpp"

int main(int argc, char** argv) {
    using namespace em;
    if (argc < 9) {
        std::fprintf(stderr, "usage: em_addmanager <EM folder> <header.HOK> <state.bin> <borland state> <name> <face> <team> <out.HOK>\n");
        return 2;
    }
    try {
        const std::filesystem::path em = argv[1];
        const Bytes exe = read_file(em / "EM.EXE");
        const Bytes db = read_file(em / "KB" / "23.vga");
        HokFile h = decode_hok(read_file(argv[2]));
        h.state = read_file(argv[3]);
        h = decode_hok(encode_hok(h));
        BorlandRandom rng(uint32_t(std::strtoul(argv[4], nullptr, 0)));
        std::vector<std::pair<long, long>> blog, glog;
        const char* draws = std::getenv("EM_WORLD_DRAWS");
        if (draws) { g_borland_log = &blog; g_game_log = &glog; }
        const HokFile out = add_manager_to_game(exe, db, h, WorldManager{argv[5], std::atoi(argv[6]), std::atoi(argv[7])}, rng);
        if (draws) {
            g_borland_log = nullptr; g_game_log = nullptr;
            std::ofstream d(draws);
            for (const auto& [lo, hi] : blog) d << "B " << lo << " " << hi << '\n';
        }
        const Bytes bytes = encode_hok(out);
        std::ofstream(argv[8], std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        std::printf("borland after %u, %zu draws\n", rng.state(), blog.size());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "em_addmanager: %s\n", e.what());
        return 1;
    }
}
