# WFC shader inventory research

## 21 November 2026: first container-pattern scan

This is an offline investigation; it did not produce or install a game build.
The extracted disc has `default.xex` SHA-256
`107314b11111cef557b9bcee243b0a865c964f59cde99030c152265033934b22`.

Scanned all 2,438 `.xxx` files under `extracted/TransGame/CookedXenon` both
as stored and after decompressing 24,717 detected UE3 LZO chunks. The local
`out/analysis/switch-reference/lzo-probe.dll` decoder returned the expected
length for each detected block. This does not prove that every compressed
structure or every game file type has been recognized.

An exploratory signature and layout scan for the 2008 `0x102A11xx` shader
containers described by the Mass Effect NX scanner found 10 plausible
occurrences, representing nine distinct pixel microcodes. None matched the
463 distinct stage/microcode pairs from the local Switch captures, either
directly or after reversing bytes within each 32-bit word. These nine are
unverified candidates, not an inventory or a total. Mass Effect's
container-layout checks have not been established for WFC; a raw signature
search also permits false positives.

Separately, an exact search of the raw and LZO-decompressed content of
`ENV_AllShader_p.xxx`, `TR_AllShader_p.xxx`,
`ENV_NEU_VariantShaders_p.xxx`, `ENV_ORB_D1_Shader_p.xxx`, and
`TransGame.xxx` found no complete direct or 32-bit-byte-reversed matches
for the locally captured shaders of at least 48 bytes. This does not rule
out other serialization, compression layers, generated data, or storage in
other packages. A search of the mapped PE found four exact matches among
361 distinct shader microcodes from the historical attempt captures.

The 463 observed stage/microcode pairs establish a lower bound for the
programs seen during those Switch runs; they do not establish the complete
game total or the count of compiled host variants. Investigate the WFC
shader registration and package loading paths, including byte order,
serialization, chunk layout, and any secondary compression, before
estimating total coverage or preparing a bulk shader pack.

Local scripts and machine-readable results are in ignored `out/analysis/`:
`inventory_wfc_shaders.py`, `shader_probe_match.py`, and
`shader-inventory-20261121.json`. The container hypothesis was compared
with [Mass Effect NX's scanner](https://github.com/nebadasSwifty/masseffect-nx/blob/650e14ef109fb0d39bd68ba8d7af61fe459265b8/shaders/tools/ue3_shader_scan.cpp)
and [container checks](https://github.com/nebadasSwifty/masseffect-nx/blob/650e14ef109fb0d39bd68ba8d7af61fe459265b8/shaders/tools/container_check.h).

## 21 November 2026: nested-compression hypothesis

The Alice NX extractor documents shader code stored in a second compression
layer, as big-endian uncompressed size, compressed size, and XMem-framed LZX
stream. Tested that hypothesis against the LZO-expanded payloads from
`ENV_AllShader_p.xxx`, `TR_AllShader_p.xxx`,
`ENV_NEU_VariantShaders_p.xxx`, `ENV_ORB_D1_Shader_p.xxx`, and
`TransGame.xxx` on the same XEX revision. Outer blocks were individually
decoded and concatenated in file order; these are exploratory payloads,
not reconstructed UE3 packages with proven virtual offsets.

Built and ran the unmodified Alice extractor at commit
`379e9265d789d540903fbbe765018caca7d318bb` against those five plain payloads.
It reported five plain inputs, zero bad inputs, zero containers, and zero
compressed code chunks containing its `0x102A11xx` signature. Its search is
limited to the size bounds, framing and shader signature it expects from
Alice. No Alice game data was available as a positive control for this run.
The tool and its dependencies were used locally, not integrated into WFC.

Independently tested the same two-size header at every byte alignment with
LZO1X in place of LZX, requiring successful decompression to the exact
declared length. Across the five payloads, 317,855 candidate headers were
tested; the per-file counts were 756, 4,810, 9,678, 4,739 and 297,872.
None decoded to the declared size. The input LZO decoder did successfully
decode all 499 selected outer blocks, which checks the decoder on outer
streams but is not a positive control for inner header parsing.

This narrowly tests Alice-style size headers, not every possible secondary
compression, shader layout, package or file type. It does not establish
that WFC has no compressed shader cache, nor does it count all shaders.
The helper `out/analysis/nested_shader_probe.py`, locally built
`out/analysis/nested-shader-probe/alice_scan.exe`, and expanded inputs
under that directory are ignored local analysis artifacts.
