# CHR_CLUSTER: from a character .dae.phyre to CHR+0x830

Written 2026-10-28 for Project Zanarkand item 1.4.6 (the ClassCharacter bundle: what a Phyre
character cluster contains and how createInstance turns it into mesh instances). Read from the
FFX.exe IDB (VA = RVA + 0x400000). The census in section 2 was measured with `tools/phyre.py` over
the shipped FFX_Data.vbf, parsed in memory, nothing extracted.

Tags: [V] read in the binary at the address given, [M] measured by a scan over the game data,
[I] inferred. Only [V] and [M] are proof.

Companion docs: `CLASS_CHARACTER.md` (the 36 byte ClassCharacter, the load chain and
buildMeshTables, which this doc starts from), `CHR_POOL.md` (FFX_Ch_Allocate), `DYNAMIC_MESHES.md`
(the cloth instancing decorator in detail), `PHYRE_FORMAT.md` (clusters), `DISPLAY_LIST.md`
section 5 (mesh groups), `PHASE1_CLONE.md` (bounds). Names applied by
`tools/apply_chr_cluster_names.py`.

---

## 1. The walk in one table

| # | step | function | thread | writes |
|---|---|---|---|---|
| 1 | model sub asset loaded | `FFX_AsyncLoad__charSubAssetJob 0x6663D0` -> `FFX_Chr_OnSubAssetLoaded 0x65AE40` | AsyncLoadManager worker | template `m_bounds->m_meshInstance = 0` (0x65AF07) |
| 2 | last sub asset | `FFX_Chr_FinishCharacterLoad 0x65A760` -> `ClassCharacter__buildMeshTables 0x6A2390` | worker | cc+4 cluster, cc+20 remaps, cc+24 mesh count, cc+28 ready |
| 3 | CHR allocated | `FFX_Ch_Allocate 0x824F90` -> `FFX_Chr_AttachModelInstance 0x63D370` (0x82550F..0x82555C) | step | queues the attach for +0x830 and +0x838 |
| 4 | attach runs | `ProcessPendingAttachments 0x639DB0` from `FFX_MainStep` (0x820C0F) -> `ClassCharacter__createInstance 0x63C540` | step | the 32 byte record |
| 5 | per template mesh | `FFX_Gfx_CreateMeshInstance 0x66F7D0` | step | fresh PMeshInstance, PWorldMatrix, PMeshInstanceBounds |
| 6 | per mesh | `ClassCharacterInstance__addClothBinding 0x6A1320`, `FFX_ChrLight_RegisterMeshInstance 0x6A96B0` | step | record +8/+12, inst+24, inst+16 |
| 7 | after the meshes | `TextureAnimationManager__registerCharInstance` (0x63C937), `FFX_Chr_ApplyLevelDepthBias 0x6E4100` (0x63C9B1) | step | texanim entry, depth bias rows |
| 8 | ready callback | `FFX_Ch_OnInstanceReady 0x827D10` | step | CHR+0x830, record +21, CHR+0x834 clone, CHR+0x868 |
| 9 | every step | `FFX_Ch_StepAll` -> SetRootTransform, `FFX_ChrInstance_SetHidden 0x63C390`, mesh group push (0x82F9ED), `FFX_Ch_UpdateCameLenAndZClip 0x82E220` | step | world matrices, record +20, m_hideFlags bit 8 |
| 10 | skin | `FFX_Ch_BuildSkinMatrices` -> `FFX_ChrInstance_UploadBoneMatrices 0x63BE60` | Phyre worker render job | inst+12 m_currentPose |
| 11 | cloth | `FFX_Chr_StepClonedCharacterList 0x63B740` -> `sub_6365F0` | present | cloth output streams |
| 12 | release | `FFX_Chr_ReleaseInstance 0x63A030` | step | everything above, see 7 |

---

## 2. What a character cluster holds

Measured [M] over all 872 `chr/*.dae.phyre` files (mon 341, npc 224, obj 110, pc 49, skl 40,
sum 29, wep 79). Every file parses.

