# Game binary map: EliteDangerous64.exe build 332841

## Status

- **Build covered:** 332841 only. FileVersion 332841, ProductVersion 4.4.1.1, file
  103,469,056 bytes, SHA-256
  `E6BE8BBE04E6A7AE226D4318945AF7F367DE13DC5A007A261964D9BA8144E988`. Every address
  below is an RVA (offset from image base 0x140000000) unless marked VA.
- **Purpose:** index only. It says where each game-side function or field is known,
  which doc explains it, and how far it is verified. It does not copy journals.
- **Date:** 2026-10-17. REA 6.5.0 with Ghidra 12.1.4, first pass
  (observed, headless, read-only). Raw output stays local (see "Not in this doc").
- **Observed by REA** (this pass): all 40 checked EDVR code addresses land inside
  analyzed functions. Entry bytes matched what EDVR/docs state for frustum 0x4F4E10,
  velocity lookup 0x3696FA0, flat refresh 0x592200, upload 0x51B640, part test
  0x42B3FC0, light grid 0x58B150 / 0x4F2FF0 / 0x57A460, store 0x4312040, vertex
  flush 0x522A50. 0x4E70B6 calls through IAT 0x4DB32E0. Slot 0x52E8368 points at
  0x2862780; slot 0x52E90B0/0x52E90B8 point at 0x28767D0 / 0x28414E0 (Ghidra made no
  function there). Joint-name and vtable data reads match source (section 2).
- **Only claimed** (docs or source, REA has not looked): every global/field offset
  (light-grid knobs, `0x5EA3399`, fade mode/amount), the camera chain 0x2868C30 ->
  0x28431D0 -> 0x283D4C0 -> 0x2878DC0, coordinator 0x594B60, view loop 0x594D90, handshake
  0x5D6D20, renderer 0x2871D30, the UI sizing chain RVAs, all struct offsets.
- **Not a contradiction, not resolved:** docs say `openvr_api.dll` is delay-loaded
  (architecture.md, openxr-oculus-selection-2026-09-14.md). Ghidra lists the five
  `OPENVR_API.DLL::VR_*` imports as plain EXTERNAL symbols and does not distinguish
  delay from normal. The PE delay-import directory was not read by REA.
- **Observed mismatch worth knowing:** the source pins `SizeOfImage` 104,894,464
  (0x6409000); the 103,469,056 above is the file size. Different quantities, both right.
- **Answered 2026-10-18:** the setter 0x2878DC0 writes camera kind 5 into each
  eye's record (immediate at RVA 0x2878F78); five `cmp` sites read the kind, two
  of them for 5 and one for 3 (section "Camera record kind").
- **Open questions:** (1) what the kind-5 branches in 0x5B1D80 and 0x2871D30
  and the kind-3 branch in 0x4F3770 do, which is what a 5->3 switch changes for
  on-foot native stereo (stereo-cpu-sharing-2026-10-16.md); (2) delay vs normal import for OpenVR; (3) no issue yet tracks
  re-deriving the build-332841 addresses after the next game patch.
- **Next steps:** decompile the kind branches in 0x5B1D80, 0x2871D30 and 0x4F3770;
  create functions at 0x28767D0 / 0x28414E0 and re-check; record per-hook recognisable
  features (strings, callers, imports) in the tables so they can be re-found after a
  patch; add REA checks for the claimed data fields (light-grid knobs, 0x5EA3399, fade mode/amount).

Status key used in the tables:
**V** = verified 332841 (REA bytes). **F** = in function (REA): address lands in an
analyzed function, bytes not compared. **D** = claimed (doc). **S** = claimed (source).
**Vd** = data bytes read by REA and consistent with source.

## Build identity

