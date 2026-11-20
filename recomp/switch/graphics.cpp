#include "graphics.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <rex/filesystem.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/types.h>
#include <rex/ui/deko3d/presentador.h>
#include <rex/x360/decodificador.h>
#include <rex/x360/deko/destinos.h>
#include <rex/x360/peticion.h>
#include <rex/x360/shader_entry.h>
#include <xxhash.h>

namespace {

namespace gpu = rex::graphics;

using ShaderKey = std::pair<bool, uint64_t>;

struct PackedShader {
  rex::x360::EntradaShader entry;
  std::vector<uint32_t> spirv;
};

using ShaderPack = std::map<ShaderKey, PackedShader>;

// The shader pack uses little-endian fields and SPIR-V, with big-endian Xenos microcode.
ShaderPack ReadShaderPack(const std::filesystem::path& path) {
  static_assert(std::endian::native == std::endian::little);
  constexpr std::array<char, 8> signature{'W', 'F', 'C', 'S', 1, 0, 0, 0};
  constexpr uint64_t pack_limit = 32 * 1024 * 1024;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const auto length = file.tellg();
  if (length < std::streamoff(signature.size()) || length > std::streamoff(pack_limit)) {
    return {};
  }
  uint64_t remaining = static_cast<uint64_t>(length);
  file.seekg(0);
  const auto read = [&](auto buffer) {
    const auto size = buffer.size_bytes();
    if (size > remaining || !file.read(reinterpret_cast<char*>(buffer.data()), size)) {
      return false;
    }
    remaining -= size;
    return true;
  };
  std::array<char, signature.size()> header{};
  if (!read(std::span(header)) || header != signature) {
    return {};
  }

  ShaderPack pack;
  while (remaining) {
    std::array<uint32_t, 7> metadata{};
    std::array<uint64_t, 1> hash{};
    if (!read(std::span(metadata)) || !read(std::span(hash))) {
      return {};
    }
    const auto [pixel, xenos_dwords, spirv_dwords, elements, samplers, outputs, kills] = metadata;
    if (pixel > 1 || !xenos_dwords || xenos_dwords > 0xFFFF || xenos_dwords % 3 ||
        spirv_dwords < 5 || (uint64_t(xenos_dwords) + spirv_dwords + elements + samplers) * 4 > remaining ||
        elements > 16 || samplers > 16 || outputs > 31 || (pixel && elements)) {
      return {};
    }
    const auto [iterator, added] = pack.try_emplace({pixel != 0, hash[0]});
    if (!added) {
      return {};
    }
    auto& shader = iterator->second;
    auto& entry = shader.entry;
    entry.vertices = !pixel;
    entry.numero = static_cast<uint32_t>(pack.size());
    entry.huella = hash[0];
    entry.salidas = outputs;
    entry.kills = kills;
    entry.descarta = kills > 1;
    for (uint32_t i = 0; i < elements; ++i) {
      std::array<uint32_t, 1> element{};
      if (!read(std::span(element)) || (element[0] & 0xFFFF) >= xenos_dwords / 3) {
        return {};
      }
      entry.elementos.push_back({uint16_t(element[0]), uint8_t(element[0] >> 16), uint8_t(element[0] >> 24)});
    }
    for (uint32_t i = 0; i < samplers; ++i) {
      std::array<uint16_t, 2> sampler{};
      if (!read(std::span(sampler)) || sampler[0] >= 16 || sampler[1] < 12 || sampler[1] > 14) {
        return {};
      }
      entry.samplers.push_back({sampler[0], sampler[1], false});
    }
    entry.microcodigo.resize(xenos_dwords);
    shader.spirv.resize(spirv_dwords);
    const std::span microcode(entry.microcodigo);
    if (!read(microcode) || XXH3_64bits(microcode.data(), microcode.size_bytes()) != hash[0] ||
        !read(std::span(shader.spirv)) || shader.spirv[0] != 0x07230203) {
      return {};
    }
    for (auto& word : microcode) {
      word = rex::byte_swap(word);
    }
    entry.spirv = shader.spirv;
  }
  return pack;
}

class SwitchCommandProcessor final : public gpu::CommandProcessor {
 public:
  explicit SwitchCommandProcessor(gpu::GraphicsSystem* system)
      : CommandProcessor(system, system->kernel_state()) {}

