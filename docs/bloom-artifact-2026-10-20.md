# Elite's bloom: blotchy, off-centre halos (2026-10-20)

## Status

- **State (2026-10-20): measured offline, nothing built.** Base Elite with
  EDVR's AA off. This holds for the flat System Map (build 334473) and for
  loading screens on both 332841 and 334473.
- **The halo is not offset.** The star's halo sits within 1 px of its disc's
  centre. Opposite clean sectors of the halo match within one 8-bit code.
- **What reads as "further to the lower right" is other emitters.** It is a
  separate cyan-white glow from the station marker, the asteroid belt and the
  planet chain; it is centred on those objects. The star halo is not displaced.
- **What reads as blotches and stripes** on the dark loading screens is
  contour banding of a wide, dim glow on black. Each band is about one output
  code. The lumpy band edges follow the emitter's shape.
- **4.4.2.0 did not introduce it.** A 332841 loading screen (2026-10-19)
  shows the same banded glow. The bloom shaders EDVR can name have the same
  hashes on both builds.
- **Open hypotheses, ranked:**
  1. Output quantization: the tone pass writes 8-bit with no dither. Confirm
     with a float bloom texture that is smooth while the 8-bit tone output
     bands in 1-code steps.
  2. Coarse-level upsample lumps: a single-tap bilinear upsample in the
     chain's `8826CACC6382C78D`. Confirm with the dumped bytecode's tap
     pattern, and kinks in the float bloom at the 39x24 or 78x48 texel
     period.
  3. Odd-size rounding as a scale about the top-left corner (156x97 and
     78x48 at the 0.65 render scale). Confirm with UVs built from
     SV_Position in the chain bytecode, and an off-centre source whose halo
     is displaced away from the top-left corner.
- **Ruled out:**
  - Half-texel offset accumulating toward one corner: at screen centre the
    fitted halo offset is (+0.5, +0.5) px, where (+6, +4) px costs 36% more.
  - Lens ghost or flare: the lobe has the marker's colour and is centred on
    it, and there is no mirror image in the upper left.
  - R11G11B10 chain precision as the cause of the bands: its 1.6% relative
    step is under half an output code for codes below 60, which is where
    the bands are.
  - The 4.4.2.0 update as the cause: see above.
  - Temporal threshold instability: the artifact is in still frames, and
    no flicker is reported.
- **Next capture (one flight, loading screen and System Map, AA off with
  F7):**
  - set `advanced.glare_shader_dump = 1` for one launch. That dumps the
    whole chain, which settles 2 and 3 from bytecode alone;
  - save a Steam *uncompressed* copy of each screenshot (the JPEGs cannot
    tell 1-code steps from JPEG DC quantization);
  - read back the tone draw's t2 (bloom), t1 (HDR) and its 8-bit target.
    This needs a new one-shot instrument, see "The capture".
- **Fix (gated on that capture):** dither the tone pass's float-to-8-bit
  write, by shader swap of the known tone variants. Bloom stays everywhere.
  If 2 is confirmed, also swap the chain's upsample PS for a tent filter.
  See "Fix proposal".
- **Environment:** flat, 1920x1200 output. The map flight ran at SSAA 0.65
  (HDR 1248x780, first bloom level 624x390) with BloomQuality 3 (Ultra),
  DoF 2, Elite AA off (log `edvr_gfx_20261020_165535`). The 332841 loading
  screen ran at HDR 1920x1200. Not tested: VR, where all of Elite's VR
  presets ship BloomQuality 0.

---

## What 4.4.2.0 changed about bloom

The patch note "Bloom too bright on certain objects" cannot be located
offline. What the files show:

- **Effects archives.** `Win64/EffectsBinary/Effects_Win64_SM50.arc` holds
  897 DXBC blobs and is unchanged since 2026-10-01; `Effects2_` holds 1,831
  and was rewritten on 2026-10-20 (16-byte header, then raw deflate).
  Neither holds a bloom, tone or copy shader: none of the hashes named in
  this doc is present. The old `Effects2_` is gone, and the launcher's
  staging copy in the Proton prefix has no EffectsBinary.
