#pragma once

#include <cstdint>

// D3D8's fallback additive colors appear almost white after composition.
// Preserve the original hue while matching that high-luminance result.
[[nodiscard]] constexpr uint32_t
th06AdditiveEffectDisplayColor(uint32_t packedColor) {
  const auto lift = [](uint32_t component) {
    return 0xf0u + (component * 0x0fu + 127u) / 255u;
  };
  const uint32_t alpha = packedColor & 0xff000000u;
  const uint32_t red = lift((packedColor >> 16) & 0xffu);
  const uint32_t green = lift((packedColor >> 8) & 0xffu);
  const uint32_t blue = lift(packedColor & 0xffu);
  return alpha | (red << 16) | (green << 8) | blue;
}
