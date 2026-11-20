# Third-party notices

The project's own source is available under GPL-3.0-only; see [LICENSE](LICENSE). Third-party material retains its own license and notices. This file identifies the principal material used by the project; consult each dependency's own notices for its full dependency tree and for binary distribution requirements.

## ReXGlue and the Switch SDK

[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) provides the recompiler and runtime. The Switch build uses [rexglue-sdk-nx](https://github.com/nx-mod/rexglue-sdk-nx) at revision `f961303fe3c6e3a90905b379ff4bf3de9a35a757`. The build configuration contains excerpts from that SDK for matching and patching its source; the graphics integration uses its interfaces. The SDK's BSD-3-Clause copyright notice, conditions, and disclaimer are reproduced in [LICENSES/ReXGlue-BSD-3-Clause.txt](LICENSES/ReXGlue-BSD-3-Clause.txt). It credits Tom Clay and portions derived from Xenia by Ben Vanik and Xenia contributors. The SDK and its dependencies are obtained separately and retain their own licenses.

## Shader tools and build dependencies

Shader preparation invokes the external `ds_hlsl` tool and `shader_common.h` from the pinned [gears2-nx](https://github.com/VasilkovND/gears2-nx/tree/c202b045cce00c96b040b095eb8e5250c7c83b59) reference, as well as DXC and UAM. The gears2-nx notices identify its XenosRecomp translator changes as MIT-licensed; see its [third-party notices](reference-docs/gears2-nx/THIRD_PARTY_NOTICES.md). These tools and their dependencies are not included in this repository. Other build dependencies include devkitA64, libnx, deko3d, FFmpeg and libmspack; refer to the versions actually used and their accompanying licenses.

## Reference documents

The documents under [reference-docs](reference-docs/README.md), other than the index, are snapshots of third-party projects. Each snapshot retains its own license, notices and source information. The root project license does not replace those terms.

## Game content

The original game, its assets, generated game code and captured shaders are not licensed by this project and are not included here. Users supply their own game files.
