// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_misc_newmanager.hpp"

namespace em::ui {
namespace {
constexpr size_t kFaces = 0x01EE;  // DS:0178
constexpr size_t kManagers = 0x0DCD, kManagerSize = 0x1AF;
std::string cstring(const Bytes& b, size_t at, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && b.at(at + i); i++) s.push_back(char(b.at(at + i)));
    return s;
}
}  // namespace

void ClassicNewManager::open(const HokFile& h) {
    rows_ = {};
    const Bytes& s = h.state;
    const int managers = 1;  // OpenEM plays manager 0 only (DS:0386)
    for (int m = 0; m < managers; m++) {
        auto& r = rows_[size_t(m)];
        r.face = s.at(kFaces + size_t(m)) == 0xFF ? -1 : s.at(kFaces + size_t(m));
        r.team = s.at(kManagers + size_t(m) * kManagerSize + 0x14);
        r.name = cstring(s, kManagers + size_t(m) * kManagerSize, 0x14);
        r.existing = true;
    }
    panel_ = Panel::Options;
    row_ = -1;
    editing_ = false;
    club_page_ = 0;
}

void ClassicNewManager::draw(ClassicCanvas& c, const HokFile&, int, int) const { c.background(150); }

ClassicNewManager::Result ClassicNewManager::click(const HokFile&, int, int, bool right) {
    return right ? Result::None : Result::None;
}
void ClassicNewManager::key(int) {}
void ClassicNewManager::text(std::string_view) {}
}  // namespace em::ui
