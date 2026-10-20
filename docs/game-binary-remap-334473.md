# EDVR address remap: build 332841 -> 334473

## Status

- **Result:** all 173 EDVR addresses re-found in build 334473: 75 exact (identical 64 entry bytes or 16-byte value), 98 moved+changed (unique masked match plus confirming structure), 0 ambiguous, 0 not found.
- **Already shipped:** main retargeted to 334473 in `869fd706` (gates, pins, fingerprints; flown, see `docs\build-334473.md`). This table is an independent cross-check and the doc-only addresses main does not pin. New PE TimeDateStamp 1791840573 (0x6ACD513D), SizeOfImage 104,906,752 (0x640C000).
- **Agrees with that retarget:** all 152 rows that `src\` pins appear among main's 334473 constants, 60 of them matched by constant name. The other 21 rows (camera chain, kind readers, draw-list sites, the PrePresent string) are doc-only, with no `src\` counterpart to compare. Two independent methods (`tools\build_diff.py map` and the masked matching here), no disagreement.
- **Spot-checked independently in REA (2026-10-20):** FlushIA 0x522BB0 matches the 16-byte pin in vertex_resync_core.h; the camera-kind write is still the single `C7 84 24 74 02 00 00 05 00 00 00` in .text, at 0x287B5A1 in setter 0x287B3F0, whose sole caller is composer 0x283FAE0; PE stamp and SizeOfImage re-read from the file header.
- **Not verified here:** no flight from this table; the 21 doc-only rows rest on this matching alone.
- **Index:** game-binary-map.md (build 332841) explains what each row is. Next update: game-binary-map.md "Re-deriving after a patch".

## Builds and addresses

Generated 2026-10-20 by read-only static analysis (REA MCP, Ghidra 12.1.4, plus byte matching on the on-disk image). RVAs; VA = 0x140000000 + RVA.

| | Old (332841) | New (334473) |
|---|---|---|
| SHA-256 | e6be8bbe04e6a7ae226d4318945af7f367de13dc5a007a261964d9ba8144e988 | 6891f70a021fa9f3a0e00458c284f48d1226e68ae7fc02ad48081aed47857e93 |
| PE TimeDateStamp | 1788384820 (0x6A989634, 2026-09-02 21:33:40 UTC) | 1791840573 (0x6ACD513D, 2026-10-12 21:29:33 UTC) |
| SizeOfImage | 104,894,464 (0x6409000) | 104,906,752 (0x640C000) |
| ProductVersion / file size | (n/a) | 4.4.2.0 / 103,482,368 bytes |

Old values come from the build-332841 REA session (evidence digest e6be8bbe...). New values come from byte-pattern matching against the new image, with rel32 call/jump targets and RIP-relative displacements wildcarded; data was found through the matched instructions that reference it. Hard cases were confirmed in REA on the new session (digest 6891f70a...). Raw evidence and the matching scripts stay local.

Status: exact = identical 64 entry bytes (code) or identical 16-byte value (data) at a new address; moved+changed = unique masked match with confirming structure; ambiguous / not found as named. Counts: moved+changed 98, exact 75 (total 173).

Camera record kind: the kind write (C7 84 24 74 02 00 00 05 00 00 00) is still a single site, inside the new setter, and still writes 5. The five `cmp [reg+0x244]` readers keep their immediates (0, 2, 3, 5, 5).

| Name | Old RVA | New RVA | Status | How matched | EDVR source |
|---|---|---|---|---|---|
| kLoadLibraryIatRva | 0x4DB32E0 | 0x4DB62E0 | moved+changed | import table KERNEL32!LoadLibraryW; loader call FF15 targets 0x4DB62E0; call operand agrees | elite_oculus_profile.h:46 |
| kLoaderCallRva | 0x4E70B6 | 0x4E71B6 | moved+changed | masked unique match (bytes 0..59); same offset 0x376 in new fn 0x4E6E40 | elite_oculus_profile.h:47 |
| kLoaderReturnRva | 0x4E70BC | 0x4E71BC | moved+changed | masked unique match (bytes 0..59); same offset 0x37C in new fn 0x4E6E40 | elite_oculus_profile.h:48 |
| oculus fp 0x8D52A0 (selector) | 0x8D52A0 | 0x8D5400 | moved+changed | masked unique match (bytes 0..64) | elite_oculus_profile.h:401 |
| oculus fp 0x4E4AA0 | 0x4E4AA0 | 0x4E4BA0 | exact | identical 64 bytes | elite_oculus_profile.h:402 |
| oculus fp 0x4E4630 | 0x4E4630 | 0x4E4730 | moved+changed | masked unique match (bytes 0..64) | elite_oculus_profile.h:403 |
| oculus fp 0x4E4870 | 0x4E4870 | 0x4E4970 | moved+changed | masked unique match (bytes 0..62) | elite_oculus_profile.h:404 |
| oculus fp 0x4E8530 | 0x4E8530 | 0x4E8630 | moved+changed | masked unique match (bytes 0..62) | elite_oculus_profile.h:405 |
| oculus fp 0x4E70D0 | 0x4E70D0 | 0x4E71D0 | moved+changed | masked unique match (bytes 0..62) | elite_oculus_profile.h:406 |
| oculus fp 0x4E6D40 (SDK shim) | 0x4E6D40 | 0x4E6E40 | moved+changed | masked unique match (bytes 0..61) | elite_oculus_profile.h:407 |
| oculus fp 0x4E0E60 | 0x4E0E60 | 0x4E0F60 | moved+changed | masked unique match (bytes 0..60) | elite_oculus_profile.h:408 |
| oculus fp 0x4E8230 | 0x4E8230 | 0x4E8330 | moved+changed | masked unique match (bytes 0..64) | elite_oculus_profile.h:409 |
| oculus fp 0x4E5CA0 | 0x4E5CA0 | 0x4E5DA0 | moved+changed | masked unique match (bytes 0..61) | elite_oculus_profile.h:410 |
| oculus fp 0x4E86B0 | 0x4E86B0 | 0x4E87B0 | moved+changed | masked unique match (bytes 0..64) | elite_oculus_profile.h:411 |
| oculus bytes selectorKinds | 0x8D530B | 0x8D546B | moved+changed | masked unique match (bytes 0..64); same offset 0x6B in new fn 0x8D5400 | elite_oculus_profile.h:434 |
| oculus bytes selectorKind2 | 0x8D5318 | 0x8D5478 | moved+changed | masked unique match (bytes 0..63); same offset 0x78 in new fn 0x8D5400 | elite_oculus_profile.h:435 |
| oculus bytes selectorKind3 | 0x8D5320 | 0x8D5480 | moved+changed | masked unique match (bytes 0..60); same offset 0x80 in new fn 0x8D5400 | elite_oculus_profile.h:436 |
| oculus bytes exitsOnSuccess | 0x8D5328 | 0x8D5488 | moved+changed | masked unique match (bytes 0..60); same offset 0x88 in new fn 0x8D5400 | elite_oculus_profile.h:437 |
| oculus bytes advances | 0x8D533C | 0x8D549C | moved+changed | masked unique match (bytes 0..63); same offset 0x9C in new fn 0x8D5400 | elite_oculus_profile.h:438 |
| oculus bytes virtualInit | 0x4E4D50 | 0x4E4E50 | exact | identical 64 bytes; same offset 0x2B0 in new fn 0x4E4BA0 | elite_oculus_profile.h:439 |
| oculus bytes initFailure | 0x4E467C | 0x4E477C | exact | identical 64 bytes; same offset 0x4C in new fn 0x4E4730 | elite_oculus_profile.h:440 |
| oculus bytes falseReturn | 0x4E46B1 | 0x4E47B1 | exact | identical 64 bytes; same offset 0x81 in new fn 0x4E4730 | elite_oculus_profile.h:441 |
| oculus bytes nullModule | 0x4E710E | 0x4E720E | exact | identical 64 bytes; same offset 0x3E in new fn 0x4E71D0 | elite_oculus_profile.h:442 |
| oculus call 0x8D5337->0x4E4AA0 | 0x8D5337 | 0x8D5497 | moved+changed | masked unique match (bytes 0..63); same offset 0x97 in new fn 0x8D5400 | elite_oculus_profile.h:443 |
| oculus call 0x4E4666->0x4E8530 | 0x4E4666 | 0x4E4766 | exact | identical 64 bytes; same offset 0x36 in new fn 0x4E4730 | elite_oculus_profile.h:444 |
| oculus call 0x4E860F->0x4E70D0 | 0x4E860F | 0x4E870F | moved+changed | masked unique match (bytes 0..64); same offset 0xDF in new fn 0x4E8630 | elite_oculus_profile.h:445 |
| oculus call 0x4E7102->0x4E6D40 | 0x4E7102 | 0x4E7202 | moved+changed | masked unique match (bytes 0..30); same offset 0x32 in new fn 0x4E71D0 | elite_oculus_profile.h:446 |
| oculus call 0x4E5CF3->0x4E86B0 | 0x4E5CF3 | 0x4E5DF3 | exact | identical 64 bytes; same offset 0x53 in new fn 0x4E5DA0 | elite_oculus_profile.h:447 |
| oculus reloc ptr ->0x4E4630 | 0x4E243F8 | 0x4E27438 | moved+changed | only .rdata pointer to new 0x4E4730 | elite_oculus_profile.h:448 |
| oculus reloc ptr ->0x4E5CA0 | 0x4E24400 | 0x4E27440 | moved+changed | only .rdata pointer to new 0x4E5DA0 | elite_oculus_profile.h:449 |
| oculus reloc ptr ->0x4E4870 | 0x4E245C0 | 0x4E27600 | moved+changed | only .rdata pointer to new 0x4E4970 | elite_oculus_profile.h:450 |
| kFreeCameraRva | 0x1071980 | 0x1072A80 | moved+changed | masked unique match (bytes 0..63) | explorer_cam_core.h:38 |
| kCollisionRva | 0x1091140 | 0x1092240 | moved+changed | masked unique match (bytes 0..59) | explorer_cam_core.h:39 |
| kBoxPushRva | 0x108F1B0 | 0x10902B0 | moved+changed | masked unique match (bytes 0..60) | explorer_cam_core.h:40 |
| boxPush call site (return 0x10728B6) | 0x10728B6 | 0x10739B6 | exact | identical 64 bytes; same offset 0xF36 in new fn 0x1072A80 | explorer_cam_core.h:40 (comment) |
| kCameraUiRva | 0x47C7640 | 0x47CA180 | exact | identical 64 bytes | explorer_cam_core.h:41 |
| kControllerRva | 0x2DF14C0 | 0x2DF3F60 | moved+changed | masked unique match (bytes 0..64) | explorer_cam_core.h:42 |
| kAvatarFadeRva | 0x3DD6040 | 0x3DD8D00 | moved+changed | masked unique match (bytes 0..60) | explorer_cam_core.h:43 |
| kFadeModeRva | 0x5E9DC28 | 0x5EA0C78 | moved+changed | RIP target of 1 matched ref insn(s) | explorer_cam_core.h:44 |
| kFadeAmountRva | 0x601E088 | 0x60211F8 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | explorer_cam_core.h:46 |
| kFindJointRva | 0xFDDB10 | 0xFDEC10 | moved+changed | masked unique match (bytes 0..62) | explorer_cam_core.h:93 |
| kZoomDofRva | 0x1078990 | 0x1079A90 | exact | identical 64 bytes | explorer_cam_core.h:101 |
| kPartNameTableRva | 0x5E9C7D0 | 0x5E9F820 | moved+changed | .data shift +0x3050 (as fade mode/vis global); REA: all 20 xrefs = old xrefs +0x2CC0; entries point to "Head","Body" | explorer_cam_core.h:1491 |
| FUN_1419B1240 (find-site host) | 0x19B1240 | 0x19B4000 | moved+changed | masked unique match (bytes 0..60) | explorer_cam_core.h:1529 (containing) |
| kFindSite1Rva | 0x19B12D5 | 0x19B4095 | moved+changed | masked unique match (masked bytes 0..62 match 2; disambiguated by offset in matched containing fn); same offset undefined in new fn 0x19B4000 | explorer_cam_core.h:1529 |
| kFindSite2Rva | 0x19B1359 | 0x19B4119 | moved+changed | masked unique match (bytes 0..64); same offset 0x119 in new fn 0x19B4000 | explorer_cam_core.h:1529 |
| kPovNameRva | 0x51F9930 | 0x51FCB00 | exact | unique string "def_c_povCamera_joint" in .rdata; matched refs agree; same 16-byte value | explorer_cam_core.h:1529 |
| kRrVtableRva | 0x559CF90 | 0x55A01B0 | moved+changed | FindJoint slot +0x30; 3rd of 3 .rdata slots (same order as old); REA xrefs from 0x43E2C59.. (old 0x43DFED9.. +0x2D80) | explorer_cam_follow_core.h:30 |
| kAoVtableRva | 0x517DC20 | 0x5180D40 | moved+changed | FindJoint slot +0x30; 1st of 3 .rdata slots (same order as old); REA xrefs from 0xFDE709.. (old 0xFDD609.. +0x1100) | explorer_cam_follow_core.h:30 |
| kHeadNameRva | 0x554EE10 | 0x5552030 | exact | unique string "def_c_head_joint" in .rdata; same 16-byte value | explorer_cam_follow_core.h:36 |
| kFootLNameRva | 0x5564118 | 0x5567338 | exact | unique string "def_l_foot_joint" in .rdata; same 16-byte value | explorer_cam_follow_core.h:36 |
| kFootRNameRva | 0x5564130 | 0x5567350 | exact | unique string "def_r_foot_joint" in .rdata; same 16-byte value | explorer_cam_follow_core.h:36 |
| per-frame VR job | 0x2868C30 | 0x286B250 | moved+changed | masked unique match (bytes 0..64) | doc only |
| VR job -> coordinator call (0x286906E) | 0x286906E | 0x286B68E | moved+changed | masked unique match (bytes 0..61); same offset 0x43E in new fn 0x286B250 | doc only |
| kConsumerRva | 0x28431D0 | 0x28457F0 | moved+changed | masked unique match (bytes 0..63) | transition_flash_eye_base.cpp:42 |
| composer | 0x283D4C0 | 0x283FAE0 | moved+changed | masked unique match (bytes 0..59) | doc only |
| camera setter | 0x2878DC0 | 0x287B3F0 | moved+changed | masked unique match (bytes 0..63); REA: sole caller = new composer 0x283FAE0 | doc only |
| camera-kind write (C7 84 24 74 02 00 00 05..) | 0x2878F71 | 0x287B5A1 | moved+changed | masked unique match (bytes 0..63); same offset 0x1B1 in new fn 0x287B3F0; REA search_bytes: only hit, inside new setter; immediate still 5 | doc only |
| coordinator | 0x594B60 | 0x594CC0 | exact | identical 64 bytes | doc only |
| view loop | 0x594D90 | 0x594EF0 | exact | identical 64 bytes | doc only |
| flat refresh fn | 0x5921F0 | 0x592350 | exact | identical 64 bytes | flat_camera_inject.cpp:99 (containing) |
| kRefreshRva (flat camera) | 0x592200 | 0x592360 | exact | identical 64 bytes; same offset 0x10 in new fn 0x592350 | flat_camera_inject.cpp:99 |
| kUploadRva | 0x51B640 | 0x51B7A0 | exact | identical 64 bytes | flat_camera_producer_probe.cpp:49 |
| kRendererRva | 0x2871D30 | 0x2874350 | exact | identical 64 bytes | flat_stationary_camera_capture.cpp:24 |
| main-view caller return 0x281ACD9 | 0x281ACD9 | 0x281D319 | moved+changed | masked unique match (bytes 0..64); same offset 0xC9 in new fn 0x281D250 | flat_stationary_camera_capture.cpp:241 |
| FUN_14281AC10 (main view caller) | 0x281AC10 | 0x281D250 | moved+changed | masked unique match (bytes 0..63) | flat_stationary_camera_capture.cpp:241 (containing) |
| producer handshake | 0x5D6D20 | 0x5D6E80 | moved+changed | masked unique match (bytes 0..63) | doc only |
| kind reader fn 0x4F3770 | 0x4F3770 | 0x4F38D0 | exact | identical 64 bytes | doc only |
| kind cmp ==2 (0x4F37C6) | 0x4F37C6 | 0x4F3926 | exact | identical 64 bytes; same offset 0x56 in new fn 0x4F38D0 | doc only |
| kind cmp ==3 (0x4F3AE0) | 0x4F3AE0 | 0x4F3C40 | exact | identical 64 bytes; same offset 0x370 in new fn 0x4F38D0 | doc only |
| kind reader fn 0x5B1D80 | 0x5B1D80 | 0x5B1EE0 | moved+changed | masked unique match (bytes 0..62) | doc only |
| kind cmp ==5 (0x5B1DD4) | 0x5B1DD4 | 0x5B1F34 | moved+changed | masked unique match (bytes 0..60); same offset 0x54 in new fn 0x5B1EE0 | doc only |
| kind reader fn 0x2042B70 | 0x2042B70 | 0x2045490 | exact | identical 64 bytes | doc only |
| kind cmp ==0 (0x2042B7A) | 0x2042B7A | 0x204549A | exact | identical 64 bytes; same offset 0xA in new fn 0x2045490 | doc only |
| kind cmp ==5 (0x2871D9A) | 0x2871D9A | 0x28743BA | moved+changed | masked unique match (bytes 0..57); same offset 0x6A in new fn 0x2874350 | doc only |
| kFrustumRva | 0x4F4E10 | 0x4F4F70 | moved+changed | masked unique match (bytes 0..62) | cull_gate_probe.h:118 |
| kVisGlobalRva | 0x5EA3399 | 0x5EA63E9 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | cull_gate_probe.h:119 |
| kPartTestRva | 0x42B3FC0 | 0x42B6C80 | exact | identical 64 bytes | kinematic_eval_hook.cpp:311 |
| kPartCallReturnRva | 0x42B4B96 | 0x42B7856 | exact | identical 64 bytes; same offset 0x776 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:312 |
| partTest call site 0x42B4B91 | 0x42B4B91 | 0x42B7851 | exact | identical 64 bytes; same offset 0x771 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:312 (comment) |
| frame pin 0x42B4429 | 0x42B4429 | 0x42B70E9 | moved+changed | masked unique match (bytes 0..61); same offset 0x9 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:318 |
| frame pin 0x42B4455 | 0x42B4455 | 0x42B7115 | exact | identical 64 bytes; same offset 0x35 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:320 |
| frame pin 0x42B445C | 0x42B445C | 0x42B711C | exact | identical 64 bytes; same offset 0x3C in new fn 0x42B70E0 | kinematic_eval_hook.cpp:322 |
| frame pin 0x42B4868 | 0x42B4868 | 0x42B7528 | exact | identical 64 bytes; same offset 0x448 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:324 |
| frame pin 0x42B48B8 | 0x42B48B8 | 0x42B7578 | exact | identical 64 bytes; same offset 0x498 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:326 |
| frame pin 0x42B48CD | 0x42B48CD | 0x42B758D | exact | identical 64 bytes; same offset 0x4AD in new fn 0x42B70E0 | kinematic_eval_hook.cpp:328 |
| frame pin 0x42B498B | 0x42B498B | 0x42B764B | exact | identical 64 bytes; same offset 0x56B in new fn 0x42B70E0 | kinematic_eval_hook.cpp:330 |
| frame pin 0x42B4FEB | 0x42B4FEB | 0x42B7CAB | exact | identical 64 bytes; same offset 0xBCB in new fn 0x42B70E0 | kinematic_eval_hook.cpp:332 |
| frame pin 0x42B4FF7 | 0x42B4FF7 | 0x42B7CB7 | exact | identical 64 bytes; same offset 0xBD7 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:334 |
| frame pin 0x42B49E3 | 0x42B49E3 | 0x42B76A3 | moved+changed | masked unique match (bytes 0..62); same offset 0x5C3 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:336 |
| frame pin 0x42B49F4 | 0x42B49F4 | 0x42B76B4 | moved+changed | masked unique match (bytes 0..59); same offset 0x5D4 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:338 |
| frame pin 0x42B4B1D | 0x42B4B1D | 0x42B77DD | exact | identical 64 bytes; same offset 0x6FD in new fn 0x42B70E0 | kinematic_eval_hook.cpp:340 |
| frame pin 0x42B4B21 | 0x42B4B21 | 0x42B77E1 | exact | identical 64 bytes; same offset 0x701 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:342 |
| call-site pin 0x42B4B67 | 0x42B4B67 | 0x42B7827 | exact | identical 64 bytes; same offset 0x747 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:344 |
| call-site pin 0x42B4B85 | 0x42B4B85 | 0x42B7845 | exact | identical 64 bytes; same offset 0x765 in new fn 0x42B70E0 | kinematic_eval_hook.cpp:346 |
| kBucketBuildRva | 0x42B4420 | 0x42B70E0 | moved+changed | masked unique match (bytes 0..64) | kinematic_eval_probe.h:38 |
| kEvalRva | 0x430EFE0 | 0x4311CA0 | moved+changed | masked unique match (bytes 0..63) | kinematic_eval_probe.h:27 |
| kRigEvalRva | 0x431AFE0 | 0x431DCE0 | exact | identical 64 bytes | kinematic_eval_probe.h:33 |
| kDirectBuildRvas[0] | 0x4312E00 | 0x4315AC0 | moved+changed | masked unique match (bytes 0..60) | kinematic_eval_probe.h:45 |
| kDirectBuildRvas[1] | 0x369C9C0 | 0x369F6C0 | moved+changed | masked unique match (bytes 0..64) | kinematic_eval_probe.h:45 |
| kJobRvas[0] UpdateRenderDataJob | 0x4321940 | 0x4324640 | moved+changed | masked unique match (bytes 0..61) | kinematic_eval_hook.cpp:124 |
| kJobRvas[1] render-data batch | 0x4320340 | 0x4323040 | moved+changed | masked unique match (bytes 0..64) | kinematic_eval_hook.cpp:125 |
| kJobRvas[2] UpdatePhysicsObjectsJob | 0x432B2A0 | 0x432DFA0 | moved+changed | masked unique match (bytes 0..62) | kinematic_eval_hook.cpp:126 |
| kJobRvas[3] PrePhysicsAdvanceJob | 0x42DF530 | 0x42E21F0 | moved+changed | masked unique match (bytes 0..62) | kinematic_eval_hook.cpp:127 |
| kJobRvas[4] PrePhysicsAdvanceCurveJob | 0x42DF550 | 0x42E2210 | moved+changed | masked unique match (bytes 0..62) | kinematic_eval_hook.cpp:128 |
| kJobRvas[5] unnamed table BA0 | 0x4321810 | 0x4324510 | moved+changed | masked unique match (bytes 0..64) | kinematic_eval_hook.cpp:129 |
| job thunk 0x42DF520 | 0x42DF520 | 0x42E21E0 | moved+changed | masked unique match (bytes 0..62) | kinematic_eval_hook.cpp:124 (comment) |
| job thunk 0x42DFAF0 | 0x42DFAF0 | 0x42E27B0 | moved+changed | masked unique match (bytes 0..63) | kinematic_eval_hook.cpp:125 (comment) |
| job thunk 0x42DF540 | 0x42DF540 | 0x42E2200 | moved+changed | masked unique match (bytes 0..58) | kinematic_eval_hook.cpp:126 (comment) |
| job thunk 0x42DFBA0 | 0x42DFBA0 | 0x42E2860 | moved+changed | masked unique match (bytes 0..64) | kinematic_eval_hook.cpp:129 (comment) |
| kLookupRva | 0x3696FA0 | 0x3699CA0 | exact | identical 64 bytes | engine_velocity.cpp:588 |
| kAppendRva | 0x4313185 | 0x4315E45 | exact | identical 64 bytes; same offset 0x385 in new fn 0x4315AC0 | engine_velocity.cpp:592 |
| kStoreRva | 0x4312040 | 0x4314D00 | moved+changed | masked unique match (bytes 0..59) | store_hook.cpp:26 |
| kOwnershipOpcodeWindowRva | 0x431B200 | 0x431DF00 | exact | identical 64 bytes; same offset 0x220 in new fn 0x431DCE0 | object_record_writer_probe.cpp:16 |
| kOwnershipDirectReturn | 0x431B212 | 0x431DF12 | moved+changed | masked unique match (bytes 0..62); same offset 0x232 in new fn 0x431DCE0 | object_record_writer_probe.cpp:13 |
| kOwnershipVirtualReturn | 0x431B21F | 0x431DF1F | moved+changed | masked unique match (bytes 0..57); same offset 0x23F in new fn 0x431DCE0 | object_record_writer_probe.cpp:14 |
| kManagementReturn | 0x434E316 | 0x4351016 | exact | identical 64 bytes | object_record_writer_probe.cpp:11 |
| writer call 0x369CE8C | 0x369CE8C | 0x369FB8C | moved+changed | masked unique match (masked bytes 0..64 match 2; disambiguated by offset in matched containing fn); same offset undefined in new fn 0x369F6C0 | object_record_writer_probe.cpp:20 |
| writer call 0x42B42EA | 0x42B42EA | 0x42B6FAA | moved+changed | 2 masked hits; REA: 0x42B6FAA lies in new FUN_1442B6DF0 at the old offset 0x1BA | object_record_writer_probe.cpp:21 |
| writer call 0x42B4ED1 | 0x42B4ED1 | 0x42B7B91 | moved+changed | masked unique match (bytes 0..64); same offset 0xAB1 in new fn 0x42B70E0 | object_record_writer_probe.cpp:22 |
| writer call 0x43130A5 | 0x43130A5 | 0x4315D65 | moved+changed | masked unique match (masked bytes 0..64 match 2; disambiguated by offset in matched containing fn); same offset undefined in new fn 0x4315AC0 | object_record_writer_probe.cpp:23 |
| writer call 0x434D144 | 0x434D144 | 0x434FE44 | moved+changed | 2 masked hits; REA: 0x434FE44 lies in new FUN_14434FE20 at the old offset 0x24 | object_record_writer_probe.cpp:24 |
| writer call 0x434E311 | 0x434E311 | 0x4351011 | exact | identical 64 bytes | object_record_writer_probe.cpp:25 |
| writer call 0x42B4843 | 0x42B4843 | 0x42B7503 | exact | identical 64 bytes; same offset 0x423 in new fn 0x42B70E0 | object_record_writer_probe.cpp:27 |
| kSkinJobsRva | 0x4C540E0 | 0x4C56CC0 | exact | identical 64 bytes | skin_entity_hook.cpp:22 |
| kBuilderRva (light grid) | 0x58B150 | 0x58B2B0 | moved+changed | masked unique match (bytes 0..62) | light_grid_cache_core.h:51 |
| kOverridePathRva | 0x5E70F7E | 0x5E73F7E | exact | RIP target of 1 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:66 |
| kTileRva | 0x5E70F80 | 0x5E73F80 | exact | RIP target of 2 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:67 |
| kSlicesRva | 0x5E70F90 | 0x5E73F90 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:68 |
| kForceRebuildRva | 0x5F27612 | 0x5F2A652 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:69 |
| kRefreshRva (view-row refresh) | 0x4F2FF0 | 0x4F3150 | moved+changed | masked unique match (bytes 0..61) | light_grid_cache_core.h:75 |
| kOverrideNearRva | 0x5E70F94 | 0x5E73F94 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:83 |
| kOverrideFarRva | 0x5E70F98 | 0x5E73F98 | exact | RIP target of 1 matched ref insn(s); same 16-byte value | light_grid_cache_core.h:83 |
| kCopyRva (view-block copy) | 0x57A460 | 0x57A5C0 | moved+changed | 2 masked hits; REA callers of 0x57A5C0 include new light-grid builder, consumer and main-view caller | light_grid_cache_core.h:84 |
| kTargetRva (cull-plane builder) | 0x443E800 | 0x4441580 | moved+changed | masked unique match (bytes 0..64) | terrain_cull_cover_core.h:42 |
| kCallRva (terrain) | 0x4456007 | 0x4458D87 | exact | identical 64 bytes; same offset 0x5C7 in new fn 0x44587C0 | terrain_cull_cover_core.h:43 |
| kSiteRva (terrain) | 0x4455FDD | 0x4458D5D | exact | identical 64 bytes; same offset 0x59D in new fn 0x44587C0 | terrain_cull_cover_core.h:44 |
| per-view record writer | 0x4455A40 | 0x44587C0 | moved+changed | masked unique match (bytes 0..62) | terrain_cull_cover_core.h:6 (comment) |
| planet draw-list builder | 0x4462E80 | 0x4465C00 | moved+changed | masked unique match (bytes 0..62) | terrain_cull_cover_core.h:6 (comment) |
| draw-list -> record writer call 0x4463A83 | 0x4463A83 | 0x4466803 | exact | identical 64 bytes; same offset 0xC03 in new fn 0x4465C00 | terrain_cull_cover_core.h:6 (comment) |
| 0.5f constant | 0x4DDF878 | 0x4DE28B8 | moved+changed | RIP target of 1 matched ref insn(s) | terrain_cull_cover_core.h:11 (comment) |
| kUiSsSetterSlotRva | 0x52E90B0 | 0x52EC270 | moved+changed | only .rdata slot holding new setter 0x2878E00 with getter at +8 | ui_sizing_math.h:653 |
| kUiSsGetterSlotRva | 0x52E90B8 | 0x52EC278 | moved+changed | slot+8 holds new getter 0x2843B00 | ui_sizing_math.h:653 |
| UI supersampling vtable | 0x52E9020 | 0x52EC1E0 | moved+changed | setter slot - 0x90 (same layout as old) | ui_sizing_math.h:649 (comment) |
| kUiSsSetterRva | 0x28767D0 | 0x2878E00 | exact | identical 64 bytes | ui_sizing_math.h:654 |
| kUiSsGetterRva | 0x28414E0 | 0x2843B00 | exact | identical 64 bytes | ui_sizing_math.h:654 |
| kLoaderSlotRva (fxcfg) | 0x52E8368 | 0x52EB528 | moved+changed | only .rdata pointer to new 0x2864DA0 | vr_ssaa_hold.cpp:30 |
| kWrapperRva (SSAA) | 0x2862780 | 0x2864DA0 | exact | identical 64 bytes | vr_ssaa_hold.cpp:31 |
| kLoaderRva (SSAA) | 0x2855A50 | 0x2858070 | moved+changed | masked unique match (bytes 0..64) | vr_ssaa_hold.cpp:32 |
| kRequestRva (window) | 0x7E9D50 | 0x7E9EB0 | exact | identical 64 bytes | vr_window_trampolines.cpp:28 |
| kCopyRva (settings copy) | 0x281C0D0 | 0x281E710 | moved+changed | masked unique match (bytes 0..64) | vr_window_trampolines.cpp:35 |
| kUiRvaRttMid | 0x4551C7D | 0x45547BD | exact | identical 64 bytes | ui_sizing_math.h:56 |
| kUiRvaRttOuter | 0x45517F6 | 0x4554336 | exact | identical 64 bytes | ui_sizing_math.h:56 |
| kUiRvaInit | 0x4570902 | 0x4573442 | moved+changed | masked unique match (bytes 0..64); same offset 0x402 in new fn 0x4573040 | ui_sizing_math.h:57 |
| kUiRvaChange | 0x456DA45 | 0x4570585 | exact | identical 64 bytes | ui_sizing_math.h:57 |
| kUiRvaColour | 0x28148A9 | 0x2816EE9 | exact | identical 64 bytes | ui_sizing_math.h:58 |
| kUiRvaDepth | 0x2814686 | 0x2816CC6 | exact | identical 64 bytes | ui_sizing_math.h:58 |
| kUiRvaGlyphInit | 0x30FC1A | 0x30FCEA | moved+changed | masked unique match (bytes 0..64) | ui_sizing_math.h:59 |
| kUiRvaGlyphRecreate | 0x312450 | 0x312520 | exact | identical 64 bytes | ui_sizing_math.h:59 |
| kUiPanelSiteRva[0] init | 0x45706FA | 0x457323A | moved+changed | masked unique match (bytes 0..63); same offset 0x1FA in new fn 0x4573040 | ui_sizing_math.h:185 |
| kUiPanelSiteRva[1] view change | 0x4571205 | 0x4573D45 | moved+changed | masked unique match (bytes 0..63); same offset 0x85 in new fn 0x4573CC0 | ui_sizing_math.h:185 |
| kUiPanelFuncRva[0] | 0x4570500 | 0x4573040 | moved+changed | masked unique match (bytes 0..63) | ui_sizing_math.h:193 |
| kUiPanelFuncRva[1] | 0x4571180 | 0x4573CC0 | exact | identical 64 bytes | ui_sizing_math.h:193 |
| kUiPanelAspectRva | 0x52F2D84 | 0x52F5F44 | exact | RIP target of 3 matched ref insn(s); same 16-byte value | ui_sizing_math.h:191 |
| kUiPanel1080Rva | 0x4DDE660 | 0x4DE16A8 | exact | RIP target of 2 matched ref insn(s); same 16-byte value | ui_sizing_math.h:191 |
| kUiPanel1920Rva | 0x4DDE664 | 0x4DE16AC | moved+changed | RIP target of 2 matched ref insn(s) | ui_sizing_math.h:192 |
| kFlushIaRva | 0x522A50 | 0x522BB0 | exact | identical 64 bytes | vertex_resync_core.h:44 |
| SetVertexBuffer (read) | 0x4E9D70 | 0x4E9E70 | exact | identical 64 bytes | vertex_resync_core.h:13 (comment) |
| pad reader 0 | 0x12C3C20 | 0x12C4D60 | moved+changed | masked unique match (bytes 0..62) | pad_state_core.h:20 |
| pad reader 1 | 0x12CAF80 | 0x12CC0C0 | moved+changed | masked unique match (bytes 0..63) | pad_state_core.h:20 |
| pad reader 2 | 0x12CADE0 | 0x12CBF20 | moved+changed | masked unique match (bytes 0..64) | pad_state_core.h:20 |
| pad reader 3 | 0x12CB320 | 0x12CC460 | moved+changed | masked unique match (bytes 0..63) | pad_state_core.h:20 |
| f3dDevice::PrePresent string | 0x4E2E0F8 | 0x4E31138 | exact | unique string "f3dDevice::PrePresent" in .rdata; same 16-byte value | doc only |
