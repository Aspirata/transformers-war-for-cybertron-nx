# Repository instructions

## Project goal

- Port the Xbox 360 release of Transformers: War for Cybertron to Nintendo Switch by static recompilation. Bring up the recompiled game on PC first so startup, imports, and gameplay can be debugged there.
- Prioritize work that helps other Xbox 360 Unreal Engine 3 ports. Document confirmed root causes, how to reproduce and verify fixes, which findings are reusable, and which depend on this game or executable revision.
- Write all READMEs, public guides, build records, and release notes in English.

## Research and debugging

- Begin troubleshooting with Terra's [UE3 port research](UE3_Xbox360_Switch_References.md) and the [reference index](reference-docs/README.md). Start with Singularity for UE3 runtime and rendering issues and Gears of War 2 for the Switch stack. Check upstream source and licensing before reusing code.
- Tie addresses, hooks, and code generation hints to a verified `default.xex` hash. Record the exact SDK revision, game revision, and toolchain. Verify behavior against this game before applying fixes from another port.
- Preserve the original input files. Keep disc images, extracted game content, generated game code, and local build artifacts out of Git. Keep the scripts and hand-authored build configuration reproducible from a user's own disc dump.
- Verify milestones on the target platform. Record compile, launch, menu, gameplay, and hardware performance evidence separately.
- During game debugging, build one version at a time. After a successful build, report its number, what changed, what is expected or might happen, and the specific in-game checks needed. Wait for the user's launch and playtest results before deciding on the next game build. If a build fails, diagnose and fix the failure before presenting a playable build for testing.
- After each successful Switch build, check whether the Switch is connected to the PC. If its storage is accessible, install the new build at `sdmc:/switch/wfc/wfc.nro` and verify that exact file before handing it off for testing. Homebrew Menu gives the NRO that matches the folder name priority. Report any connection or transfer issue in the build handoff.
- Perform all Switch file operations in the background. Use direct device APIs or non-interactive command-line tools for deployment, verification, and log retrieval. Avoid Explorer windows, Shell copy dialogs, and confirmation prompts. If a background transfer is unavailable, report the blocker.
- Follow the PC debugging sequence for Switch builds. After the user reports their launch or playtest results, retrieve the logs, crash reports, and any other files needed to investigate that run directly from the connected Switch. Review them alongside the user's observations before deciding on the next build. Ask the user for files only if they cannot be retrieved from the console.
- After each Switch playtest, compare the shader capture with the installed pack. Compile and validate any missing shaders from the verified game revision and include them in the next Switch build.
- Treat the user's observations as valuable evidence, but verify uncertain or conflicting details against reliable sources such as the corresponding build and game logs. Identify the build and run being examined, distinguish observations from conclusions, and explain any discrepancy without assuming the user's report is wrong.

## Build records and branches

- Give each build attempt a plain sequential number without leading zeroes before it runs and keep a local record of its commands, input hash, target, and result. Keep failed attempts so failures can be reproduced. Local numbers are scoped to the checkout.
- Give every game build recorded or shared by the project the next repository-wide number without leading zeroes, written as `build-21`, in [the build history](docs/builds.md). Existing historical identifiers may remain as recorded. Include the date, source commit, input hash, SDK/toolchain revisions, target and artifact, observed problems, fixes and remaining blockers, and the checks actually run.
- Do not run game builds simultaneously. For substantial or uncertain changes, make changes incrementally and test each build in sequence so regressions can be attributed to a smaller set of changes.
- Create a `milestone/` branch at the verified commit for major milestones. Use `milestone/first-pc-boot` for the first PC boot and `milestone/first-switch-boot` for the first hardware boot. Routine iterations go in the build history. Reserve additional milestone branches for results worth preserving independently.
- Keep the Windows `.cmd` and Linux `.sh` build entry points aligned. Default to the PC debug target during bring-up and expose the Switch target when an actual Switch build path is ready.
