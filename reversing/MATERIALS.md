# Materials (PMaterial, PParameterBuffer, captures, cb0)

How a PMaterial in a model cluster becomes the constant buffer and texture bindings of a draw. The
cluster container is PHYRE_FORMAT.md, the effect side (variants, passes, programs, states) is
SHADERS.md. The C++ reader is `zan::phyre` Material.h in projectzanarkand, checked archive wide by
`phyrecheck materials`.

Addresses are IDA virtual addresses at the preferred base 0x400000. The IDB was read only for this
work. Functions without an IDB name are quoted as `sub_XXXXXX` here with a proposed name in section 9.

Written 2026-10-13.

## 1. The objects

**PMaterial** (24 bytes, every offset from the cluster's own class table)

| offset | member | what it holds in the archive |
|---|---|---|
| 0 | m_effectVariant (PEffectVariant*) | an import (43,686 materials) or a local object (793, the default material every .fx.phyre carries) |
| 4 | m_parameterBuffer (PParameterBuffer*) | always a local object, the only object of its own instance list |
| 8 | m_remapFrom (PSceneRenderPassType*) | never set |
| 12 | m_remapTo (PSceneRenderPassType*) | never set |
| 16 | m_texAnimID (PString) | set on 4,141 materials |
| 20 | m_chLightAmt (PShaderParameterDefinition*) | never set |

**PParameterBuffer** is a header class (PHYRE_FORMAT.md section 7). The 16 byte fixed part is
`m_parameterBufferSize` @0 (PParameterBufferBase, 16 aligned), `m_effectVariant` @4 and
`m_tweakableShaderParameterDefinitions` @8. The children follow, and every definition's
`m_bufferLoc.m_offset` is a byte offset from the first byte of the object, not from the children. In
c001.dae.phyre the first value (`LcMatrix`) sits at 16, right after the fixed part.

`m_parameterBufferSize` is the stored object rounded up to 16: c001's first buffer is 1,124 bytes
stored and says 1,136. It equals the variant's `m_tweakableParameterBufferSize` for all 44,479
materials, and the buffer's definitions equal the variant's tweakable definitions one for one (name,
types, count, offset, size). The buffer's own `m_effectVariant` names the same variant as the material
in every case.

**Capture objects.** A texture or sampler parameter is a 16 byte capture inside the block:

| class | +0 | +4 | +8 | +12 |
|---|---|---|---|---|
| PShaderParameterCaptureBufferTexture2D | m_parameterType (66) | m_textureBufferIndex | m_samplerState (PSamplerState*) | m_texture (PTexture2D*) |
| PShaderParameterCaptureBufferTextureCubeMap | 68 | | | m_texture |
| PShaderParameterCaptureBufferSampler | 71 | | m_samplerState | m_unusedPointer |

The pointers are pointer fixups of the PParameterBuffer object at the capture's offset + 8 and + 12.
Samplers are local PSamplerState objects. Textures are imports of the model's `.dds` clusters. A slot
the material leaves empty is all zero, capture type 0, no pointers: 22,125 of the 67,664 Texture2D
definitions and 89 of the 421 cube map definitions. Every non empty texture capture resolves to a
PTexture2D or PTextureCubeMap of the right kind, and every sampler capture has its sampler state.

**PMaterialSet** is `PSharray<PMaterial*> m_materials`: the count, then one pointer inline in m_u or a
pointer array for more. It is almost always the `m_defaultMaterials` member inside a PMesh at +48,
which is also where `PMeshInstance::m_materialSet` points (a pointer with target offset 48). One
PMaterialSet object list exists in the whole archive.

**PMaterialSwitch** (`m_name`, `m_value` PStrings) belongs to the variant, `PEffectVariant::m_switches`,
4,716 of them over the 793 variants. They record the switch set the instance was compiled with. The
runtime selects nothing with them: the selection happened when the material instance was built.

**Imports and exports.** A PAssetReferenceImport has `m_id` @4 and `m_targetAssetType` @0 (a
PClassDescriptor user fixup). A pointer that names an import is a user fixup of type
PAssetReferenceImport whose two byte big endian payload is the index into the cluster's import list.
The target cluster is at

    "ffx_data/gamedata/" + lower(directory of m_id) + "/d3d11/" + lower(file name of m_id) + ".phyre"

and it exports the object with a PAssetReference whose `m_id` equals the import id exactly (same case),
`m_asset` @28 pointing at it and `m_assetType` @32 naming its class. A material instance cluster
exports two: `PS3Data/Shaders/X.fx#MD5` (the PEffectVariant) and `PS3Data/Shaders/X.cgfx#MD5#material`
(its default PMaterial).

## 2. Lighting contexts

`PEffect::m_contextSwitches` @36 is a pointer array of PContextSwitch user fixups. Every lit shader in
the archive has one, `NUM_LIGHTS`. `m_contextVariantSwitches` @44 is a `PArray<PNodeContext>`, each
`PNodeContext::m_packedSwitches` a `PSharray<PUInt32>` (one value inline, more through an array).
phyrechrlitshader with `m_maxLightCount` 3 has four:

| index | packed switches |
|---|---|
| 0 | 0 |
| 1 | 17 (0x11) |
| 2 | 16402 (0x4012) |
| 3 | 16793619 (0x1004013), 0 |

`PEffect__findContextVariant 0x4EF440` asks each context switch for its part of the key
(`sub_4EF3F0`, a virtual call per switch) and binary searches the list with `sub_4EEA10`, which orders
by element count first and then value by value. The index found picks `m_shaders[index]` of the scene
render pass. The values read as a light count in the low nibble plus one bit per light at 4, 14 and
24, so index i is i lights. That reading is inference from the values, the NUM_LIGHTS switch was not
decompiled. Over the archive: 530 materials with one context, 34,821 with two (lights 0 and 1), 9,128
with four, and every list is sorted so the original's search finds each of its own entries.

## 3. From a mesh to a draw command

`sub_5B9A90` records one mesh instance (DISPLAY_LIST.md). For each segment it takes
`materialSet[segment.m_materialIndex]` and calls `PRenderer__findShaderForPass 0x5BA690`:

1. If `material.m_remapFrom` is the current pass type, the pass type becomes `material.m_remapTo`.
   (verified, the `a6 + 8` / `a6 + 12` test.)
2. The variant's scene render pass of that type, through `m_sceneRenderPassLookup` when filled (it is
   empty in every file) or `PEffectVariant__findSceneRenderPass 0x4E71D0`.