| Fact | Value | Basis |
|---|---|---|
| Image base | 0x140000000 | observed (REA) |
| Procedures / strings | 143,890 / 62,026 | observed (Ghidra auto-analysis) |
| PE TimeDateStamp | 1788384820 (0x6A989634, 2026-09-02 21:33:40 UTC) | claimed (source), `ui_sizing_math.h:184` etc. |
| SizeOfImage | 104,894,464 (0x6409000), 7 sections | claimed (source), `elite_oculus_profile.h:198` |
| Gate used by | `explorer_cam_core.h:47`, `flat_stationary_camera_capture.cpp:26`, `transition_flash_eye_base.cpp:31`, `object_record_writer_probe.cpp`, `store_hook.cpp`, `kinematic_eval_hook.cpp`, `pad_state_probe.cpp`, `terrain_cull_cover_core.h:169`, `ui_sizing_math.h:184`, `light_grid_cache_core.h:6` | source |
| Imports | D3D11CreateDevice, CreateDXGIFactory1; OPENVR_API.DLL: VR_InitInternal, VR_GetGenericInterface, VR_ShutdownInternal, VR_GetInitToken, VR_IsInterfaceVersionValid. No OpenXR, no d3dcompiler | observed |
| LibOVR | not imported; loaded by name `LibOVRRT%hs_%d.dll` via KERNEL32 LoadLibraryW; `ovr_*` table 0x144E24A50-0x144E25070 (VA) | observed (strings), mechanism in openxr-oculus-selection-2026-09-14.md |
| OpenVR interfaces | IVRSystem_012, IVRCompositor_014, IVRChaperone_003, IVROverlay_011, IVRExtendedDisplay_001 | observed (strings) |
| VR strings (VA) | HMDRenderTargetMultiplier 0x1452EB9B0, StereoscopicMode 0x1452EBA08, IPDAmount 0x1452EBA20, CycleRenderScale 0x145386A78, `f3dDevice::PrePresent` 0x144E2E0F8; also StereoFocalDistance, HMDService/IHMDService, HMDHiddenAreaMesh, StereoExtraSwapChain, Stereoscopic_FrameCombineNode | observed (strings) |

Each hook adds its own pin on top of the PE pair: byte prologues (engine_velocity.cpp
lookup + append; skin_entity_hook.cpp 28 bytes; flush-IA 16 bytes
`48 89 5C 24 18 56 57 41 56 48 83 EC 30 49 8B F8`; pad readers 6 bytes `40 53 48 83
EC 20`; light grid three 16-byte pins; terrain head plus call site; Oculus profile
FNV fingerprints and call-chain checks, `elite_oculus_profile.h:228-458`). REA read
at 0x592200: `41 57 48 81 EC E0 00 00 00 4D 8B F8 4C 8B EA 4C`.

## Map by system

Columns: RVA, name, what it is, EDVR source, explaining doc, status. Docs live in
`docs/`. `+` after a function means the hook address is inside the function (REA
reports the containing function start).

### Loader / IAT (Oculus selection)

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x4DB32E0 | kLoadLibraryIatRva | KERNEL32 LoadLibraryW IAT slot | elite_oculus_profile.h:46 | openxr-oculus-selection-2026-09-14 | V |
| 0x4E70B6 | kLoaderCallRva | `call [IAT]` in SDK shim FUN_1404E6D40 | :47 | same | V |
| 0x4E70BC | kLoaderReturnRva | return point after that call | :48 | same | S |

### Cameras, free camera (Explorer Cam), avatar, joints

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x1071980 | kFreeCameraRva | FreeCameraActivity per-frame update | explorer_cam_core.h:38 | design-explorer-cam-free-camera-2026-10-07 | F |
| 0x1091140 | kCollisionRva | camera sweep/ray collision step | :39 | same | F |
| 0x108F1B0 | kBoxPushRva | pushes point out of commander box | :40 | same | F |
| 0x47C7640 | kCameraUiRva | VanityCameraUIActivity update | :41 | same | F |
| 0x2DF14C0 | kControllerRva | VesselCameraMountControl update (vtable only) | :42 | same | F |
| 0x3DD6040 | kAvatarFadeRva | AvatarModelComponent dither fade (head hiding) | :43 | same | F |
| 0xFDDB10 | kFindJointRva | skeleton `FindJoint(name)` | :93 | same | F |
| 0x1078990 | kZoomDofRva | VanityCameraDofAndZoomControls update | :101 | same | F |
| 0x19B12D5, 0x19B1359 | kFindSite1/2Rva | two call sites in FUN_1419B1240 | :1529 | same | F |
| 0x5E9DC28 | kFadeModeRva | int in .data, dither mode (-1 auto) | :44 | same | S |
| 0x601E088 | kFadeAmountRva | float in .data | :46 | same | S |
| 0x5E9C7D0 | kPartNameTableRva | part-name pointer table | :1491 | same | Vd |
| 0x51F9930 | kPovNameRva | string `def_c_povCamera_joint` | :1529 | same | Vd |
| 0x554EE10, 0x5564118, 0x5564130 | kHead/FootL/FootRNameRva | strings `def_c_head_joint`, `def_l_foot_joint`, `def_r_foot_joint` | explorer_cam_follow_core.h:36 | same | Vd |
| 0x559CF90, 0x517DC20 | kRrVtableRva, kAoVtableRva | RuntimeRigComponent / AvatarObject vtables | :30 | same | Vd |

