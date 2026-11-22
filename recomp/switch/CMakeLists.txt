cmake_minimum_required(VERSION 3.25)
project(wfc_switch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("${REXSDK_DIR}" rexglue-sdk EXCLUDE_FROM_ALL)
include("${REXSDK_DIR}/cmake/rexglue_helpers.cmake")

# The SDK's cabextract path contains symlinks, which Windows Git and ZIP
# extraction can materialize as text files. Use their real source location.
set(mspack_sources "${REXSDK_DIR}/thirdparty/libmspack/libmspack/mspack")
set_property(TARGET mspack PROPERTY SOURCES "${mspack_sources}/lzxd.c")
target_include_directories(mspack BEFORE PUBLIC "${mspack_sources}")

include(generated/default/sources.cmake)

add_library(wfc_guest OBJECT ${GENERATED_SOURCES})
target_link_libraries(wfc_guest PRIVATE rex::runtime)
target_include_directories(wfc_guest PRIVATE "${REXSDK_DIR}/switch_compat")
target_compile_options(wfc_guest PRIVATE -g0)
rexglue_apply_target_settings(wfc_guest)
target_precompile_headers(wfc_guest PRIVATE generated/default/wfc_pch.h)
# These units contain more than 100,000 entries and run only at startup.
set_source_files_properties(
    generated/default/wfc_init.cpp generated/default/wfc_register.cpp
    PROPERTIES COMPILE_OPTIONS -O1 SKIP_PRECOMPILE_HEADERS ON)

add_executable(wfc src/main.cpp src/graphics.cpp)
target_include_directories(wfc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${REXSDK_DIR}/switch_compat")
target_compile_definitions(wfc PRIVATE WFC_BUILD_ATTEMPT=${WFC_BUILD_ATTEMPT})
target_sources(libavutil PRIVATE "${REXSDK_DIR}/thirdparty/FFmpeg/libavutil/lzo.c")
target_link_libraries(wfc PRIVATE wfc_guest rex::runtime libavutil)
rexglue_apply_target_settings(wfc)
rexglue_configure_target(wfc GPU_PLUGINS xenos)
rexglue_switch_add_deko3d(wfc)

# Generate narrow source overrides without modifying the pinned SDK checkout.
function(wfc_overlay_sdk_file original overlay)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" patched)
    set(arg 2)
    while(arg LESS ARGC)
        set(expected "${ARGV${arg}}")
        math(EXPR arg "${arg} + 1")
        set(replacement "${ARGV${arg}}")
        math(EXPR arg "${arg} + 1")
        string(FIND "${patched}" "${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "The pinned SDK source no longer matches the WFC overlay in ${original}")
        endif()
        string(REPLACE "${expected}" "${replacement}" patched "${patched}")
    endwhile()
    file(CONFIGURE OUTPUT "${overlay}" CONTENT "@patched@" @ONLY)
endfunction()

set(deko_draws "${REXSDK_DIR}/src/x360/deko/dibujos.cpp")
set(wfc_sdk_overlay "${CMAKE_CURRENT_BINARY_DIR}/wfc-sdk-overlay")

# The geometry program is independent of the game's shader pack.
find_program(WFC_UAM NAMES uam HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH REQUIRED)
set(rectangle_shader "${CMAKE_CURRENT_SOURCE_DIR}/src/rectangle.geom")
set(rectangle_dksh "${wfc_sdk_overlay}/rectangle.dksh")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rectangle_shader}")
file(MAKE_DIRECTORY "${wfc_sdk_overlay}")
execute_process(COMMAND "${WFC_UAM}" -s geom -o "${rectangle_dksh}" "${rectangle_shader}"
    COMMAND_ERROR_IS_FATAL ANY)
file(READ "${rectangle_dksh}" rectangle_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," rectangle_bytes "${rectangle_hex}")
file(CONFIGURE OUTPUT "${wfc_sdk_overlay}/wfc-rectangle-dksh.h"
    CONTENT "inline constexpr unsigned char kWfcRectangleDksh[] = {@rectangle_bytes@};\n" @ONLY)

# The game uses all 256 Xenos pixel constants.
wfc_overlay_sdk_file("${deko_draws}" "${wfc_sdk_overlay}/dibujos.cpp"
    "constexpr uint32_t kUboBytesPs = 224 * 16;" "constexpr uint32_t kUboBytesPs = 256 * 16;"
    "constexpr uint32_t kUboBytesCompartidas = 31 * 16;" "constexpr uint32_t kUboBytesCompartidas = 32 * 16;"
    [=[  if (!entrada.vertices && variante_ps && en_fondo) {]=]
    [=[  if (!entrada.vertices && variante_ps && en_fondo &&
      !(variante_ps & wfc::deko::kColorExponentMask)) {]=]
    [=[  const deko::Compilador::Resultado r = compilador_.Compilar(entrada.vertices, g.glsl);]=]
    [=[  if (!entrada.vertices && (variante_ps & wfc::deko::kColorExponentMask)) {
    wfc::deko::ApplyColorExponents(g.glsl, variante_ps);
  }
  const deko::Compilador::Resultado r = compilador_.Compilar(entrada.vertices, g.glsl);]=]
    [=[  uint32_t variante_ps = clase.variante;]=]
    [=[  uint32_t variante_ps = clase.variante;
  for (uint32_t target = 0; target < 4; ++target) {
    if (claves[target]) {
      variante_ps |= wfc::deko::ColorExponentKey(r[kInfoColor[target]], target);
    }
  }
  if (!wfc_reported_color_exponent_ && (variante_ps & wfc::deko::kColorExponentMask)) {
    wfc_reported_color_exponent_ = true;
    REXLOG_INFO("WFC: applied color exponents [{}, {}, {}, {}] (PS {:016X})",
                wfc::deko::ColorExponent(variante_ps, 0), wfc::deko::ColorExponent(variante_ps, 1),
                wfc::deko::ColorExponent(variante_ps, 2), wfc::deko::ColorExponent(variante_ps, 3), ps->huella);
  }]=]
    [=[    imagenes[4] = DestinoProfundidad(uint32_t(claves[4] >> 24) & 0xFFF, uint32_t(claves[4] >> 16) & 0x1, pitch);
    if (!imagenes[4]) {
      return Rechazar(41, "no hay destino de profundidad");
    }
  }]=]
    [=[    const uint32_t scene_depth_mode = uint32_t(claves[4] >> 17) & 3;
    imagenes[4] = DestinoProfundidad(uint32_t(claves[4] >> 24) & 0xFFF, uint32_t(claves[4] >> 16) & 0x1,
                                    pitch, scene_depth_mode);
    if (!imagenes[4]) {
      return Rechazar(41, "no hay destino de profundidad");
    }
    uint64_t& cleared_frame = wfc_scene_depth_frames_[scene_depth_mode];
    if (wfc::deko::IsSceneReverseDepth(uint32_t(claves[4] >> 24) & 0xFFF,
                                      uint32_t(claves[4] >> 16) & 0x1, pitch) &&
        cleared_frame != fotograma_) {
      cleared_frame = fotograma_;
      ImagenDk& imagen = *imagenes[4];
      imagen.contenido_invalido = false;
      imagen.borrado_z = 0.0f;
      imagen.borrado_estencil = 0;
      imagen.borrado_pendiente = true;
      borrado_previo_[imagen.serie] = {0, dibujados_};
      ++borrados_profundidad_;
      if (fotograma_ % 120 == 0) {
        REXLOG_INFO("WFC: cleared reused scene depth on frame {} (MSAA mode {})", fotograma_, scene_depth_mode);
      }
    }
  }]=]
    [=[    claves[4] = (uint64_t(1) << 62) | (uint64_t(info & 0xFFF) << 24) | (uint64_t((info >> 16) & 0x1) << 16) | pitch;]=]
    [=[    const uint32_t depth_base = info & 0xFFF;
    const uint32_t depth_format = (info >> 16) & 1;
    const uint32_t scene_depth_mode =
        wfc::deko::SceneDepthMode(depth_base, depth_format, pitch, r[gr::XE_GPU_REG_RB_SURFACE_INFO]);
    claves[4] = (uint64_t(1) << 62) | (uint64_t(depth_base) << 24) | (uint64_t(scene_depth_mode) << 17) |
                (uint64_t(depth_format) << 16) | pitch;]=]
    [=[  if (vacio) {
    return true;  // empty viewport or scissor
  }]=]
    [=[  if (vacio) {
    return true;  // empty viewport or scissor
  }
  const bool wfc_hdr = claves[0] && wfc::deko::FrameProbe::IsSceneTarget(
      uint32_t(claves[0] >> 24) & 0xFFF, pitch, uint32_t(claves[0] >> 16) & 0xF);
  const uint32_t wfc_depth_info = r[gr::XE_GPU_REG_RB_DEPTH_INFO];
  const bool wfc_scene_z = claves[4] &&
      wfc::deko::IsSceneReverseDepth(wfc_depth_info & 0xFFF, (wfc_depth_info >> 16) & 1, pitch);
  const bool wfc_mask_draw = claves[0] && pitch == 560 && ((claves[0] >> 24) & 0xFFF) == 0x085;
  const bool wfc_mask_depth_draw = claves[4] && pitch == 560 && wfc_depth_info == 0x00010000;
  const bool wfc_shadow_mask_init = ps && wfc::deko::IsShadowMaskInit(
      p.vs->huella, ps->huella, pitch, r[kInfoColor[0]], mascaras, control_profundidad, r[kMezcla[0]]);
  const bool wfc_shadow_stencil_clear = wfc_mask_depth_draw && !ps &&
      wfc::deko::IsShadowStencilClear(p.vs->huella, mascaras, control_profundidad) &&
      tijera.x == 0 && tijera.y == 0 && tijera.width >= wfc::deko::kShadowMaskWidth &&
      tijera.height >= wfc::deko::kShadowMaskHeight;
  const bool wfc_atlas_projection = wfc_mask_draw && ps && ps->huella == 0x1E5DF3EC4FDECE1Full &&
      (r[gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 7] & 0x1FFFF000) == wfc::deko::kShadowAtlasAddress;
  if ((wfc_hdr || wfc_scene_z || wfc_mask_draw || wfc_mask_depth_draw) &&
      (wfc_frame_capture_.Active(fotograma_) || wfc_frame_capture_.Begin(fotograma_))) {
    const wfc::deko::FrameCapture::DrawState state{
        claves[0] ? r[kInfoColor[0]] : 0xFFFFFFFFu, mascaras, control_profundidad,
        r[gr::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL], r[gr::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR],
        r[gr::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL], r[gr::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
        r[gr::XE_GPU_REG_SQ_PROGRAM_CNTL], r[gr::XE_GPU_REG_SQ_CONTEXT_MISC], r[kMezcla[0]],
        r[gr::XE_GPU_REG_RB_SURFACE_INFO],
    };
    if (wfc_frame_capture_.TraceDraw(state)) {
      REXLOG_INFO("WFC frame {} scene draw VS {:016X} PS {:016X} RT0 {:08X} mask {:04X} depth {:08X} "
                  "screen {:08X}/{:08X} window {:08X}/{:08X} SQ {:08X}/{:08X} blend {:08X}, "
                  "host viewport ({},{}) {}x{}, scissor ({},{}) {}x{}, MSAA mode {}, depth target {:08X}",
                  fotograma_, p.vs->huella, ps ? ps->huella : 0, state[0], state[1], state[2], state[3],
                  state[4], state[5], state[6], state[7], state[8], state[9],
                  viewport[0], viewport[1], viewport[2], viewport[3], tijera.x, tijera.y, tijera.width, tijera.height,
                  (state[10] >> 16) & 3, claves[4] ? wfc_depth_info : 0xFFFFFFFFu);
    }
  }]=]
    [=[  // --- Specialization and shared constants]=]
    [=[  if (wfc_shadow_atlas_needs_clear_ && claves[4] && wfc::deko::IsShadowAtlasDepthWrite(
          wfc_depth_info, pitch, (r[gr::XE_GPU_REG_RB_SURFACE_INFO] >> 16) & 3u, mascaras, control_profundidad)) {
    // The projected map has been consumed. Start the next map without depth from the previous light.
    AntesDe3D();
    const DkScissor atlas_area{0, 0, wfc::deko::kShadowAtlasSize, wfc::deko::kShadowAtlasSize};
    dkCmdBufSetScissors(cmd, 0, &atlas_area, 1);
    dkCmdBufClearDepthStencil(cmd, true, 1.0f, 0, 0);
    g_.valido = false;
    wfc_shadow_atlas_needs_clear_ = false;
    if (wfc_frame_capture_.Trace(fotograma_)) {
      REXLOG_INFO("WFC frame {} reset reused shadow atlas depth before caster VS {:016X}, depth control {:08X}",
                  fotograma_, p.vs->huella, control_profundidad);
    }
  }

  // --- Specialization and shared constants]=]
    [=[  uint32_t especializacion = entrada->especializacion;]=]
    [=[  uint32_t especializacion = entrada->especializacion;
  if (compartidas[124] || compartidas[125]) {
    especializacion |= wfc::deko::kSpecTextureGamma;
  }]=]
    [=[  cortar_etapa(1);
  // --- Upload space for everything this draw needs from here on, all in one slot]=]
    [=[  if (ps && n_texturas && wfc::deko::FrameProbe::IsSceneTarget(uint32_t(claves[0] >> 24) & 0xFFF, pitch,
                                                         uint32_t(claves[0] >> 16) & 0xF)) {
    WfcBindNeutralLightMask(r, *ps, texturas);
    // HLSL reads two words at byte 496, one four-bit result mask per sampler.
    for (const auto& sampler : ps->samplers) {
      const auto* fetch = r + gr::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + sampler.registro * 6;
      const uint32_t gamma = wfc::deko::TextureGammaMask(fetch[0], fetch[3]);
      compartidas[124 + sampler.registro / 8] |= gamma << (4 * (sampler.registro % 8));
    }
    if ((compartidas[124] || compartidas[125]) &&
        (!wfc_reported_texture_gamma_ ||
         (wfc_texture_gamma_frame_ != fotograma_ && wfc_frame_capture_.Active(fotograma_)))) {
      wfc_reported_texture_gamma_ = true;
      wfc_texture_gamma_frame_ = fotograma_;
      REXLOG_INFO("WFC frame {} HDR material texture gamma, PS {:016X}, masks {:08X}/{:08X}",
                  fotograma_, ps->huella, compartidas[124], compartidas[125]);
    }
  }
  cortar_etapa(1);
  // --- Upload space for everything this draw needs from here on, all in one slot]=]
    [=[      if (con_indices) {
        dkCmdBufDrawIndexed(cmd, primitiva, n_indices, 1, 0, -int32_t(vmin), 0);
      } else {
        dkCmdBufDraw(cmd, primitiva, cuenta, 1, 0, 0);
      }
      if (marcar) {]=]
    [=[      if (k + 1 == n_veces && ps && wfc_frame_capture_.Active(fotograma_)) {
        WfcCaptureShaderInputs(r, *ps);
        if (ps->huella == 0x47BC429554F5829Cull) {
          WfcCaptureLightMask(r);
        } else if (ps->huella == 0x1E5DF3EC4FDECE1Full) {
          WfcTraceShadowInputs(r);
        }
      }
      const bool hdr_draw = k + 1 == n_veces && ps && (mascaras & 7) &&
          wfc::deko::FrameProbe::IsSceneTarget(uint32_t(claves[0] >> 24) & 0xFFF, pitch,
                                               uint32_t(claves[0] >> 16) & 0xF);
      const bool trace_edge = hdr_draw && fotograma_ >= 900 && wfc_frame_capture_.Active(fotograma_) &&
                              wfc_hdr_edge_trace_.Open() &&
                              imagenes_pase_[0]->formato == DkImageFormat_RGBA16_Float &&
                              imagenes_pase_[0]->alto >= 592;
      if (trace_edge && wfc_hdr_edge_trace_.Empty()) {
        wfc_hdr_edge_trace_.Sample(nucleo_, imagenes_pase_[0]->imagen);
      }
      const bool probe_draw = hdr_draw && wfc_frame_probe_.BeginDraw(nucleo_, fotograma_, p.vs->huella, ps->huella,
                                     elementos, r[gr::XE_GPU_REG_PA_CL_CLIP_CNTL], control_profundidad);
      if (wfc_shadow_stencil_clear) {
        const uint32_t stencil = r[gr::XE_GPU_REG_RB_STENCILREFMASK];
        const DkScissor area{0, 0, wfc::deko::kShadowMaskWidth, wfc::deko::kShadowMaskHeight};
        dkCmdBufSetScissors(cmd, 0, &area, 1);
        dkCmdBufClearDepthStencil(cmd, false, 0.0f, uint8_t(stencil >> 16), uint8_t(stencil));
        g_.valido = false;
        if (wfc_frame_capture_.Trace(fotograma_)) {
          REXLOG_INFO("WFC frame {} cleared shadow stencil {}x{}, reference {:02X}, write mask {:02X}",
                      fotograma_, area.width, area.height, stencil & 0xFF, (stencil >> 16) & 0xFF);
        }
      } else if (con_indices) {
        dkCmdBufDrawIndexed(cmd, primitiva, n_indices, 1, 0, -int32_t(vmin), 0);
      } else {
        dkCmdBufDraw(cmd, primitiva, cuenta, 1, 0, 0);
      }
      if (probe_draw) {
        wfc_frame_probe_.EndDraw(nucleo_);
      }
      if (wfc_atlas_projection) {
        wfc_shadow_atlas_needs_clear_ = true;
      }
      if (wfc_shadow_mask_init && k + 1 == n_veces) {
        // Run 032 still showed a black 480x288 region with a constant-white fragment export.
        // Fill the entire resolved extent independently of the guest rectangle's rasterization.
        const DkScissor mask_area{0, 0, wfc::deko::kShadowMaskWidth, wfc::deko::kShadowMaskHeight};
        dkCmdBufSetScissors(cmd, 0, &mask_area, 1);
        dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGB, 1.0f, 1.0f, 1.0f, 0.0f);
        g_.valido = false;
        wfc_shadow_mask_initialized_frame_ = fotograma_;
        if (wfc_frame_capture_.Active(fotograma_)) {
          const auto& image = *imagenes_pase_[0];
          wfc_frame_capture_.Record(nucleo_, wfc::deko::FrameCapture::Stage::kLightInitial, image.imagen,
                                    image.formato, mask_area.width, mask_area.height, false);
          REXLOG_INFO("WFC frame {} cleared shadow attenuation RGB across {}x{} before projection",
                      fotograma_, mask_area.width, mask_area.height);
        }
      }
      if (wfc_mask_depth_draw && (control_profundidad & 0x6) == 0x6) {
        wfc_shadow_depth_written_frame_ = fotograma_;
      }
      if (trace_edge) {
        wfc_hdr_edge_trace_.Draw(nucleo_, imagenes_pase_[0]->imagen, p.vs->huella, ps->huella,
                                 control_profundidad, r[kMezcla[0]]);
      }
      if (marcar) {]=]
    "#include <rex/x360/deko/render.h>"
    "#include <rex/x360/deko/render.h>\n#include \"src/deko-color.h\"\n#include \"wfc-rectangle-dksh.h\""
    [=[    if (formato != uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) &&
        formato != uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {]=]
    [=[    if (wfc::deko::ColorTargetFormat(wfc::deko::RenderFormat(formato)) == DkImageFormat_None) {]=]
    [=[    const uint32_t registro = (vs.microcodigo[size_t(elemento.instruccion) * 3] >> 12) & 0x3F;
    const uint32_t original = vs.microcodigo[size_t(elemento.instruccion) * 3 + 1] & 0xFFF;
    size_t q = SIZE_MAX;
    for (const ElementoVertice& otro : vs.elementos) {
      const size_t i = size_t(otro.instruccion) * 3;
      if (((parcheado[i] >> 12) & 0x3F) != registro || (parcheado[i] & 0x1F) != 0) {
        continue;
      }
      if (q == SIZE_MAX) {
        q = i;
      }
      if (MascaraEscrita(parcheado[i + 1] & 0xFFF) == MascaraEscrita(original)) {
        q = i;  // the one that writes the same components
        break;
      }
    }
    if (q == SIZE_MAX) {
      return Rechazar(21, "fetch de vertices no encontrado en el VS parcheado");
    }
    const uint32_t d0 = parcheado[q], d1 = parcheado[q + 1], d2 = parcheado[q + 2];
    const uint32_t ranura = ((d0 >> 20) & 0x1F) * 3 + ((d0 >> 25) & 0x3);
    const uint32_t formato = (d1 >> 16) & 0x3F;
    const uint32_t zancada = (d2 & 0xFF) * 4;]=]
    [=[    // WFC matches the exact live shader, so the declaration identifies
    // the fetch by its instruction offset even when destination registers repeat.
    const size_t q = size_t(elemento.instruccion) * 3;
    if (q + 2 >= parcheado.size() || (parcheado[q] & 0x1F) != 0) {
      return Rechazar(21, "WFC vertex fetch outside the active microcode");
    }
    const uint32_t d1 = parcheado[q + 1], d2 = parcheado[q + 2];
    const uint32_t original = d1 & 0xFFF;
    // A mini-fetch uses the address operands and stride of the previous full fetch.
    size_t full = q;
    if (d1 & (1u << 30)) {
      for (const auto& fetch : vs.elementos) {
        const size_t offset = size_t(fetch.instruccion) * 3;
        if (offset >= q) {
          break;
        }
        if (!(parcheado[offset + 1] & (1u << 30))) {
          full = offset;
        }
      }
      if (full == q) {
        return Rechazar(25, "WFC mini-fetch has no preceding full fetch");
      }
    }
    const uint32_t address_word = parcheado[full];
    const uint32_t ranura = ((address_word >> 20) & 0x1F) * 3 + ((address_word >> 25) & 0x3);
    const uint32_t formato = (d1 >> 16) & 0x3F;
    const uint32_t zancada = (parcheado[full + 2] & 0xFF) * 4;]=]
    [=[    while (enlace < entrada.enlaces.size() && entrada.enlaces[enlace].ranura != ranura) {]=]
    [=[    while (enlace < entrada.enlaces.size() &&
           (entrada.enlaces[enlace].ranura != ranura || entrada.enlaces[enlace].zancada != zancada)) {]=]
    [=[    } else if (!entrada.enlaces[enlace].zancada) {
      entrada.enlaces[enlace].zancada = zancada;
    }
    DkVtxAttribState& a]=]
    [=[    }
    const uint32_t attribute_words =
        std::bit_width(xenos::GetVertexFormatNeededWords(xenos::VertexFormat(formato), 15));
    auto& stream = entrada.enlaces[enlace];
    stream.min_bytes = std::max(stream.min_bytes, uint32_t(offset) + attribute_words * 4);
    DkVtxAttribState& a]=]
    [=[    if (!entrada.enlaces[b].zancada) {
      return Rechazar(25, "stream de vertices sin zancada");
    }
    entrada.estado_bufers[b] = {entrada.enlaces[b].zancada, 0};]=]
    [=[    entrada.estado_bufers[b] = {entrada.enlaces[b].zancada, 0};]=]
    [=[    uint64_t necesarios = uint64_t(vertices) * enlace.zancada;
    if (inicio + necesarios > disponibles) {
      Avisar(11, "vertices mas alla del final de su bufer: se recorta");
      necesarios = disponibles > inicio ? (disponibles - inicio) / enlace.zancada * enlace.zancada : 0;
    }]=]
    [=[    uint64_t necesarios = enlace.min_bytes;
    if (enlace.zancada) {
      necesarios = uint64_t(vertices) * enlace.zancada;
      if (inicio + necesarios > disponibles) {
        Avisar(11, "vertices mas alla del final de su bufer: se recorta");
        necesarios = disponibles > inicio ? (disponibles - inicio) / enlace.zancada * enlace.zancada : 0;
      }
    } else if (necesarios > disponibles) {
      return Rechazar(25, "WFC constant vertex stream exceeds its fetch range");
    }]=]
    [=[  const uint32_t tipo = iniciador & 0x3F;]=]
    [=[  const uint32_t tipo = iniciador & 0x3F;
  const bool rectangle = tipo == uint32_t(xenos::PrimitiveType::kRectangleList);]=]
    [=[    case 4:
      primitiva = DkPrimitive_Triangles;]=]
    [=[    case 4:
    case uint32_t(xenos::PrimitiveType::kRectangleList):
      primitiva = DkPrimitive_Triangles;]=]
    [=[  if (clase.cielo && mascaras) {]=]
    [=[  if (!rectangle && clase.cielo && mascaras) {]=]
    [=[  const DkShader* vs = ShaderDe(*p.vs, especializacion, ps ? InfoDe(*ps).interpoladores : 0u);]=]
    [=[  if (rectangle && !rectangle_shader_attempted_) {
    rectangle_shader_attempted_ = true;
    rectangle_shader_loaded_ = nucleo_.CargarShader(rectangle_shader_, kWfcRectangleDksh, "WFC rectangle list");
  }
  if (rectangle && !rectangle_shader_loaded_) {
    return Rechazar(108, "WFC rectangle geometry shader unavailable");
  }
  const DkShader* gs = rectangle ? &rectangle_shader_ : nullptr;
  const uint32_t vertex_outputs = rectangle ? ~0u : (ps ? InfoDe(*ps).interpoladores : 0u);
  const DkShader* vs = ShaderDe(*p.vs, especializacion, vertex_outputs);]=]
    [=[  if (vs != g_.vs || fs != g_.ps) {
    const DkShader* shaders[2] = {vs, fs};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
    g_.vs = vs;
    g_.ps = fs;]=]
    [=[  if (vs != g_.vs || fs != g_.ps || gs != g_.gs) {
    const DkShader* shaders[3] = {vs, fs, gs};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, gs ? 3 : 2);
    g_.vs = vs;
    g_.ps = fs;
    g_.gs = gs;
    if (gs && !first_rectangle_) {
      first_rectangle_ = true;
      REXLOG_INFO("WFC: rectangle-list shader bound ({} vertices, VS {}, PS {})",
                  cuenta, p.vs->numero, ps ? ps->numero : 0);
    }]=])
wfc_overlay_sdk_file("${REXSDK_DIR}/include/rex/x360/deko/render.h"
    "${wfc_sdk_overlay}/rex/x360/deko/render.h"
    "#include <rex/x360/vertices_dedupe.h>"
    "#include <rex/x360/vertices_dedupe.h>\n#include \"src/frame-probe.h\"\n#include \"src/frame-capture.h\""
    "void PrepararDestino(ImagenDk& imagen);"
    "void PrepararDestino(ImagenDk& imagen, bool reverse_depth = false);"
    "ImagenDk* DestinoProfundidad(uint32_t base, uint32_t formato, uint32_t pitch);"
    "ImagenDk* DestinoProfundidad(uint32_t base, uint32_t formato, uint32_t pitch, uint32_t scene_depth_mode);"
    [=[  DkMemBlock lectura_bloque_ = nullptr;]=]
    [=[  DkMemBlock lectura_bloque_ = nullptr;
  wfc::deko::FrameProbe wfc_frame_probe_;
  wfc::deko::FrameCapture wfc_frame_capture_;
  wfc::deko::HdrEdgeTrace wfc_hdr_edge_trace_;
  void WfcCaptureShaderInputs(const uint32_t* registers, const EntradaShader& shader);
  void WfcCaptureLightMask(const uint32_t* registers);
  void WfcBindNeutralLightMask(const uint32_t* registers, const EntradaShader& shader,
                               std::array<DkResHandle, 32>& textures);
  void WfcTraceShadowInputs(const uint32_t* registers);
  void WfcCaptureDepth(const ImagenDk& image, wfc::deko::FrameCapture::Stage stage,
                       uint32_t width, uint32_t height, uint32_t source_width = 0, uint32_t source_height = 0);
  void WfcPopulateShadowDepth(ImagenDk& image);
  void WfcMergeLateSceneDepth();
  void WfcCapturePresentedFrame(const ImagenDk& image, uint32_t width, uint32_t height);
  void WfcSampleFrame(const char* stage, const ImagenDk& image, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height);
  std::array<uint64_t, 4> wfc_scene_depth_frames_{UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
  uint64_t wfc_scene_depth_prepass_frame_ = UINT64_MAX;
  uint64_t wfc_scene_depth_merged_frame_ = UINT64_MAX;
  uint64_t wfc_light_mask_frame_ = UINT64_MAX;
  uint64_t wfc_shadow_inputs_frame_ = UINT64_MAX;
  uint64_t wfc_shadow_depth_frame_ = UINT64_MAX;
  uint64_t wfc_shadow_depth_written_frame_ = UINT64_MAX;
  uint64_t wfc_shadow_mask_initialized_frame_ = UINT64_MAX;
  uint64_t wfc_shadow_atlas_capture_frame_ = UINT64_MAX;
  ImagenDk wfc_depth_preview_;
  bool wfc_reported_neutral_light_mask_ = false;
  bool wfc_reported_late_scene_depth_ = false;
  bool wfc_reported_shadow_depth_ = false;
  bool wfc_reported_shadow_atlas_clear_ = false;
  bool wfc_shadow_atlas_needs_clear_ = true;
  bool wfc_reported_color_exponent_ = false;
  bool wfc_reported_texture_gamma_ = false;
  uint64_t wfc_texture_gamma_frame_ = UINT64_MAX;
  std::array<bool, 2> wfc_reported_float_depth_clear_{};]=]
    "static constexpr uint32_t kPalabrasCompartidas = 124;  // 31 float4: what the deko3d library declares"
    "static constexpr uint32_t kPalabrasCompartidas = 128;  // WFC appends per-sampler gamma masks"
    "std::array<uint32_t, 896> sombra_ps_{};   // 224 registers"
    "std::array<uint32_t, 1024> sombra_ps_{};  // 256 registers"
    "    uint32_t zancada = 0;  // bytes"
    "    uint32_t zancada = 0;  // bytes\n    uint32_t min_bytes = 0;  // footprint of a constant vertex stream"
    [=[  bool Copiar3D(const ImagenDk& origen, uint32_t x0, uint32_t y0, uint32_t ancho, uint32_t alto, ImagenDk& destino,
                uint32_t dx, uint32_t dy);]=]
    [=[  bool Copiar3D(const ImagenDk& origen, uint32_t x0, uint32_t y0, uint32_t ancho, uint32_t alto, ImagenDk& destino,
                uint32_t dx, uint32_t dy, int32_t exp_bias = 0, uint32_t source_width = 0,
                uint32_t source_height = 0, bool merge_reverse_depth = false, bool source_7e3 = false);]=]
    [=[  DkShader fs_copia_color_{}, fs_copia_profundidad_{};]=]
    [=[  DkShader fs_copia_color_{}, fs_copia_profundidad_{}, fs_copia_color_tipada_{};
  bool fs_copia_color_tipada_intentada_ = false;
  bool fs_copia_color_tipada_valida_ = false;
  DkShader wfc_shadow_depth_shader_{};
  bool wfc_shadow_depth_shader_attempted_ = false;
  bool wfc_shadow_depth_shader_valid_ = false;
  DkShader rectangle_shader_{};
  bool rectangle_shader_attempted_ = false;
  bool rectangle_shader_loaded_ = false;
  bool first_rectangle_ = false;]=]
    [=[    const DkShader* ps = nullptr;
    DkRasterizerState raster{};]=]
    [=[    const DkShader* ps = nullptr;
    const DkShader* gs = nullptr;
    DkRasterizerState raster{};]=])
include(deko-color.cmake)
get_target_property(wfc_sources wfc SOURCES)
get_filename_component(deko_draws "${deko_draws}" ABSOLUTE)
get_filename_component(deko_targets "${deko_targets}" ABSOLUTE)
list(REMOVE_ITEM wfc_sources "${deko_draws}" "${deko_targets}")
set_property(TARGET wfc PROPERTY SOURCES "${wfc_sources}")
target_sources(wfc PRIVATE "${wfc_sdk_overlay}/dibujos.cpp" "${wfc_sdk_overlay}/destinos.cpp")
target_include_directories(wfc BEFORE PRIVATE "${wfc_sdk_overlay}")

# Physical memory touched by the guest already uses lazy 4 MiB backing chunks.
# Use that granularity for explicit allocations too, while keeping the heap's
# guest-visible sizes and per-page permissions. Limit each new mapping to one
# chunk so the decommit path can still reclaim complete chunks.
set(sdk_guest_memory "${REXSDK_DIR}/src/core/guest_memory_switch.cpp")
set(wfc_guest_memory "${wfc_sdk_overlay}/guest_memory_switch.cpp")
wfc_overlay_sdk_file("${sdk_guest_memory}" "${wfc_guest_memory}"
[=[    const size_t begin = AlignDown(offset, kPageSize);
    const size_t end   = AlignUp(offset + length, kPageSize);

    size_t cur = begin;]=]
[=[    constexpr size_t kPhysicalBase = 0x100000000ull;
    constexpr size_t kPhysicalEnd = 0x120000000ull;
    constexpr size_t kPhysicalChunkSize = 4 * 1024 * 1024;
    const bool physical = offset >= kPhysicalBase && offset < kPhysicalEnd &&
                          length <= kPhysicalEnd - offset;
    const size_t granularity = physical ? kPhysicalChunkSize : kPageSize;
    const size_t begin = AlignDown(offset, granularity);
    const size_t end = AlignUp(offset + length, granularity);

    size_t cur = begin;]=]
    "size_t hueco_fin = end;"
    "size_t hueco_fin = physical ? AlignUp(cur + 1, kPhysicalChunkSize) : end;"
    # libnx takes a guard size, so reserve padding and align the returned address.
    [=[    s.base = static_cast<uint8_t*>(virtmemFindAslr(size, kBlockSize));
    if (s.base) s.reservation = virtmemAddReservation(s.base, size);]=]
    [=[    if (void* window = virtmemFindAslr(size + kBlockSize - kPageSize, kBlockSize)) {
        s.base = reinterpret_cast<uint8_t*>(AlignUp(reinterpret_cast<uintptr_t>(window), kBlockSize));
        s.reservation = virtmemAddReservation(s.base, size);
    }]=]
    [=[        void* shadow = virtmemFindCodeMemory(tam, align);]=]
    [=[        void* shadow = virtmemFindCodeMemory(tam + align - kPageSize, kPageSize);
        if (shadow) shadow = reinterpret_cast<void*>(AlignUp(reinterpret_cast<uintptr_t>(shadow), align));]=])
set_source_files_properties("${sdk_guest_memory}" TARGET_DIRECTORY rexcore PROPERTIES HEADER_FILE_ONLY ON)
target_sources(rexcore PRIVATE "${wfc_guest_memory}")
set_property(SOURCE "${wfc_guest_memory}" TARGET_DIRECTORY rexcore PROPERTY INCLUDE_DIRECTORIES
    "${REXSDK_DIR}/src/core")

# Header enumeration can overlap a checkpoint rewrite. Use the opened stream
# for reads so libnx I/O errors reach the existing enumeration fallback.
set(sdk_content_manager "${REXSDK_DIR}/src/system/xam/content_manager.cpp")
set(wfc_content_manager "${wfc_sdk_overlay}/content_manager.cpp")
wfc_overlay_sdk_file("${sdk_content_manager}" "${wfc_content_manager}"
    [=[#include <array>
#include <cstring>]=]
    [=[#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>]=]
    [=[// A folder name the SD accepts, derived from the one the player sees.]=]
    [=[std::mutex content_header_mutex;

// A folder name the SD accepts, derived from the one the player sees.]=]
    [=[  if (!std::filesystem::exists(header_path)) {
    return;
  }

  auto file = rex::filesystem::OpenFile(header_path, "rb");
  if (!file) {
    return;
  }

  auto file_size = std::filesystem::file_size(header_path);
  if (file_size < sizeof(XCONTENT_AGGREGATE_DATA) + sizeof(license_)) {
    fclose(file);
    return;
  }

  fseek(file, sizeof(XCONTENT_AGGREGATE_DATA), SEEK_SET);
  fread(&license_, 1, sizeof(license_), file);
  fclose(file);]=]
    [=[  const std::lock_guard lock(content_header_mutex);
  auto file = rex::filesystem::OpenFile(header_path, "rb");
  if (!file) {
    return;
  }

  uint32_t license = 0;
  if (fseek(file, sizeof(XCONTENT_AGGREGATE_DATA), SEEK_SET) == 0 &&
      fread(&license, 1, sizeof(license), file) == sizeof(license)) {
    license_ = license;
  }
  fclose(file);]=]
    [=[  auto parent_path = header_path.parent_path();

  if (!std::filesystem::exists(parent_path)) {
    if (!std::filesystem::create_directories(parent_path)) {
      return X_ERROR_ACCESS_DENIED;
    }
  }

  rex::filesystem::CreateEmptyFile(header_path);]=]
    [=[  const std::lock_guard lock(content_header_mutex);
  std::error_code ec;
  std::filesystem::create_directories(header_path.parent_path(), ec);
  if (ec) {
    return X_ERROR_ACCESS_DENIED;
  }]=]
    [=[  fwrite(&data, 1, sizeof(XCONTENT_AGGREGATE_DATA), file);
  if (license_mask != 0) {
    fwrite(&license_mask, 1, sizeof(license_mask), file);
  }
  const bool bien = fclose(file) == 0;]=]
    [=[  const bool header_written = fwrite(&data, 1, sizeof(data), file) == sizeof(data);
  const bool data_written = header_written && (license_mask == 0 ||
      fwrite(&license_mask, 1, sizeof(license_mask), file) == sizeof(license_mask));
  const bool closed = fclose(file) == 0;
  const bool written = data_written && closed;]=]
    [=[              bien ? "escrita" : "FALLO al cerrarse", rex::path_to_utf8(header_path));
  return X_ERROR_SUCCESS;]=]
    [=[              written ? "escrita" : "FALLO al escribir", rex::path_to_utf8(header_path));
  return written ? X_ERROR_SUCCESS : X_ERROR_ACCESS_DENIED;]=]
    [=[  if (!std::filesystem::exists(header_file_path)) {
    return X_ERROR_FILE_NOT_FOUND;
  }

  auto file = rex::filesystem::OpenFile(header_file_path, "rb");
  if (!file) {
    return X_ERROR_FILE_NOT_FOUND;
  }

  auto file_size = std::filesystem::file_size(header_file_path);
  if (file_size < header_size) {
    fclose(file);
    return X_ERROR_FILE_NOT_FOUND;
  }]=]
    [=[  const std::lock_guard lock(content_header_mutex);
  auto file = rex::filesystem::OpenFile(header_file_path, "rb");
  if (!file) {
    const int error = errno;
    if (error != ENOENT) {
      REXSYS_WARN("WFC: could not open save header {} (errno {})",
                  rex::path_to_utf8(header_file_path), error);
    }
    return X_ERROR_FILE_NOT_FOUND;
  }]=])
set_source_files_properties("${sdk_content_manager}" TARGET_DIRECTORY rexruntime PROPERTIES HEADER_FILE_ONLY ON)
target_sources(rexruntime PRIVATE "${wfc_content_manager}")

# The enumeration cache and its readers need the same lock. The driver's
# per-pad lock only protects libnx polling, not the guest slot assignments.
set(sdk_input_system "${REXSDK_DIR}/src/input/input_system.cpp")
set(wfc_input_system "${wfc_sdk_overlay}/input_system.cpp")
wfc_overlay_sdk_file("${sdk_input_system}" "${wfc_input_system}"
    [=[#include <cmath>]=]
    [=[#include <cmath>
#include <mutex>]=]
    [=[constexpr uint32_t kSyntheticOrdinal = UINT32_MAX;]=]
    [=[constexpr uint32_t kSyntheticOrdinal = UINT32_MAX;

std::mutex input_system_mutex;

void TracePrimaryController(uint32_t user_index, const char* query, X_RESULT result,
                            uint32_t& previous) {
  if (user_index == 0 && result != previous) {
    previous = result;
    REXLOG_INFO("WFC: guest controller 1 {} result 0x{:08X}", query, result);
  }
}]=]
    [=[void InputSystem::Shutdown() {]=]
    [=[void InputSystem::Shutdown() {
  const std::lock_guard lock(input_system_mutex);]=]
    [=[                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");]=]
    [=[                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");
  const std::lock_guard lock(input_system_mutex);
  static uint32_t previous = UINT32_MAX;]=]
    [=[  if (ids.empty()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Prefer the pad in hand]=]
    [=[  if (ids.empty()) {
    TracePrimaryController(user_index, "capabilities", X_ERROR_DEVICE_NOT_CONNECTED, previous);
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Prefer the pad in hand]=]
    [=[  auto* driver = DriverForDevice(chosen);
  if (!driver) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return driver->GetDeviceCapabilities(chosen, flags, out_caps);]=]
    [=[  auto* driver = DriverForDevice(chosen);
  const X_RESULT result = driver ? driver->GetDeviceCapabilities(chosen, flags, out_caps)
                                 : X_ERROR_DEVICE_NOT_CONNECTED;
  TracePrimaryController(user_index, "capabilities", result, previous);
  return result;]=]
    [=[X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");]=]
    [=[X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");
  const std::lock_guard lock(input_system_mutex);
  static uint32_t previous = UINT32_MAX;]=]
    [=[  if (!any) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;]=]
    [=[  const X_RESULT result = any ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  TracePrimaryController(user_index, "state", result, previous);
  if (any && out_state) {
    *out_state = merged;
  }
  return result;]=]
    [=[X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");]=]
    [=[X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");
  const std::lock_guard lock(input_system_mutex);]=]
    [=[                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");]=]
    [=[                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");
  const std::lock_guard lock(input_system_mutex);]=])

set(sdk_switch_input "${REXSDK_DIR}/src/input/switch/switch_input_driver.cpp")
set(wfc_switch_input "${wfc_sdk_overlay}/switch_input_driver.cpp")
wfc_overlay_sdk_file("${sdk_switch_input}" "${wfc_switch_input}"
    [=[  if (connected != slot.connected) {
    slot.connected = connected;]=]
    [=[  if (connected != slot.connected) {
    REXLOG_INFO("WFC: Switch pad slot {} {}, HID style 0x{:08X}, handheld {}",
                index + 1, connected ? "connected" : "disconnected",
                padGetStyleSet(&slot.pad), slot.pad.active_handheld);
    slot.connected = connected;]=])
set_source_files_properties("${sdk_input_system}" "${sdk_switch_input}"
    TARGET_DIRECTORY rexinput PROPERTIES HEADER_FILE_ONLY ON)
target_sources(rexinput PRIVATE "${wfc_input_system}" "${wfc_switch_input}")

if(WFC_ICON AND EXISTS "${WFC_ICON}")
    rexglue_switch_add_nro(wfc NAME "Transformers: War for Cybertron" VERSION "0.1.${WFC_BUILD_ATTEMPT}" ICON "${WFC_ICON}")
else()
    rexglue_switch_add_nro(wfc NAME "Transformers: War for Cybertron" VERSION "0.1.${WFC_BUILD_ATTEMPT}")
endif()
