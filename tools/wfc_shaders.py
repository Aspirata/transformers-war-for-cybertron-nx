"""Translate a local Switch XESH startup capture into a WFC deko3d shader pack."""

import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


CAPTURE_HEADER = b"XESH\x20\x20\x12\x19"
PACK_HEADER = b"WFCS\x01\x00\x00\x00"
EXEC_OPS = {1, 2, 3, 4, 5, 6, 13, 14}
CAPTURE_LIMIT = 8 * 1024 * 1024
PACK_LIMIT = 32 * 1024 * 1024
# Vertex attributes use the pinned renderer's semantic-to-location mapping.
VERTEX_SEMANTICS = (
    (0, 0, 0, "Position"), (5, 0, 4, "TexCoord"), (5, 1, 5, "TexCoord"),
    (5, 2, 6, "TexCoord"), (5, 3, 7, "TexCoord"), (10, 0, 8, "Color"),
    (10, 1, 11, "Color"), (5, 4, 12, "TexCoord"), (5, 5, 13, "TexCoord"),
    (5, 6, 14, "TexCoord"), (5, 7, 15, "TexCoord"), (3, 0, 1, "Normal"),
    (6, 0, 2, "Tangent"), (7, 0, 3, "Binormal"), (1, 0, 10, "BlendWeight"),
    (2, 0, 9, "BlendIndices"),
)
VARYING = re.compile(r"^([ \t]*)((?:in float4 i|out float4 o)(TexCoord|Color)(\d+) : \w+)", re.M)
SAMPLER = re.compile(r"\[\[vk::binding\((\d+), 0\)\]\] Texture(2D|3D|Cube)<float4> \w+_Textura;")
MANUAL_FETCH = re.compile(
    r"\br0\.[xyzw]+ = (remapInput\(dsVfetch\((\d+), r0\.x, (?:true|false)\), g_UboLecturas\.v\[\2\]\.w\))"
)
R0_X_WRITE = re.compile(r"(?m)^[ \t]*r0(?:\.[yzw]*x[xyzw]*)?\s*[+*/-]?=(?!=)")


def capture_records(path):
    if path.stat().st_size > CAPTURE_LIMIT + len(CAPTURE_HEADER):
        raise ValueError("Capture exceeds the runtime capture limit.")
    data = path.read_bytes()
    if not data.startswith(CAPTURE_HEADER):
        raise ValueError("Expected a ReXGlue XESH capture.")
    cursor = len(CAPTURE_HEADER)
    seen = set()
    while cursor < len(data):
        if len(data) - cursor < 12:
            raise ValueError("Truncated shader record.")
        key, flags = struct.unpack_from("<QI", data, cursor)
        pixel = bool(flags >> 31)
        dwords = flags & 0x7FFFFFFF
        start = cursor + 12
        cursor = start + dwords * 4
        if not 0 < dwords <= 0xFFFF or cursor > len(data):
            raise ValueError(f"Invalid microcode size for shader {key:016x}.")
        if (pixel, key) in seen:
            raise ValueError(f"Duplicate shader {key:016x}.")
        seen.add((pixel, key))
        yield pixel, key, data[start:cursor]


def instruction_slots(code):
    slots = list(struct.iter_unpack(">III", code))
    code_start = len(slots)
    executions = []
    for index, (lo, middle, hi) in enumerate(slots):
        if index >= code_start:
            break
        for first, second in ((lo, middle & 0xFFFF),
                              (((hi & 0xFFFF) << 16) | (middle >> 16), hi >> 16)):
            if second >> 12 not in EXEC_OPS:
                continue
            address = first & 0xFFF
            count = (first >> 12) & 7
            if not index < address <= len(slots) - count or count > 6:
                raise ValueError("Invalid Xenos execution clause.")
            code_start = min(code_start, address)
            executions.append((address, count, first >> 16))
    if not executions or any(address < code_start for address, _, _ in executions):
        raise ValueError("Xenos shader has no valid execution clauses.")
    instructions = {}
    for address, count, sequence in executions:
        for offset in range(count):
            slot = address + offset
            fetch = bool(sequence & (1 << (offset * 2)))
            if instructions.setdefault(slot, fetch) != fetch:
                raise ValueError("Conflicting Xenos instruction types.")
    return [(slot, fetch, slots[slot]) for slot, fetch in sorted(instructions.items())]


