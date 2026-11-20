#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/logging.h>
#include <rex/math.h>
#include <rex/ui/deko3d/nucleo.h>

namespace wfc::deko {

// Read the same small region before and after a 1:1 resolve. Read two
// additional regions so that a black center pixel alone is inconclusive.
class FrameProbe {
 public:
  bool Empty() const { return count_ == 0; }

  // This XEX renders the 1040-wide HDR geometry pass at EDRAM tile 0x3C2.
  static bool IsSceneTarget(uint32_t base, uint32_t pitch, uint32_t format) {
    return base == 0x3C2 && pitch == 1040 && (format == 3 || format == 12);
  }

  bool WantsSample(uint64_t frame, DkImageFormat format, uint32_t width, uint32_t height) const {
    return IsProbeFrame(frame) && count_ < samples_.size() &&
           width >= kSide && height >= kSide &&
           (format == DkImageFormat_RGBA8_Unorm || format == DkImageFormat_RGBA16_Float);
  }

  void Record(rex::deko3d::Nucleo& gpu, const char* stage, const DkImage& image, DkImageFormat format,
              uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!EnsureBuffer(gpu)) {
      return;
    }
    const bool half = format == DkImageFormat_RGBA16_Float;
    const uint32_t bytes_per_pixel = half ? 8 : 4;
    const uint32_t row_bytes = kSide * bytes_per_pixel;
    const uint32_t offset = count_ * kSampleBytes;
    dkMemBlockFlushCpuCache(buffer_, offset, kSampleBytes);
    const auto address = dkMemBlockGetGpuAddr(buffer_) + offset;
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    for (uint32_t patch = 0; patch < kPatches; ++patch) {
      const uint32_t patch_x = x + (width - kSide) * (patch + 1) / (kPatches + 1);
      const uint32_t patch_y = y + (height - kSide) * (patch + 1) / (kPatches + 1);
      const DkImageRect rect{patch_x, patch_y, 0, kSide, kSide, 1};
      const DkCopyBuf output{address + patch * kPatchBytes, row_bytes, row_bytes * kSide};
      dkCmdBufCopyImageToBuffer(gpu.comandos(), &view, &rect, &output, 0);
    }
    samples_[count_++] = {stage, half};
  }