3. The context index, then the PShader at `m_shaders + 32 * index`.

Before the call `sub_5B9A90` drops a material from the Opaque pass when its `m_remapFrom` is Transparent
and its `m_remapTo` is not. That is the Opaque / Transparent test SHADERS.md left open. No shipped
material sets a remap, so it never fires on shipped data.

`sub_5B5150` sizes the draw's buffers from `PShader::m_parameterBufferFrequenciesRequired`, and
`sub_5B5A50` fills them. The four bits are four parameter buffers, in this order everywhere they appear:

| bit | index | buffer | size | filled by |
|---|---|---|---|---|
| 0x1 | 0 | scene | the renderer's | `sub_4FDEC0`, renderer+88, cached per renderer |
| 0x2 | 1 | material | `m_parameterBufferSize` rounded to 16 | `sub_5B7F30` |
| 0x4 | 2 | variant | `m_untweakableParameterBufferSize` | `sub_5B7FE0`, World, WorldView and the other engine supplied types 128 to 145 |
| 0x8 | 3 | instance | `PShader::m_parameterBufferSize` | `sub_5B6CF0` |

So `PShader::m_parameterBufferSize` is the size of the per instance buffer, and
`m_parameterBufferFrequenciesRequired` says which of the four a shader needs. Both were open in
SHADERS.md.

