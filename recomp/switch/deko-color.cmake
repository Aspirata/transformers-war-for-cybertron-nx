# Extend the pinned SDK's existing render target and resolve paths.
set(deko_targets "${REXSDK_DIR}/src/x360/deko/destinos.cpp")
wfc_overlay_sdk_file("${deko_targets}" "${wfc_sdk_overlay}/destinos.cpp"
    "#include <rex/x360/deko/destinos.h>"
    "#include <rex/x360/deko/destinos.h>\n#include \"src/deko-color.h\""
    [=[void RenderDk::PrepararDestino(ImagenDk& imagen) {
  AsegurarTrabajo();
  MarcarGpu(kGpuBorrados);
  // The values as the game's clears write them: color 0, and depth 1.0 with stencil 0 as RB_DEPTH_CLEAR.
  borrado_previo_[imagen.serie] = {imagen.profundidad ? uint64_t(0xFFFFFF00u) : 0, dibujados_};]=]
    [=[void RenderDk::PrepararDestino(ImagenDk& imagen, bool reverse_depth) {
  AsegurarTrabajo();
  MarcarGpu(kGpuBorrados);
  // Keep the cached clear word consistent with the actual initial depth and stencil.
  borrado_previo_[imagen.serie] = {imagen.profundidad && !reverse_depth ? uint64_t(0xFFFFFF00u) : 0, dibujados_};]=]
    [=[  imagen.banda_sucia = false;
  imagen.borrado_z = 1.0f;
  imagen.borrado_estencil = 0;]=]
    [=[  imagen.banda_sucia = false;
  imagen.borrado_z = reverse_depth ? 0.0f : 1.0f;
  imagen.borrado_estencil = 0;]=]
    [=[    dkCmdBufClearDepthStencil(cmd, true, 1.0f, 0xFF, 0);]=]
    [=[    dkCmdBufClearDepthStencil(cmd, true, imagen.borrado_z, 0xFF, 0);]=]
    [=[ImagenDk* RenderDk::DestinoProfundidad(uint32_t base, uint32_t formato, uint32_t pitch) {
  if (!pitch) {
    Rechazar(11, "sin destino de profundidad (pitch 0)");
    return nullptr;
  }
  const uint64_t clave = (uint64_t(base) << 20) | (uint64_t(formato) << 16) | pitch;]=]
    [=[ImagenDk* RenderDk::DestinoProfundidad(uint32_t base, uint32_t formato, uint32_t pitch, uint32_t scene_depth_mode) {
  if (!pitch) {
    Rechazar(11, "sin destino de profundidad (pitch 0)");
    return nullptr;
  }
  const uint64_t clave = (uint64_t(base) << 20) | (uint64_t(scene_depth_mode) << 17) |
                         (uint64_t(formato) << 16) | pitch;]=]
    [=[  ImagenDk& nueva = profundidades_.emplace(clave, imagen).first->second;
  PrepararDestino(nueva);]=]
    [=[  ImagenDk& nueva = profundidades_.emplace(clave, imagen).first->second;
  // Run 017 showed WFC's 1040-wide scene using GEQUAL with writes to this depth target.
  const bool reverse_depth = wfc::deko::IsSceneReverseDepth(base, formato, pitch);
  PrepararDestino(nueva, reverse_depth);
  if (reverse_depth) {
    wfc_scene_depth_frames_[scene_depth_mode] = fotograma_;
    REXLOG_INFO("WFC: initialized scene reverse-Z depth target to {} (MSAA mode {})", nueva.borrado_z,
                scene_depth_mode);
  }]=]
    [=[  const uint32_t info = reg.rb_depth_info;
  ImagenDk* profundidad = DestinoProfundidad(info & 0xFFF, (info >> 16) & 0x1, pitch);]=]
    [=[  const uint32_t info = reg.rb_depth_info;
  const uint32_t native_pitch = wfc::deko::ShadowAtlasClearPitch(info, pitch, reg.rb_surface_info);
  if (info == wfc::deko::kShadowAtlasDepthInfo &&
      (!wfc_reported_shadow_atlas_clear_ || wfc_frame_capture_.Trace(fotograma_))) {
    wfc_reported_shadow_atlas_clear_ = true;
    REXLOG_INFO("WFC frame {} shadow atlas depth clear pitch {} mode {} -> pitch {}, clear {:08X}",
                fotograma_, pitch, (reg.rb_surface_info >> 16) & 3, native_pitch, reg.rb_depth_clear);
  }
  pitch = native_pitch;
  const uint32_t scene_depth_mode = wfc::deko::SceneDepthMode(info & 0xFFF, (info >> 16) & 1, pitch, reg.rb_surface_info);
  ImagenDk* profundidad = DestinoProfundidad(info & 0xFFF, (info >> 16) & 1, pitch, scene_depth_mode);]=]
    [=[  ++copias_profundidad_;
  ImagenDk* profundidad = DestinoProfundidad(info & 0xFFF, (info >> 16) & 0x1, pitch);]=]
    [=[  ++copias_profundidad_;
  const uint32_t scene_depth_mode = wfc::deko::SceneDepthMode(info & 0xFFF, (info >> 16) & 1, pitch, reg.rb_surface_info);
  ImagenDk* profundidad = DestinoProfundidad(info & 0xFFF, (info >> 16) & 1, pitch, scene_depth_mode);]=]
    [=[  SombraMinimoAntesDeBorrar(*profundidad);]=]
    [=[  if (wfc::deko::IsSceneReverseDepth(info & 0xFFF, (info >> 16) & 0x1, pitch)) {
    wfc_scene_depth_frames_[scene_depth_mode] = fotograma_;
  }
  if (info == wfc::deko::kShadowAtlasDepthInfo && pitch == wfc::deko::kShadowAtlasPitch) {
    wfc_shadow_atlas_needs_clear_ = false;
  }
  SombraMinimoAntesDeBorrar(*profundidad);]=]
    [=[bool RenderDk::Presentar(rex::ui::Presenter* presentador, const TexturaSwap& swap, uint32_t ancho, uint32_t alto) {]=]
    [=[void RenderDk::WfcMergeLateSceneDepth() {
  // Run 027 resolves the 1x prepass before the 2x scene, then samples that depth in DOF.
  if (wfc_scene_depth_prepass_frame_ != fotograma_ || wfc_scene_depth_frames_[1] != fotograma_ ||
      wfc_scene_depth_merged_frame_ == fotograma_) {
    return;
  }
  const auto it = resueltas_.find(0x189DD000);
  if (it == resueltas_.end() || !it->second.escrita || it->second.formato_guest != 23) {
    return;
  }
  ImagenDk& resolved = it->second.imagen;
  if (!resolved.profundidad || resolved.ancho != 1040 || resolved.alto != 592) {
    return;
  }
  const auto* late = DestinoProfundidad(0, uint32_t(xenos::DepthRenderTargetFormat::kD24FS8), 1040, 1);
  if (!late || late->contenido_invalido || late->borrado_pendiente || late->serie == resolved.serie ||
      !Copiar3D(*late, 0, 0, resolved.ancho, resolved.alto, resolved, 0, 0, 0, 0, 0, true)) {
    return;
  }
  ++resolved.version;
  wfc_scene_depth_merged_frame_ = fotograma_;
  if (!wfc_reported_late_scene_depth_ || wfc_frame_capture_.Active(fotograma_)) {
    wfc_reported_late_scene_depth_ = true;
    REXLOG_INFO("WFC frame {} merged late reverse-Z depth into scene resolve 189DD000, version {}",
                fotograma_, resolved.version);
  }
}

void RenderDk::WfcBindNeutralLightMask(const uint32_t* registers, const EntradaShader& shader,
                                       std::array<DkResHandle, 32>& textures) {
  if (wfc_shadow_depth_frame_ == fotograma_ && wfc_shadow_mask_initialized_frame_ == fotograma_) {
    return;
  }
  namespace gr = rex::graphics;
  // Keep the shadow texture intact for its producer passes and only substitute its RGB
  // channels when a material reads it in the observed HDR scene pass.
  for (const auto& sampler : shader.samplers) {
    const auto* fetch = registers + gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + sampler.registro * 6;
    if ((fetch[0] & 3) != uint32_t(xenos::FetchConstantType::kTexture) ||
        !wfc::deko::IsSceneLightMaskFetch(fetch[1], fetch[2], fetch[5])) {
      continue;
    }
    const auto it = resueltas_.find(fetch[1] & 0x1FFFF000);
    if (it == resueltas_.end() || !it->second.escrita || it->second.formato_guest != 6) {
      continue;
    }
    const auto& image = it->second.imagen;
    if (image.formato != DkImageFormat_RGBA8_Unorm || image.ancho != wfc::deko::kShadowMaskWidth ||
        image.alto != wfc::deko::kShadowMaskHeight) {
      continue;
    }
    const uint32_t swizzle = wfc::deko::NeutralLightMaskSwizzle((fetch[3] >> 1) & 0xFFF);
    if (const uint32_t view = VistaDe(image, swizzle, image.intercambio_rb ? 0x60A : 0x688)) {
      textures[sampler.registro] = dkMakeTextureHandle(view, textures[sampler.registro] >> 20);
      if (!wfc_reported_neutral_light_mask_) {
        wfc_reported_neutral_light_mask_ = true;
        REXLOG_INFO("WFC frame {} bound neutral light mask RGB for HDR materials, PS {:016X}, slot {}",
                    fotograma_, shader.huella, sampler.registro);
      }
    }
  }
}

void RenderDk::WfcTraceShadowInputs(const uint32_t* registers) {
  if (wfc_shadow_inputs_frame_ == fotograma_) {
    return;
  }
  wfc_shadow_inputs_frame_ = fotograma_;
  namespace gr = rex::graphics;
  for (uint32_t slot = 0; slot < 3; ++slot) {
    const auto* fetch = registers + gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + slot * 6;
    const uint32_t base = fetch[1] & 0x1FFFF000;
    const uint32_t width = (fetch[2] & 0x1FFF) + 1;
    const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
    const auto it = resueltas_.find(base);
    const bool resolved = it != resueltas_.end() && it->second.escrita;
    REXLOG_INFO("WFC frame {} shadow projector slot {} base {:08X}, fetch format {} {}x{}, "
                "swizzle {:03X}, resolved version {}",
                fotograma_, slot, base, fetch[1] & 0x3F, width, height, (fetch[3] >> 1) & 0xFFF,
                resolved ? it->second.imagen.version : 0);
  }
  const auto* projection = registers + gr::XE_GPU_REG_SHADER_CONSTANT_256_X;
  for (uint32_t c = 1; c <= 6; ++c) {
    const auto* value = projection + c * 4;
    REXLOG_INFO("WFC frame {} shadow projection c{} ({},{},{},{})", fotograma_, c,
                std::bit_cast<float>(value[0]), std::bit_cast<float>(value[1]),
                std::bit_cast<float>(value[2]), std::bit_cast<float>(value[3]));
  }
}

void RenderDk::WfcPopulateShadowDepth(ImagenDk& image) {
  if (wfc_shadow_depth_written_frame_ == fotograma_) {
    wfc_shadow_depth_frame_ = fotograma_;
    return;
  }
  if (wfc_scene_depth_prepass_frame_ != fotograma_ || wfc_scene_depth_merged_frame_ == fotograma_ ||
      wfc_shadow_depth_frame_ == fotograma_) {
    return;
  }
  const auto full = resueltas_.find(0x189DD000);
  if (full == resueltas_.end() || !full->second.escrita || full->second.formato_guest != 23) {
    return;
  }
  const auto& source = full->second.imagen;
  if (!source.profundidad || source.ancho != 1040 || source.alto != 592 ||
      !Copiar3D(source, 0, 0, 520, 296, image, 0, 0, 0, source.ancho, source.alto)) {
    return;
  }
  invalidar_zcull_ = true;
  wfc_shadow_depth_frame_ = fotograma_;
  if (!wfc_reported_shadow_depth_ || wfc_frame_capture_.Active(fotograma_)) {
    wfc_reported_shadow_depth_ = true;
    REXLOG_INFO("WFC frame {} populated shadow scene depth 520x296 from prepass 189DD000 version {}",
                fotograma_, source.version);
  }
}

void RenderDk::WfcCaptureDepth(const ImagenDk& image, wfc::deko::FrameCapture::Stage stage,
                                uint32_t width, uint32_t height, uint32_t source_width, uint32_t source_height) {
  if (!wfc_depth_preview_.viva &&
      !CrearImagen(wfc_depth_preview_, DkImageType_2D, DkImageFormat_RGBA16_Float,
                   wfc::deko::kShadowMaskWidth, wfc::deko::kShadowMaskHeight, 1, 1,
                   DkImageFlags_UsageRender, "WFC shadow depth preview")) {
    return;
  }
  if (Copiar3D(image, 0, 0, width, height, wfc_depth_preview_, 0, 0, 0, source_width, source_height)) {
    wfc_frame_capture_.Record(nucleo_, stage, wfc_depth_preview_.imagen, wfc_depth_preview_.formato,
                              width, height, false);
  }
}

void RenderDk::WfcCaptureLightMask(const uint32_t* registers) {
  if (wfc_light_mask_frame_ == fotograma_) {
    return;
  }
  wfc_light_mask_frame_ = fotograma_;
  namespace gr = rex::graphics;
  const auto* fetch = registers + gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
  const uint32_t base = fetch[1] & 0x1FFFF000;
  const uint32_t width = (fetch[2] & 0x1FFF) + 1;
  const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
  const auto it = resueltas_.find(base);
  if (it == resueltas_.end() || !it->second.escrita || it->second.imagen.profundidad) {
    REXLOG_INFO("WFC frame {} light mask slot 0 base {:08X}, fetch format {} {}x{}, no resolved color image",
                fotograma_, base, fetch[1] & 0x3F, width, height);
    return;
  }
  const auto& image = it->second.imagen;
  const auto* c1 = registers + gr::XE_GPU_REG_SHADER_CONSTANT_256_X + 4;
  const auto* c5 = c1 + 16;
  REXLOG_INFO("WFC frame {} original light mask slot 0 base {:08X}, fetch format {} {}x{}, resolve format {} {}x{} "
              "version {}, c1 ({},{},{},{}), c5.xy ({},{})",
              fotograma_, base, fetch[1] & 0x3F, width, height, it->second.formato_guest, image.ancho,
              image.alto, image.version, std::bit_cast<float>(c1[0]), std::bit_cast<float>(c1[1]),
              std::bit_cast<float>(c1[2]), std::bit_cast<float>(c1[3]),
              std::bit_cast<float>(c5[0]), std::bit_cast<float>(c5[1]));
  wfc_frame_capture_.Record(nucleo_, wfc::deko::FrameCapture::Stage::kLightMask, image.imagen, image.formato,
                            image.ancho, image.alto, image.intercambio_rb);
}

void RenderDk::WfcCaptureShaderInputs(const uint32_t* registers, const EntradaShader& shader) {
  using Capture = wfc::deko::FrameCapture;
  namespace gr = rex::graphics;
  const bool composite = shader.huella == 0x78D7485B6D8AC825ull;
  const bool scene = shader.huella == 0xF44F44E979D7DCEBull;
  if ((!scene && !composite) || !wfc_frame_capture_.Trace(fotograma_)) {
    return;
  }
  const char* pass = composite ? "composite" : "scene postprocess";
  REXLOG_INFO("WFC frame {} {} SQ {:08X}/{:08X}, viewport {:08X}/{:08X}, scissor {:08X}/{:08X}",
              fotograma_, pass, registers[gr::XE_GPU_REG_SQ_PROGRAM_CNTL], registers[gr::XE_GPU_REG_SQ_CONTEXT_MISC],
              registers[gr::XE_GPU_REG_PA_CL_VPORT_XSCALE], registers[gr::XE_GPU_REG_PA_CL_VPORT_YSCALE],
              registers[gr::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL], registers[gr::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR]);
  if (composite) {
    const auto* constants = registers + gr::XE_GPU_REG_SHADER_CONSTANT_256_X;
    REXLOG_INFO("WFC frame {} composite weights c0.xy ({},{}), c4.xy ({},{}), edge threshold {}",
                fotograma_, std::bit_cast<float>(constants[0]), std::bit_cast<float>(constants[1]),
                std::bit_cast<float>(constants[16]), std::bit_cast<float>(constants[17]),
                std::bit_cast<float>(constants[12]));
  }
  for (const auto& sampler : shader.samplers) {
    const auto* fetch = registers + gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + sampler.registro * 6;
    const uint32_t format = fetch[1] & 0x3F;
    const uint32_t base = fetch[1] & 0x1FFFF000;
    if (((fetch[5] >> 9) & 3) != uint32_t(xenos::DataDimension::k2DOrStacked)) {
      REXLOG_INFO("WFC frame {} {} slot {} base {:08X} format {} non-2D", fotograma_, pass, sampler.registro,
                  base, format);
      continue;
    }
    const uint32_t width = (fetch[2] & 0x1FFF) + 1;
    const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
    const auto it = resueltas_.find(base);
    if (it == resueltas_.end() || !it->second.escrita) {
      REXLOG_INFO("WFC frame {} {} slot {} base {:08X} format {} {}x{} no resolved image",
                  fotograma_, pass, sampler.registro, base, format, width, height);
      continue;
    }
    const auto& image = it->second.imagen;
    REXLOG_INFO("WFC frame {} {} slot {} base {:08X} fetch format {} {}x{} resolve format {} {}x{} version {}",
                fotograma_, pass, sampler.registro, base, format, width, height,
                it->second.formato_guest, image.ancho, image.alto, image.version);
    // The postprocess also samples depth, a smaller blurred image and a 3D LUT.
    if (composite || (!image.profundidad && width == 1040 && height == 592)) {
      wfc_frame_capture_.Record(nucleo_, composite ? Capture::Stage::kComposite : Capture::Stage::kScene,
                                image.imagen, image.formato, image.ancho, image.alto, image.intercambio_rb);
    }
  }
}

void RenderDk::WfcCapturePresentedFrame(const ImagenDk& image, uint32_t width, uint32_t height) {
  if (wfc_frame_capture_.Active(fotograma_)) {
    wfc_frame_capture_.Record(nucleo_, wfc::deko::FrameCapture::Stage::kFront, image.imagen, image.formato,
                              width, height, false);
  }
}

void RenderDk::WfcSampleFrame(const char* stage, const ImagenDk& image, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height) {
  if (!wfc_frame_probe_.WantsSample(fotograma_, image.formato, width, height) ||
      x > image.ancho || width > image.ancho - x || y > image.alto || height > image.alto - y) {
    return;
  }
  // Order the diagnostic copy after rendering and before any later overwrite.
  trabajo_3d_sin_barrera_ = true;
  AntesDeTransferir();
  wfc_frame_probe_.Record(nucleo_, stage, image.imagen, image.formato, x, y, width, height);
}

bool RenderDk::Presentar(rex::ui::Presenter* presentador, const TexturaSwap& swap, uint32_t ancho, uint32_t alto) {]=]
    [=[      pintado = fuente && PintarSalida(contexto.imagen(), contexto.ancho(), contexto.alto(), *fuente, w, h);]=]
    [=[      if (fuente) {
        WfcSampleFrame("front-buffer", *fuente, 0, 0, w, h);
        WfcCapturePresentedFrame(*fuente, w, h);
      }
      pintado = fuente && PintarSalida(contexto.imagen(), contexto.ancho(), contexto.alto(), *fuente, w, h);]=]
    [=[  ++fotograma_;
  geometria_.NuevoFotograma(fotograma_);]=]
    [=[  wfc_frame_probe_.Report(nucleo_, fotograma_);
  wfc_frame_capture_.Save(nucleo_, fotograma_);
  wfc_hdr_edge_trace_.Report(nucleo_, fotograma_);
  ++fotograma_;
  geometria_.NuevoFotograma(fotograma_);]=]
    [=[  profundidad->borrado_z = float(reg.rb_depth_clear >> 8) / 16777215.0f;]=]
    [=[  const uint32_t depth_bits = reg.rb_depth_clear >> 8;
  const auto depth_format = rex::graphics::reg::RB_DEPTH_INFO{info}.depth_format;
  // This backend keeps draw depth in the host's 0..1 range.
  profundidad->borrado_z = depth_format == xenos::DepthRenderTargetFormat::kD24FS8
                               ? std::clamp(xenos::Float20e4To32(depth_bits), 0.0f, 1.0f)
                               : float(depth_bits) / 16777215.0f;
  if (depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    const float previous_decode = float(depth_bits) / 16777215.0f;
    auto& reported = wfc_reported_float_depth_clear_[previous_decode != profundidad->borrado_z];
    if (!reported) {
      reported = true;
      REXLOG_INFO("WFC: float depth clear, target {:03X}, raw {:06X}, decoded {} (previously {})",
                  info & 0xFFF, depth_bits, profundidad->borrado_z, previous_decode);
    }
  }]=]
    [=[  const bool entero = x0 == 0 && y0 == 0 && dx == 0 && dy == 0 && ancho == profundidad->ancho &&]=]
    [=[  if (base == 0x189DD000 && scene_depth_mode == 0 &&
      wfc::deko::IsSceneReverseDepth(info & 0xFFF, (info >> 16) & 1, pitch) &&
      x0 == 0 && y0 == 0 && ancho == 1040 && alto == 592 && !borrara) {
    wfc_scene_depth_prepass_frame_ = fotograma_;
  }
  if (((pitch == 1040 && ancho >= 960) || base == 0x18933000 || base == wfc::deko::kShadowAtlasAddress) &&
      wfc_frame_capture_.Trace(fotograma_)) {
    REXLOG_INFO("WFC frame {} depth resolve EDRAM {:03X} ({},{}) {}x{} to {:08X} ({},{}) {}x{} format {} clear {} MSAA mode {}",
                fotograma_, info & 0xFFF, x0, y0, ancho, alto, base, dx, dy,
                resuelta->imagen.ancho, resuelta->imagen.alto, formato_textura, borrara, scene_depth_mode);
  }
  const bool entero = x0 == 0 && y0 == 0 && dx == 0 && dy == 0 && ancho == profundidad->ancho &&]=]
    [=[  SombraMinimoCopiaDesde(*profundidad);]=]
    [=[  if (base == 0x18933000 && info == 0x00010000 && pitch == 560 && ((reg.rb_surface_info >> 16) & 3) == 0 &&
      x0 == 0 && y0 == 0 && ancho == 520 && alto == 296 && !borrara) {
    // Run 030 resolved this interpretation of tile zero before drawing any half-resolution depth.
    WfcPopulateShadowDepth(*profundidad);
    if (wfc_frame_capture_.Active(fotograma_)) {
      WfcCaptureDepth(*profundidad, wfc::deko::FrameCapture::Stage::kMaskDepth,
                      wfc::deko::kShadowMaskWidth, wfc::deko::kShadowMaskHeight);
    }
  }
  SombraMinimoCopiaDesde(*profundidad);]=]
    [=[  SombraMinimoResueltaEscrita(base);
  ++copias_;
  OlvidarEstado();
}]=]
    [=[  SombraMinimoResueltaEscrita(base);
  ++copias_;
  if (base == wfc::deko::kShadowAtlasAddress && dx == 0 && dy == 0 &&
      wfc_shadow_atlas_capture_frame_ != fotograma_ && wfc_frame_capture_.Active(fotograma_)) {
    wfc_shadow_atlas_capture_frame_ = fotograma_;
    WfcCaptureDepth(resuelta->imagen, wfc::deko::FrameCapture::Stage::kShadowAtlas,
                    std::min(ancho, 256u), std::min(alto, 256u), ancho, alto);
    REXLOG_INFO("WFC frame {} captured shadow atlas version {}, updated region {}x{}",
                fotograma_, resuelta->imagen.version, ancho, alto);
  }
  OlvidarEstado();
}]=]
    [=[constexpr const char* kFsCopiaProfundidad = R"(#version 460]=]
    [=[constexpr const char* kFsCopiaColorTipado = R"(#version 460
layout(binding = 0) uniform sampler2D origen;
layout(std140, binding = 0) uniform Copia {
  ivec4 desplazamiento;
  vec4 escala;
} copia;
layout(location = 0) out vec4 color;
void main() {
  vec2 destino = gl_FragCoord.xy - vec2(copia.desplazamiento.zw);
  ivec2 origen_pixel = ivec2(destino * copia.escala.xy) +
                       copia.desplazamiento.xy + copia.desplazamiento.zw;
  vec4 value = texelFetch(origen, origen_pixel, 0);
  if (copia.escala.w != 0.0) {
    // The host FP16 target can store values outside the guest 7e3 range.
    // Clamp in EDRAM units; the ordered comparison also maps NaNs to zero.
    value = mix(vec4(0.0), min(value, vec4(31.875, 31.875, 31.875, 1.0)),
                greaterThan(value, vec4(0.0)));
  }
  color = value * copia.escala.z;
}
)";
constexpr const char* kFsWfcShadowDepth = R"(#version 460
layout(binding = 0) uniform sampler2D origen;
layout(std140, binding = 0) uniform Copia {
  ivec4 desplazamiento;
} copia;

void main() {
  ivec2 p = ivec2(gl_FragCoord.xy) * 2 + copia.desplazamiento.xy;
  // In reverse-Z, the smaller value is farther away. Keep the farthest
  // sample at a silhouette, as the game's captured depth reduction does.
  float a = texelFetch(origen, p, 0).r;
  float b = texelFetch(origen, p + ivec2(1, 0), 0).r;
  float c = texelFetch(origen, p + ivec2(0, 1), 0).r;
  float d = texelFetch(origen, p + ivec2(1, 1), 0).r;
  gl_FragDepth = min(min(a, b), min(c, d));
}
)";
constexpr const char* kFsCopiaProfundidad = R"(#version 460]=]
    [=[ImagenDk& destino, uint32_t dx, uint32_t dy) {
  const bool z = destino.profundidad;]=]
    [=[ImagenDk& destino, uint32_t dx, uint32_t dy, int32_t exp_bias,
                        uint32_t source_width, uint32_t source_height, bool merge_reverse_depth,
                        bool source_7e3) {
  const bool z = destino.profundidad;]=]
    [=[  if (!vs_salida_valido_ || !(z ? fs_copia_profundidad_valido_ : fs_copia_color_valido_)) {
    return false;
  }
  AsegurarTrabajo();
  EmitirCieloPendiente();  // the copy binds other targets]=]
    [=[  if (!vs_salida_valido_) {
    return false;
  }
  const bool downsample_depth = z && source_width && source_height &&
                                source_width == ancho * 2 && source_height == alto * 2;
  const bool typed = !z && (source_7e3 || exp_bias || source_width || source_height ||
                           origen.formato != DkImageFormat_RGBA8_Unorm ||
                           destino.formato != DkImageFormat_RGBA8_Unorm);
  const DkShader* fragment;
  if (downsample_depth) {
    if (!wfc_shadow_depth_shader_attempted_) {
      wfc_shadow_depth_shader_attempted_ = true;
      const auto translation = compilador_.Compilar(false, kFsWfcShadowDepth);
      wfc_shadow_depth_shader_valid_ =
          translation.ok && nucleo_.CargarShader(wfc_shadow_depth_shader_, translation.dksh, "WFC shadow scene depth");
      if (!wfc_shadow_depth_shader_valid_) {
        REXLOG_ERROR("WFC: could not initialize shadow depth downsample: {}", translation.log);
      }
    }
    if (!wfc_shadow_depth_shader_valid_) {
      return false;
    }
    fragment = &wfc_shadow_depth_shader_;
  } else if (typed) {
    if (!fs_copia_color_tipada_intentada_) {
      fs_copia_color_tipada_intentada_ = true;
      const auto translation = compilador_.Compilar(false, kFsCopiaColorTipado);
      fs_copia_color_tipada_valida_ =
          translation.ok && nucleo_.CargarShader(fs_copia_color_tipada_, translation.dksh, "WFC typed color resolve");
      if (!fs_copia_color_tipada_valida_) {
        REXLOG_ERROR("WFC: could not initialize the typed color resolve shader: {}", translation.log);
      }
    }
    if (!fs_copia_color_tipada_valida_) {
      return false;
    }
    fragment = &fs_copia_color_tipada_;
  } else {
    if (!(z ? fs_copia_profundidad_valido_ : fs_copia_color_valido_)) {
      return false;
    }
    fragment = z ? &fs_copia_profundidad_ : &fs_copia_color_;
  }
  AsegurarTrabajo();
  EmitirCieloPendiente();  // the copy binds other targets]=]
    [=[  const int32_t desplazamiento[4] = {int32_t(x0) - int32_t(dx), int32_t(y0) - int32_t(dy), 0, 0};
  std::memcpy(ubo.cpu, desplazamiento, sizeof(desplazamiento));]=]
    [=[  const int32_t depth_scale = downsample_depth ? 2 : 1;
  const int32_t offset[4] = {int32_t(x0) - int32_t(dx) * depth_scale,
                              int32_t(y0) - int32_t(dy) * depth_scale,
                              typed ? int32_t(dx) : 0, typed ? int32_t(dy) : 0};
  std::memcpy(ubo.cpu, offset, sizeof(offset));
  if (typed) {
    const float scale[4] = {source_width ? float(source_width) / float(ancho) : 1.0f,
                            source_height ? float(source_height) / float(alto) : 1.0f,
                            std::ldexp(1.0f, exp_bias), source_7e3 ? 1.0f : 0.0f};
    std::memcpy(static_cast<uint8_t*>(ubo.cpu) + sizeof(offset), scale, sizeof(scale));
  }]=]
    [=[  ds.depthCompareOp = DkCompareOp_Always;
  ds.stencilTestEnable = false;]=]
    [=[  ds.depthCompareOp = merge_reverse_depth ? DkCompareOp_Gequal : DkCompareOp_Always;
  ds.stencilTestEnable = false;]=]
    [=[  const DkShader* shaders[2] = {&vs_salida_, z ? &fs_copia_profundidad_ : &fs_copia_color_};]=]
    [=[  const DkShader* shaders[2] = {&vs_salida_, fragment};]=]
    [=[  const uint64_t clave = (uint64_t(base) << 20) | (uint64_t(formato) << 16) | pitch;
  if (auto it = destinos_.find(clave);]=]
    [=[  const auto guest_format = wfc::deko::CanonicalTarget(wfc::deko::RenderFormat(formato));
  const auto host_format = wfc::deko::ColorTargetFormat(guest_format);
  if (host_format == DkImageFormat_None) {
    Rechazar(100 + formato, "unsupported WFC color render target");
    return nullptr;
  }
  const uint64_t clave = (uint64_t(base) << 20) | (uint64_t(guest_format) << 16) | pitch;
  if (auto it = destinos_.find(clave);]=]
    [=[  uint32_t flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
  if (REXCVAR_GET(nfsmw_dk_compresion_color)) {
    flags |= DkImageFlags_HwCompression;
  }
  if (!CrearImagen(imagen, DkImageType_2D, DkImageFormat_RGBA8_Unorm, pitch, alto, 1, 1, flags, "destino de color")) {]=]
    [=[  uint32_t flags = DkImageFlags_UsageRender;
  if (host_format == DkImageFormat_RGBA8_Unorm) {
    flags |= DkImageFlags_Usage2DEngine;
    if (REXCVAR_GET(nfsmw_dk_compresion_color)) {
      flags |= DkImageFlags_HwCompression;
    }
  }
  if (!CrearImagen(imagen, DkImageType_2D, host_format, pitch, alto, 1, 1, flags, "destino de color")) {]=]
    [=[  const DkImageFormat formato_host = profundidad ? DkImageFormat_Z24S8 : DkImageFormat_RGBA8_Unorm;]=]
    [=[  const DkImageFormat formato_host = profundidad
      ? DkImageFormat_Z24S8
      : wfc::deko::ColorResolveFormat(wfc::deko::ResolveFormat(formato)).format;
  if (formato_host == DkImageFormat_None) {
    Rechazar(500 + formato, "unsupported WFC color resolve texture");
    return nullptr;
  }]=]
    [=[                  : DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
  if (!CrearImagen(r.imagen, DkImageType_2D, formato_host,]=]
    [=[                  : DkImageFlags_UsageRender |
                        (formato_host == DkImageFormat_RGBA8_Unorm ? DkImageFlags_Usage2DEngine : 0u);
  if (!CrearImagen(r.imagen, DkImageType_2D, formato_host,]=]
    [=[  if (formato_color != uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) &&
      formato_color != uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
    return Rechazar(100 + formato_color, "formato de destino de render todavia no soportado");
  }]=]
    [=[  if (wfc::deko::ColorTargetFormat(wfc::deko::RenderFormat(formato_color)) == DkImageFormat_None) {
    return Rechazar(100 + formato_color, "unsupported WFC color resolve source");
  }]=]
    [=[    const uint32_t formato_destino = (info_destino >> 7) & 0x3F;]=]
    [=[    const uint32_t formato_destino = (info_destino >> 7) & 0x3F;
    const auto storage = wfc::deko::ColorResolveFormat(wfc::deko::ResolveFormat(formato_destino));
    const int32_t exp_bias = rex::graphics::reg::RB_COPY_DEST_INFO{info_destino}.copy_dest_exp_bias;]=]
    [=[    } else if (formato_destino != uint32_t(xenos::ColorFormat::k_8_8_8_8) &&
               formato_destino != uint32_t(xenos::ColorFormat::k_8_8_8_8_A) &&
               formato_destino != uint32_t(xenos::ColorFormat::k_8_8_8_8_AS_16_16_16_16)) {
      Rechazar(200 + formato_destino, "formato de copia todavia no soportado");]=]
    [=[    } else if (storage.format == DkImageFormat_None) {
      Rechazar(500 + formato_destino, "unsupported WFC color resolve texture");]=]
    [=[      // 4 bytes per texel: the base in multiples of 32 texels (GetResolveInfo).]=]
    [=[      // The Xenos tiled offset uses the guest destination's texel size.]=]
    [=[                                                                                     pitch_destino, 2));]=]
    [=[                                                                                     pitch_destino, storage.bytes_per_pixel_log2));]=]
    [=[      const bool frontal = frontal_perezoso && !frontal_apagado_ && base_resuelta == ultimo_frontal_ && !encoger &&]=]
    [=[      const bool typed_resolve = exp_bias || destino_render->formato != DkImageFormat_RGBA8_Unorm ||
                                 resuelta->imagen.formato != DkImageFormat_RGBA8_Unorm;
      const bool frontal = !typed_resolve && frontal_perezoso && !frontal_apagado_ &&
                           base_resuelta == ultimo_frontal_ && !encoger &&]=]
    [=[        if (encoger || se_lee ||
            !Copiar3D(*destino_render, uint32_t(x0), uint32_t(y0), ancho, alto, resuelta->imagen, dx, dy)) {]=]
    [=[        const bool mask_resolve = base_resuelta == 0x18C50000 && formato_destino == 6 &&
            x0 == 0 && y0 == 0 && dx == 0 && dy == 0 && ancho == 520 && alto == 296 && !encoger &&
            !exp_bias && destino_render->formato == DkImageFormat_RGBA8_Unorm &&
            resuelta->imagen.formato == DkImageFormat_RGBA8_Unorm;
        if (mask_resolve && wfc_frame_capture_.Trace(fotograma_)) {
          REXLOG_INFO("WFC frame {} light mask resolve source EDRAM {:03X} format {} pitch {}, "
                      "Z info {:08X}, copy control {:08X}, destination {:08X} {}x{}",
                      fotograma_, info_color & 0xFFF, formato_color, pitch, reg.rb_depth_info,
                      control, base_resuelta, ancho, alto);
          wfc_frame_capture_.Record(nucleo_, wfc::deko::FrameCapture::Stage::kLightSource,
                                    destino_render->imagen, destino_render->formato, ancho, alto, false);
        }
        if (typed_resolve) {
          const bool sample_hdr = !encoger && wfc_frame_probe_.Empty() &&
                                  wfc::deko::FrameProbe::IsSceneTarget(info_color & 0xFFF, pitch, formato_color) &&
                                  resuelta->imagen.formato == DkImageFormat_RGBA16_Float;
          if (sample_hdr) {
            WfcSampleFrame("hdr-source", *destino_render, uint32_t(x0), uint32_t(y0), ancho, alto);
          }
          const bool scene_hdr = !encoger && x0 == 0 && y0 == 0 && ancho >= 960 &&
              wfc::deko::FrameProbe::IsSceneTarget(info_color & 0xFFF, pitch, formato_color) &&
              destino_render->formato == DkImageFormat_RGBA16_Float;
          if (scene_hdr && (wfc_frame_capture_.Active(fotograma_) || wfc_frame_capture_.Begin(fotograma_))) {
            if (fotograma_ >= 900 && alto >= 592 && ancho >= 976) {
              wfc_hdr_edge_trace_.BeforeResolve(nucleo_, destino_render->imagen);
            }
            wfc_frame_capture_.Record(nucleo_, wfc::deko::FrameCapture::Stage::kHdr,
                                      destino_render->imagen, destino_render->formato, ancho, alto, false);
          }
          if (pitch == 1040 && ancho >= 960 && wfc_frame_capture_.Trace(fotograma_)) {
            REXLOG_INFO("WFC frame {} color resolve EDRAM {:03X} format {} ({},{}) {}x{} to "
                        "{:08X} format {} ({},{}) {}x{} exponent {} clear {}",
                        fotograma_, info_color & 0xFFF, formato_color, x0, y0, ancho, alto, base_resuelta,
                        formato_destino, dx, dy, resuelta->imagen.ancho, resuelta->imagen.alto, exp_bias,
                        borrar_color);
          }
          const bool source_7e3 = wfc::deko::CanonicalTarget(wfc::deko::RenderFormat(formato_color)) ==
                                  wfc::deko::RenderFormat::k_2_10_10_10_FLOAT;
          if (!Copiar3D(*destino_render, uint32_t(x0), uint32_t(y0),
                        encoger ? cabe_ancho : ancho, encoger ? cabe_alto : alto, resuelta->imagen, dx, dy,
                        exp_bias, encoger ? pedido_ancho : 0, encoger ? pedido_alto : 0, false, source_7e3)) {
            return Rechazar(530, "WFC typed color resolve failed");
          }
          if (scene_hdr && ancho == 1040 && alto == 592 && dx == 0 && dy == 0) {
            WfcMergeLateSceneDepth();
          }
          if (sample_hdr) {
            WfcSampleFrame("hdr-resolve", resuelta->imagen, dx, dy, ancho, alto);
          }
        } else if (encoger || se_lee ||
                   !Copiar3D(*destino_render, uint32_t(x0), uint32_t(y0), ancho, alto, resuelta->imagen, dx, dy)) {]=]
    [=[    for (uint32_t j = 0; j < 4; ++j) {
      destino_render->borrado_color[j] = float((valor >> (j * 8)) & 0xFF) * (1.0f / 255.0f);
    }]=]
    [=[    const auto color = wfc::deko::DecodeClearColor(wfc::deko::RenderFormat(formato_color), valor);
    std::copy(color.begin(), color.end(), destino_render->borrado_color);]=])
