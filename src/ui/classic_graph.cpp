// SPDX-License-Identifier: GPL-3.0-only
// The "fever curve" in the main-menu header: league position of the own
// club (and optionally of the next opponent) over the rounds played so far,
// with rank labels and the matchday caption.
#include "ui/classic_graph.hpp"

#include <array>
#include <cstdlib>
#include <string>

namespace em::ui {
namespace {
constexpr size_t kPackedHistory = 0x3bd9; // save-state position history: one byte per two rounds (low nibble first)
constexpr size_t kTableStride = 80;       // bytes per team in the statistics block
constexpr size_t kManagers = 0x0dcd;      // manager records in the save state
constexpr size_t kManagerStride = 0x1af;  // bytes per manager record
constexpr int kRegularRounds = 44;        // rounds of the regular season in each of the three leagues

// Only national teams 52..63 count as national here. In particular both
// sentinels 254/255 and European club IDs are not national, as in the game.
bool national_team(int team) { return team >= 52 && team < 64; }

// The game stores matchday counters as signed chars.
int native_matchday(int value) {
    return int(static_cast<int8_t>(static_cast<uint8_t>(value)));
}

// League (0..2) of the given club for the label placement.
int own_division(const HokFile& h, int team) {
    // The game uses the active manager's stored league (manager record +0xEB).
    // Recover that manager from the explicit own-team argument (+0x14)
    // rather than assuming manager zero.
    for (size_t manager = 0; manager < 6; ++manager) {
        const size_t record = kManagers + manager * kManagerStride;
        if (record + 0xeb >= h.state.size()) break;
        if (h.state[record + 0x14] == team && h.state[record + 0xeb] < 3)
            return h.state[record + 0xeb];
    }
    // A decoded ordinary club always has a matching manager; this also lets
    // a minimal synthetic snapshot describe its league by its club index range.
    return team < 36 ? team / 12 : 0;
}

// 0-based league position of team after round (0-based); throws if the
// history lies outside the save state.
int position(const HokFile& h, int team, int round) {
    const size_t offset = kPackedHistory + size_t(team) * kTableStride + size_t(round / 2);
    if (offset >= h.state.size()) throw FormatError("classic graph: truncated ranking history");
    const uint8_t packed = h.state[offset];
    return (packed >> ((round & 1) * 4)) & 15;
}

void centered(ClassicCanvas& canvas, int left, int right, int baseline,
              std::string_view text, uint8_t color) {
    // The game halves each term separately. Combining the two integer
    // divisions would shift an odd-width label one pixel.
    const int x = left + (right - left) / 2 - canvas.font().measure(ClassicFontId::Mini, text) / 2;
    canvas.text(ClassicFontId::Mini, x, baseline, text, color);
}
}

// Curve geometry: x = 5 + round*step with step = (8500/count)/100 (1 is
// raised to 2), y = 47 + 2*position. Own curve colour 24, opponent 7.
void draw_classic_ranking_graph(ClassicCanvas& canvas, const HokFile& h,
                                int ownTeam, int opponent, bool leagueEvent) {
    if (ownTeam < 0 || ownTeam >= 150 || national_team(ownTeam)) return;
    // Accept the customary -1 for "no opponent" as the game's 0xFF too.
    opponent = int(static_cast<uint8_t>(opponent));
    if (national_team(opponent)) return;

    const int next = native_matchday(h.matchday[0]); // the game uses league zero's counter unconditionally
    const int count = next - 1;
    const bool opponent_curve = opponent < 52;
    if (count > 0) {
        // Check the required range before drawing; do not synthesize history
        // from current rankings or silently limit the game's loop to 44.
        (void)position(h, ownTeam, count - 1);
        if (opponent_curve) (void)position(h, opponent, count - 1);
    }

    canvas.line(5, 46, 5, 69, 6);
    canvas.line(6, 70, 101, 70, 8);
    if (count <= 0) {
        if (opponent != 254)
            centered(canvas, 1, 110, 39, "Vor dem ersten Spieltag.", 7);
        return;
    }

    int step = (8500 / count) / 100;
    if (step == 1) step = 2;
    const auto curve = [&](int team, uint8_t color) {
        int x = 6;
        int y = 47 + 2 * position(h, team, 0);
        int last = 0;
        for (int i = 0; i < count; ++i) {
            last = position(h, team, i);
            const int end_x = 5 + (i + 1) * step;
            const int end_y = 47 + 2 * last;
            canvas.line(x, y, end_x, end_y, color);
            x = end_x;
            y = end_y;
        }
        return last;
    };

    const int own_last = curve(ownTeam, 24);
    std::array<int, 2> rank{own_last + 1, 255};
    std::array<int, 2> label_y{49 + 2 * own_last, 0};
    if (opponent_curve) {
        const int other_last = curve(opponent, 7);
        rank[1] = other_last + 1;
        label_y[1] = 49 + 2 * other_last;

        // Labels closer than 7 pixels are pushed apart exactly as the game
        // does: the ranks stay fixed while one label's baseline moves. With a
        // top-four own rank the labels separate downward, otherwise upward.
        if (std::abs(label_y[0] - label_y[1]) < 7) {
            const int direction = rank[0] < 5 ? 1 : -1;
            while (std::abs(label_y[0] - label_y[1]) < 7) {
                if ((direction == 1 && rank[0] >= rank[1]) ||
                    (direction == -1 && rank[0] <= rank[1]))
                    label_y[0] += direction;
                else
                    label_y[1] += direction;
            }
        }
    }

    const int manager_next = native_matchday(h.matchday[own_division(h, ownTeam)]);
    for (size_t team = 0; team < rank.size(); ++team) {
        if (rank[team] == 255) continue;
        const bool lower_after_regular = rank[team] > 9 && manager_next > kRegularRounds;
        const int x = (lower_after_regular ? 6 : 7) + count * step;
        const int y = label_y[team] - 2 - int(lower_after_regular);
        canvas.text(ClassicFontId::Mini, x, y, std::to_string(rank[team]) + ".", team == 0 ? 24 : 7);
    }

    // The game compares the regular-season limit with the NEXT round: the
    // caption is shown during the regular season and suppressed after it.
    // Nothing is cleared or filled behind it.
    if (leagueEvent && next <= kRegularRounds)
        centered(canvas, 6, 110, 39, std::to_string(next) + ".Spieltag", 7);
}

} // namespace em::ui
