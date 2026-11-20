#include "generated/default/wfc_init.h"
#include "graphics.h"

#include <atomic>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

extern "C" {
#include <libavutil/lzo.h>
}

#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/input/switch/switch_input_driver.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

namespace {

void TraceFirstCall(std::atomic_flag& seen, const char* entry, uint32_t caller) {
  if (!seen.test_and_set(std::memory_order_relaxed)) {
    REXLOG_INFO("WFC: first {} call from guest return address 0x{:08X}", entry, caller);
  }
}

class WfcSwitchApp final : public rex::ReXApp {
 public:
  explicit WfcSwitchApp(rex::ui::WindowedAppContext& context)
      : ReXApp(context, "wfc", PPCImageConfig) {}

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::make_unique<WfcSwitchApp>(context);
  }

 private:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (paths.game_data_root.empty()) {
      paths.game_data_root = folder / "game";
    }
  }

  void OnPostInitLogging() override {
    REXLOG_INFO("WFC Switch build, local attempt {}", WFC_BUILD_ATTEMPT);
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin.clear();
    config.graphics = CreateSwitchGraphicsSystem();
    // The native pad driver polls libnx directly. The desktop mouse and
    // keyboard driver can keep a stale window reference during SDK shutdown.
    config.input_factory = [](bool) -> std::unique_ptr<rex::system::IInputSystem> {
      auto input = std::make_unique<rex::input::InputSystem>(nullptr);
      auto pad = std::make_unique<rex::input::nx::SwitchInputDriver>(nullptr, 0);
      if (XSUCCEEDED(pad->Setup())) {
        input->AddDriver(std::move(pad));
      }
      input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
      return input;
    };
  }

  bool ConstructRuntime(const rex::PathConfig& paths) override {
    std::error_code error;
    if (!std::filesystem::is_regular_file(paths.game_data_root / "TransGame" / "Xbox360TOC.txt",
                                          error)) {
      REXLOG_ERROR("WFC: missing TransGame/Xbox360TOC.txt under {}", paths.game_data_root.string());
      return false;
    }
    if (!ReXApp::ConstructRuntime(paths)) {
      return false;
    }
    REXLOG_INFO("WFC: loaded default.xex from {}", paths.game_data_root.string());
    return true;
  }

  void OnPostLaunchModule(rex::system::XThread*) override {
    REXLOG_INFO("WFC: resuming initial guest thread");
  }
};

}  // namespace

// Entry points verified for the retail XEX with SHA-256 107314b11111cef557b9bcee243b0a865c964f59cde99030c152265033934b22.
// The strong definitions trace the generated functions and forward all guest state unchanged.
REX_EXTERN(xstart) {
  REXLOG_INFO("WFC: entered recompiled Xbox 360 code at 0x826FB0B8");
  __imp__xstart(ctx, base);
}

REX_EXTERN(sub_82480360) {
  static std::atomic_flag seen;
  TraceFirstCall(seen, "Direct3D device creation at 0x82480360", static_cast<uint32_t>(ctx.lr));
  __imp__sub_82480360(ctx, base);
}

REX_EXTERN(sub_8248E850) {
  static std::atomic_flag seen;
  TraceFirstCall(seen, "Direct3D swap at 0x8248E850", static_cast<uint32_t>(ctx.lr));
  __imp__sub_8248E850(ctx, base);
}

REX_EXTERN(sub_82B1CC80) {
  // The package reader uses this virtual call to load the index consumed by
  // sub_82B89DD8 at 0x82B89E2C. Report an implausible value without changing it.
  const bool read_package_index = ctx.lr == 0x82B89E18 && ctx.r5.u32 == 4;
  const uint32_t archive = ctx.r3.u32;
  const uint32_t output = ctx.r4.u32;
  __imp__sub_82B1CC80(ctx, base);
  if (read_package_index) {
    const auto index = rex::memory::load_and_swap<uint32_t>(base + output);
    static std::atomic_flag reported;
    if (index >= (1u << 24) && !reported.test_and_set(std::memory_order_relaxed)) {
      const auto position = rex::memory::load_and_swap<uint32_t>(base + archive + 156);
      const auto table = rex::memory::load_and_swap<uint32_t>(base + uint32_t(ctx.r30.u32 - 68));
      REXLOG_ERROR("WFC: package index 0x{:08X} from archive 0x{:08X} at position 0x{:08X}, table 0x{:08X}",
                   index, archive, position, table);
    }
  }
}