| class | files | notes |
|---|---|---|
| PMesh, PMeshInstance, PMeshInstanceBounds, PMeshInstanceSegmentContext, PMeshSegment, PNode, PDataBlock, PVertexStream, PWorldMatrix, PAssetReference | 857 | the mesh core |
| PMaterial, PParameterBuffer, PSamplerState, PShaderParameterDefinition, PAssetReferenceImport | 856 | materials, imports of PhyreChrLitShader.fx |
| PMatrix4, PSkeletonJointBounds, PSkinBoneRemap, PString | 688 | skinned models |
| PCameraPerspective / PCameraOrthographic | 73 / 72 | exporter leftovers, FFX reads none of them [I, no reader seen on this path] |
| PAnimation* | 67 | |
| PMeshInstanceSegmentStreamBinding | 11 | m087 m227 n145 n183 n184 n236 n255 n352 f047 c121 c921 |
| PModifierNetwork* | 2 | c902 and s019, the only files whose template has `m_dynamicMeshInstance` set |

- 15 clusters are empty, with no instance lists at all: m999, c051, k002, k004, k007, k014, k015,
  k017, k201, k212, k214, k302, k303, k403, k999. [M]
- Mesh instances per file: 0:15, 1:699, 2:63, 3:43, 4:15, 5:1, 6:9, 7:1, 9:7, 11:2, 13:3, 20:1,
  23:1, 24:8, 31:1, 35:1, 37:1, 60:1. [M]
- 1602 template PMeshInstances in all. Every one has `m_bounds`. None has `m_lodLevel` or
  `m_instanceSegment`. Every `m_materialSet` is mesh+48. PMesh count equals PMeshInstance count in
  every file and no PMesh is shared between two instances. [M]
- 122 models carry at least one cloth segment (`m_clothmodelIndex` and `m_clothIndex` both not -1).
  No segment has exactly one of the two at -1. `m_clothmodelIndex` runs 0..7, `m_clothIndex`
  0..1806. [M]

The five surveyed models [M]:

| model | meshes | segments (cloth) | materials | nodes | joint bounds | other |
|---|---|---|---|---|---|---|
| c001 Tidus | 1 `MeshNode0Shape` | 13 (5) | 24 | 143 | 53 | 211 PMatrix4 = 53 skeleton + 79 defaultPose + 79 template m_currentPose, 127 PSkinBoneRemap, 104 PVertexStream, 206 PSamplerState, 726 PShaderParameterDefinition, 6 PWorldMatrix, 3 ortho + 1 perspective camera |
| c002 Yuna | 1 | 22 (17) | 41 | 118 | 54 | |
| c006 | 1 | 24 (16) | 40 | 184 | 79 | 321 PMatrix4 |
| m031 (monster) | 1 | 3 (0) | | 103 | | no cloth |
| n002, n003, n008 (npc) | 1 | 4 | | | | |

c001's cloth segments are 6, 7, 10, 11, 12 with (clothmodel, cloth) = (0,1), (1,2), (0,0), (0,2),
(1,1). Its one mesh instance has bounds min -6.98577 and size 13.9729, world PWorldMatrix #5. f001
and w001 hold 3 meshes each, and w001 has 2 cloth segments. [M]

What FFX reads from a template PMeshInstance, and nothing else [V]: `m_mesh` (+0, at 0x66F83F),
`m_bounds` (+28) for its min and size (0x66F895..0x66F932) and to unlink it (0x65AF07), and
`m_dynamicMeshInstance` (+24, at 0x66F9EA). The template's world matrix, current pose, segment
contexts, stream bindings and material set are never carried over. From the PMesh it reads the
segments (cloth indices), `m_defaultPose` (+28) and the remap count (+24).

---

## 3. Mesh instance creation

### 3.1 The template unlink, on the worker

`FFX_Chr_OnSubAssetLoaded 0x65AE40` walks the model cluster's PMeshInstance lists and writes
`m_bounds->m_meshInstance = 0` for each (0x65AF03 loads m_bounds, 0x65AF07 is
`mov dword ptr [eax+1Ch], 0`). A PMeshInstanceBounds with a null m_meshInstance is not drawn, so
this takes the loaded templates out of the render, and they stay out for good. It is a dword, and
it dereferences m_bounds with no null check, which the census says is safe. [V] [M]

### 3.2 The 32 byte instance record

`ClassCharacter__createInstance 0x63C540(cc, chr)` [V]:

- returns 0 when cc is null (0x63C55F).
- `FFX_Alloc(0x20)`, zeroes +0, +4, +8, +12 (0x63C587..0x63C59B), then +24 = chr (0x63C5AC) and
  +16 = cc (0x63C5AF), then `ClassCharacter__addInstance 0x6A0D00`.
- iterates cc+4's instance lists (+28 of the cluster) for `g_phyreClassDescriptor_PMeshInstance`
  (0x63C5F3, Seek 0x63C5FA), and for each template:
  - `FFX_Gfx_CreateMeshInstance(template, chr, cc+32 != 0)` (0x63C619..0x63C62B)
  - push the result onto the record's +0/+4 PArray (realloc to count+1 at 0x63C646, push 0x63C656)
  - `ClassCharacterInstance__addClothBinding` with the new instance (0x63C65F)
  - `FFX_ChrLight_RegisterMeshInstance` on `FFX_ChrLight_Get()` (0x63C665, 0x63C66C)

| off | what | writer | reader |
|---|---|---|---|
| +0 / +4 | PArray of runtime PMeshInstance* (count with the 0x80000000 flag, data) | createInstance | everything |
| +8 / +12 | PArray of cloth bindings, one slot per mesh, null when the mesh has no cloth | addClothBinding | releaseClothBindings |
| +16 | ClassCharacter* | 0x63C5AF | release |
| +20 | shown byte, `a2 == 1` | `FFX_ChrInstance_SetHidden 0x63C390` | `FFX_GfxCtx_LinkMeshGroupLayer 0x6589E0`, `FFX_GfxCtx_LinkAllMeshGroups 0x66C770` |
| +21 | mesh group layer (signed char) | `sub_63B7D0`, from OnInstanceReady (0x827E7A, layer 0) and `sub_830160` (0x830176) | `FFX_GfxCtx_LinkMeshGroupLayer` compares it with its layer argument |
| +24 | CHR* | 0x63C5AC | |
| +28 | zeroed when the record joins the gfx+0xFB48 mesh group list | `sub_656590` | not found |

+20, +21 and +28 are not initialized by createInstance [V, the zeroing stops at +12]. +20 is
garbage until the first SetHidden and +21 until OnInstanceReady.

After the loop, createInstance takes the name between the last '/' and the last '.' of the
ClassCharacterTable key, registers the record with the TextureAnimationManager (0x63C937) and hands
an array copy of the mesh instances to `FFX_Chr_ApplyLevelDepthBias` (0x63C9B1). See 6.

### 3.3 FFX_Gfx_CreateMeshInstance 0x66F7D0

`__thiscall(gfx, template, chr, alternate)`, returns the new PMeshInstance [V]:

1. cluster = gfx+0x1234 when `alternate` is set, else gfx+0x11E4 (0x66F7FD..0x66F809). Both are
   80 byte PClusters built by `PCluster__ctor 0x576630` in `FFX_GfxCtx_ctor` (0x64E086, 0x64E0A0).
2. a 12 byte record {instance, world, bounds}, zeroed (0x66F814..0x66F82F).
3. the instance: `PCluster__allocateInstanceListForCount(1, PMeshInstance)` (0x66F84A), then
   `PMeshInstance__ctor(template->m_mesh)` in place at list+52 (0x66F83F, 0x66F868).
   `PMeshInstance__ctor 0x4F27E0` sets m_mesh and m_materialSet = mesh+48 and calls
   `PMeshInstance__setMesh 0x4F8B90`, which sizes m_segmentContext (36 bytes per segment) and
   m_currentPose (64 bytes per entry) and copies `m_defaultPose` (mesh+28, read at 0x4F8C81) into
   m_currentPose (`Vectormath_Matrix4_copy` 0x4F8C9D). A new character instance starts in the
   bind pose.
4. the world: `PCluster__createCount(1, PWorldMatrix)` (0x66F887), stored in record +4 and
   instance+4 (0x66F88C, 0x66F88F).
5. the bounds, only when template m_bounds is set (0x66F895..0x66F89D):
   `PCluster__allocateInstanceListForCount(1, PMeshInstanceBounds 0xCA89B8)` (0x66F8AE),
   `PMeshInstanceBounds__ctorFromInstance` (0x66F8CC), which links +12 = the new world and
   +28 = the new instance and sets instance.m_bounds. Then `PMeshInstanceBounds__setMinAndSize` with
   the template's min (+0..+8) and size (+16..+24) (0x66F932). Record +8 = bounds (0x66F937).