`sub_5B7F30` is the material capture: it allocates `(m_parameterBufferSize + 15) & ~15` bytes of
frame memory, `memcpy`s the PParameterBuffer object from its first byte for `m_parameterBufferSize`
bytes, then walks the tweakable definitions and for each texture type (66, 67, 68, 18, 217 to 220)
writes the capture's `+4` with the texture's runtime handle (`m_texture + 8`), or 0. Nothing a constant
location reads changes.

`sub_5BA380` then queues one 72 byte command per PShaderPass of the PShader (`m_passes`, stride 596),
each holding the pass and the four buffer pointers at +40, +44, +48, +52.

## 4. The copy into cb0 (the routine SHADERS.md section 11 asked for)

On the render thread `sub_61C400` applies a command. When the pass changed it binds the four programs
(`sub_59AC40`) and the state objects (`sub_59AD20`), and every frequency counts as changed. Otherwise a
frequency is changed when its buffer pointer differs from the previous command's. For each changed
frequency f it calls `sub_59AB50(f)` for the constants and `sub_59B010(f)` for the textures, then
`sub_5934C0` commits each stage.

**The location tables.** Each PShaderPassD3D11 has eight 24 byte
`PShaderPassParameterLocationTypesConstantBuffer`, constants at 404, 428, 452, 476 for vertex,
fragment, geometry, compute and textures at 500, 524, 548, 572. The layout is

| offset | member | meaning |
|---|---|---|
| 0 | m_parameterStart, u16 x 4 | first location of each frequency |
| 8 | m_parameterCount, u16 x 4 | locations of each frequency |
| 16 | m_parameterLocations | PArray of 16 byte locations, all frequencies in one run |

SHADERS.md section 5 read start and count as single u16s. They are arrays of four, one per frequency,
which `sub_59AB50` indexes with `2 * f`. phyrechrlitshader's Opaque pass with no lights has, for both
the vertex and the fragment stage, starts (0, 19, 65, 70) and counts (19, 46, 5, 4).

A location (`PShaderParameterCaptureBufferLocationTypeConstantBuffer`) is `m_offset` u16 @0 into the
frequency's buffer, `m_constantBufferLocation` u32 @4, `m_size` u32 @8, `m_type` u8 @12.

**The copy**, `sub_59A650`, runs with the stage's program as `this`. For every location of the
frequency:

- skip it unless `m_constantBufferLocation` is below the program's `m_constantBufferSize` (+20) and
  below the shadow's length (+4). Only the start is checked, not the end.
- `m_type` 49: `m_size / 64` PMatrix4s, each transposed, stopping at an element whose first dword is
  0xFFFFFFFF.
- `m_type` 48: `m_size / 48` matrices of 4 rows by 3 floats, each written as 3 rows of 4, same stop.
- anything else: `memcpy` of `m_size` bytes.

The source is the frequency's buffer + `m_offset`, the destination the program's shadow (+8) +
`m_constantBufferLocation`. The shadow is a `PArray<PUInt8>` at +4, sized to `m_constantBufferSize`
when the program's buffer is created (`sub_58F490`: ByteWidth `m_constantBufferSize`, usage default,
bind flags constant buffer, debug name "PShaderProgramConstantBuffer").

**The upload.** `sub_593510` (vertex) and `sub_5932B0` (fragment) do, when `m_globalConstantBufferIndex`
(+24) is not 0xFFFFFFFF, `UpdateSubresource` of the whole shadow into the buffer, put the buffer in
slot `m_globalConstantBufferIndex` of an 8 entry array at +636, and bind all 8 with
`VSSetConstantBuffers` / `PSSetConstantBuffers`, then the 128 shader resources at +28 and the 16
samplers at +540. Every program a material reaches has `m_globalConstantBufferIndex` 0 or
0xFFFFFFFF, so for materials this is cb0.