// LZO1X decoder for the verified XEX. The four direct arguments are the input
// pointer, input length, output pointer and big-endian output capacity/result.
REX_EXTERN(sub_835846E0) {
  const uint32_t input_address = ctx.r3.u32;
  const uint32_t input_size = ctx.r4.u32;
  const uint32_t output_address = ctx.r5.u32;
  const uint32_t size_address = ctx.r6.u32;
  const auto valid_range = [](uint32_t address, uint32_t size) {
    return address != 0 && uint64_t(address) + size <= (uint64_t{1} << 32);
  };
  if (!valid_range(size_address, sizeof(uint32_t))) {
    ctx.r3.s64 = -1;
    return;
  }
  const auto capacity = rex::memory::load_and_swap<uint32_t>(base + size_address);
  rex::memory::store_and_swap<uint32_t>(base + size_address, 0);
  constexpr auto max_size = uint32_t(std::numeric_limits<int>::max());
  if (input_size < 3 || input_size > max_size || !valid_range(input_address, input_size)) {
    ctx.r3.s64 = -4;  // LZO_E_INPUT_OVERRUN
    return;
  }
  if (!capacity || capacity > max_size || !valid_range(output_address, capacity)) {
    ctx.r3.s64 = -5;  // LZO_E_OUTPUT_OVERRUN
    return;
  }

  // FFmpeg requires padding. Keep it on the host so the decoder never reads
  // or writes across the end of a guest streaming buffer for that padding.
  struct DecodeBuffers {
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
  };
  static thread_local DecodeBuffers scratch;
  try {
    scratch.input.resize(size_t(input_size) + AV_LZO_INPUT_PADDING);
    scratch.output.resize(size_t(capacity) + AV_LZO_OUTPUT_PADDING);
  } catch (const std::bad_alloc&) {
    ctx.r3.s64 = -2;  // LZO_E_OUT_OF_MEMORY
    return;
  }
  std::memcpy(scratch.input.data(), base + input_address, input_size);
  std::fill_n(scratch.input.data() + input_size, AV_LZO_INPUT_PADDING, uint8_t{0});
  int input_remaining = static_cast<int>(input_size);
  int output_remaining = static_cast<int>(capacity);
  const int error =
      av_lzo1x_decode(scratch.output.data(), &output_remaining, scratch.input.data(), &input_remaining);
  if (error || input_remaining) {
    static std::atomic<uint32_t> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 4) {
      REXLOG_ERROR("WFC: LZO error {}, input 0x{:08X}/{} ({} bytes left), output 0x{:08X}/{} "
                   "({} bytes left), guest caller 0x{:08X}",
                   error, input_address, input_size, input_remaining, output_address, capacity,
                   output_remaining, uint32_t(ctx.lr));
    }
    ctx.r3.s64 = -1;  // LZO_E_ERROR
    if (error & AV_LZO_INPUT_DEPLETED) {
      ctx.r3.s64 = -4;
    } else if (error & AV_LZO_OUTPUT_FULL) {
      ctx.r3.s64 = -5;
    } else if (error & AV_LZO_INVALID_BACKPTR) {
      ctx.r3.s64 = -6;
    } else if (!error) {
      ctx.r3.s64 = -8;  // LZO_E_INPUT_NOT_CONSUMED
    }
    return;
  }
  const auto written = capacity - uint32_t(output_remaining);
  std::memcpy(base + output_address, scratch.output.data(), written);
  rex::memory::store_and_swap<uint32_t>(base + size_address, written);
  ctx.r3.s64 = 0;
}

REX_DEFINE_APP(wfc, WfcSwitchApp::Create)