def make_container(pixel, code):
    elements = []
    outputs = 0
    for slot, fetch, (word, _, _) in instruction_slots(code):
        if not pixel and fetch and word & 31 == 0 and word & (1 << 19):
            if len(elements) == len(VERTEX_SEMANTICS):
                raise ValueError("Shader exceeds the renderer's 16 vertex attributes.")
            usage, index, _, _ = VERTEX_SEMANTICS[len(elements)]
            elements.append((slot, usage, index))
        if pixel and not fetch and word & (1 << 15):
            destination = word & 63
            if destination < 4:
                outputs |= 1 << destination
            elif destination == 61:
                outputs |= 16

    # A raw capture lacks the D3D declarations. Link by Xenos export/GPR ordinal.
    # Position generation is left disabled until the draw's SQ state is captured.
    interpolators = [(index << 8) | (5 << 4) | index for index in range(16)]
    shader = [0, len(code), 0, 0xFF00, 0, len(interpolators) << 5, 0]
    if pixel:
        shader.append(outputs)
    else:
        shader.extend((len(elements), 0))
        shader.extend(slot | (usage << 12) | (index << 16) for slot, usage, index in elements)
    shader.extend(interpolators)
    header = [0x102A1100 | int(not pixel), 36 + len(shader) * 4, len(code), 0, 0, 0, 36, 0, 0]
    return struct.pack(f">{len(header) + len(shader)}I", *header, *shader) + code, elements, outputs


def shader_kills(spirv):
    words = struct.unpack(f"<{len(spirv) // 4}I", spirv)
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("Invalid DXC SPIR-V output.")
    cursor = 5
    kills = 0
    while cursor < len(words):
        token = words[cursor]
        size = token >> 16
        if not size or cursor + size > len(words):
            raise ValueError("Invalid SPIR-V instruction size.")
        kills += (token & 0xFFFF) in (252, 4416, 5380)
        cursor += size
    return kills


def fix_self_fetches(text):
    # The external translator sees the current fetch's r0 destination as a prior
    # write. Only substitute fetches whose r0.x still holds the vertex index.
    main = text.index("void main(")
    body = text.index("\n{", main)
    matches = list(MANUAL_FETCH.finditer(text, body))
    for match in reversed(matches):
        if R0_X_WRITE.search(text, body, match.start()):
            continue
        usage, index, location, name = VERTEX_SEMANTICS[int(match[2])]
        attribute = f"i{name}{index}"
        if usage == 5:
            attribute = f"tfetchTexcoord(g_SwappedTexcoords, {attribute}, {index})"
        fetch = f"remapInput(float4({attribute}), g_InputRemap({location}))"
        start, end = match.span(1)
        text = text[:start] + fetch + text[end:]
    if "dsVfetch(" in text[text.index("void main("):]:
        raise ValueError("Computed vertex fetch is unsupported by the pinned renderer.")
    return text.replace("[[vk::constant_id(1)]] const uint g_ManualesR0", "const uint g_ManualesR0")


