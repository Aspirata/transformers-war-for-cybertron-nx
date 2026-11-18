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

## Build records and branches

- Give each build attempt a number before it runs and keep a local record of its commands, input hash, target, and result. Keep failed attempts so failures can be reproduced. Local numbers are scoped to the checkout.
- Give every game build recorded or shared by the project the next repository-wide `build-NNNN` number in [the build history](docs/builds.md). Include the date, source commit, input hash, SDK/toolchain revisions, target and artifact, observed problems, fixes and remaining blockers, and the checks actually run.
- Create a `milestone/` branch at the verified commit for major milestones. Use `milestone/first-pc-boot` for the first PC boot and `milestone/first-switch-boot` for the first hardware boot. Routine iterations go in the build history. Reserve additional milestone branches for results worth preserving independently.
- Keep the Windows `.cmd` and Linux `.sh` build entry points aligned. Default to the PC debug target during bring-up and expose the Switch target when an actual Switch build path is ready.