  bool BeginDraw(rex::deko3d::Nucleo& gpu, uint64_t frame, uint64_t vs, uint64_t ps,
                 uint32_t elements, uint32_t clip, uint32_t depth) {
    if (!IsProbeFrame(frame) || draw_count_ == draws_.size() || elements < 1000) {
      return false;
    }
    for (uint32_t i = 0; i < draw_count_; ++i) {
      if (draws_[i].vs == vs && draws_[i].ps == ps) {
        return false;
      }
    }
    if (!EnsureBuffer(gpu)) {
      return false;
    }
    draws_[draw_count_] = {vs, ps, elements, clip, depth};
    auto* data = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(buffer_));
    std::memset(data + DrawOffset(), 0, kDrawBytes);
    dkMemBlockFlushCpuCache(buffer_, DrawOffset(), kDrawBytes);
    WriteCounters(gpu, DrawOffset());
    return true;
  }

  void EndDraw(rex::deko3d::Nucleo& gpu) {
    WriteCounters(gpu, DrawOffset() + kCounterBytes);
    ++draw_count_;
  }

  void Report(rex::deko3d::Nucleo& gpu, uint64_t frame) {
    if (count_ == 0 && draw_count_ == 0) {
      return;
    }
    gpu.EsperarGpu();
    dkMemBlockFlushCpuCache(buffer_, 0, kBufferBytes);
    const auto* data = static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(buffer_));
    for (uint32_t slot = 0; slot < count_; ++slot) {
      const auto& sample = samples_[slot];
      uint32_t nonzero = 0;
      for (uint32_t patch = 0; patch < kPatches; ++patch) {
        const auto* pixels = data + slot * kSampleBytes + patch * kPatchBytes;
        for (uint32_t pixel = 0; pixel < kSide * kSide; ++pixel) {
          if (sample.half) {
            const auto* rgb = pixels + pixel * 8;
            // Treat either sign of half-float zero as black and ignore alpha.
            nonzero += (rgb[0] | (rgb[1] & 0x7F) | rgb[2] | (rgb[3] & 0x7F) |
                        rgb[4] | (rgb[5] & 0x7F)) != 0;
          } else {
            const auto* rgb = pixels + pixel * 4;
            nonzero += (rgb[0] | rgb[1] | rgb[2]) != 0;
          }
        }
      }
      REXLOG_INFO("WFC frame {} {}: {}/{} sampled pixels with nonzero RGB",
                  frame, sample.stage, nonzero, kPatches * kSide * kSide);
    }
    for (uint32_t slot = 0; slot < draw_count_; ++slot) {
      std::array<uint64_t, kCounters.size()> delta{};
      const auto* reports = data + kSamples * kSampleBytes + slot * kDrawBytes;
      bool complete = true;
      for (uint32_t counter = 0; counter < kCounters.size(); ++counter) {
        const auto* before = reports + counter * kReportBytes;
        const auto* after = before + kCounterBytes;
        complete &= ReadCounter(before + sizeof(uint64_t)) && ReadCounter(after + sizeof(uint64_t));
        delta[counter] = ReadCounter(after) - ReadCounter(before);
      }
      const auto& draw = draws_[slot];
      if (!complete) {
        REXLOG_WARN("WFC frame {} HDR draw VS {:016X} PS {:016X}: incomplete GPU counters",
                    frame, draw.vs, draw.ps);
        continue;
      }
      REXLOG_INFO("WFC frame {} HDR draw VS {:016X} PS {:016X}: {} elements, {} input primitives, "
                  "{} after clip, {} fragment invocations, {} samples passed, clip {:08X}, depth {:08X}",
                  frame, draw.vs, draw.ps, draw.elements, delta[0], delta[1], delta[2], delta[3],
                  draw.clip, draw.depth);
    }
    count_ = 0;
    draw_count_ = 0;
  }

 private:
  static bool IsProbeFrame(uint64_t frame) { return frame != 0 && frame % 120 == 0; }

  DkMemBlock EnsureBuffer(rex::deko3d::Nucleo& gpu) {
    if (!buffer_) {
      buffer_ = gpu.CrearBloqueLectura(kBufferBytes, "WFC frame diagnostics");
    }
    return buffer_;
  }

  uint32_t DrawOffset() const { return kSamples * kSampleBytes + draw_count_ * kDrawBytes; }

  void WriteCounters(rex::deko3d::Nucleo& gpu, uint32_t offset) const {
    const auto cmd = gpu.comandos();
    dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
    const auto address = dkMemBlockGetGpuAddr(buffer_) + offset;
    for (uint32_t counter = 0; counter < kCounters.size(); ++counter) {
      dkCmdBufReportCounter(cmd, kCounters[counter], address + counter * kReportBytes);
    }
  }

  static uint64_t ReadCounter(const uint8_t* report) {
    uint64_t value;
    std::memcpy(&value, report, sizeof(value));
    return value;
  }

  static constexpr uint32_t kSide = 8;
  static constexpr uint32_t kPatches = 3;
  static constexpr uint32_t kPatchBytes = kSide * kSide * 8;
  static constexpr uint32_t kSampleBytes = kPatches * kPatchBytes;
  struct Sample {
    const char* stage = nullptr;
    bool half = false;
  };
  static constexpr uint32_t kSamples = 3;
  static constexpr uint32_t kDraws = 4;
  static constexpr std::array kCounters{DkCounter_InputPrimitives, DkCounter_ClipperOutputPrimitives,
                                         DkCounter_FragmentShaderInvocations, DkCounter_SamplesPassed};
  static constexpr uint32_t kReportBytes = 16;
  static constexpr uint32_t kCounterBytes = kCounters.size() * kReportBytes;
  static constexpr uint32_t kDrawBytes = 2 * kCounterBytes;
  static constexpr uint32_t kTotalBytes = kSamples * kSampleBytes + kDraws * kDrawBytes;
  static constexpr uint32_t kBufferBytes =
      ((kTotalBytes + DK_MEMBLOCK_ALIGNMENT - 1) / DK_MEMBLOCK_ALIGNMENT) * DK_MEMBLOCK_ALIGNMENT;
  struct Draw {
    uint64_t vs, ps;
    uint32_t elements, clip, depth;
  };
  std::array<Sample, kSamples> samples_{};
  std::array<Draw, kDraws> draws_{};
  DkMemBlock buffer_ = nullptr;
  uint32_t count_ = 0;
  uint32_t draw_count_ = 0;
};

// Sample either side of the observed edges before, during and after the HDR draw sequence.
class HdrEdgeTrace {
 public:
  bool Open() const { return !closed_; }
  bool Empty() const { return draw_count_ == 0; }

  void Draw(rex::deko3d::Nucleo& gpu, const DkImage& image, uint64_t vs, uint64_t ps, uint32_t depth,
            uint32_t blend) {
    if (draw_count_ < draws_.size()) {
      draws_[draw_count_] = {vs, ps, depth, blend};
    }
    ++draw_count_;
    // Sample small groups at the start, then reduce the frequency for long geometry sequences.
    const uint32_t interval = draw_count_ <= 128 ? 8 : 32;
    if (sample_count_ + 1 < sample_draws_.size() && (draw_count_ == 1 || draw_count_ % interval == 0)) {
      Sample(gpu, image);
    }
  }

  void BeforeResolve(rex::deko3d::Nucleo& gpu, const DkImage& image) {
    if (closed_) {
      return;
    }
    closed_ = true;
    Sample(gpu, image);
  }