6. the record goes into the gfx+0xF9D4 map keyed by CHR (find `sub_64B9A0` at 0x66F94B, insert
   `sub_65FCA0` at 0x66F9CE). `FFX_Gfx_ReleaseChrMeshRecords 0x668680` walks it on release.
7. the dynamic mesh: when template+24 is set, `inst+24 = templateDyn->m_mesh->vtbl[1](cluster,
   templateDyn)` (0x66F9EA..0x66F9FD). Both implementations found,
   `PModifierNetworkDynamicMesh__vf01 0x5ADF70` and `ClothInstancingDynamicMesh__vf01 0x6E1EC0`,
   return the existing instance when it is non null. So for c902 and s019 every CHR shares the one
   dynamic mesh instance of the template.

Templates therefore contribute exactly three things: the PMesh, the bounds min and size, and the
shared dynamic instance.

### 3.4 Phyre helpers this path uses

| ea | name | what |
|---|---|---|
| 0x440B70 | PCluster__allocateInstanceListForCount | count * desc+28 into the size variant 0x440BA0 (named `PCluster__allocateInstanceList` in the IDB). No ctor runs |
| 0x441010 | PCluster__createCount | refuses a class without a default ctor (desc+144 bit 8), else allocates and default constructs |
| 0x443250 / 0x443B70 | PWorld__addCluster / PWorld__removeCluster | |
| 0x576630 / 0x43FA30 | PCluster__ctor / PCluster__dtor | |
| 0x4FF800 / 0x500C20 | PMeshInstanceBounds__ctorFromInstance / setMinAndSize | |
| 0x6377A0, 0x6A22D0 | the two copies of the instance list iterator Next | |

### 3.5 Where the instances draw

The +0x11E4 cluster is re-added to the worlds gfx+0x7C and gfx+0x9C when gfx+0xFB44 is set
(`FFX_Frame_RenderSlot1CameraPass` 0x6532DF..0x653303). [V] Which pass draws the +0x1234
(alternate render set) cluster is open.

Draw membership is the bounds link. Three writers toggle `bounds->m_meshInstance` [V]:
`FFX_Gfx_LinkMeshToNode 0x66D910` (its "node" is the m_bounds), called per mesh by
`FFX_ChrInstance_SetHidden`, and the mesh group pair `FFX_GfxCtx_LinkMeshGroupLayer 0x6589E0`
(links the records of one layer by their +20 byte, unlinks every other record) and
`FFX_GfxCtx_LinkAllMeshGroups 0x66C770` (relinks all by +20), both called from
`FFX_Frame_RenderScene` at 0x669AF3 and 0x669B92. StepAll pushes CHR+0x830 into the gfx+0xFB48
mesh group list each step that is not a catch up step (0x82F9DE..0x82F9ED).

---

## 4. Cloth binding

`ClassCharacterInstance__addClothBinding 0x6A1320` (this = the record, arg = the new instance) [V]:

- scans `inst->m_mesh` segments (PArray at PMesh+0, 108 byte stride) for any with dword +32 != -1
  and +36 != -1 (0x6A1364, 0x6A136A). The scan latches on the first hit.
- on a hit it builds a 20 byte `ClothInstancingBinding` (ctor 0x6E0800) and runs
  `Cloth_InstallInstancingMesh 0x6E2600`: a 40 byte ClothInstancingDynamicMesh (Color semantic
  `dword_C98650`, type 3 float4), a 0x2010 byte `ClothInstancingDynamicMeshInstance` (ctor
  0x6E0860), inner = the instance's original dynamic mesh or the default implementation, and
  `ClothInstancingDynamicMesh__buildStreamDescs 0x6E17F0`. Then inst+24 = the cloth instance.
- always pushes the binding or null onto record +8/+12, so that PArray has one slot per mesh.

So the install is per mesh instance when any segment of its mesh qualifies, not per segment. The
per segment part is the stream descriptor table that buildStreamDescs fills, one 16 byte segment
descriptor per PMeshSegment and one 12 byte stream descriptor per Color stream of each cloth
segment. The cloth simulation itself is fed from the CHR+0x834 clone of section 8. [V]