### VR camera job, flat camera, transition flash

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x2868C30 | per-frame VR job | calls driver, then 0x594B60 | none | design-transition-flash-engine-fix-2026-09-23, canted-projection | D |
| 0x28431D0 | kConsumerRva | camera driver (4x4 mailbox copy) | transition_flash_eye_base.cpp:42 | design-transition-flash-engine-fix-2026-09-23 | F |
| 0x283D4C0 | composer | builds both eyes, calls the setter for eye 0 then eye 1 (see "Camera record kind") | none | canted-projection:407 | V (decompiled) |
| 0x2878DC0 | camera setter | builds one eye's camera record, writes kind 5 (see "Camera record kind") | none | canted-projection, terrain-culling:206, stereo-cpu-sharing | V (decompiled) |
| 0x594B60 | coordinator | uploads headset camera block (sole caller 0x286906E) | none | stereo-cpu-sharing, design-flat-camera-integration | D |
| 0x594D90 | view loop | per-view render update | none | design-flat-camera-integration:699 | D |
| 0x5921F0+ | kRefreshRva (0x592200) | flat camera refresh | flat_camera_inject.cpp:99 | design-flat-camera-integration, design-flat-temporal-aa-2026-09-23 | V |
| 0x51B640 | kUploadRva | camera block upload | flat_camera_producer_probe.cpp:49 | design-flat-temporal-aa-2026-09-23 | V |
| 0x2871D30 | kRendererRva | builds mono camera (kind 0/3) | flat_stationary_camera_capture.cpp:24 | terrain-culling:818 | S |
| 0x5D6D20 | producer handshake | render thread waits here | none | stereo-cpu-sharing:60 | D |

### Camera record kind (the "setter 5->3" arc)

Observed 2026-10-18 by REA (Ghidra decompile, assembly, byte reads and a
`.text`-wide byte search). Interpretation of what each kind means is inferred.

- **Composer 0x283D4C0.** Holds the HMD service at `this+0x7D0`. Per eye (0, 1)
  it calls vtable `+0xB0` (eye-to-head pose, takes an extra argument) and
  `+0xB8` (projection, takes near/far), multiplies each eye pose by the base
  matrix, then calls the setter for eye 0 and eye 1. If byte `this+0x660` is
  set it skips the HMD calls and reuses eye poses and projections cached at
  `this+0x670..0x768` (written when its 7th argument is non-zero).
- **Setter 0x2878DC0** (sole caller: the composer). Calls HMD vtable `+0x90`
  (per-eye pair of values; stored at record +0x240 and +0x260), orthonormalises
  the pose basis, copies the projection, then copies the record out with
  0x59F1A0 (a plain camera-record copy). Record layout seen here: basis rows
  +0x00/+0x10/+0x20, position +0x30, flags +0x230 (`|= 0xF`), two floats
  +0x234/+0x238 (stack args 5 and 6), **kind +0x244**, byte +0x248,
  projection 4x4 at +0x2B0.
