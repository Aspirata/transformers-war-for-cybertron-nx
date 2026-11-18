# Transformers: War for Cybertron for Nintendo Switch

This project investigates statically recompiling the Xbox 360 release for Nintendo Switch. Its first target is a PC debug build. The public build records and UE3 troubleshooting notes are intended to help maintainers of other Xbox 360 ports reproduce the work.

The bootstrap can extract the disc, verify the supported executable, and compile it for Windows x64. [Build 0016](docs/builds.md#build-0016--first-confirmed-windows-main-menu) reaches the main menu and the Campaign brightness prompt. A playable PC build and Switch NRO are pending.

## PC build

Use your own uncompressed Xbox 360 disc image or an extracted game directory containing `default.xex` and `TransGame/`. The bootstrap currently accepts one executable revision, Title ID `4156084C`, Media ID `02619995`, version `0.0.0.4`. See the [input fingerprints](UE3_Xbox360_Switch_References.md#war-for-cybertron-discs-and-packages).

On Windows x64 or Linux x64, install these prerequisites once.

- Python 3.11 or newer.
- The matching [ReXGlue SDK 0.10.0 release](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0). Unzip it into `out/deps/` so the CLI is at `out/deps/win-amd64/bin/rexglue.exe` or `out/deps/linux-amd64/bin/rexglue`. To use a different install directory, pass `--sdk PATH` or set `REXSDK` to the SDK prefix.
- For ISO input, [extract-xiso](https://github.com/XboxDev/extract-xiso/releases). Put the extractor on `PATH`, pass `--extract-xiso PATH`, or unzip the release into `out/deps/extract-xiso/` if the archive contains `artifacts/extract-xiso` or `artifacts/extract-xiso.exe`. Allow about 7 GB for the extracted game, plus space for generated C++ and compiler outputs.
- For native compilation, CMake 3.25+, Ninja, and Clang 20+. On Windows, use a Visual Studio Developer Command Prompt for the x64 C++ build tools and Windows SDK, with `clang++` on `PATH`. On Linux, make `clang++-20` available and install the SDK's host development dependencies. [ReXGlue build workflow](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/.github/workflows/_build-platform.yaml)

For an existing portable Windows toolchain, `build.cmd` can load an optional `out/deps/pc-env.cmd` to set its environment for that build. Keep the file local to the checkout.

Run the entry point for your system and enter the image path at the prompt. You can also supply the path as the first argument.

```bat
build.cmd "D:\Games\War for Cybertron.iso"
```

```sh
sh ./build.sh "/path/to/War for Cybertron.iso"
```

PC Debug is the default. Add `--codegen-only` to inspect the ReXGlue result without the compiler toolchain. Linux native execution still needs verification. Switch support will use `--target switch` once the runtime and NRO packaging are available.

Use `build.cmd extracted --pc-config relwithdebinfo` to select the matching ReXGlue runtime and graphics plugin with symbols for Windows bring-up. This configuration passes the vertex-fetch assertion observed with the Debug plugin during frontend loading. See the [desktop comparison](docs/builds.md#build-0013--compare-the-desktop-launch-with-the-non-debug-graphics-module).

Each invocation writes a numbered log at `out/attempts/NNNN/build.log`. The script keeps generated source and compiler outputs under that attempt and caches extracted images by ISO SHA-256 under `out/game-data/`. Keep those folders local.

## PC launch

After a successful Windows build, run `run-pc.cmd`. It selects the newest numbered attempt with an existing executable, passes the game directory used by that attempt to ReXGlue, and stores user data under `out/pc-user-data/`.

If Windows reports a missing debug CRT DLL, use the debug runtime from your Microsoft C++ and Windows SDK installation. For a portable toolchain, set `WFC_WINDOWS_DEBUG_RUNTIME_DIR` to the directory containing the x64 debug runtime DLLs before building. The Debug build copies the required DLLs next to the local executable. Keep these Microsoft Debug DLLs in your local build directory.

## Development notes

- [Repository instructions](AGENTS.md)
- [Build history](docs/builds.md)
- [Short-term roadmap](docs/roadmap.md)
- [Terra's Xbox 360 UE3 porting research](UE3_Xbox360_Switch_References.md)