 private:
  bool SetupContext() override {
    auto* provider = static_cast<rex::deko3d::ProveedorDeko3d*>(graphics_system_->provider());
    renderer_ = rex::x360::CrearDestinosDeko3d(provider, memory_);
    if (!renderer_) {
      REXLOG_ERROR("WFC: deko3d command processor initialization failed");
      return false;
    }

    const auto folder = rex::filesystem::GetExecutableFolder();
    shaders_ = ReadShaderPack(folder / "wfc-shaders.bin");
    if (shaders_.empty()) {
      REXLOG_WARN("WFC: shader pack is missing or invalid");
    } else {
      REXLOG_INFO("WFC: loaded {} translated shaders", shaders_.size());
    }
    const auto path = folder / "logs" /
                      ("wfc-shaders-" + std::to_string(WFC_BUILD_ATTEMPT) + ".xsh");
    shader_capture_.open(path, std::ios::binary | std::ios::trunc);
    if (shader_capture_) {
      // ReXGlue XESH v0x20201219. Record hashes and lengths use host byte order.
      constexpr std::array<unsigned char, 8> header{'X', 'E', 'S', 'H', 0x20, 0x20, 0x12, 0x19};
      shader_capture_.write(reinterpret_cast<const char*>(header.data()), header.size());
    } else {
      REXLOG_WARN("WFC: could not open shader capture at {}", path.string());
    }
    REXLOG_INFO("WFC: deko3d command processor ready");
    return true;
  }

  void ShutdownContext() override {
    shader_capture_.close();
    renderer_.reset();
  }

  void OnGammaRamp256EntryTableValueWritten() override { display_gamma_dirty_ = true; }

  void UpdateDisplayGamma(gpu::xenos::TextureFormat format) {
    const bool uses_pwl = format == gpu::xenos::TextureFormat::k_2_10_10_10 ||
                          format == gpu::xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;
    if (uses_pwl ? !display_gamma_active_ : !display_gamma_dirty_) {
      return;
    }
    std::array<std::array<uint16_t, 3>, 256> ramp;
    const auto* table = gamma_ramp_256_entry_table();
    int max_delta = 0;
    for (uint32_t i = 0; i < ramp.size(); ++i) {
      const uint16_t linear = uint16_t((i * 1023 + 127) / 255);
      const auto& entry = table[i];
      if (uses_pwl) {
        // The current deko output shader has only the 256-entry lookup.
        ramp[i] = {linear, linear, linear};
      } else {
        ramp[i] = {entry.color_10_red, entry.color_10_green, entry.color_10_blue};
        for (const uint16_t channel : ramp[i]) {
          max_delta = std::max(max_delta, std::abs(int(channel) - linear));
        }
      }
    }
    renderer_->RampaGamma(ramp);
    display_gamma_active_ = !uses_pwl;
    display_gamma_dirty_ = uses_pwl;
    if (!uses_pwl) {
      const auto& mid = ramp[128];
      REXLOG_INFO("WFC: applied 256-entry display gamma at swap {}, format {}, "
                  "midpoint RGB {}/{}/{}, max delta from linear {} (10-bit)",
                  frames_, uint32_t(format), mid[0], mid[1], mid[2], max_delta);
    }
  }

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t width, uint32_t height) override {
    if (!renderer_ || !width || !height) {
      return;
    }
    const auto texture = rex::x360::LeerTexturaSwap(register_file_->values);
    const auto format = gpu::xenos::TextureFormat(texture.dword[1] & 0x3F);
    UpdateDisplayGamma(format);
    const auto presented = renderer_->Presentar(graphics_system_->presenter(), texture, width, height);
    if (!first_swap_) {
      first_swap_ = true;
      REXLOG_INFO("WFC: first GPU swap, front buffer 0x{:08X}, {}x{}, presented={}",
                  frontbuffer_ptr, width, height, presented);
    }
    if (presented && !first_present_) {
      first_present_ = true;
      REXLOG_INFO("WFC: deko3d presented a guest front buffer");
    }
    if (++frames_ % 120 == 0 && missing_shader_draws_) {
      REXLOG_INFO("WFC: {} draws skipped for untranslated shaders in the last 120 frames", missing_shader_draws_);
      missing_shader_draws_ = 0;
    }
  }

