# Elite's bloom: blotchy, off-centre halos (2026-10-20)

## Status

- **State (2026-10-20 21:20): the stripes are confirmed as undithered
  8-bit banding (lossless shots); nothing built.** Base Elite with EDVR's AA off: the flat System Map and loading
  screens, on builds 332841 and 334473. The chain was dumped on the 20:49
  flight (log `edvr_gfx_20261020_204909`) with commit `4675c3c5`, which
  lets flat read `advanced.glare_shader_dump`. See "The chain's bytecode".
- **The halo is not offset.** The star's halo sits within 1 px of its
  disc. The map's "lower right" is a separate cyan-white glow from the
  station marker, the belt and the planets, centred on them.
- **The chain is a standard pyramid.** It has a Karis-weighted 13-tap first
  downsample with a soft threshold and a luma clamp, 13-tap downsamples,
  and upsamples that average two 3x3 tents (`8826CACC`). Every UV is the
  vertex UV plus a CPU offset from cb2.
- **4.4.2.0 did not introduce it.** A 332841 loading screen shows the same
  banded glow, and the named bloom shaders have the same hashes on both
  builds.
- **Open hypotheses, ranked:**
  1. **Confirmed:** output quantization. The tone pass writes 8-bit with no
     dither, so a wide, dim glow on black falls in clean one-code bands,
     up to 99 px wide at codes 1 and 2 (see "Lossless loading screens").
  2. Tent offsets wider than a texel. If Ultra's `FilterRadius 3` and
     `FilterRadiusWide 4` scale the tents' cb2 offsets, each up pass adds
     nine copies of the coarser level up to 3-4 texels apart (150-200
     output px at 39x24), so far halos turn lumpy and squarish. Weak
     support: the far star halo is 5-20% brighter on the diagonals than on
     the axes beyond 90 px. Confirm with a Bloom A/B (below) or with the
     upsample draws' cb2[3..12].
- **Ruled out:**
  - Half-texel offset accumulating toward a corner: the halo fits at
    (+0.5, +0.5) px.
  - Lumps from a single-tap bilinear upsample: `8826CACC` is an 18-tap
    dual tent.
  - Odd-size rounding as a scale about the corner: no chain shader uses
    SV_Position or a size, and the UVs span each target whatever its size.
  - Lens ghost or flare: the lobe has the marker's colour and centre.
  - R11G11B10 precision as the band source: under half a code below
    code 60.
  - Temporal threshold instability: the artifact is in still frames.
  - The 4.4.2.0 update: see above.
- **Next:** which Bloom tier the 21:15:15 and 21:16:01 shots used (only
  21:16:39 is known, Ultra), to judge 2. At 8-10x gain no texel-grid lumps
  show in any of the three.
- **Fix (gated on that capture):**
  - for 1, dither the tone write by swapping the known tone pixel shaders;
  - for 2, swap `8826CACC` for a dense tent of the same footprint.
  Bloom stays everywhere. See "Fix proposal".
- **Environment:** flat, 1920x1200 output, SSAA 0.65 (HDR 1248x780,
  first bloom level 624x390), BloomQuality 3 (Ultra), DoF 2, Elite AA off.
  The 332841 loading screen ran at HDR 1920x1200. Not tested: VR, where
  Elite's VR presets ship BloomQuality 0.

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

The XeSS image (1.jpg, bloom doubled by the tone re-issue bug) fits (0, 0).

Blotchiness in the clean sectors is the residual's robust SD against the
star's excess:

| Distance from limb (px) | 5-30 | 30-80 | 80-160 |
|---|---|---|---|
| Residual SD / excess | 7% | 9% | 15% |

That is 1 to 2 output codes, which is the JPEG's noise level.

