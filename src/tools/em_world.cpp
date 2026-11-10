// SPDX-License-Identifier: GPL-3.0-only
// em_world: writes the new-game world of game/world_generator.hpp as a HOK file, for comparing it with the
// original's first save (AUTOSAVE.HOK after "Los jetzt!").
//   em_world <EM folder> <out.HOK> <seed start> <seed menu> <seed after> <name> <face> <team> [level end squad mode]
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/world_generator.hpp"

int main(int argc, char** argv) {
    using namespace em;
    if (argc < 9) {
        std::fprintf(stderr, "usage: em_world <EM folder> <out.HOK> <seed start> <seed menu> <seed after> <name> <face> <team> [level end squad mode]\n");
        return 2;
    }
    try {
        const std::filesystem::path em = argv[1];
        const Bytes exe = read_file(em / "EM.EXE");
        const Bytes db = read_file(em / "KB" / "23.vga");
        WorldSettings settings;
        settings.managers.push_back({argv[6], std::atoi(argv[7]), std::atoi(argv[8])});
        if (argc > 9) settings.level = std::atoi(argv[9]);
        if (argc > 10) settings.end_choice = std::atoi(argv[10]);
        if (argc > 11) settings.squad_size = std::atoi(argv[11]);
        if (argc > 12) settings.start_mode = std::atoi(argv[12]);
        WorldSeeds seeds;
        seeds.start = uint16_t(std::strtoul(argv[3], nullptr, 0));
        seeds.menu = uint16_t(std::strtoul(argv[4], nullptr, 0));
        seeds.after = uint16_t(std::strtoul(argv[5], nullptr, 0));
        const WorldResult world = generate_world(exe, db, settings, seeds);
        const Bytes hok = encode_hok(world.save);
        std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(hok.data()), std::streamsize(hok.size()));
        for (const auto& line : world.log) std::printf("%s\n", line.c_str());
        std::printf("written %s (%zu bytes)\n", argv[2], hok.size());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "em_world: %s\n", e.what());
        return 1;
    }
}