  bool IssueCopy() override {
    if (!renderer_) {
      return false;
    }
    const auto registers = rex::x360::LeerRegistrosCopia(register_file_->values);
    const auto did_copy = renderer_->Copiar(registers);
    const gpu::reg::RB_COPY_CONTROL control{registers.rb_copy_control};
    if (control.copy_src_select < gpu::xenos::kMaxColorRenderTargets &&
        (control.copy_command == gpu::xenos::CopyCommand::kRaw ||
         control.copy_command == gpu::xenos::CopyCommand::kConvert)) {
      const auto source = (registers.rb_color_info[control.copy_src_select] >> 16) & 0xF;
      const gpu::reg::RB_COPY_DEST_INFO destination{registers.rb_copy_dest_info};
      const auto format = uint32_t(destination.copy_dest_format);
      if (resolve_formats_.size() < 64 && resolve_formats_.emplace(source, format).second) {
        REXLOG_INFO("WFC: color resolve {} to {}, exponent {}, processed={}", source, format,
                    int32_t(destination.copy_dest_exp_bias), did_copy);
      }
    }
    if (!first_copy_) {
      first_copy_ = true;
      REXLOG_INFO("WFC: first GPU clear or resolve, processed={}", did_copy);
    }
    return did_copy;
  }

  bool IssueDraw(gpu::xenos::PrimitiveType primitive, uint32_t index_count, IndexBufferInfo*, bool) override {
    if (rex::x360::EsCopia(register_file_->values)) {
      return IssueCopy();
    }
    const auto mode = register_file_->Get<gpu::reg::RB_MODECONTROL>().edram_mode;
    const bool color_draw = mode == gpu::xenos::EdramMode::kColorDepth;
    if (!renderer_ || (!color_draw && mode != gpu::xenos::EdramMode::kDepthOnly)) {
      return true;
    }
    const auto surface = register_file_->Get<gpu::reg::RB_SURFACE_INFO>();
    const auto msaa = surface.msaa_samples;
    if (surface.surface_pitch == 1040 && reported_scene_bins_.size() < 12 &&
        reported_scene_bins_.emplace(bin_select_, bin_mask_, msaa).second) {
      REXLOG_INFO("WFC scene bin state at swap {}: select {:016X}, mask {:016X}, MSAA mode {}",
                  frames_, bin_select_, bin_mask_, uint32_t(msaa));
    }
    if (!vertex_ || (color_draw && !pixel_)) {
      if ((!vertex_ && vertex_key_) || (color_draw && !pixel_ && pixel_key_)) {
        ++missing_shader_draws_;
        if (reported_missing_draws_.size() < 64 && reported_missing_draws_.emplace(vertex_key_, pixel_key_).second) {
          REXLOG_INFO("WFC: untranslated draw VS {:016x}, PS {:016x}, primitive {}, color mask {:04x}, depth {:08x}",
                      vertex_key_, pixel_key_, uint32_t(primitive), register_file_->Get<gpu::reg::RB_COLOR_MASK>().value,
                      register_file_->Get<gpu::reg::RB_DEPTHCONTROL>().value);
        }
      }
      return true;
    }
    if (primitive == gpu::xenos::PrimitiveType::kPointList && reported_point_draws_.size() < 16 &&
        reported_point_draws_.emplace(vertex_key_, pixel_key_).second) {
      REXLOG_INFO("WFC: point draw at swap {} VS {:016X}, PS {:016X}, count {}, size {:08X}, mask {:04X}, depth {:08X}",
                  frames_, vertex_key_, pixel_key_, index_count,
                  register_file_->Get<gpu::reg::PA_SU_POINT_SIZE>().value,
                  register_file_->Get<gpu::reg::RB_COLOR_MASK>().value,
                  register_file_->Get<gpu::reg::RB_DEPTHCONTROL>().value);
    }
    const uint64_t generation = ++draw_generation_;
    const rex::x360::PeticionDibujo draw{
        .registros = register_file_->values,
        .vs = vertex_,
        .ps = pixel_,
        // The pack is keyed by exact patched microcode, so this is also the live fetch layout.
        .vs_microcodigo = vertex_->microcodigo,
        .generacion_vs = vertex_->numero,
        .generacion_constantes_vs = generation,
        .generacion_constantes_ps = generation,
        .generacion_fetch = generation,
        .generacion_encuadre = generation,
    };
    if (renderer_->Dibujar(draw)) {
      if (!first_draw_) {
        first_draw_ = true;
        REXLOG_INFO("WFC: deko3d accepted a shader draw (VS {:016x}, PS {:016x})",
                    vertex_->huella, pixel_ ? pixel_->huella : 0);
      }
    } else if (!first_rejected_draw_) {
      first_rejected_draw_ = true;
      REXLOG_WARN("WFC: deko3d rejected a shader draw (VS {:016x}, PS {:016x})",
                  vertex_->huella, pixel_ ? pixel_->huella : 0);
    }
    // The renderer records skipped draws by cause. Keep processing the ring during bring-up.
    return true;
  }

