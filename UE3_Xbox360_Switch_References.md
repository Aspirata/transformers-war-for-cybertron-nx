# Xbox 360 UE3 porting references for Nintendo Switch

English edition of the GPT-6.1 Terra research from 17 November 2026. Use these findings to choose what to inspect and how to test it. The [reference index](reference-docs/README.md) links to local snapshots, original repositories, and license notices.

## War for Cybertron discs and packages

The Xbox 360 release of Transformers: War for Cybertron (2010, High Moon Studios) uses a game-specific branch of Unreal Engine 3. The executable is PowerPC and the console uses a Xenos GPU. The Switch target is ARM64.

| Evidence | Observed on the reference disc |
| --- | --- |
| Disc label in local filename | USA (En,Fr), with English and French files observed |
| ISO size | 7,838,695,424 bytes |
| ISO SHA-256 | `4fa29ef7ccee8fe269771ef1b0cffe9577def35068bfabfeb30a215b3b641866` |
| XDVDFS | XGD2 partition at `0x0FD90000`, volume descriptor at `0x0FDA0000` |
| Game partition | 2,512 files across 10 directories, 7,004,928,727 bytes including the system update |
| `default.xex` size | 25,247,744 bytes |
| `default.xex` SHA-256 | `107314b11111cef557b9bcee243b0a865c964f59cde99030c152265033934b22` |
| XEX identity | Title ID `4156084C`, Media ID `02619995`, raw version and base version `00000004` |
| XEX layout | Load address `0x82000000`, entry point `0x826FB0B8`, normal encryption, basic compression |
| XEX header import libraries | `xam.xex` and `xboxkrnl.exe` |
| UE3 packages | Big-endian signature `9E 2A 83 C1`, package version 511, licensee version 144, engine header field 3428 |

The game partition matched `extracted/` byte for byte when checked independently with the SA2 XDVDFS parser. Sizes and MD5 digests matched all 2,573 parseable records in `TransGame/Xbox360TOC.txt`, with one line outside the parser's format. TOC entries can repeat. The full ISO also contains an ISO 9660 filesystem with `_SYSTEMU`, `AUDIO_TS`, and `VIDEO_TS` outside the extracted XDVDFS partition. Retain the disc image when exact preservation matters. [SA2 disc extractor](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/tools/extract_iso.py)