- **The kind write:** RVA 0x2878F71, bytes `C7 84 24 74 02 00 00 05 00 00 00`
  (`mov dword [rsp+0x274], 5`; record base is rsp+0x30). The immediate is at
  RVA 0x2878F78. A 5->3 patch changes that byte.
- **Readers of +0x244** found by searching for `cmp dword [reg+0x244], imm8`
  (this misses other instruction forms such as loads or switch tables):

| Site (RVA) | In function | Compares kind to |
|---|---|---|
| 0x2042B7A | 0x2042B70 | 0 |
| 0x4F37C6 | 0x4F3770 | 2 |
| 0x4F3AE0 | 0x4F3770 | 3 |
| 0x5B1DD4 | 0x5B1D80 | 5 |
| 0x2871D9A | 0x2871D30 (kRendererRva, mono camera) | 5 |

So switching the eye cameras from 5 to 3 (inferred) takes them out of the
kind-5 branches in 0x5B1D80 and 0x2871D30 and into the kind-3 branch in
0x4F3770, which sits beside the view-row refresh 0x4F2FF0. What those branches
do is the next thing to decompile.

### Frustum, visibility, motion / velocity, skinning

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x4F4E10 | kFrustumRva | frustum test `(view, float4, float2)` | cull_gate_probe.h:118 | design-occlusion-culling-2026-09-22 | V |
| 0x5EA3399 | kVisGlobalRva | visibility global byte | :119 | same | S |
| 0x42B3FC0 | kPartTestRva | per-part frustum+LOD test, sets view mask | kinematic_eval_hook.cpp:311 | design-occlusion-culling-2026-09-22 | V |
| 0x42B4B96 | kPartCallReturnRva | return inside draw-item builder | :312 | same | F |
| 0x42B4420 | kBucketBuildRva | draw-item builder (view loop) | kinematic_eval_probe.h:38 | engine-render-performance-2026-09-19 | F |
| 0x430EFE0 | kEvalRva | kinematic eval | :27 | settlement-flicker-2026-09-17 | F |
| 0x431AFE0 | kRigEvalRva | rig eval | :33 | same | F |
| 0x3696FA0 | kLookupRva | velocity lookup | engine_velocity.cpp:588 | kinematic-motion-injection-2026-09-19 | V |
| 0x4313185 | kAppendRva | append site inside FUN_144312E00 | :592 | same | F |
| 0x4312040 | kStoreRva | store hook | store_hook.cpp:26 | design-occlusion-culling-2026-09-22, settlement-flicker-2026-09-17 | V |
| 0x431B200 | kOwnershipOpcodeWindowRva | ownership opcode window | object_record_writer_probe.cpp:16 | kinematic-motion-injection-2026-09-19 | S |
| 0x4C540E0 | kSkinJobsRva | skinning job assembly | skin_entity_hook.cpp:22 | kinematic-motion-injection-2026-09-19:4059 | F |

### Light grid (fragment-culling clusters)

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x58B150 | kBuilderRva | cluster builder (caller 0x590368) | light_grid_cache_core.h:51 | light-grid-cache-2026-10-13 | V |
| 0x4F2FF0 | kRefreshRva | view-row refresh | :75 | same | V |
| 0x57A460 | kCopyRva | view-block field copy | :84 | same | V |
| 0x5E70F7E, 0x5E70F80, 0x5E70F90 | override byte, tile px, slices | globals the builder reads | :66-68 | same | S |
| 0x5E70F94, 0x5E70F98 | near, far floats | override near/far | :83 | same | S |
| 0x5F27612 | kForceRebuildRva | byte; set disables game memo | :69 | same | S |

### Terrain cull

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x443E800 | kTargetRva | cull-plane builder (widened by terrain cull cover) | terrain_cull_cover_core.h:42 | terrain-culling | F |
| 0x4456007 | kCallRva | the one call to it | :43 | same | F |
| 0x4455FDD | kSiteRva | call-site signature start | :44 | same | F |
| 0x4455A40 | per-view record writer | writes one record per view (called from 0x4462E80 at 0x4463A83) | :6 comment | terrain-culling:13 | F |