The shadow keeps its bytes between draws. A byte no location of the current command writes holds what
the last draw with that program left there. The material frequency alone is fully static, which is
what `MaterialConstantBuffer` reproduces: the shadow zero filled, then the material locations copied
as above, with a mask of the bytes the material wrote.

**Textures.** `sub_59B3D0` walks the texture table the same way. `m_constantBufferLocation` is a slot,
0xFFFFFFFF skips. It switches on the capture's `m_parameterType` at the location's offset: 66, 67, 68
put the texture's view in shader resource slot `slot`, 71 puts `m_samplerState + 32` (the D3D11
sampler) in sampler slot `slot`, 72 to 78, 222 and 226 handle buffers and UAVs that no material uses.
A capture of type 0 binds nothing, the slot keeps the previous draw's resource. c001's first material
binds TextureSampler at t0 and TextureSamplerState at s0 in the pixel shader.

## 5. What the archive holds (phyrecheck materials, 2026-10-13)

47,095 clusters parsed, 30 foreign skipped, 2,059 clusters with materials, 136 seconds on 16 threads.

| | count |
|---|---|
| materials resolved | 44,479 of 44,479 |
| with an imported variant / local variant | 43,686 / 793 |
| parameters / constants / captures | 1,290,678 / 973,423 / 317,255 |
| Texture2D captures filled / empty | 45,539 / 22,125 |
| cube map captures filled / empty | 332 / 89 |
| sampler captures | 249,170 |
| texture captures that resolve, all imports | 45,871 |
| cb0 images built (every pass, every context, vertex and fragment) | 927,602 |
| locations copied | 32,188,878 |
| locations skipped, clipped, or reading past the block | 0, 0, 0 |
| material locations without a definition at their offset | 0 |
| cb slot 0 / no cb | 844,779 / 82,823 |
| texture bindings of type 66 / 68 / 71 / 0 | 335,308 / 592 / 512,969 / 200,321 |
| failures | 0 |

Every pass uses the material frequency in every constant table it has. 473,024 of the 927,602 stage
tables also use the instance frequency.

## 6. Post effects and occluders (PROGRESS 5.7)

phyre_reflection_meta.json registers the PPostProcessing classes (PPostEffectBase, PPostEffectManager,
PDeferredLighting, PDepthOfField, PMotionBlur, PScreenSpaceAmbientOcclusion, PMLAA, PGlow,
PLegacyGlow, PFXAA, PScreenSpaceReflection, PMeshParticleSystemBase and their Base and D3D11 forms)
and POccluderGeometryInstance and POccluderGeometryObject, with no reflected members for any of them.
`phyrecheck materials` looks for every class with those name prefixes in the class table of all
47,095 clusters and in their instance lists: none declares one and none holds one. The post effects
FFX draws are built in code over the .fx.phyre effects SHADERS.md section 9 lists (copybuffer,
phyreglow, phyrefxaa and the rest), and the IDB's `PPostEffectManager__getPostEffect 0xA16980` is
called only from `sub_A03DC0`. There is no post effect or occluder data to load.

## 7. Verified and inferred

Verified in IDA: the remap test and the Opaque skip in `sub_5B9A90` and `PRenderer__findShaderForPass`,
the frequency bits and sizes in `sub_5B5150` and `sub_5B5A50`, the memcpy of the whole object in
`sub_5B7F30`, the u16 x 4 tables and their offsets in `sub_59AB50` and `sub_59B010`, the copy, its
bounds and both transposes in `sub_59A650` (read in the disassembly, not only the decompiler), the
upload and slot binding in `sub_593510`, the buffer creation in `sub_58F490`, the texture switch in
`sub_59B3D0`, the context search and compare in `0x4EF440` and `sub_4EEA10`.

Verified against the files: everything in sections 1, 2 (the values) and 5. The cb0 images of c001's
first material and of bsil03 material 7:76 match a separate Python implementation byte for byte
(PhyreMaterialTest).