  gpu::Shader* LoadShader(gpu::xenos::ShaderType type, uint32_t, const uint32_t* code,
                          uint32_t dwords) override {
    const bool pixel = type == gpu::xenos::ShaderType::kPixel;
    auto*& active = pixel ? pixel_ : vertex_;
    auto& active_key = pixel ? pixel_key_ : vertex_key_;
    active = nullptr;
    active_key = 0;
    if (!dwords || dwords > 0xFFFF) {
      return nullptr;
    }
    const size_t byte_size = size_t(dwords) * sizeof(*code);
    const ShaderKey key{pixel, XXH3_64bits(code, byte_size)};
    active_key = key.second;
    if (const auto it = shaders_.find(key); it != shaders_.end()) {
      active = &it->second.entry;
    }

    constexpr size_t capture_limit = 8 * 1024 * 1024;
    constexpr size_t record_header_size = sizeof(uint64_t) + sizeof(uint32_t);
    if (!shader_capture_ || capture_bytes_ + record_header_size + byte_size > capture_limit) {
      return nullptr;
    }
    if (captured_.insert(key).second) {
      if (!active) {
        REXLOG_DEBUG("WFC: captured untranslated {} {:016x}", pixel ? "PS" : "VS", key.second);
      }
      const uint32_t type_and_size = (uint32_t(pixel) << 31) | dwords;
      shader_capture_.write(reinterpret_cast<const char*>(&key.second), sizeof(key.second));
      shader_capture_.write(reinterpret_cast<const char*>(&type_and_size), sizeof(type_and_size));
      shader_capture_.write(reinterpret_cast<const char*>(code), byte_size);
      shader_capture_.flush();
      capture_bytes_ += record_header_size + byte_size;
    }
    return nullptr;
  }

  std::unique_ptr<rex::x360::DestinosNativos> renderer_;
  ShaderPack shaders_;
  const rex::x360::EntradaShader* vertex_ = nullptr;
  const rex::x360::EntradaShader* pixel_ = nullptr;
  std::ofstream shader_capture_;
  std::set<ShaderKey> captured_;
  std::set<std::pair<uint32_t, uint32_t>> resolve_formats_;
  std::set<std::pair<uint64_t, uint64_t>> reported_missing_draws_;
  std::set<std::pair<uint64_t, uint64_t>> reported_point_draws_;
  std::set<std::tuple<uint64_t, uint64_t, gpu::xenos::MsaaSamples>> reported_scene_bins_;
  size_t capture_bytes_ = 0;
  uint64_t draw_generation_ = 0;
  uint64_t vertex_key_ = 0;
  uint64_t pixel_key_ = 0;
  uint64_t frames_ = 0;
  uint64_t missing_shader_draws_ = 0;
  bool display_gamma_dirty_ = false;
  bool display_gamma_active_ = false;
  bool first_swap_ = false;
  bool first_present_ = false;
  bool first_copy_ = false;
  bool first_draw_ = false;
  bool first_rejected_draw_ = false;
};

class SwitchGraphicsSystem final : public gpu::GraphicsSystem {
 public:
  std::string name() const override { return "WFC deko3d"; }

 private:
  void CreateProvider(bool) override {
    provider_ = rex::deko3d::ProveedorDeko3d::Crear();
  }

  std::unique_ptr<gpu::CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<SwitchCommandProcessor>(this);
  }
};

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> CreateSwitchGraphicsSystem() {
  return std::make_unique<SwitchGraphicsSystem>();
}