### UI supersampling, window, SSAA hold

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x52E90B0 / 0x52E90B8 | kUiSsSetter/GetterSlotRva | vtable slots (vtable 0x52E9020) | ui_sizing_math.h:653 | ui-layer-2026-09-23, vr-supersampling-gate-2026-10-10 | V |
| 0x28767D0 / 0x28414E0 | kUiSsSetter/GetterRva | Supersampling setter / getter | :654 | same | V (slot targets; no Ghidra function) |
| 0x52E8368 | kLoaderSlotRva | fxcfg loader slot | vr_ssaa_hold.cpp:30 | vr-supersampling-gate-2026-10-10, design-unified-install-2026-10-12 | V |
| 0x2862780 | kWrapperRva | 19-byte wrapper, slot target | :31 | same | V |
| 0x2855A50 | kLoaderRva | stores LODDistanceScale; reads Settings | :32 | design-settlement-lod-bias-2026-09-22:132 | F |
| 0x7E9D50 | kRequestRva | window-mode request | vr_window_trampolines.cpp:28 | design-unified-install-2026-10-12:119 | F |
| 0x281C0D0 | kCopyRva | settings copy | :35 | same:120 | F |
| UI size chain | 0x4551C7D, 0x45517F6, 0x4570902, 0x456DA45, 0x28148A9, 0x2814686, 0x30FC1A, 0x312450 | stack-return RVAs the verdict reads | ui_sizing_math.h:56-59 | ui-layer-2026-09-23 | S |
| UI panel | 0x45706FA, 0x4571205 (sites); 0x4570500, 0x4571180 (funcs); 0x52F2D84, 0x4DDE660, 0x4DDE664 (aspect, 1080, 1920) | panel scale | :185-193 | same | S |

### Vertex flush, docking-state readers

| RVA | Name | What | Source | Doc | St |
|---|---|---|---|---|---|
| 0x522A50 | kFlushIaRva | FlushIA (CodeHook zeroes applied buffer) | vertex_resync_core.h:44 | scanner-body | V |
| 0x12C3C20, 0x12CAF80, 0x12CADE0, 0x12CB320 | pad-state readers | docking-state readers (this = rcx) | pad_state_probe.cpp:1 | optimized-station-2026-10-12:79 | F |

### Swap chain / present

No game addresses: EDVR hooks the D3D11 device/swap chain vtables (`device_hook.cpp`,
hookedPresent). Game feature for re-finding: the `f3dDevice::PrePresent` string
(0x144E2E0F8 VA) and imports D3D11CreateDevice / CreateDXGIFactory1.

## Re-deriving after a patch

1. Open the new exe once in REA (`open_binary`) and keep the session. Cold import took
   2,342 s, Java peak about 2.47 GB; set `GHIDRA_HEADLESS_MAXMEM=16G` (`list_names`
   ran out of memory at the 2 GB default). Do not reopen per query.
2. Check identity first: new PE stamp and SizeOfImage mean every gate refuses, which is
   safe. Update the gates only after the addresses are re-derived.
3. For each row, find the function by its recognisable feature, then `procedure_info`
   / `resolve_containing_procedure`: strings (`search_strings`: joint names,
   `HMDRenderTargetMultiplier`, `LODDistanceScale`, `Supersampling`, `LibOVRRT`),
   `xrefs` to those strings, `procedure_callers` / `procedure_callees` from a known
   anchor (e.g. 0x4455A40 -> 0x443E800 has one call; 0x594B60 has one caller;
   0x4462E80 calls 0x4455A40), and vtable slot reads (`read_bytes` at the slot).
4. Compare entry bytes (`read_bytes`) against the gates in section "Build identity";
   a mid-function hook (0x592200, 0x4313185) needs the containing function first.
5. Verify each re-found function against its explaining doc before editing a constant.

Measured cost (observed, first pass): cold import 2,342 s; warm queries were cheap
(about 10 KB of JSON per `resolve_containing_procedure` or `read_bytes`).

## Not in this doc

Raw REA output and decompiled game code stay local, outside the repo, with the
REA snapshot of the pass that produced them. The repo is public: record only
addresses, signatures and short descriptions here.