- **Asset containers.** `Win64/xx/<sha1>` files are either zlib-wrapped
  assets (0 of 130 sampled hold DXBC) or `FREA` containers. Those are
  opaque (7.99 bits/byte median entropy over 50 files). The post shaders
  live there, so only the running game's shader dump can read them.
- **Graphics configuration.** `GraphicsConfiguration.xml` (2026-10-01) is
  unchanged. Its `<Bloom>` tiers are:

  | Tier | Method | Parameters |
  |---|---|---|
  | Medium | HighPassCount | GlareWeight0..4 = 1, .4499, .2014, .2999, .4148 (five levels), scale .04 |
  | High | CustomPassCount | the same weights, FilterRadius 3 |
  | Ultra | ImprovedCustomPassCount | MaxThreshold 0.25, ThresholdType 1, FilterRadius 3, FilterRadiusWide 4, scale .04 |

  `OptionDefaults/VR*.fxcfg` all set BloomQuality 0.
- **Shader hashes across the update.** The bloom chain's first downsample
  `DFED8E1C9E191BEC`/`143AAE0597E2F7BF`, the stock bloom-on tone
  `E8948389387DA083` and the composite tone `9270C355389DA302` appear in
  332841's log `edvr_gfx_20261019_211738` and in 334473's logs of
  2026-10-20. The other chain shaders (`5ECFE708`, `129F602B`/`8826CACC`)
  are never logged, so whether they changed is unknown.

So the fix is in a material, asset data or an unlogged chain pass, and the
artifact studied here is older than it.

## The measurements

The method uses sRGB-decoded linear values from the Steam JPEGs. Medians
are taken per (radius, 45° sector) bin. Masks remove the left UI panel, the
orbit line, the asteroid belt, the station marker, the planets and the
bottom bar. Images:

- `20261020170131_1.jpg`: the map, AA off; the session's 2.jpg.
- `20261020170135_1.jpg`: the same view with XeSS (1.jpg).
- `20261020194500_1.jpg`: a loading screen on 334473.
- `20261019074137_1.jpg`: a loading screen on 332841.

All are in Steam's `userdata/.../760/remote/359320/screenshots`.

**Star (brown dwarf, System Map).** The disc's centroid is (959.5, 599.6),
with radius 114.3 px. Luminance excess in the lower-right quadrant (0..90°,
y down) against the upper-left quadrant:

| distance from limb (px) | 4-12 | 12-25 | 25-40 | 40-60 | 60-90 | 90-130 | 130-180 | 180-240 |
|---|---|---|---|---|---|---|---|---|
| LR / UL | 1.14 | 1.19 | 1.32 | 1.48 | 1.85 | 3.94 | 7.13 | 9.92 |

That is the reported asymmetry, and it grows with distance. Five of the
eight sectors agree with each other; all the excess is in the three
holding the belt, the marker and the planets. Colour separates it:

- the star's halo at 10-40 px, in linear R, G, B: .0137, .0018, .0037;
- the lower-right lobe at 90-180 px: .0044, .0075, .0080, which is cyan
  and white, not the star's magenta.

Subtracting a radial model of the star's halo, built from the clean
sectors, leaves:

- flat residual all round the star;
- a cyan glow whose centroid is within 3 px of the station marker's icon
  (+0.5, +2.8 px within 60 px of it);
- white glow around the planets.

Fitting the star halo's centre against the disc, on clean pixels only:

| Annulus | Best offset (px) | Cost at (+3, +2) | Cost at (+6, +4) |
|---|---|---|---|
| R+30..R+200 | (+0.5, +0.5) | +8% | +36% |
| R+8..R+120 | (-0.75, -0.25) | +51% | 15x |

The XeSS image (1.jpg), with doubled bloom from the separate tone re-issue
bug, fits (0, 0).

Blotchiness in the clean sectors is the residual's robust SD against the
star's excess:

| Distance from limb (px) | 5-30 | 30-80 | 80-160 |
|---|---|---|---|
| Residual SD / excess | 7% | 9% | 15% |

That is 1 to 2 output codes, which is the JPEG's noise level.

