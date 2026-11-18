// shim/common/peripherals/WS2812.hpp - the Kastle's LED colours by Bastl's names; the Sidecar shows no LEDs.
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstdint>
namespace kastle2 {
struct WS2812 {
  static constexpr uint32_t RED = 0, GREEN = 0, BLUE = 0, WHITE = 0, BLACK = 0, NONE = 0, YELLOW = 0, CYAN = 0, MAGENTA = 0, ORANGE = 0, PINK = 0, PURPLE = 0, BROWN = 0, GRAY = 0, LIGHT_RED = 0, LIGHT_GREEN = 0, LIGHT_BLUE = 0, DARK_RED = 0, DARK_GREEN = 0, DARK_BLUE = 0, NAVY = 0, FUCHSIA = 0, AQUAMARINE = 0, CHARTREUSE = 0, CORAL = 0, CRIMSON = 0, INDIGO = 0, KHAKI = 0, LAVENDER = 0, LIME = 0, MAROON = 0, OLIVE = 0, PEACH = 0, TEAL = 0, TURQUOISE = 0, VIOLET = 0, RASPBERRY = 0, LIGHT_PINK = 0, MEDIUM_CYAN = 0, MEDIUM_MAGENTA = 0, WARM_WHITE = 0, COLD_WHITE = 0;
  static constexpr uint32_t ApplyBrightness(uint32_t c, uint8_t) { return c; }
  static constexpr uint32_t Mix(uint32_t a, uint32_t) { return a; }
};
}  // namespace kastle2
