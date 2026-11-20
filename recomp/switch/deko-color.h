#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>

#include <deko3d.h>
#include <rex/graphics/xenos.h>
#include <rex/math.h>

namespace wfc::deko {

using RenderFormat = rex::graphics::xenos::ColorRenderTargetFormat;
using ResolveFormat = rex::graphics::xenos::ColorFormat;

// Keep the SDK shader variant flags in the low byte.
constexpr uint32_t kColorExponentMask = 0xFFFFFF00u;

constexpr uint32_t kShadowMaskWidth = 520;
constexpr uint32_t kShadowMaskHeight = 296;
constexpr uint32_t kShadowAtlasDepthInfo = 0x0000010A;
constexpr uint32_t kShadowAtlasAddress = 0x17A06000;
constexpr uint32_t kShadowAtlasPitch = 1040;
constexpr uint32_t kShadowAtlasSize = 1024;

constexpr uint32_t ShadowAtlasClearPitch(uint32_t info, uint32_t pitch, uint32_t surface_info) {
  using rex::graphics::xenos::MsaaSamples;
  const auto msaa = MsaaSamples((surface_info >> 16) & 3u);
  // A 4x alias has twice as many samples per EDRAM row as its logical pitch.
  return info == kShadowAtlasDepthInfo && pitch == kShadowAtlasPitch / 2 && msaa == MsaaSamples::k4X
             ? kShadowAtlasPitch : pitch;
}

constexpr bool IsShadowAtlasDepthWrite(uint32_t depth_info, uint32_t pitch, uint32_t msaa,
                                        uint32_t colors, uint32_t depth_control) {
  using rex::graphics::xenos::CompareFunction;
  const auto compare = CompareFunction((depth_control >> 4) & 7u);
  return depth_info == kShadowAtlasDepthInfo && pitch == kShadowAtlasPitch && msaa == 0 &&
         colors == 0 && (depth_control & 7u) == 6 &&
         (compare == CompareFunction::kLess || compare == CompareFunction::kLessEqual);
}

constexpr uint32_t ColorExponentKey(uint32_t color_info, uint32_t target) {
  return ((color_info >> 20) & 63u) << (8 + 6 * target);
}

constexpr int ColorExponent(uint32_t variant, uint32_t target) {
  const int raw = int((variant >> (8 + 6 * target)) & 63u);
  return raw < 32 ? raw : raw - 64;
}

inline void ApplyColorExponents(std::string& glsl, uint32_t variant) {
  std::string writes;
  for (uint32_t target = 0; target < 4; ++target) {
    const int exponent = ColorExponent(variant, target);
    if (exponent == 0) {
      continue;
    }
    const std::string output = "out_var_SV_Target" + std::to_string(target);
    if (glsl.find("out vec4 " + output + ';') != std::string::npos) {
      writes += "    " + output + " *= exp2(" + std::to_string(exponent) + ".0);\n";
    }
  }
  if (!writes.empty()) {
    // The generated pixel shaders have a single exit from main. Apply the
    // exponent after their alpha test and before host blending.
    glsl.insert(glsl.rfind('}'), writes);
  }
}

constexpr bool IsShadowMaskInit(uint64_t vs, uint64_t ps, uint32_t pitch, uint32_t color_info,
                                uint32_t color_mask, uint32_t depth_control, uint32_t blend) {
  return vs == 0x1E6883FCCDE1F688ull && ps == 0xA4A965C189287B99ull && pitch == 560 &&
         color_info == 0x00000085 && color_mask == 0xF && depth_control == 0 &&
         (blend & 0x1FFF1FFF) == 0x00010001;
}

constexpr bool IsShadowStencilClear(uint64_t vs, uint32_t colors, uint32_t depth_control) {
  // Direct3D's internal clear shader, ALWAYS/REPLACE on both faces with depth disabled.
  return vs == 0x1E6883FCCDE1F688ull && colors == 0 && depth_control == 0x00008701;
}

constexpr bool IsSceneReverseDepth(uint32_t base, uint32_t format, uint32_t pitch) {
  using rex::graphics::xenos::DepthRenderTargetFormat;
  return base == 0 && format == uint32_t(DepthRenderTargetFormat::kD24FS8) && pitch == 1040;
}

constexpr uint32_t SceneDepthMode(uint32_t base, uint32_t format, uint32_t pitch, uint32_t surface_info) {
  // Keep the observed prepass and HDR depth allocations independent while the host uses single-sample targets.
  return IsSceneReverseDepth(base, format, pitch) ? (surface_info >> 16) & 3u : 0;
}

constexpr bool IsSceneLightMaskFetch(uint32_t address_format, uint32_t size, uint32_t layout) {
  using rex::graphics::xenos::ColorFormat;
  using rex::graphics::xenos::DataDimension;
  return (address_format & 0x1FFFF03F) == (0x18C50000u | uint32_t(ColorFormat::k_8_8_8_8)) &&
         (size & 0x03FFFFFF) == ((kShadowMaskWidth - 1) | ((kShadowMaskHeight - 1) << 13)) &&
         ((layout >> 9) & 3) == uint32_t(DataDimension::k2DOrStacked);
}

constexpr uint32_t NeutralLightMaskSwizzle(uint32_t swizzle) {
  constexpr uint32_t kOne = 5;
  return (swizzle & 0xE00) | kOne | (kOne << 3) | (kOne << 6);
}

constexpr RenderFormat CanonicalTarget(RenderFormat format) {
  switch (format) {
    case RenderFormat::k_2_10_10_10_AS_10_10_10_10:
      return RenderFormat::k_2_10_10_10;
    case RenderFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return RenderFormat::k_2_10_10_10_FLOAT;
    default:
      return format;
  }
}

constexpr DkImageFormat ColorTargetFormat(RenderFormat format) {
  switch (CanonicalTarget(format)) {
    case RenderFormat::k_8_8_8_8:
    case RenderFormat::k_8_8_8_8_GAMMA:
      return DkImageFormat_RGBA8_Unorm;
    case RenderFormat::k_2_10_10_10:
      return DkImageFormat_RGB10A2_Unorm;
    case RenderFormat::k_16_16:
    case RenderFormat::k_16_16_FLOAT:
      return DkImageFormat_RG16_Float;
    case RenderFormat::k_2_10_10_10_FLOAT:
    case RenderFormat::k_16_16_16_16:
    case RenderFormat::k_16_16_16_16_FLOAT:
      return DkImageFormat_RGBA16_Float;
    case RenderFormat::k_32_FLOAT:
      return DkImageFormat_R32_Float;
    case RenderFormat::k_32_32_FLOAT:
      return DkImageFormat_RG32_Float;
    default:
      return DkImageFormat_None;
  }
}

struct ResolveStorage {
  DkImageFormat format;
  uint32_t bytes_per_pixel_log2;
};

constexpr ResolveStorage ColorResolveFormat(ResolveFormat format) {
  switch (format) {
    case ResolveFormat::k_8_8_8_8:
    case ResolveFormat::k_8_8_8_8_A:
    case ResolveFormat::k_8_8_8_8_AS_16_16_16_16:
      return {DkImageFormat_RGBA8_Unorm, 2};
    case ResolveFormat::k_2_10_10_10:
    case ResolveFormat::k_2_10_10_10_AS_16_16_16_16:
      return {DkImageFormat_RGB10A2_Unorm, 2};
    case ResolveFormat::k_16_16:
      return {DkImageFormat_RG16_Unorm, 2};
    case ResolveFormat::k_16_16_16_16:
      return {DkImageFormat_RGBA16_Unorm, 3};
    case ResolveFormat::k_16_FLOAT:
      return {DkImageFormat_R16_Float, 1};
    case ResolveFormat::k_16_16_FLOAT:
      return {DkImageFormat_RG16_Float, 2};
    case ResolveFormat::k_16_16_16_16_FLOAT:
      return {DkImageFormat_RGBA16_Float, 3};
    case ResolveFormat::k_32_FLOAT:
      return {DkImageFormat_R32_Float, 2};
    case ResolveFormat::k_32_32_FLOAT:
      return {DkImageFormat_RG32_Float, 3};
    case ResolveFormat::k_32_32_32_32_FLOAT:
      return {DkImageFormat_RGBA32_Float, 4};
    default:
      return {DkImageFormat_None, 0};
  }
}

inline std::array<float, 4> DecodeClearColor(RenderFormat format, uint64_t bits) {
  std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
  switch (CanonicalTarget(format)) {
    case RenderFormat::k_8_8_8_8:
    case RenderFormat::k_8_8_8_8_GAMMA:
      for (uint32_t component = 0; component < 4; ++component) {
        color[component] = float((bits >> (component * 8)) & 255) * (1.0f / 255.0f);
      }
      break;
    case RenderFormat::k_2_10_10_10:
    case RenderFormat::k_2_10_10_10_FLOAT:
      for (uint32_t component = 0; component < 3; ++component) {
        const uint32_t value = (bits >> (component * 10)) & 1023;
        color[component] = CanonicalTarget(format) == RenderFormat::k_2_10_10_10_FLOAT
                               ? rex::graphics::xenos::Float7e3To32(value)
                               : float(value) / 1023.0f;
      }
      color[3] = float((bits >> 30) & 3) / 3.0f;
      break;
    case RenderFormat::k_16_16:
    case RenderFormat::k_16_16_FLOAT:
    case RenderFormat::k_16_16_16_16:
    case RenderFormat::k_16_16_16_16_FLOAT: {
      const bool fixed = format == RenderFormat::k_16_16 || format == RenderFormat::k_16_16_16_16;
      const uint32_t components =
          format == RenderFormat::k_16_16 || format == RenderFormat::k_16_16_FLOAT ? 2 : 4;
      for (uint32_t component = 0; component < components; ++component) {
        const auto value = uint16_t(bits >> (component * 16));
        color[component] = fixed ? std::max(float(std::bit_cast<int16_t>(value)) * (32.0f / 32767.0f), -32.0f)
                                 : rex::xenos_half_to_float(value);
      }
      break;
    }
    case RenderFormat::k_32_32_FLOAT:
      color[1] = std::bit_cast<float>(uint32_t(bits >> 32));
      [[fallthrough]];
    case RenderFormat::k_32_FLOAT:
      color[0] = std::bit_cast<float>(uint32_t(bits));
      break;
    default:
      break;
  }
  return color;
}

}  // namespace wfc::deko