Inferred: that the render thread command is the queued 72 byte command shifted by 8 (the frequency
order agrees in both, the shift was not traced). That index i of a context list is i lights. That the
shadow's initial bytes are not zeroed (`sub_42FB80` was not read). That no header class child is
repacked for PParameterBuffer at load, from the file and runtime class sizes being equal.

## 8. Open

- What `sub_5B7FE0` writes for each engine supplied type 128 to 145 (World and the rest), and what the
  scene buffer `sub_4FDEC0` and the instance buffer `sub_5B6CF0` hold. Those are the other three
  frequencies of cb0.
- What NUM_LIGHTS packs, decompiled rather than read off the values.
- What `m_texAnimID` drives.

## 9. Proposed names

| address | proposed name | evidence |
|---|---|---|
| sub_5B9A90 | PRenderer__drawMeshInstance | per segment material from the set, PRenderer__findShaderForPass, the Opaque remap skip, queues the pass draws |
| sub_5B5150 | PRenderer__measureDrawBuffers | frequency bits 2, 4, 8 add m_parameterBufferSize rounded to 16, m_untweakableParameterBufferSize, PShader +28 |
| sub_5B5A50 | PRenderer__captureDrawBuffers | bit 1 scene buffer, bit 2 sub_5B7F30, bit 4 sub_5B7FE0, bit 8 sub_5B6CF0, into a[0..3] |
| sub_5B7F30 | PRenderer__captureMaterialBuffer | ring alloc of the rounded size, memcpy(object, m_parameterBufferSize), texture capture +4 patch |
| sub_5B7FE0 | PRenderer__captureUntweakableBuffer | alloc m_untweakableParameterBufferSize, switch on parameter types 128 to 145, BoneTransforms "Could not process skeleton matrices" |
| sub_5B6CF0 | PRenderer__captureInstanceBuffer | frequency 8 producer, called from sub_5B5A50 |
| sub_5BA380 | PRenderer__queuePassDraws | one 72 byte command per PShaderPass, stride 596, the four buffer pointers at +40..+52 |
| sub_61C400 | PRenderContextD3D11__applyDrawState | per frequency change mask, calls the constant and texture walks, then the stage commits |
| sub_59AB50 | PRenderContextD3D11__copyConstantLocations | the four constant tables at 404 to 476 with start and count indexed by frequency |
| sub_59A650 | PShaderProgramD3D11__copyParameterLocations | the copy into the program shadow, types 48 and 49 transposed |
| sub_59B010 | PRenderContextD3D11__bindTextureLocations | the four texture tables at 500 to 572 |
| sub_59B3D0 | PShaderProgramD3D11__bindCapturedTextures | switch on capture type 66 to 78, 222, 226, "Unknown texture type for captured texture" |
| sub_5934C0 | PRenderContextD3D11__commitPrograms | calls the four stage binds |
| sub_593510 | PShaderVertexProgramD3D11__bind | UpdateSubresource of the shadow, VSSetShader, VSSetConstantBuffers 8, VSSetShaderResources 128, VSSetSamplers 16 |
| sub_5932B0 | PShaderFragmentProgramD3D11__bind | the PS twin |
| sub_58F490 | PShaderProgramD3D11__createConstantBuffer | CreateBuffer ByteWidth m_constantBufferSize bind flags 4, "Constant buffer creation failed", "PShaderProgramConstantBuffer" |
| sub_59AC40 | PShaderPassD3D11__bindPrograms | VSSetShader, GSSetShader, PSSetShader, CSSetShader from the pass's programs |
| sub_59AD20 | PShaderPassStateD3D11__bindStates | lazily creates "RasterState", "BlendState", "DepthStencilState" then binds |
| sub_4EF3F0 | PEffect__buildContextKey | one virtual call per m_contextSwitches entry fills the key |
| sub_4EEA10 | PNodeContext__compare | count first, then value by value |
| sub_44A010 | PArray_PUInt8__resize | allocates the new size, frees the old storage |
