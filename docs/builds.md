# Build history

Project records use sequential `build-NNNN` identifiers. Include attempts that reveal a problem or establish a baseline. A local script invocation also receives a checkout-specific attempt number in `out/attempts/`. Reference it when it helps readers match a record to a local log.

Each project record should give the date, source commit and any local changes, target, game input hash, SDK and compiler versions, command, artifact or failure stage, problems found and resolved, and verified behavior. Write down major milestone branches only once the milestone has been verified.

## build-0001 · Windows x64 debug baseline from ISO

- **Date** 18 November 2026
- **Source** `ca2d5ccd0e1a7a70096f93296abf3b1241396149` plus the uncommitted bootstrap
- **Input** Reference Xbox 360 disc image and [fingerprints](../UE3_Xbox360_Switch_References.md#war-for-cybertron-discs-and-packages)
- **Tools** [ReXGlue 0.10.0](https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.0) at `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, [extract-xiso build-202609111233](https://github.com/XboxDev/extract-xiso/releases/tag/build-202609111233), Python 3.13.12
- **Reproduction** `build.cmd "path to War for Cybertron.iso"`. Local attempt `0001`.
- **Verified** The extracted XEX matched the expected SHA-256. An independent comparison matched all 2,512 files in the new extraction to the previous extraction by SHA-256, totaling 7,004,928,727 bytes. ReXGlue initialized the project and read Title ID `4156084C`, Media ID `02619995`, version `0.0.0.4`.
- **Result** ReXGlue exited with status 1 at validation after 398.2 seconds. It reported 23 unresolved branches to 22 distinct targets. C++ emission and native compilation were not reached. There is no executable for this attempt.

## build-0002 · Windows x64 debug baseline from the extracted directory

- **Date** 18 November 2026
- **Source and tools** Same baseline as build-0001. Supplying the extracted directory skips extract-xiso.
- **Reproduction** `build.cmd extracted`. Local attempt `0002`.
- **Verified** The same XEX hash and the same 23 source/target pairs appeared with both input methods.
- **Result** ReXGlue exited with status 1 at validation after 377.8 seconds. The unresolved branches remain the first codegen blocker.

### Reproducible unresolved branches

Every diagnostic refers to an unconditional PowerPC `b` instruction. Audit each source and target in the supported XEX, including surrounding function boundaries and exception metadata. Some targets may be internal labels or shared code fragments. Establish what each target represents before adding a hint. For prior investigations, see [Singularity diagnostics](../UE3_Xbox360_Switch_References.md#singularity-diagnostics) and the [ReXGlue codegen pipeline](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/src/codegen/analyze.cpp).

| Target | Branch source |
| --- | --- |
| `0x82703000` | `0x8273B928` |
| `0x82703030` | `0x8273B880` |
| `0x8271DAC8` | `0x8271F738` |
| `0x8273B9C0` | `0x8273B788` |
| `0x8273BA70` | `0x8273B890` |
| `0x8273BAD8` | `0x8273B920` |
| `0x82899240` | `0x82898EE8` |
| `0x828EDA00` | `0x8271D650` |
| `0x8291C6E8` | `0x82950348` |
| `0x8291CDB8` | `0x82898E18` |
| `0x8291CFE8` | `0x828990D0` |
| `0x8291D5F8` | `0x82898F30` |
| `0x82A2BEA0` | `0x8294F670` |
| `0x82A2BEA0` | `0x82ABF918` |
| `0x82E2CD08` | `0x82DCA9E0` |
| `0x83076D50` | `0x828ED938` |
| `0x830C0CB0` | `0x8291D788` |
| `0x830C0D08` | `0x8291D7D0` |
| `0x830C0E38` | `0x8291D590` |
| `0x830C0F70` | `0x8291CE70` |
| `0x830C1190` | `0x8291D6A8` |
| `0x830C2A28` | `0x827BC3B8` |
| `0x83102CE8` | `0x83102CF4` |

## build-0003 · Verified branch overrides, first pass

- **Date** 18 November 2026
- **Source and input** Same base commit and XEX as build-0002, with an override for each of its 22 distinct branch targets. Override SHA-256 `03936bebe412bad40f7d332b68a321749953150a2cfedd60d07f5da8f5e701ad`.
- **Tools and command** ReXGlue 0.10.0 using `build.cmd extracted --codegen-only`. Local attempt `0003`. The analysis used [xex2ool 0.4.0](https://github.com/landaire/acceleration/releases/tag/v0.4.0) and [Capstone 5.0.7](https://github.com/capstone-engine/capstone/releases/tag/5.0.7).
- **Evidence** Extract the mapped base image with `xex2ool basefile extracted/default.xex -o out/analysis/default.pe`. Its SHA-256 is `398d4e208989d39c93a4bb481a5ff26c698aefc96bb0191e622c3bb9701f0725`. Disassemble the branch sources and destinations as big-endian PowerPC instructions at their virtual addresses, using offsets relative to `0x82000000`. Check each target against the `.pdata` function ranges before setting its size. [ReXGlue's `.pdata` parser](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/src/codegen/phase_register.cpp)
- **Reusable finding** Most first-pass targets begin immediately after a `bctr` tail call and lack an entry in `.pdata`. The v0.10.0 gap filler splits on returns and direct tail branches to known functions. When a port reports an unresolved branch into this layout, disassemble past the preceding terminator and check for a complete thunk or leaf function. [ReXGlue gap filler](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/src/codegen/phase_gapfill.cpp)
- **Result** The original 23 diagnostics disappeared. Validation exposed one more direct branch from the newly registered thunk at `0x8291CDB8` to `0x830C1048`. Inspection showed a single `blr` between an unconditional `bctr` and padding. The next attempt adds a four-byte entry for it. No C++ was emitted in this pass.

## build-0004 · First Windows x64 executable

- **Date** 18 November 2026
- **Source and input** Same base commit and XEX as build-0002. Override SHA-256 `eb486f0022dfd4679d9c210d40754a691252e6c01b2b67ab4bc78df39b408cd3`.
- **Tools and command** `build.cmd extracted` with ReXGlue 0.10.0, Clang and LLD 20.1.8, CMake 4.1.3, Ninja 1.13.2, MSVC CRT 14.44, and Windows SDK 10.0.26100 on x64. Local attempt `0004`.
- **Code generation** Validation passed and emitted 297 C++ translation units among 597 files. A second pass at CMake build time left all generated files unchanged. The bootstrap now writes the manifest with LF line endings to match the ReXGlue stamp writer.
- **Native compile** All 303 Ninja steps succeeded. The local artifact was `wfc.exe`, SHA-256 `058b26b622cc09fb5cba23f72da23ce398a8700daeb6f8442fcc72a9ef78f865`, with debug symbols in `wfc.pdb`.
- **Host loader** A toolchain probe linked successfully, then failed to start with Windows status `0xC0000135`. Its missing non-system imports were `MSVCP140D.dll`, `VCRUNTIME140D.dll`, and `ucrtbased.dll`. The probe exited 0 after the debug CRT was placed next to it. The game also needs `MSVCP140D_ATOMIC_WAIT.dll` and `VCRUNTIME140_1D.dll` through the SDK. A Windows Debug CMake check staged all five from `WFC_WINDOWS_DEBUG_RUNTIME_DIR` and loaded the SDK runtime and Xenos plugin successfully.
- **Game launch** The host executable loaded the Xenos plugin, registered 106,317 guest functions, and executed guest startup through creation of a graphics pipeline. The first fatal error was an unregistered indirect call to `0x83403660`. No menu or gameplay was verified. See local `out/attempts/0004/pc-boot.log`.
- **Callback investigation** At `0x8341C9A0` and `0x8341C9A8`, `lis` and `addi` construct `0x83403660` in argument register `r6`. The caller passes it to the sort routine at `0x830EF590`. Disassembly shows a frameless comparator from `0x83403660` through the second return at `0x834036E4`, ending at the next `.pdata` entry. A sweep of the 18 direct calls to that sort routine found that this was the only missing callback among them. Static validation can pass before these indirect calls are exercised.

## build-0005 · Comparator registration and Windows Debug deployment

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `16d02849858973a0ed03cbdce8c5bec8939c3ab49c105041fbcab81605dd1b14`.
- **Tools and command** Same toolchain and command as build-0004, with the local debug DLL path set in `WFC_WINDOWS_DEBUG_RUNTIME_DIR`. Local attempt `0005`.
- **Codegen and compile** The C++ registration contains `0x83403660`, and Ninja reused the generated files. It completed all 303 steps and deployed the five debug CRT DLLs. The local `wfc.exe` SHA-256 was `b60d6775007f4bd2cc25f480de3f551e26efbd91c6ce547e5c626676e2f95810`.
- **Launch** This build passed the previous comparator failure and stopped on an unregistered indirect call to `0x82D2D3E8`. The `.rdata` slot at `0x822D39AC` points to a two-instruction forwarding thunk. It loads the object pointer from `r3 + 0x38` and tail-branches to the registered `0x824D9570`.

## build-0006 · First pass over unregistered vtable entries

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `1f6b1d53b71a448c9b1ea33ac73efa3f80e8374c9adfef26c213763fa6379073`.
- **Tools and command** Same toolchain and command as build-0005. Local attempt `0006`.
- **Evidence** Scan aligned big-endian code pointers in `.rdata`. Look for unregistered targets with registered function pointers on both sides. Of 158 such targets in build-0005, 40 were eight-byte entries outside `.pdata` starting at padding or immediately after a terminator. Each ends in a tail branch to a known entry or a return. The override file records the table slots and sizes. [SA2 pointer audit guide](../reference-docs/sa2-nx/docs/porting-guide.md#32-code-the-generator-misses)
- **Build and launch** All 303 Ninja steps passed. The local `wfc.exe` SHA-256 was `9f07700a347c56bf106499bfd5eb13100cc38b84ef4b9b7ef0a65d3d110913d6`. Startup passed `0x82D2D3E8`, initialized playback control, and created another graphics pipeline. The next unregistered call was `0x82822568`, referenced by `.rdata` slot `0x8210ACFC`. The 52-byte leaf after the previous tail branch has an early conditional return and a final `blr`.

## build-0007 · Repeat of the first vtable pass

- **Date, source, and input** 18 November 2026. Same base commit, XEX, and overrides as build-0006. Local attempt `0007`.
- **Tools and command** Same Windows debug toolchain and extracted game input as build-0006.
- **Result** All 303 Ninja steps passed and the debug CRT DLLs were staged. The local `wfc.exe` SHA-256 was `4c637e51e9e68d9ce3d60ec12d210cb84bb60d0a5e2729cbe55b9271444d7802`. No separate launch test was recorded.

## build-0008 · Second vtable pass and portable build entry point

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `e2c5b40abe294f39f50e9a2a5111113781f031763b10edf69a4c89c470629b7e`.
- **Tools and command** Same versions as build-0004. Ran `build.cmd extracted` with the portable toolchain loaded by `out/deps/pc-env.cmd`. Local attempt `0008`.
- **Evidence** Rescanned `.rdata` after the first vtable pass. Added 62 verified leaf ranges whose table slots are surrounded by registered pointers. Each range is outside `.pdata`, follows padding or a terminator, and ends in a return or direct tail branch to a registered entry. Direct branches from outside the range were checked for interior targets. All 126 project overrides appeared in the generated registration table.
- **Build** All 303 Ninja steps succeeded and the launcher dependencies were staged. The local `wfc.exe` SHA-256 was `6a91efc67c81f107e830b6845d1465b05f60810b4ccc8c784c99a947f7cf5e32`.
- **Launch** ReXGlue registered 106,419 functions and passed the previous failures, including `0x82822568`. Startup then stopped before the menu on an unregistered dispatch to `0x832B4D50`. The pointer at `.rdata` address `0x82387D48` reaches code immediately after a `bctr`. This method has a larger control-flow graph with conditional targets through at least `0x832B4EAC`. Its complete boundary still needs to be verified. See local `out/attempts/0008/pc-boot.log`.
- **Player reproduction** Logos appeared and the animated loading screen kept running while the Windows Debug CRT displayed `abort() has been called`. The player's `wfc_001.log` recorded the same fatal dispatch to `0x832B4D50`.

## build-0009 · Restore three methods split at internal returns

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `32589ac780f3ca2bab248b05c2848550596bcaa8cd07f0b910ce0d1f02e43203`.
- **Tools and command** Same toolchain and `build.cmd extracted` command as build-0008. Local attempt `0009`.
- **Fix** The vtable around `0x82387D48` had three missing entries. The full ranges are `[0x832B4B48, 0x832B4CB8)`, `[0x832B4CB8, 0x832B4D50)` and `[0x832B4D50, 0x832B4EC0)`. Internal returns had split these methods into partial registrations. Verified the local branch targets and terminal vtable dispatches in the mapped image. A scan of `.text`, `.rdata` and `.data` found no external direct branches or aligned pointers to their interiors. Regeneration produced the three entries and absorbed their eight internal registrations.
- **Build and launch** The Windows x64 Debug executable had SHA-256 `1a269e9ece3588e922dd484e6fec61474ed11c88b992aca94424853fff0a8fed`. Boot passed the player's `0x832B4D50` crash and reached further file I/O. The next unresolved call was `0x82821460`. Eleven `.rdata` slots reference its entry. Its reachable leaf body ends at `0x82821634`. See local `out/attempts/0009/pc-boot.log`.

## build-0010 · Rebuild leaf control flow from vtable targets

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `1df282602be5de3aa779b7cd27606fa1d6a128a656eaeb8c73ea2c5cfd94ef07`.
- **Tools and command** Same Windows debug toolchain and `build.cmd extracted` command as build-0008. Local attempt `0010`.
- **Evidence** The audit walked branches and fallthrough paths from unregistered addresses in contiguous code-pointer tables in `.rdata`. It admitted 166 more non-overlapping leaves with verified returns and tail targets. It checked for aligned data pointers and direct branches entering their interiors from outside. Codegen registered all 295 overrides.
- **Build and launch** All 303 Ninja steps succeeded. The local `wfc.exe` SHA-256 was `8b6b1be8d890bc9ac5a30ae043cc573b628c52614e9754c4a3c0f6938da8e816`. Startup passed `0x82821460` and reached sustained XMA audio activity. The next fatal dispatch was `0x824F24E0`. Inspection found a nine-instruction callback with a null check and a vtable tail dispatch through a global object. It had no aligned pointer in the mapped image and no external direct branch into its interior. See local `out/attempts/0010/pc-boot.log`.

## build-0011 · Register the runtime-observed callback

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `1d95f273871d04eb6b6cc7ddfebcd9e0cf6442abd791e8536369ad6523be89d2`.
- **Tools and command** Same Windows debug toolchain and `build.cmd extracted` command as build-0008. Local attempt `0011`.
- **Build and launch** All 296 overrides were registered and all 303 Ninja steps passed. The local `wfc.exe` SHA-256 was `03f358d22f3b2f552c08caec82e7bd6dacf220cc6cda06fec3cdaa6b99d7251b`. Startup passed `0x824F24E0`. The next fatal dispatch was `0x832786F8`. The aligned pointer at `0x823857DC` is the middle slot of a three-method table with both neighbors already registered. The function has two local paths that join at the final store before returning. The audited range is `[0x832786F8, 0x83278718)`. See local `out/attempts/0011/pc-boot.log`.

## build-0012 · Complete the three-method table and diagnose a shader assertion

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `f73b8359a47d83c40768a8e29c58f2cd2f73e8c3ede0f639b947d95cc9015f81`.
- **Tools and command** Same Windows debug toolchain and `build.cmd extracted` command as build-0008. Local attempt `0012`. Used LLDB 20.1.8 for the native assertion.
- **Build** All 297 overrides registered and all 303 Ninja steps succeeded. The local `wfc.exe` SHA-256 was `6538a92e9122e8861b5bccd65dc11b948d6608e4028c1f5f62ef484aca2737f6`.
- **Launch** A 40-second debug probe and a 100-second info probe completed without a missing-function diagnostic. They passed `0x832786F8` and reached audio output initialization and the frontend loading-video probes. A separate visible desktop run displayed a CRT `abort()` dialog and exited with code 3. Menu appearance remained unverified.
- **Native cause** LLDB stopped in `ucrtbased.dll!_wassert` on the GPU command thread, called by `rexgpu-xenosd.dll`. The assertion argument was `(fetch_instr.attributes.stride) != 0` in `Shader::GatherVertexFetchInformation`. The shader translator assumes a newly registered vertex binding has a nonzero stride. The abort observed under LLDB came from that check. See local `out/attempts/0012/pc-debugger.log` and [the pinned ReXGlue translator](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/src/graphics/pipeline/shader/translator.cpp#L352-L400).

## build-0013 · Compare the desktop launch with the non-Debug graphics module

- **Date, source, and input** 18 November 2026. Reused the generated project, XEX, and override snapshot from build-0012. Local attempt `0013`.
- **Tools and configuration** Same compiler and SDK versions as build-0012. Configured the generated project with `cmake --preset win-amd64-relwithdebinfo`, a separate binary directory, and `-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O0 -g -DNDEBUG`. Built with `cmake --build <binary-directory>`. This uses the matching `rexgpu-xenosrd.dll` and `rexruntimerd.dll` from the pinned SDK.
- **Build and launch** All 303 Ninja steps passed. The local `wfc.exe` SHA-256 was `fac23c19a5dc86d2bbd6a901b7ff341f549e96fe0ccd61a80b0a4d7cbd1ea770`. The desktop run passed the vertex-fetch assertion site and continued into frontend startup. The next fatal dispatch was `0x82ABDA90`. The mapped executable shows a four-instruction vtable forwarding thunk between the previous indirect tail call and the next entry at `0x82ABDAA0`. No main menu was verified. See local `out/attempts/0013/pc-boot-desktop.log`.
- **Interpretation** This comparison isolates a Debug-only shader assertion from the next guest dispatch failure. It does not establish rendering correctness for the zero-stride shader. The Debug plugin still contains the assertion. [ReXGlue assertion definitions](https://github.com/rexglue/rexglue-sdk/blob/v0.10.0/include/rex/assert.h#L26-L31)

## build-0014 · Reproduce the non-Debug launch through the build wrapper

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `1dd78c51aabc0f89c3b7ee4ce9d4942bc65d2b45651d4a61263b186e5a22ca80`.
- **Tools and command** Same Windows toolchain and pinned SDK as build-0012. Ran `build.cmd extracted --pc-config relwithdebinfo` for local attempt `0014`. The stock CMake configuration uses `-O2 -DNDEBUG`, emits a host PDB, and stages the matching non-Debug ReXGlue modules.
- **Fix** Registered `0x82ABDA90` and `0x82ABDAA0` as four-instruction vtable forwarders. The game constructs both addresses in code with `lis` followed by signed `addi`, then saves them with `stw`. The construction at `0x82AD8600/0x82AD860C` writes the first address into a stack descriptor at `0x82AD8614`.
- **Build and launch** All 303 Ninja steps succeeded. The local `wfc.exe` SHA-256 was `b0eee313aac87c2028a8fffc65bc19c4cd7133712dd6b73f9b661d5060b9100e`. On the Windows desktop, startup passed `0x82ABDA90` and reached frontend loading. The next fatal dispatch was `0x830C14F8`. See local `out/attempts/0014/pc-boot-desktop.log`.

### Diagnosing computed PowerPC callbacks

When ReXGlue reports an unregistered indirect call and the target has no aligned pointer in the mapped executable, look for code that assembles the address at runtime. For `lis rH, hi` followed by `addi rD, rH, lo`, reconstruct the 32-bit address with a signed low half. Check that the instructions are on the same straight-line path, that neither register is overwritten before use, and that the resulting pointer is consumed, for example by `stw` into a callback descriptor.

Check the target against `.pdata` and previously registered functions. For a tiny vtable forwarder, follow the vtable load, the method load, the transfer to CTR, and the terminal `bctr`. Some forwarders also unpack arguments. Check direct branches and aligned data pointers for references into the candidate's interior before adding an exclusive `end` override. Regenerate and confirm that the target appears in the generated registration table. A launch through the original failure site verifies the runtime effect.

## build-0015 · Register computed vtable forwarders

- **Date, source, and input** 18 November 2026. Same base commit and XEX as build-0002. Override SHA-256 `4ff0a6125c63760c83e8d48c776b14d2b293cdbfa8c6829810d653a5005c6541`.
- **Tools and command** Same toolchain and `build.cmd extracted --pc-config relwithdebinfo` command as build-0014. Local attempt `0015`.
- **Fix** Audited 308 stored function addresses pointing to vtable forwarders. At `0x830799A4/0x830799A8`, the game constructs `0x830C14F8` and saves it to a stack descriptor at `0x830799AC`. Its range is `[0x830C14F8, 0x830C1510)`. It reads a byte argument, then dispatches through the vtable. Checked the batch for overlapping ranges and external branches or aligned data pointers into their interiors. The generated table contains all 607 overrides.
- **Build and launch** All 303 Ninja steps succeeded. The local `wfc.exe` SHA-256 was `52253192345290b38f593aaff1458d0d4fab4445411d69e0dea0c81fdf500ad9`. The desktop run passed `0x830C14F8` and continued loading the frontend. The next fatal dispatch was `0x82756490`, the only missing pointer in the 23-entry table at `0x820B3688`. See local `out/attempts/0015/pc-boot-desktop.log`.

## build-0016 · First confirmed Windows main menu

- **Date, source, and input** 18 November 2026. Built from the same base and XEX as build-0002, with override SHA-256 `9af938dbca375ac355174cb6b86554b4f3506a4ed347096f43e1bfbe6e1cef10`. The source snapshot is preserved on `milestone/first-pc-boot`.
- **Tools and command** Same toolchain and `build.cmd extracted --pc-config relwithdebinfo` command as build-0014. Local attempt `0016`.
- **Fix** Registered `[0x82756490, 0x827564B8)` and an analogous vtable method at `0x82FBB618`. The former returns zero on its early path and moves the receiver into `r3` before a virtual tail call on the other path. Also registered 22 computed callback addresses with verified returns or tail targets.
- **Build and launch** All 631 overrides appeared in the generated registration table. All 303 Ninja steps succeeded. The local `wfc.exe` SHA-256 was `6f4aaab633755643bab1adef25a4a12774bcc05c3c79c5c5d856feca275d590a`. The desktop run passed the previous loading failure and displayed the opening Bink cinematic. The player reached the main menu, selected Campaign, and saw the brightness prompt.
- **Campaign blocker** Confirming brightness caused a crash. Two player launch logs and a separate desktop probe all reported a fatal indirect call to unregistered guest address `0x82A259F8`. The player reported low frame rate without a measurement. The first playable campaign segment remains pending.