---

## 5. Character light

`g_ffxChrLight` 0xCCCA84 is a 0x234 byte manager (`FFX_ChrLight_ctor 0x6A63F0`, created by
`FFX_ChrLight_CreateSingleton 0x6A6CD0` from FFX_GraphicInitialize, destroyed by
`FFX_ChrLight_DestroySingleton 0x6A6DD0`). Byte +0x1CC is `light count == 4`, set by
`FFX_ChrLight_Initialize` at 0x6A91C2. [V]

`FFX_ChrLight_RegisterMeshInstance 0x6A96B0` runs per mesh, and only does work when +0x1CC is set
[V]: a new PMaterialSet holding a copy of every material and parameter buffer (allocated in the
manager's +160 cluster), CharacterAmbientLight bound for effects with USE_CHARACTER_LIGHTING,
`inst->m_materialSet` = the copy, LgtMatClr and LgtMatVec zeroed, and a {instance, 0.0, 2} row in
the 100 row table at `dword_CCCA88`+248. `FFX_ChrLight_WriteLightMatParams 0x6AABF0` later writes
LgtMatClr and LgtMatVec per material. `dword_CCCA88` (0xFC bytes, +0 enable) is not named yet.
2026-10-28: it is `g_characterLightingTweakUtil`, the CharacterLighting debug page object
(`BOOT_SEQUENCE.md` step 47). +0 is not a lighting enable, it is the page's labels ready byte,
set to 1 at 0x6A93AB on every retail boot because `FFX_DebugText_LoadFont` always returns 0
(0x6F63AB). So the 100 row table is filled in retail too. [V]

So a lit character's mesh instance does not draw with mesh+48, it draws with a private material
copy whose light parameters FFX writes per instance.

---

## 6. Texture animation and level depth bias

- `TextureAnimationManager__get 0x6B02E0` returns the singleton `dword_CCCAA8`. createInstance
  registers the record at 0x63C937. [V]
- `FFX_Chr_ApplyLevelDepthBias 0x6E4100(name, count, instances)` [V]: on the level with hash 393
  every mesh instance's bounds gets a row {bounds, 1.0, 800.0} in `g_ffxDepthBiasTable 0x194299C`
  (12 byte rows, `g_ffxDepthBiasCount 0x1942998`, at most 256), and on level hash 632 with name
  m121, or any other level with name m121 or m281, fixed biases go on mesh instances 0..3. Any row
  sets `g_ffxDepthBiasActive 0xCE5176`. It frees the array it was given.

---

## 7. Release

`FFX_Chr_ReleaseInstance 0x63A030` [V], in order:

1. `FFX_ChrLight_UnregisterMeshInstance 0x6A8210` per mesh: restores m_materialSet (mesh+48 when
   the saved one is null) and frees the material copies when the manager knows the set, and always
   clears the table row.
2. texture animation unregister.
3. `ClassCharacterInstance__releaseClothBindings 0x6A1590`: `ClothInstancing_QueueDeferredRelease
   0x6E2E40` per non null binding (the binding as this, ecx at 0x6A15A3), which queues
   {mesh, instance, inner, 4} in `g_ffxClothDeferredReleaseList 0xCDEFA8` (count 0xCDEFA4) so the
   cloth objects outlive the 4 frames the render side may still hold them, then frees the binding.
4. `ClassCharacter__removeInstance 0x6A2B90` under the mutex.
5. `ClassCharacterInstance__releaseMeshInstances 0x6A15E0`: `FFX_Gfx_ReleaseMeshInstance 0x65A4C0`
   per mesh, always passing gfx+0x11E4 (0x6A15F6), even for an instance made in gfx+0x1234.
6. `FFX_Gfx_ReleaseChrMeshRecords 0x668680` on the CHR: each 12 byte record released from both
   clusters, then the map node erased.

---

## 8. CHR+0x834 is a cloth vertex clone, not a ClassCharacter

`FFX_Ch_OnInstanceReady 0x827D10` builds CHR+0x834 only when m_flags1 bit 0x200000 and
m_hasClonedClassChar (+0x83C) are set, through `FFX_Chr_CloneClassCharacterVertexData 0x63A810`
[V]:

- a 560 byte record from `g_ffxClothClonePool 0xCCB454` (`push 230h` at 0x63A84F).
- +0 the instance record, +4 the shared per mesh vertex tables, +16 = 1, +20 the mesh count,
  +24..+44 bounds initialized to plus and minus 100000, +48 per mesh arrays of 80 byte cloth objects.
- a cloth object per segment whose words +32 and +36 are not 0xFFFF (0x63AA27..0x63AA45). The two
  indices go to object +0x40 and +0x42 (0x63AE30, 0x63AE3E, and again at 0x63AFA2, 0x63AFB0),
  +68 is the vertex count and +76 a float4 buffer copied from the Color stream.
- it maps D3D11 data blocks for read on the step thread (`PDataBlockD3D11__mapForRead`, spinning
  on `sub_430030`).
- the record goes into `g_ffxClonedChrList` (count `g_ffxClonedChrCount 0xCCB250`).

`FFX_Chr_BindClonedVertsToBones 0x827E90` then writes CHR+0x868 (0x827F1E). The present thread
runs `FFX_Chr_StepClonedCharacterList 0x63B740` from FFXApplication__render, which batches the
entries with [3] == 2 and [2] == 0 into `sub_6365F0`. That pushes each cloth object's buffer into
the "ClothWorld" and, per Color stream, either hands +72/+76 to the cloth instance
(`sub_6E2B10`, `sub_6E2AC0`) or copies +76 into the next data block buffer. [V]

The comments in `ffx_types.h`, `FFX_GAME_NOTES.md` and `VISUAL_CENSUS.md` called it a cloned
ClassCharacter, which it is not (see 12).

---

## 9. Skin and pose

- `ClassCharacter__buildMeshTables` builds cc+20, the per mesh joint remap. [V, CLASS_CHARACTER.md]
- `FFX_ChrInstance_UploadBoneMatrices 0x63BE60`, from `FFX_Ch_BuildSkinMatrices` (0x832D4E,
  0x832D64) on the Phyre worker render job, copies `palette[remap[j]]` into inst+12 for
  j < mesh+24, with the chr 8298 >= 66 skip. [V]
- Until the first upload, m_currentPose holds m_defaultPose from setMesh (3.3 step 3). [V]

---

## 10. Visibility coupling

`FFX_Ch_UpdateCameLenAndZClip 0x82E220` (from StepAll at 0x82F256) [V]: when `m_hideFlags` (+0x180)
is 0 (0x82E2BB) and m_flags1 (+0x194) bit 0x800 is set (0x82E2D3), it calls
`FFX_Chr_InstanceAnyMeshVisible(CHR+0x830)` (0x82E2E9), and a 0 sets `m_hideFlags |= 8`
(0x82E2F5). AnyMeshVisible returns 0 for a null record, for zero meshes, and when no mesh passes
`FFX_Gfx_IsMeshInstanceVisible 0x6603C0` (bounds -> `FFX_Gfx_TestBoundsAgainstFrustum 0x663CC0`
against gfx+0xDC's frustum at +0x74, a mesh without bounds counts as visible).
`FFX_Ch_UpdateMotionAll` clears bit 8 at 0x832E5C before its tests.

So a CHR whose attach has not run yet, or whose model is one of the 15 empty clusters, gets bit 8
every step it has flag 0x800 and no other hide bit. The bounds the test reads are the runtime bounds of 3.3 step 5, with
the world written by `FFX_Gfx_SetMeshTransform 0x66D810` (which also sets bounds+12).

---

## 11. Threads and hash visibility

| work | thread | hash visible |
|---|---|---|
| load, template unlink, FinishCharacterLoad, buildMeshTables | AsyncLoadManager worker | no, but cc+28 decides when step 4 can run |
| Allocate, attach, createInstance, CreateMeshInstance, OnInstanceReady, the clone | step | the step on which the attach lands depends on load completion, which is wall clock [I] |
| SetRootTransform, SetHidden, mesh group push, UpdateCameLenAndZClip | step | m_hideFlags bit 8 is CHR state [V write 0x82E2F5] |
| BindClonedVertsToBones | step | CHR+0x868 is a pointer, never hash it |
| bone upload | Phyre worker render job | no |
| cloth stepping | present | no |
| mesh group linking | render | no |

The bit 8 test reads the frustum at gfx+0xDC+0x74, which is a renderer value [I: the writer of
that frustum was not traced here]. The OnInstanceReady path also nulls the m_parts bone buffers.
Pointers in the record, the clone and CHR+0x830..0x838 must never enter a hash.

---

## 12. What the recreation gets wrong or misses

Read from `E:\Git\projectzanarkand\src` (not edited):

1. `src/engine/chr/ClassCharacter.h` `ClassCharacterInstance` has the mesh instances, the model
   (+16) and the CHR (+24) but not the +8/+12 cloth binding array or the +20 shown and +21 layer
   bytes, which the mesh group passes read (3.2, 3.5).
2. `CreateInstance` builds through `env.CreateMeshInstance` per template. The binary takes only the
   PMesh, the bounds min and size and the shared dynamic instance from a template (3.3). Anything
   the port copies beyond that (world, pose, material set, segment contexts) is not FFX.
3. `src/render/SkinPalette.cpp` line 280 marks the bind pose start [inferred]. It is [V] at
   `PMeshInstance__setMesh 0x4F8B90` (0x4F8C9D).
4. No character light material clone: the port draws with mesh+48 where FFX draws with a private
   copy carrying USE_CHARACTER_LIGHTING, CharacterAmbientLight and LgtMatClr/LgtMatVec (5).
5. No cloth. The Color stream of a cloth segment is replaced by the cloth output (4, 8).
6. `src/engine/chr/ChrStepPasses.h` leaves `UpdateCameLenAndZClip` unported, waiting on the
   frustum query. Its inputs are now known: runtime bounds min and size, the world written by
   SetMeshTransform, the camera frustum, and the null record and zero mesh cases (10).
7. The alternate render set (cc+32) builds into gfx+0x1234 but is released through gfx+0x11E4
   (7 step 5). A port that releases per owning cluster is fine but should know it differs.
8. The template unlink (3.1) means a port that draws the loaded cluster directly draws a second
   copy of every character at the template's world matrix.

---

## 13. Corrections to other docs

| doc | was | now |
|---|---|---|
| CLASS_CHARACTER.md 2 step 6 | "the `m_bounds+28` byte" | the dword `m_bounds->m_meshInstance` (0x65AF07) |
| DYNAMIC_MESHES.md section 8 | installed "only for segments whose" indices are not -1 | per mesh instance when any segment qualifies |
| DYNAMIC_MESHES.md section 11 | 0x6A1320 `ClassCharacter_SetupDynamicMeshes` | `ClassCharacterInstance__addClothBinding` |
| DYNAMIC_MESHES.md section 12 | `sub_6E17F0` as the setter of the dirty flag | it is `ClothInstancingDynamicMesh__buildStreamDescs` |
| ffx_types.h CHR +0x834 | cloned ClassCharacter | the 560 byte cloth vertex clone record |
| FFX_GAME_NOTES.md CHR list | `+0x834` is a cloned `ClassCharacter` | the cloth vertex clone record |
| VISUAL_CENSUS.md cloned ClassChar row | cloned ClassChar | cloth vertex clone |

---

## 14. Open items

- Which render pass draws the gfx+0x1234 cluster, and what the four asset ids and the 800..900
  range share.
- The reader of record +28.
- `dword_CCCA88`, the 0xFC byte object with the 100 row light table at +248.
- The writer of the frustum at gfx+0xDC+0x74 and its thread.
- `sub_6365F0`, the ClothWorld feed, is not named here. `sub_6E2AC0` and `sub_6E2B10` have names
  proposed in DYNAMIC_MESHES.md section 11 and are left to that doc.
- The exact wait condition in `ProcessPendingAttachments 0x639DB0` (assumed cc+28).

---

## 15. Names

`tools/apply_chr_cluster_names.py`: 31 functions and 8 globals, all over placeholders. The name
`PCluster__create` already belongs to 0x441120 (the reflected method), so 0x441010 is
`PCluster__createCount`. 0x440BA0 keeps its existing name `PCluster__allocateInstanceList` though
it is the size variant.