The disc contains 2,438 `.xxx` packages and 54 `.bik` videos. `TransGame/CookedXenon/` contains engine, game, level, localized, audio, and possible shader packages. Start the shader inventory with `ENV_AllShader_p.xxx` and inspect the actual binary structures. `TransGame/Config/Xenon/Cooked/Coalesced.ini` is cooked data. Preserve it and the original TOC while testing. Package header field 3428 is an observed value. The precise UE3 source revision remains unknown. [UEViewer game database](https://github.com/gildor2/UEViewer/blob/master/Unreal/GameDatabase.cpp)

The first analysis used XexTool 6.3 to obtain a base image from a copy of the XEX. The offsets below refer to that base image. Trace guest calls before drawing conclusions about which subsystems and paths run. [Xenia XEX2 header definitions](https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/util/xex2_info.h)

| Middleware | Evidence to follow up |
| --- | --- |
| FMOD | Stream thread string at `0x3CBF74`, XMA codec strings at `0x3CE298` and `0x3CE2C4`, Xenon output strings at `0x3CEF38` and `0x3CEFC0`. Establish the API in use and inspect the audio packages before assigning a bank format. |
| PhysX 2.8.3 | SDK paths starting at `0x3E3334` and `NxPhysicsSDK` at `0x3E6228`. `Engine.xxx` includes `PhysXTiming` and `bDisablePhysXHardwareSupport`. Trace startup, cooking, and gameplay calls. |
| Scaleform GFx | Loader diagnostic at `0x375C68` expects GFX 3.x files. `GFxUI.xxx` has engine and movie classes. Game packages contain `TnScaleformHelpers`, `GetScaleformInterface`, `TnInteractActionPlayGFxMovie`, and `SavingContent_GFX`. The SDK version remains unknown. |
| Bink | Video files under `TransGame/Movies/` and decoder strings at `0x3E326C` and `0x3E32E4`. Test logo playback and campaign transitions independently. |

## Reference ports

Start with Singularity and Gears of War 2, then use Alice, Mass Effect, and MK9 as targeted references. Performance figures below describe the authors' reported conditions.

| Port | Where to look | Reported coverage |
| --- | --- | --- |
| [Singularity](reference-docs/singularity-nx/README.md) | Xenos ring handling, UE3 depth and tiling, runtime fixes, codegen overrides, and profiling | Full playthrough reported at 540p, CPU 1785 MHz, GPU 460.8 MHz. Complex scenes can fall below 20 fps with stock CPU clocks. |
| [Gears of War 2](reference-docs/gears2-nx/README.md) | ReXGlue Switch SDK, shader tooling, GPU ring, PPC-to-ARM64 integration. Compare `gow2/src/`, `gow2/codegen/`, and the SDK and host patch directories. | About 25 fps on average with a CPU overclock, including 20–25 fps in combat. |
| [Alice: Madness Returns](reference-docs/alice-nx/README.md) | Renut Direct3D hooks, deko3d, advance shader compilation, depth and blending discrepancies, game-thread CPU limits | Full playthrough reported. About 29.5 fps on average and dips to 21 fps at 480p with shadows disabled and a high overclock. |
| [Mass Effect](reference-docs/masseffect-nx/README.md) | PM4 and Vulkan/NVK path, EDRAM, Xenos shader discovery and LZO1X blocks, profiling | Prologue, areas, conversations, and battles tested. About 25 fps at stock clocks at 960×544, about 20 fps in heavy scenes. |
| [Mortal Kombat Komplete Edition](reference-docs/mk9-nx/README.md) | Renut and deko3d, arena load shader compilation, runtime and cache instrumentation | About 60 fps in most arenas at 720p with CPU 1785, GPU 768, RAM 1800 MHz. Authors report specific modes and the start of story mode. |

### Singularity diagnostics

Singularity builds on the Gears of War 2 port. Refer to its codegen pipeline and SDK patches when matching the following symptoms. [Singularity change notes](https://github.com/panalexeu/singularity-nx#changes)

- A red strip at the bottom of the screen led to Xbox 360 tiling and MSAA. Inspect `GUseTilingMode` and compare individual GPU passes. A red world and stale frames led to different depth-prepass issues.
- Black objects after transitions led to texture descriptor exhaustion. Inspect allocation and release counts, especially across map changes. The port enlarged a descriptor table from 8,192 to 32,768 entries and added reclamation.
- A disappearing weapon against the sky led to a depth-of-field pass using a depth resolve captured before the weapon was drawn. Inspect pass order, stencil, and the handling of late depth.
- Notification-triggered flicker and GPU crashes led to conflicting bindings between threads. The port applied bindings at each submission. See SDK patches 0007–0012.
- Audio crackle and buffers that failed to finish led to CPU spin waits around GPU occlusion and incorrect completion ordering. Verify the sound pipeline in WFC before testing those specific fixes.
- Shader conversion in the first-use path caused stalls. Moving metadata work ahead of time and reducing vertex-buffer copies helped in measured scenes. Record cold and warm cache measurements and trace the frame before choosing optimizations.
- The port added 277 missed function entries. Use WFC codegen diagnostics and disassembly to identify function starts, data ranges, jump tables, and indirect calls for this XEX.

### Other investigation paths

- Alice's visual issues provide depth, decal, blending, gamma, and texture test cases. The renderer intercepts Direct3D through Renut and uses deko3d. Check register-hoisting and fence changes in isolation before adopting them. [Alice known issues](https://github.com/adumah/alice-nx#known-issues)
- Mass Effect follows GPU commands through Vulkan/NVK and uses XenosRecomp and DXC for advance translation. Confirm WFC container structure and compression first. The Mass Effect project's LTO, PGO, and function-ordering results are specific to its benchmark. [Shaders](reference-docs/masseffect-nx/docs/shaders.md), [optimizations](reference-docs/masseffect-nx/docs/optimization-paths.md), [known issues](reference-docs/masseffect-nx/docs/known-issues.md)
- MK9 first-use stalls can occur when an effect adds a shader variant. Compare this with Alice's advance shader compilation and record shader discovery, compile time, cache hits, and completeness. [MK9 port notes](reference-docs/mk9-nx/docs/mk9-notes.md)
- The [Sonic Adventure 2 guide](reference-docs/sa2-nx/docs/porting-guide.md) covers the general Xbox 360 recompilation process. Its older sections cover Vulkan and the [deko3d guide](reference-docs/sa2-nx/docs/deko3d-renderer-guide.md) covers the Direct3D interception path. Review its pointer audit and jump-table tools. Check whether WFC dynamically loads additional guest modules using its own binaries and runtime. SA2 uses a different engine.
- Commercial UE3 ports also supply profiling ideas. The Bulletstorm team describes a Switch renderer, ASTC texture work, and audio adaptation. Turn Me Up reported challenges around expensive particle and ability effects on Borderlands. Treat these as reference points for test scenes. [Bulletstorm developer case study](https://dragonslake.com/portfolio/bulletstorm/), [Borderlands developer interview](https://nintendoeverything.com/borderlands-switch-dev-made-proprietary-tech-for-unreal-2-5-unreal-3-titles-to-run-natively-on-the-system/)

## Applying the research

1. Match the XEX hash before applying any address-specific findings. Collect import ordinals and look for additional modules, relocations, guest exception handlers, and indirect control flow.
2. Establish a reference capture for boot logos, menu, the first campaign map, level transitions, checkpoints, and input. Record video, sound, and timing separately.
3. Compare the PC baseline with the source game, then use a capture from a reproducible WFC frame to decide between replaying Xenos commands and intercepting Direct3D. Test EDRAM resolves, depth, UI, materials, and particles.
4. Measure correctness first on Switch. Then capture CPU, GPU, memory, synchronization, and copying in named scenes at specified resolution and clocks. Include frame-time percentiles, cold and warm cache results, and any regressions.
5. For each fix, publish the symptom, affected revision, reproduction, evidence, root cause, patch, and before-and-after verification. Credit the upstream ports and keep transferable findings separate from findings tied to this executable.
