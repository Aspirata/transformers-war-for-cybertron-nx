"""Local build and launch entry point for War for Cybertron."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out"
ATTEMPTS = OUT / "attempts"
SDK_VERSION = "0.10.0"
SWITCH_SDK_REVISION = "f961303fe3c6e3a90905b379ff4bf3de9a35a757"
XEX_SHA256 = "107314b11111cef557b9bcee243b0a865c964f59cde99030c152265033934b22"
WINDOWS = sys.platform == "win32"
EXE_SUFFIX = ".exe" if WINDOWS else ""


class BuildError(Exception):
    pass


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_game(root, note=print):
    xex = root / "default.xex"
    if not xex.is_file():
        raise BuildError(f"default.xex is missing in {root}")
    digest = sha256(xex)
    note(f"default.xex SHA-256 = {digest}")
    if digest != XEX_SHA256:
        raise BuildError(f"Unsupported Xbox 360 executable. Expected SHA-256 {XEX_SHA256}.")
    transgame = root / "TransGame"
    if not ((transgame / "Xbox360TOC.txt").is_file() and (transgame / "CookedXenon").is_dir()):
        raise BuildError(f"Game data is incomplete in {root}. Provide the full disc image or extraction.")
    return xex


def command(args, cwd, log):
    args = [os.fspath(arg) for arg in args]
    print(f"Running {Path(args[0]).name} {args[1] if len(args) > 1 else ''}".rstrip(), flush=True)
    print(f"Command in {cwd}: {args!r}", file=log, flush=True)
    result = subprocess.run(args, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise BuildError(f"{Path(args[0]).name} exited with status {result.returncode}.")


def game_data(source, extractor, run, note):
    if source.is_dir():
        return source
    if not source.is_file() or source.suffix.lower() not in {".iso", ".xiso"}:
        raise BuildError(f"Provide an Xbox 360 ISO or an extracted game directory. Received {source}")

    digest = sha256(source)
    note(f"Disc SHA-256 = {digest}")
    cache = OUT / "game-data"
    target = cache / digest
    if target.is_dir():
        note(f"Using cached extraction: {target}")
        return target

    if extractor is None:
        local = OUT / "deps" / "extract-xiso" / "artifacts" / f"extract-xiso{EXE_SUFFIX}"
        extractor = local if local.is_file() else shutil.which("extract-xiso")
    if extractor is None:
        raise BuildError("Install extract-xiso or pass --extract-xiso PATH. See README.md.")

    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="extract-", dir=cache) as temp:
        staging = Path(temp) / "game"
        run([extractor, "-x", "-q", "-d", staging, source])
        validate_game(staging, note)
        try:
            staging.rename(target)
        except OSError:
            if not target.is_dir():
                raise
    return target


def next_attempt():
    ATTEMPTS.mkdir(parents=True, exist_ok=True)
    existing = [int(path.name) for path in ATTEMPTS.iterdir()
                if path.name.isascii() and path.name.isdecimal()]
    number = max(existing, default=0) + 1
    while True:
        attempt = ATTEMPTS / str(number)
        try:
            attempt.mkdir()
            return attempt
        except FileExistsError:
            number += 1


def sdk_platform():
    if sys.platform not in {"win32", "linux"} or platform.machine().lower() not in {
        "amd64", "x86_64"
    }:
        raise BuildError("The bootstrap currently targets Windows x64 and Linux x64 hosts.")
    return "win-amd64" if WINDOWS else "linux-amd64"


def record_revision(note):
    git = shutil.which("git")
    if git is None:
        return
    revision = subprocess.run([git, "rev-parse", "HEAD"], cwd=ROOT, text=True, capture_output=True)
    if revision.returncode == 0:
        note(f"Source HEAD: {revision.stdout.strip()}")
        status = subprocess.run([git, "status", "--porcelain", "--untracked-files=normal"],
                                cwd=ROOT, text=True, capture_output=True)
        if status.stdout:
            note(f"Local changes:\n{status.stdout.rstrip()}")


def build_switch(args, attempt, project, cmake, run, note):
    sdk = (args.switch_sdk or OUT / "deps" / "rexglue-sdk-nx").expanduser().resolve()
    if not (sdk / "cmake" / "rexglue_switch.cmake").is_file():
        raise BuildError(f"ReXGlue Switch source is missing at {sdk}. See README.md.")
    if not (sdk / "thirdparty/FFmpeg/config_switch_aarch64.h").is_file():
        raise BuildError(f"Run python tools/switch_setup.py in {sdk} to prepare the Horizon dependencies.")
    note(f"ReXGlue Switch reference revision: {SWITCH_SDK_REVISION}; source: {sdk}")
    if (sdk / ".git").exists():
        revision = subprocess.check_output(["git", "-C", sdk, "rev-parse", "HEAD"],
                                           text=True).strip()
        note(f"ReXGlue Switch checkout revision: {revision}")
        if revision != SWITCH_SDK_REVISION:
            raise BuildError(f"Check out ReXGlue Switch revision {SWITCH_SDK_REVISION}.")

    devkitpro = args.devkitpro or os.environ.get("DEVKITPRO")
    if not devkitpro:
        raise BuildError("Set DEVKITPRO or pass --devkitpro PATH. See README.md.")
    devkitpro = Path(devkitpro).expanduser().resolve()
    gcc = devkitpro / "devkitA64" / "bin" / f"aarch64-none-elf-g++{EXE_SUFFIX}"
    if not gcc.is_file():
        raise BuildError(f"devkitA64 compiler is missing at {gcc}.")
    run([gcc, "--version"])
    note(f"devkitPro: {devkitpro}")

    # GCC ignores the alias attributes when the 0.10.0 template puts them before extern "C".
    pch = project / "generated" / "default" / "wfc_pch.h"
    header = pch.read_text(encoding="utf-8")
    old_alias = ('  __attribute__((alias("__imp__" #name)))                           \\\n'
                 '  __attribute__((weak, noinline)) extern "C" REX_FUNC(name);        \\\n')
    gcc_alias = ('  extern "C" REX_FUNC(name)                                         \\\n'
                 '      __attribute__((alias("__imp__" #name), weak, noinline));       \\\n')
    if header.count(old_alias) != 1:
        raise BuildError("Unexpected ReXGlue function alias template in wfc_pch.h.")
    pch.write_text(header.replace(old_alias, gcc_alias), encoding="utf-8", newline="\n")

    output = attempt / "switch"
    run([cmake, "-S", project, "-B", output, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_TOOLCHAIN_FILE={(ROOT / 'recomp/switch/devkitA64.cmake').as_posix()}",
         f"-DDEVKITPRO={devkitpro.as_posix()}", f"-DREXSDK_DIR={sdk.as_posix()}",
         f"-DWFC_ICON={(OUT / 'wfc-icon.jpg').as_posix()}",
         f"-DWFC_BUILD_ATTEMPT={attempt.name}"])
    run([cmake, "--build", output, "--target", "wfc"])
    artifact = output / "wfc.nro"
    if not artifact.is_file():
        raise BuildError(f"Build completed without the expected Switch NRO: {artifact}")
    note(f"NRO SHA-256 = {sha256(artifact)}")
    note(f"Result: built Switch NRO {artifact}")
    print(f"Switch NRO: {artifact}")
    return 0


def build(args):
    attempt = next_attempt()
    log_path = attempt / "build.log"
    print(f"Attempt {attempt.name} | {log_path}", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        def note(message):
            print(message, file=log, flush=True)

        def run(argv, cwd=ROOT):
            command(argv, cwd, log)

        note(f"Started {datetime.now(timezone.utc).isoformat()}")
        note(f"Target: {args.target}; codegen only: {args.codegen_only}; host: {platform.platform()}")
        if args.target == "pc":
            note(f"PC configuration: {args.pc_config}")
        try:
            record_revision(note)
            host = sdk_platform()
            source = args.source
            if not source:
                source = input("Path to your Xbox 360 disc ISO or extracted game folder: ").strip().strip('"')
            if not source:
                raise BuildError("No game input was provided.")
            source = Path(source).expanduser().resolve()
            note(f"Input: {source}")

            data = game_data(source, args.extract_xiso, run, note)
            xex = validate_game(data, note)
            sdk = Path(args.sdk or os.environ.get("REXSDK") or OUT / "deps" / host).expanduser().resolve()
            rexglue = sdk / "bin" / f"rexglue{EXE_SUFFIX}"
            if not rexglue.is_file():
                raise BuildError(f"ReXGlue {SDK_VERSION} CLI is missing at {rexglue}. See README.md.")
            version = subprocess.check_output([rexglue, "--version"], text=True).strip()
            note(f"ReXGlue SDK {version}: {sdk}")
            if version != SDK_VERSION:
                raise BuildError(f"Expected ReXGlue {SDK_VERSION}, found {version}.")

            project = attempt / "project"
            run([rexglue, "init", "--project-name", "wfc", "--project-root", project,
                 "--xex-path", xex, "--game-root", data])
            config_root = ROOT / "recomp"
            if args.target == "switch":
                config_root /= "switch"
                for filename in ("main.cpp", "graphics.cpp", "graphics.h", "deko-color.h",
                                 "frame-capture.h", "frame-probe.h", "rectangle.geom"):
                    shutil.copyfile(config_root / filename, project / "src" / filename)
                shutil.copyfile(config_root / "deko-color.cmake", project / "deko-color.cmake")
            shutil.copyfile(config_root / "CMakeLists.txt", project / "CMakeLists.txt")
            overrides = project / "wfc-overrides.toml"
            shutil.copyfile(ROOT / "recomp" / overrides.name, overrides)
            note(f"Codegen overrides SHA-256 = {sha256(overrides)}")
            manifest = project / "wfc_manifest.toml"
            config = manifest.read_text(encoding="utf-8")
            if config.count("\nincludes = []\n") != 1:
                raise BuildError("The generated ReXGlue manifest has an unexpected entrypoint layout.")
            # ReXGlue stamps the manifest with LF line endings after codegen.
            manifest.write_text(config.replace("\nincludes = []\n", f'\nincludes = ["{overrides.name}"]\n'),
                                encoding="utf-8", newline="\n")
            run([rexglue, "codegen", manifest.as_posix()], project)
            if args.codegen_only:
                note("Result: code generation complete.")
                print(f"Generated sources: {project / 'generated'}")
                return 0

            cmake = shutil.which("cmake")
            if not cmake:
                raise BuildError("CMake 3.25+ is required to compile the generated code. See README.md.")
            run([cmake, "--version"])
            if args.target == "switch":
                return build_switch(args, attempt, project, cmake, run, note)
            preset = f"{host}-{args.pc_config}"
            run([cmake, "--preset", preset, f"-DCMAKE_PREFIX_PATH={sdk}",
                 f"-DREXSDK_VERSION={SDK_VERSION}"], project)
            run([cmake, "--build", "--preset", preset], project)
            artifact = project / "out" / "build" / preset / f"wfc{EXE_SUFFIX}"
            if not artifact.is_file():
                raise BuildError(f"Build completed without the expected executable: {artifact}")
            record = attempt / "pc-artifact.tmp"
            record.write_text(json.dumps({"executable": str(artifact), "game_data": str(data)}),
                              encoding="utf-8")
            record.replace(attempt / "pc-artifact.json")
            note(f"Result: built {artifact}")
            print(f"Executable: {artifact}")
            return 0
        except (BuildError, OSError, subprocess.CalledProcessError, EOFError, KeyboardInterrupt) as error:
            message = str(error) or "Build interrupted."
            note(f"Result: failed. {message}")
            print(f"{message}\nSee {log_path}", file=sys.stderr)
            return 1


def launch():
    records = sorted(ATTEMPTS.glob("*/pc-artifact.json"),
                     key=lambda record: int(record.parent.name), reverse=True)
    for record in records:
        artifact = json.loads(record.read_text(encoding="utf-8"))
        executable = Path(artifact["executable"])
        if executable.is_file():
            data = Path(artifact["game_data"])
            validate_game(data)
            print(f"Launching PC build {int(record.parent.name)}: {executable}", flush=True)
            build_overrides = record.parent / "project" / "wfc-overrides.toml"
            current_overrides = ROOT / "recomp" / build_overrides.name
            if build_overrides.is_file() and sha256(build_overrides) != sha256(current_overrides):
                print("This build uses different recompilation fixes from the current workspace. "
                      "Rebuild to use the current fixes.", file=sys.stderr, flush=True)
            return subprocess.call([executable, "--game_data_root", str(data),
                                    "--user_data_root", str(OUT / "pc-user-data"),
                                    "--gpu_plugin", "xenos"],
                                   cwd=executable.parent)
    raise BuildError("No compiled PC executable found. Run build.cmd or sh build.sh first.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", help="Xbox 360 ISO or extracted game directory")
    parser.add_argument("--target", choices=("pc", "switch"), default="pc", help="Default: pc")
    parser.add_argument("--pc-config", choices=("debug", "relwithdebinfo"), default="debug",
                        help="PC configuration and matching SDK runtime (default: debug)")
    parser.add_argument("--sdk", type=Path, help="ReXGlue SDK install prefix")
    parser.add_argument("--switch-sdk", type=Path, help="ReXGlue Switch SDK source tree")
    parser.add_argument("--devkitpro", type=Path, help="devkitPro installation for Switch")
    parser.add_argument("--extract-xiso", type=lambda path: Path(path).expanduser().resolve(),
                        help="Path to the extract-xiso executable")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--codegen-only", action="store_true", help="Stop after generating C++")
    modes.add_argument("--run-pc", action="store_true", help="Launch the most recent compiled PC build")
    args = parser.parse_args()
    try:
        return launch() if args.run_pc else build(args)
    except (BuildError, ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