**Loading screens (orange hologram on black).** The glow's reach left and
right of the figure is equal within 18 px, with no consistent sign, at
every threshold from R+G code 60 down to 4, on both builds. The
334473 glow reaches about 1.6x further, for example R+G code 15 at about
150-170 px against 85-105 px. That comparison is confounded: render scale
was 0.65 against 1.0, and the 2026-10-19 BloomQuality is unknown.

At 8x gain the glow is a stack of contour bands. Along row y=600, 42 of 51
code steps are +1 and 7 are +2. At the dark end R runs 0, 2, 4 in 22 px
plateaus, which a JPEG's DC quantization can also produce; a lossless
screenshot decides it. The outer contours wiggle 1.2-1.9 px rms with no
consistent period. A curvature fold at the chain's output texel periods
(12.3, 24.6, 49.2 px across; 12.4, 25, 50 down) shows nothing above control
periods (37, 43, 61 px). So coarse-level lumps are not detected in the
JPEG, but not excluded either.

## The bloom chain as far as EDVR knows it

The chain is from the census in docs/design-flat-temporal-aa-2026-09-23.md
section 81, at a 3840x2160 H:

1. `DFED8E1C/143AAE05` reads H (R11G11B10) into a half-size target.
2. Four `/5ECFE708` downsamples follow, to 120x67.
3. Four `129F602B/8826CACC` upsamples run back to half size, f26
   (R11G11B10_FLOAT).
4. The tone reads the result at PS t2 (E8948389) or t1 (9270C355).

At the 0.65 render scale used here, the levels are 624x390, 312x195,
156x97, 78x48 and 39x24. Two floor steps (195 to 97, 97 to 48) drop half a
texel. At 1920x1200 the levels end at 60x37, with one floor step.

The stock tone PS `E8948389387DA083` (disassembled from the dump) samples
the HDR at t1 and the bloom at t2 at the same interpolated UV, with no
offset. With `cb2[1].w >= 0` it lerps the scene toward the bloom by
`cb2[1].w`; otherwise it adds. It then applies a rational filmic curve, a
16³ LUT and grading, and writes `o0` with no dither. `CFA91824129ECBBC` is
a pass-through fullscreen VS (position and UV). The tone and the copy add
no geometric error, which agrees with the zero offset measured above.

The loading screen's tone pass in 334473's log `edvr_gfx_20261020_194355`
(19:44-19:45) is `F9CFC798F21E9AEA`/`E8948389387DA083`. The map's tone pass
is `CFA918`/`9270C355` (log 165535, 17:01:27), and the map's bloom
composite is `CFA91824`/`FA01CD495BB27AEF`. In VR, where bloom lives is
only believed (docs/cockpit-hud-layer-design-2026-09-27.md).

## Candidate mechanisms

| # | Mechanism | Evidence | State |
|---|---|---|---|
| 1 | 8-bit tone output with no dither bands a wide, dim gradient | Bands are about 1 code apart, worst on black; the tone PS has no dither | open, leading |
| 2 | Bilinear single-tap upsample of coarse levels leaves lumps | Not seen in the JPEG (no period lock, 1-2 px wiggle) | open |
| 3 | Odd-size floor as a scale about the corner | Zero offset at centre is expected either way; needs an off-centre source or the bytecode | open, low |
| 4 | Half-texel offset per level, accumulating | Halo at (+0.5, +0.5) px of its disc | ruled out |
| 5 | Lens ghost or flare | Lobe has the marker's colour and centre; no mirror image | ruled out |
| 6 | R11G11B10 banding | 1.6% relative step is at most 0.5 code under code 60 | ruled out as the band source |
| 7 | Jittered or temporally unstable threshold | Still frames show it | ruled out as the cause seen |
| 8 | Other emitters' bloom read as the star's | Colour and centroid of the lobe | confirmed (the map's "lower right") |

## The capture

One session, AA off (F7), on a loading screen and on the System Map:

1. **Shader dump.** Set `advanced.glare_shader_dump = 1` for that launch
   only, which `device_hook.cpp` reads. That gives the bytecode of
   `5ECFE708…`, `129F602B…`/`8826CACC…`, `FA01CD495BB27AEF` and
   `C89DD4ED362D743F`. Disassemble them with `tools/dxbc_disasm.py`. This
   settles 2 (the tap pattern) and 3 (UVs interpolated, or built from
   SV_Position and a size constant) with no headset time. Set the key back
   to 0 afterwards.
2. **Lossless screenshots.** Use Steam's "save an uncompressed copy". If
   the dark end still runs 0, 2, 4, something upstream of the 8-bit write
   quantizes; if it runs 0, 1, 2, 3, the bands are the 8-bit write.
3. **Float readback at the tone draw.** This is a new one-shot instrument.
   It copies the E8948389 draw's PS t2 (located by
   `flat_ui_layer.cpp toneBloomInput`), its t1 and its RT to staging, and
   writes them raw with format and size. The existing F10 packet capture
   treats only the registry tones (`tonePsHdrSlot`: FEE777, EAA5F1, DE65BF,
   9270C3) as the tone draw, so it may catch the map's 9270C355 but not the
   loading screen's E8948389.
   - Smooth float bloom and 1-code bands in the RT confirm 1.
   - Kinks at a coarse level's texel centres in the float bloom confirm 2.
   - If the instrument never runs, the log must say so, by an
     armed/fired/written line triple.

