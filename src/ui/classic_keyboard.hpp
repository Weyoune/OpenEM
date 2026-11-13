// SPDX-License-Identifier: GPL-3.0-only
// Windows keyboard workaround for the classic frontend.
#pragma once
#include <cstdint>
#include <optional>

namespace em::ui {
// SDL2 ignores ordinary virtual keys when Windows supplies no scan code.
// Software keyboards/accessibility input can produce exactly those messages.
// Physical-key messages remain on SDL's normal path to avoid duplicate input.
//
// message: the window message (only WM_KEYDOWN, 0x0100, is considered);
// key: its wParam (virtual-key code); details: its lParam (bits 16..23 =
// scan code). Returns Backspace (8), Enter (13) or Escape (27) for such a
// scan-code-less key-down, otherwise nothing.
inline std::optional<int> classic_virtual_key(unsigned message,uint64_t key,uint64_t details) {
    if(message!=0x0100 || ((details>>16)&255)!=0)return {};
    switch(key){case 8:return 8;case 13:return 13;case 27:return 27;default:return {};}
}
}