**Loading screens (orange hologram on black).** The glow's reach left and
right of the figure is equal within 18 px, with no consistent sign, at
R+G thresholds 60 down to 4, on both builds. The
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
| 2 | Tent taps 3-4 texels apart (FilterRadius) leave lumpy, squarish far halos | Far halo 5-20% brighter on the diagonals; the tent offsets are CPU constants | open |
| 2a | Bilinear single-tap upsample of coarse levels leaves lumps | `8826CACC` is an 18-tap dual tent | ruled out (bytecode) |
| 3 | Odd-size floor as a scale about the corner | No chain shader uses SV_Position or a size | ruled out (bytecode) |
| 4 | Half-texel offset per level, accumulating | Halo at (+0.5, +0.5) px of its disc | ruled out |
| 5 | Lens ghost or flare | Lobe has the marker's colour and centre; no mirror image | ruled out |
| 6 | R11G11B10 banding | 1.6% relative step is at most 0.5 code under code 60 | ruled out as the band source |
| 7 | Jittered or temporally unstable threshold | Still frames show it | ruled out as the cause seen |
| 8 | Other emitters' bloom read as the star's | Colour and centroid of the lobe | confirmed (the map's "lower right") |

## The capture

One session, AA off (F7), on a loading screen and on the System Map:

1. **Shader dump: done.** The 20:19 flat flight dumped nothing, because
   flat read only the keys in `flat_profile_keys.h`. Commit `4675c3c5`
   added `advanced.glare_shader_dump` to that list, and the 20:49 flight
   dumped all 691 shaders (see "The chain's bytecode").
2. **Bloom A/B.** One loading screen at Bloom Medium, High and Ultra, AA
   off. This tells 1 from 2 with no new code.
3. **The upsample's constants**, only if 2 needs a number: cb2[3..12] of
   the `129F602B` draws, with their target sizes, give the tap spacing in
   texels. `advanced.census_cb_watch` can dump them, but flat does not
   read the census keys yet.
4. **Lossless screenshots.** Use Steam's "save an uncompressed copy". If
   the dark end still runs 0, 2, 4, something upstream of the 8-bit write
   quantizes; if it runs 0, 1, 2, 3, the bands are the 8-bit write.
5. **Float readback at the tone draw.** This is a new one-shot instrument.
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
  first; this form only if the variants keep multiplying.

**B. Dense tent upsample (only if 2 is confirmed).**

- **What EDVR patches.** Swap `8826CACC6382C78D` (VS `129F602B2A9CA439`)
  for a tent with the same footprint and weight envelope, read from the
  same cb2 offsets, but with taps no more than one texel apart. One hash
  runs all four up passes in every scene, so the fix is generic by
  construction.
- **What it keeps.** Bloom's weights and extent; only the sparse-tap
  lumps go.
- **Risks.** A small change in the halo's look. The swap is inert if a game
  update changes the hash.

**Not proposed:** EDVR's own replacement bloom (Ultra's internals are
unknown, so its look would be a guess); threshold, sharpening or contrast
changes (compensation); anything for the map's "lower right", which is
correct bloom from other objects.

**Flight evidence before building:**

- Capture 2 and 4 above. Then 3 and 5 if they leave it ambiguous.
- After building A: the same loading screen, lossless, AA off. Expect a
  smooth glow with no bands, a mean within 0.5 code of stock per 64x64
  block, and the swap log naming each variant it took.

## The chain's bytecode (2026-10-20, 20:49 flight)

The dump comes from flight `edvr_gfx_20261020_204909`: build `4675c3c5`,
game 334473, BloomQuality 3. It disassembles with `tools/dxbc_disasm.py`
under Wine, using the SDK's `d3dcompiler_47.dll`.

- `DFED8E1C9E191BEC` (VS) passes the vertex UV plus cb2[3..9] as 13 tap
  positions.
- `143AAE0597E2F7BF` is the first downsample: 13 taps in five 2x2 boxes,
  each tap weighted 1/(1 + luma), each box normalised. The four outer boxes
  weigh 1/8 each and the inner one 1/2. It then applies:
  - exposure (cb2[1].x times cb2[15].w);
  - a luma clamp at cb2[16].y;
  - a soft threshold, saturate((luma - cb2[0].x) * cb2[0].y) squared;
  - NaN and Inf rejection;
  - a final scale, cb2[16].x.
- `5ECFE7085464069B` is the same 13-tap pattern without the luma weights,
  for the remaining downsamples.
- `129F602B2A9CA439` (VS) passes the vertex UV plus cb2[3..12] as 18 tap
  positions.
- `8826CACC6382C78D` averages two 3x3 tents (weights 1/16, 2/16, 4/16) over
  one texture at two offset sets, and writes alpha 1. The add of the finer
  level is therefore blend state, which the dump does not show.
- `FA01CD495BB27AEF` (the map's composite) is one tap scaled by
  cb2[0].x.

No chain shader reads SV_Position or a texture size, and none adds a fixed
half-texel. Any offset error would have to be in the CPU-side cb2 values;
the measured centre offset bounds it to under a pixel at screen centre.
The two offset sets and Ultra's `FilterRadius 3` / `FilterRadiusWide 4`
match one-for-one, which is what hypothesis 2 rests on.

## Lossless loading screens (2026-10-20, 21:15-21:17)

Build `98ba10fa` (rebased on `aed71854`), AA off, SSAA 1.0 from launch
(HDR 1920x1200; levels 960x600 down to 60x37). There were three Steam
uncompressed copies, each taken right after a reload from the main menu
(journal `LoadGame` 03:15:13, 03:15:57, 03:16:35 UTC). Elite wrote
`Custom.4.4.fxcfg` once, at 21:16:20 (BloomQuality 3), so only the last
shot's tier is known.

- **The output is not dithered.** Every R code from 0 to 20 occurs, so the
  JPEGs' even-only plateaus (0, 2, 4) were JPEG DC quantization. 76-88% of
  R steps and 90-98% of G steps in the tails are exactly one code; larger
  steps sit on lines and hull texture. The tail rows are monotone runs with
  no back-and-forth flips. Right of the nose in the Ultra shot, codes 5 to
  1 each hold 8-10 px; above the ship in the 21:15:15 shot, code 1 holds
  99 px. That is the striping, and fix A addresses exactly that.
- **No texel-grid lumps.** At 8-10x gain the outer contours are one-code
  iso-lines stepping on the pixel grid, not 16 px or 32 px blocks. The
  glow's shape follows the ship.
- **Reach differs by shot.** Right of the nose, the glow reaches code 0 at
  64 px in the 21:16:01 shot and at 147 px in the Ultra shot.