## Fix proposal

Sean asked for a generic fix (one that does not depend on the scene or
the object); each option below is given in that form.

**A. Dither the tone write (if 1 is confirmed).** This is the leading
candidate, and it is generic over scenes and objects.

- **What EDVR patches.** Swap each known tone PS through `shader_swap.cpp`
  for its own math plus a triangular-PDF dither of ±1 LSB of the 8-bit
  target. Seed the dither from SV_Position and the frame index. Add it
  after the final clamp and before `mov o0`.
- **Hashes, flat:** E8948389387DA083 (loading screens and flight),
  9270C355389DA302 (maps), FEE777E92850B390 and DE65BFFF2F12ECC6 (bloom
  off and DoF), EAA5F18F10533BD1 and 0D4329BDF65DE8F7 (EDHM), and the
  map's C89DD4ED362D743F.
- **What it keeps.** Bloom, its weights and its shape everywhere; the
  change is noise below one code.
- **Risks.**
  - Each swapped PS must reproduce the game's output exactly apart from
    the dither.
  - An unknown variant (a new EDHM build, the next game update) misses
    the dither and stays stock. That is harmless, and the swap log should
    name it.
  - With EDVR's temporal AA on, an animated dither averages out (good). It
    must not move the history or reactive heuristics; the tone output is
    their input.
  - It touches the same tone draw as the crisp HUD re-issue being fixed on
    `fix/hud-tone-bloom`. Land that first.
- **Fully hash-free form:** promote the tone target to R16G16B16A16_FLOAT
  and dither in the output copy to the backbuffer (20F383BB/DED87960, only
  when its target is the swap chain). That is two draw-time substitutions
  keyed by role, but EDVR's AA routes consume that target too. Build A
  first; consider this form only if the variant count keeps growing.

**B. Tent upsample in the chain (only if 2 is confirmed by bytecode or
readback).**

- **What EDVR patches.** Swap `8826CACC6382C78D` (VS `129F602B2A9CA439`)
  for a 3x3 tent upsample with the same constants and energy. One hash runs
  all four up passes in every scene, so the fix is generic by
  construction.
- **What it keeps.** Bloom's weights and extent; only the coarse-level
  lumps go.
- **Risks.** A small change in the halo's look. The swap is inert if a game
  update changes the hash.

**Not proposed:**

- Replacing the whole bloom with EDVR's own: Ultra's internals are
  unknown, so matching its look would be a guess.
- Thresholds, sharpening or contrast changes: they compensate, they do not
  fix.
- Anything for the map's "lower right": that is correct bloom from other
  objects.

**Flight evidence before building:**

- Capture 1 and 2 above. Then 3 if 1 and 2 leave it ambiguous.
- After building A: the same loading screen, lossless, AA off. Expect a
  smooth glow with no bands, a mean within 0.5 code of stock per 64x64
  block, and the swap log naming each variant it took.