def translate(args, work):
    containers = work / "containers"
    hlsl = work / "hlsl"
    containers.mkdir()
    hlsl.mkdir()
    records = {}
    programs = {}
    for capture in (args.capture, *args.extra_capture):
        for pixel, key, code in capture_records(capture):
            if programs.setdefault((pixel, key), code) != code:
                raise ValueError(f"Conflicting microcode for shader {key:016x}.")
    for (pixel, key), code in programs.items():
        name = f"{'ps' if pixel else 'vs'}_{key:016x}"
        try:
            container, elements, outputs = make_container(pixel, code)
        except (ValueError, struct.error) as error:
            print(f"{name}: {error}", file=sys.stderr)
            continue
        (containers / f"{name}.bin").write_bytes(container)
        records[name] = (pixel, key, code, elements, outputs)

    tool = subprocess.run([args.translator.resolve(), containers, hlsl, args.common.resolve(),
                           "--saltos-lineales"], check=False)
    if tool.returncode not in (0, 2):
        raise ValueError(f"Xenos translator failed with status {tool.returncode}.")
    pack = bytearray(PACK_HEADER)
    stages = [0, 0]
    for name, (pixel, key, code, elements, outputs) in records.items():
        source = hlsl / f"{name}.hlsl"
        if not source.is_file():
            print(f"{name}: no translated HLSL.", file=sys.stderr)
            continue
        text = source.read_text(encoding="utf-8")
        if pixel:
            # Match the full Xenos pixel constant bank provided by the WFC build.
            text = text.replace("struct NfsmwBloquePs { float4 v[224]; };",
                                "struct NfsmwBloquePs { float4 v[256]; };")
            text = text.replace("select((INDEX) < 224, g_UboPixel.v[min(INDEX, 223)], 0.0)",
                                "select((INDEX) < 256, g_UboPixel.v[min(INDEX, 255)], 0.0)")
        else:
            try:
                text = fix_self_fetches(text)
            except ValueError as error:
                print(f"{name}: {error}", file=sys.stderr)
                continue
        # Preserve gaps when DXC discards unused inputs from a pixel shader.
        # Exclude vertex input attributes, which already have explicit locations.
        direction = "in" if pixel else "out"
        for match in reversed(list(VARYING.finditer(text))):
            spaces, declaration, semantic, index = match.groups()
            if declaration.startswith(direction):
                location = int(index) + (16 if semantic == "Color" else 0)
                text = text[:match.start()] + f"{spaces}[[vk::location({location})]] {declaration}" + text[match.end():]
        source.write_text(text, encoding="utf-8")
        spv_path = work / f"{name}.spv"
        compiler = subprocess.run(
            [args.dxc.resolve(), "-spirv", "-T", "ps_6_6" if pixel else "vs_6_6", "-E", "main",
             "-HV", "2021", "-fspv-target-env=vulkan1.2", "-fvk-use-dx-layout",
             "-Werror=parameter-usage", "-Fo", spv_path, source],
            capture_output=True, text=True, errors="replace", check=False,
        )
        if compiler.returncode:
            print(f"{name}: DXC rejected the shader.\n{compiler.stderr}", file=sys.stderr)
            continue
        spirv = spv_path.read_bytes()
        kills = shader_kills(spirv)
        sampler_types = {"2D": 12, "3D": 13, "Cube": 14}
        samplers = sorted({(int(register), sampler_types[dimension])
                           for register, dimension in SAMPLER.findall(text)})
        pack += struct.pack("<7I", int(pixel), len(code) // 4, len(spirv) // 4,
                            len(elements), len(samplers), outputs, kills)
        pack += struct.pack("<Q", key)
        pack += b"".join(struct.pack("<HBB", *element) for element in elements)
        pack += b"".join(struct.pack("<HH", *sampler) for sampler in samplers)
        pack += code
        pack += spirv
        stages[pixel] += 1
    if not all(stages) or len(pack) > PACK_LIMIT:
        raise ValueError(f"Invalid shader pack size or stage coverage: {stages}, {len(pack)} bytes.")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(pack)
    print(f"Packed {stages[0]} vertex and {stages[1]} pixel shaders into {args.output}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--extra-capture", type=Path, action="append", default=[],
                        help="Merge another capture by shader stage and key. Can be repeated.")
    parser.add_argument("--translator", type=Path, required=True, help="External Gears 2 ds_hlsl tool")
    parser.add_argument("--common", type=Path, required=True, help="Matching shader_common.h")
    parser.add_argument("--dxc", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="Keep intermediate shader files in a new directory")
    args = parser.parse_args()
    try:
        if args.work_dir:
            args.work_dir.mkdir(parents=True)
            translate(args, args.work_dir.resolve())
        else:
            with tempfile.TemporaryDirectory(prefix="wfc-shaders-") as work:
                translate(args, Path(work))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
