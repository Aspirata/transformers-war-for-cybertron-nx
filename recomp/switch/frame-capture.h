#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/ui/deko3d/nucleo.h>

namespace wfc::deko {

// Store one half-resolution snapshot per selected stage of the same scene frame.
class FrameCapture {
 public:
  enum class Stage : uint32_t {
    kHdr, kScene, kComposite, kFront, kLightMask, kLightSource, kMaskDepth, kLightInitial, kShadowAtlas
  };
  using DrawState = std::array<uint32_t, 11>;

  bool Begin(uint64_t frame) {
    if (sequence_ == kMaxFrames || frame < next_frame_) {
      return false;
    }
    frame_ = frame;
    ++sequence_;
    // Leave time to reach a gameplay repro after the four early comparison frames.
    next_frame_ = frame + (sequence_ == kMaxFrames - 1 ? 960 : 360);
    trace_count_ = 0;
    draw_state_count_ = 0;
    return true;
  }

  bool Active(uint64_t frame) const { return frame_ == frame; }

  bool Trace(uint64_t frame) {
    if (!Active(frame) || trace_count_ == 48) {
      return false;
    }
    ++trace_count_;
    return true;
  }

  bool TraceDraw(const DrawState& state) {
    const auto end = draw_states_.begin() + draw_state_count_;
    if (end == draw_states_.end() || std::find(draw_states_.begin(), end, state) != end) {
      return false;
    }
    draw_states_[draw_state_count_++] = state;
    return true;
  }

  static bool Supports(DkImageFormat format) {
    return format == DkImageFormat_RGBA8_Unorm || format == DkImageFormat_RGB10A2_Unorm ||
           format == DkImageFormat_RGBA16_Float;
  }

  void Record(rex::deko3d::Nucleo& gpu, Stage stage, const DkImage& image, DkImageFormat format,
              uint32_t width, uint32_t height, bool swap_rb) {
    auto& sample = samples_[uint32_t(stage)];
    if (sample.width || width < 2 || height < 2 || width > kWidth || height > kHeight || !Supports(format)) {
      return;
    }
    const bool floating = format == DkImageFormat_RGBA16_Float;
    const uint32_t row_bytes = width * (floating ? 8 : 4);
    const uint32_t bytes = row_bytes * height;
    if (bytes > kBufferBytes - used_) {
      return;
    }
    if (!buffer_) {
      buffer_ = gpu.CrearBloqueLectura(kBufferBytes, "WFC scene capture");
    }
    if (!buffer_) {
      return;
    }
    const uint32_t offset = used_;
    dkMemBlockFlushCpuCache(buffer_, offset, bytes);
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const DkImageRect rect{0, 0, 0, width, height, 1};
    const DkCopyBuf output{dkMemBlockGetGpuAddr(buffer_) + offset, row_bytes, bytes};
    const auto cmd = gpu.comandos();
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &output, 0);
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    sample = {offset, width, height, row_bytes, format, swap_rb};
    used_ += (bytes + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1);
  }

  void Save(rex::deko3d::Nucleo& gpu, uint64_t frame) {
    if (!Active(frame) || used_ == 0) {
      return;
    }
    gpu.EsperarGpu();
    const auto* data = static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(buffer_));
    const auto folder = rex::filesystem::GetExecutableFolder() / "logs";
    std::vector<uint8_t> pixels;
    for (uint32_t slot = 0; slot < samples_.size(); ++slot) {
      const auto& sample = samples_[slot];
      if (!sample.width) {
        continue;
      }
      dkMemBlockFlushCpuCache(buffer_, sample.offset, sample.row_bytes * sample.height);
      const auto path = folder / fmt::format("wfc-{}-view-{}-{}.ppm", WFC_BUILD_ATTEMPT, sequence_, kStages[slot]);
      const uint32_t row_bytes = (sample.width / 2) * 3;
      const uint32_t pixel_stride = sample.row_bytes / sample.width;
      pixels.resize(row_bytes * (sample.height / 2));
      for (uint32_t y = 0; y < sample.height / 2; ++y) {
        const auto* input = data + sample.offset + (y * 2) * sample.row_bytes;
        auto* row = pixels.data() + y * row_bytes;
        for (uint32_t x = 0; x < sample.width / 2; ++x) {
          const auto* pixel = input + x * 2 * pixel_stride;
          for (uint32_t channel = 0; channel < 3; ++channel) {
            const uint32_t component = sample.swap_rb ? 2 - channel : channel;
            row[x * 3 + channel] = PreviewComponent(pixel, sample.format, component);
          }
        }
      }
      std::ofstream output(path, std::ios::binary);
      output << "P6\n" << sample.width / 2 << ' ' << sample.height / 2 << "\n255\n";
      output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
      output.close();
      if (output) {
        REXLOG_INFO("WFC frame {} saved {} ({}x{} source)", frame, path.filename().string(),
                    sample.width, sample.height);
      } else {
        REXLOG_WARN("WFC frame {} could not save {}", frame, path.filename().string());
      }
    }
    samples_ = {};
    used_ = 0;
  }

 private:
  static constexpr uint32_t kMaxFrames = 5;
  static constexpr uint32_t kWidth = 1280;
  static constexpr uint32_t kHeight = 720;
  static constexpr uint32_t kBufferBytes = kWidth * kHeight * 24;
  static_assert(kBufferBytes % DK_MEMBLOCK_ALIGNMENT == 0);
  static constexpr std::array kStages{
      "hdr", "scene", "input", "front", "light-mask", "light-source", "mask-depth", "light-initial", "shadow-atlas"};

  static uint8_t PreviewComponent(const uint8_t* pixel, DkImageFormat format, uint32_t component) {
    if (format == DkImageFormat_RGBA16_Float) {
      uint16_t half;
      std::memcpy(&half, pixel + component * sizeof(half), sizeof(half));
      const float value = rex::xenos_half_to_float(half);
      return value > 0.0f ? uint8_t(std::sqrt(std::min(value, 1.0f)) * 255.0f) : 0;
    }
    if (format == DkImageFormat_RGB10A2_Unorm) {
      uint32_t packed;
      std::memcpy(&packed, pixel, sizeof(packed));
      return uint8_t(packed >> (component * 10 + 2));
    }
    return pixel[component];
  }

  struct Sample {
    uint32_t offset, width, height, row_bytes;
    DkImageFormat format;
    bool swap_rb;
  };
  std::array<Sample, kStages.size()> samples_{};
  DkMemBlock buffer_ = nullptr;
  uint64_t frame_ = UINT64_MAX;
  uint64_t next_frame_ = 0;
  uint32_t used_ = 0;
  uint32_t trace_count_ = 0;
  std::array<DrawState, 16> draw_states_{};
  uint32_t draw_state_count_ = 0;
  uint32_t sequence_ = 0;
};

}  // namespace wfc::deko