  void Sample(rex::deko3d::Nucleo& gpu, const DkImage& image) {
    if (!buffer_) {
      buffer_ = gpu.CrearBloqueLectura(kBufferBytes, "WFC HDR edge trace");
    }
    if (!buffer_ || sample_count_ == sample_draws_.size()) {
      return;
    }
    const uint32_t offset = sample_count_ * kSampleBytes;
    dkMemBlockFlushCpuCache(buffer_, offset, kSampleBytes);
    const auto address = dkMemBlockGetGpuAddr(buffer_) + offset;
    DkImageView view;
    dkImageViewDefaults(&view, &image);
    const auto cmd = gpu.comandos();
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    for (uint32_t index = 0; index < kRegions.size(); ++index) {
      const auto& rect = kRegions[index];
      const DkCopyBuf output{address + index * kRegionBytes, rect.width * 8, kRegionBytes};
      dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &output, 0);
    }
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    sample_draws_[sample_count_++] = draw_count_;
  }

  void Report(rex::deko3d::Nucleo& gpu, uint64_t frame) {
    if (sample_count_) {
      gpu.EsperarGpu();
      dkMemBlockFlushCpuCache(buffer_, 0, sample_count_ * kSampleBytes);
      const auto* pixels = static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(buffer_));
      std::array<float, 2> previous_gap{}, max_change{};
      std::array<uint32_t, 2> widest_step{};
      for (uint32_t sample = 0; sample < sample_count_; ++sample) {
        const auto sums = Sums(pixels + sample * kSampleBytes);
        REXLOG_INFO("WFC HDR edge frame {} draw {}: right inner/outer {:.4f}/{:.4f}, bottom inner/outer {:.4f}/{:.4f}",
                    frame, sample_draws_[sample], sums[0][0], sums[0][1], sums[1][0], sums[1][1]);
        for (uint32_t axis = 0; axis < 2; ++axis) {
          const float gap = sums[axis][1] - sums[axis][0];
          const float change = std::abs(gap - previous_gap[axis]);
          if (sample && change > max_change[axis]) {
            max_change[axis] = change;
            widest_step[axis] = sample;
          }
          previous_gap[axis] = gap;
        }
      }
      for (uint32_t axis = 0; axis < 2; ++axis) {
        const uint32_t step = widest_step[axis];
        if (!step) {
          continue;
        }
        const uint32_t first = sample_draws_[step - 1];
        const uint32_t last = sample_draws_[step];
        if (first == last) {
          REXLOG_INFO("WFC HDR edge frame {} {} largest change {:.4f} after draw {} before resolve",
                      frame, axis == 0 ? "right" : "bottom", max_change[axis], last);
          continue;
        }
        REXLOG_INFO("WFC HDR edge frame {} {} largest change {:.4f} in draws {}-{}", frame,
                    axis == 0 ? "right" : "bottom", max_change[axis], first + 1, last);
        const uint32_t end = std::min({last, uint32_t(draws_.size()), first + 32});
        for (uint32_t index = first; index < end; ++index) {
          const auto& draw = draws_[index];
          REXLOG_INFO("WFC HDR edge draw {} VS {:016X} PS {:016X} depth {:08X} blend {:08X}",
                      index + 1, draw.vs, draw.ps, draw.depth, draw.blend);
        }
      }
    }
    sample_count_ = 0;
    draw_count_ = 0;
    closed_ = false;
  }

 private:
  static std::array<std::array<float, 2>, 2> Sums(const uint8_t* data) {
    std::array<std::array<float, 2>, 2> sums{};
    for (uint32_t index = 0; index < kRegions.size(); ++index) {
      const bool vertical = index >= 2;
      const auto& rect = kRegions[index];
      for (uint32_t pixel = 0; pixel < rect.width * rect.height; ++pixel) {
        const uint32_t edge_position = vertical ? pixel / rect.width : pixel % rect.width;
        if (edge_position >= 12 && edge_position < 20) {
          continue;  // Leave a gap around the observed boundary and minor filtering shifts.
        }
        float brightness = 0.0f;
        for (uint32_t channel = 0; channel < 3; ++channel) {
          uint16_t bits;
          std::memcpy(&bits, data + index * kRegionBytes + pixel * 8 + channel * 2, sizeof(bits));
          brightness = std::max(brightness, rex::xenos_half_to_float(bits));
        }
        sums[vertical][edge_position >= 20] += std::min(brightness, 1.0f) / (2 * 12 * 8);
      }
    }
    return sums;
  }

  static constexpr std::array kRegions{
      DkImageRect{944, 72, 0, 32, 8, 1}, DkImageRect{944, 360, 0, 32, 8, 1},
      DkImageRect{352, 560, 0, 8, 32, 1}, DkImageRect{736, 560, 0, 8, 32, 1},
  };
  static constexpr uint32_t kRegionBytes = 32 * 8 * 8;
  static constexpr uint32_t kSampleBytes = kRegionBytes * kRegions.size();
  static constexpr uint32_t kSamples = 60;
  static constexpr uint32_t kBufferBytes = kSampleBytes * kSamples;
  static_assert(kBufferBytes % DK_MEMBLOCK_ALIGNMENT == 0);
  struct DrawState {
    uint64_t vs, ps;
    uint32_t depth, blend;
  };
  std::array<DrawState, 2048> draws_{};
  std::array<uint32_t, kSamples> sample_draws_{};
  DkMemBlock buffer_ = nullptr;
  uint32_t sample_count_ = 0;
  uint32_t draw_count_ = 0;
  bool closed_ = false;
};

}  // namespace wfc::deko
