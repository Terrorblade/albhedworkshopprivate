# Shaders (.fx.phyre)

Every shader FFX HD draws with ships as a PhyreEngine effect cluster, `*.fx.phyre` under
`ffx_data/gamedata/ps3data/shaders/d3d11/` in FFX_Data.vbf. Each one carries compiled D3D11 DXBC,
the render state of every pass and the parameter layout a material fills. This doc covers what the
files hold, how the game reaches them, how a material ends up on one pass, and what it takes to load
them on D3D12. The cluster container itself is PHYRE_FORMAT.md.

Addresses are IDA virtual addresses at the preferred base 0x400000. The IDB was read only for this
work, so functions that are still `sub_XXXXXX` are quoted that way and the proposed names are in
section 10.

The machine-readable lists are in SHADERS_INVENTORY.md next to this file: one row per source, one per
distinct DXBC blob and one per .fx.phyre. It is generated, do not edit it by hand:

    python -I tools/phyre_shaders.py inventory --out <coop>/reversing/SHADERS_INVENTORY.md

(run from the projectzanarkand repo). No DXBC and no disassembly text is checked in anywhere.
`phyre_shaders.py disasm OUTDIR` writes the disassembly to a directory outside the repo when you
need to read it.

Related docs: PHYRE_FORMAT.md (the cluster format), DISPLAY_LIST.md (the pass type records the
renderer points at), RENDER_PASSES.md (which passes run per frame), RENDER_DEVICE.md (the D3D11
device).

## 1. The 34 and the 793

The earlier count of 34 shader files was the number of shader paths FFX.exe names as strings. The
archive holds 793 .fx.phyre clusters. They are 110 distinct shader sources, and the gap is material
instances.

| | count |
|---|---|
| .fx.phyre clusters | 793 |
| distinct sources (name before `.fx`) | 110 |
| sources FFX.exe names by path | 34 |
| distinct DXBC blobs, by MD5 of the bytes | 2,515 (491 vs, 2,024 ps) |
| program objects carrying those blobs | 12,607 |
| bytes of distinct DXBC | 14,109,248 |

A source compiled once with default switches gives one file, `copybuffer.fx.phyre`. A source that
models use as a material is compiled once per distinct switch set, and each result is its own cluster
named `<source>.fx#<MD5>.phyre`, the MD5 being the material instance id. phyredefaultlitshader alone is
337 files and phyrechrlitshader 220.

How each source is reached:

| loader | sources | files | what it is |
|---|---|---|---|
| exe | 34 | 34 | a path string in FFX.exe, section 2 |
| manifest | 63 | 63 | a line in `shaders/vfxshader_fx.txt`, the VFX shaders |
| import | 8 | 691 | a PAssetReferenceImport in a model cluster, the material instances |
| none | 5 | 5 | no reference found |

The import sources, with the number of model clusters (.dae and .ags) that import any of their files
and the PMaterial objects that point at them:

| source | files | importers | materials |
|---|---|---|---|
| phyredefaultlitshader | 337 | 340 | 34,329 |
| phyrechrlitshader | 220 | 820 | 5,943 |
| phyrewatershader | 74 | 101 | 252 |
| phyreuvanimationshader | 28 | 33 | 115 |
| phyreiceshader | 16 | 8 | 84 |
| phyreprerenderedshader | 10 | 118 | 2,953 |
| additiveeffectsshader | 4 | 4 | 8 |
| phyrewaveshader | 2 | 1 | 2 |

1,266 model clusters import an effect. Every import names a shipped file, none dangle.

The five with no reference are chreasyshadow, ssao, vfxadditivewaterwithnormalshader,
vfxwaterspecialshader and vfxwaterwithnormalshader. They may still be reached by a path built at run
time. That was not ruled out.

## 2. How the game reaches a source

**Path strings.** 34 strings at 0xB42708..0xB4BE18 name
`/FFX_Data/GameData/PS3Data/Shaders/GCM/<Name>.cgfx.phyre`, 33 of them `.cgfx` and
`PauseMenuShader.fx.phyre` the one `.fx`. These are the PS3 names. `FFX_fiosUnifyFilename 0x679820`
rewrites `.cgfx.phyre` to `.fx.phyre` and `GCM` to `D3D11` before the archive lookup, so they land on
the D3D11 files. Section 9 lists all 34 with their passes and entry points.

**The VFX manifest.** `sub_6C0FC0`, called from `FFX_Boot_FirstAsyncLoad` at 0x6364F1, reads
`shaders/vfxshader_fx.txt`. Each line is `Name.fx.phyre:FLAG|FLAG...` and `sub_6C0A70` turns the
flag names into a property word. The flags are VSP_ENABLE_TEXTURE, VSP_ZWRITE_ENABLE,
VSP_ALPHA_MODULATE, VSP_ALPHA_ADDITIVE, VSP_ZMAT, VSP_ALPHA_SUBSTRACT (and 2, 3), VSP_ZTEST_DISABLE,
VSP_ALPHA_NO_BLENDING, VSP_TK_RUNTIME_CLUT_16x16, VSP_ENABLE_NCLIP, VSP_EXTENDED_ALPHA,
VSP_SEA_WAVE_SPECIAL, VSP_POINTLIGHT and a dozen one-off specials (VSP_GRAY_SCALE, VSP_YIQ,
VSP_NOISE_BLUR and so on). The file's own header comment documents three bits: 0x80000000 water
special, 0x1 zMat, 0x8 texture. `vfxshader.txt` with the PS3 `.cgfx` names also ships, and nothing
was seen reading it. The 63 manifest sources are all shipped.

**Material imports.** A model cluster carries `PAssetReferenceImport` entries such as
`PS3Data/Shaders/PhyreChrLitShader.fx#FD2580E6C5B7BFC2656ED3C5A990EBB0` of type PEffectVariant.
The cluster with that MD5 in its name holds the variant, and also exports a PMaterial named
`...cgfx#<MD5>#material`. The model's PMaterial objects point at the imported variant.

`sub_6BFE90` is a text material parser (`texture_name`, `shader`, `PARAMETER_BEGIN` lines, shader
paths under Shaders/GCM/). It has no callers in this build.

`sub_69B940` is the cluster cache load the shader paths go through. It unifies the name, flags
`/Shaders/` paths and calls `PCluster__loadFromFile`.

## 3. Classes

One .fx.phyre holds one PEffect and one PEffectVariant, plus everything below them. The offsets are
the reflected ones from the cluster's own class table.

    PEffect                  m_effectVariants, m_maxLightCount @56, m_contextVariantSwitches @44
     -> PEffectVariant       m_switches @4, m_sceneRenderPasses @12, m_sceneRenderPassLookup @20,
                             m_largestShaderPassCount @28, tweakable / untweakable definitions @32 / @40,
                             their buffer sizes @48 / @50
         -> PSceneRenderPass (40 bytes)  m_passType @0 (PSceneRenderPassType user fixup by name),
                             m_shaders @4, m_entryPoints @12, m_variantsFoldingTable @20
             -> PShader (32 bytes)  m_parameterBufferFrequenciesRequired @0, m_passes @4,
                             m_parameterDefinitionsForPasses @12, m_streamDefinitionsForPasses @20,
                             m_parameterBufferSize @28
                 -> PShaderPass (596 bytes)  vertex, fragment, geometry, compute program @0/4/8/12,
                             m_streamLocations @16, m_state @24 (380 bytes),
                             8 parameter location blocks @404..@572
                     -> PShaderVertexProgram / PShaderFragmentProgram (1252 + layout)
                             m_compiledCode @12 (the DXBC), m_constantBufferSize @20,
                             m_globalConstantBufferIndex @24, m_shaderProfile @1244,
                             vertex only: m_inputLayout @1248

Counts over all 793 files: PEffect 793, PEffectVariant 793, PSceneRenderPass 3,566, PShader
10,786, PShaderPass 8,656, PShaderVertexProgram 3,958, PShaderFragmentProgram 8,649,
PShaderParameterDefinition 70,056, PMaterial 793. No geometry or compute programs ship.

`m_shaderProfile` reads as a big-endian fourcc, 0x76733430 `vs40` and 0x70733430 `ps40`.
`PShaderPassInfo` (the m_entryPoints elements) holds the HLSL entry point name and profile per stage,
which is where the entry point names in the tables come from.

A PSceneRenderPass holds one PShader per lighting context. phyrechrlitshader has m_maxLightCount 3
and four PShaders in each lit pass, whose constant buffers grow 160, 192, 208, 240 bytes. Unlit passes
of the same file repeat one identical PShader four times. Reading the count as 0..3 lights is
inference from those sizes, the context list was not decoded.

Seven PShaderPass objects in phyredepthoffield have no programs at all. Nothing in the variant
reaches them, they are leftovers in the file.

## 4. How a material picks a shader and a pass

The renderer holds the current pass type at renderer+72, a pointer to one of the 16-byte
`{name, 0, 0, index}` PSceneRenderPassType records listed in DISPLAY_LIST.md (0xC169B4 Opaque,
0xC169D4 Transparent and the rest). Each record is linked into a registry at load. For Opaque that
is the static initializer `sub_AE65D0`, which pushes 0xC169B4 onto the list head at 0xCA54F8. The
PSceneRenderPass m_passType user fixup in a cluster resolves to the record of the same name.

Per mesh, `sub_5B9A90` (the mesh draw record, see DISPLAY_LIST.md) reads renderer+72 and calls
`sub_5BA690` for each material:

1. Take the variant's `m_sceneRenderPassLookup[record.index]`. If the index is unset or the slot is
   empty, `sub_4E71D0` walks `m_sceneRenderPasses` (stride 40) for the entry whose m_passType is the
   record. No match means the material does not draw in this pass (return 7).
2. `sub_4EF440` binary-searches the PEffect's `m_contextVariantSwitches` for the current lighting
   context and returns its index, or -1.
3. The PShader is `renderPass.m_shaders[index]` (stride 32). Its m_passes run in order.

So a material picks its variant once, at import. The renderer picks the pass type per draw, and the
light context picks one PShader inside it. `sub_5B9A90` has an extra Opaque / Transparent test before
the call that was not decoded.

The render pass types that ship: Opaque, OpaqueNoMSAA, Transparent, TransparentNoMSAA, ZPrePass,
Shadow, ShadowTransparent, preTransparent, preTransparentATC, postOpaque, postTransparent and the
special ones of the post effects (CopyBuffer, GaussianBlur, RenderFXAA and so on, see section 9).

## 5. The parameter block

A material's values live in its PParameterBuffer. That is a header class (PHYRE_FORMAT.md section
7): `m_parameterBufferSize`, the variant pointer, the tweakable definitions, then a children block
of `m_parameterBufferSize` bytes holding every value at the offset its definition gives.

**PShaderParameterDefinition** (16 bytes)

| offset | member | meaning |
|---|---|---|
| 0 | m_arrayElementCount (u16) | 0 for a scalar |
| 2 | m_parameterType (u8) | see below |
| 3 | m_dataType (u8) | see below |
| 4 | m_name (PString) | the HLSL name |
| 8 | m_bufferLoc.m_offset, m_size (u16 each) | where the value sits in the PParameterBuffer block |
| 12 | m_constantBufferLocation (u32) | byte offset in the program's cb0, 0xFFFFFFFF for a texture or sampler |

Values seen over all 793 variants:

| m_parameterType | m_dataType | count | example | meaning |
|---|---|---|---|---|
| 64 | 0 / 1 / 2 / 3 | 7,077 / 838 / 1,456 / 8,244 | globalColorMask | material constant, float to float4 |
| 64 | 3, arrays | 4 | GaussianBlurOffsets | float4 array |
| 64 | 28 | 4 | g_numRing | int |
| 64 | 49 | 3,324 | f44ReservedVar1 | float4x4 |
| 66 | 52 | 1,955 | TextureSampler | 2D texture |
| 68 | 52 | 89 | CubeMapSampler | cube texture |
| 71 | 52 | 5,063 | globalSamplerState | sampler state |
| 130 | 49 | 768 | World | engine supplied |
| 131 | 49 | 732 | WorldView | engine supplied |
| 132 | 49 | 767 | WorldViewProjection | engine supplied |
| 133 | 49 | 693 | WorldInverse | engine supplied |
| 134 | 49 | 693 | WorldViewInverse | engine supplied |
| 143 | 49 | 121 | BoneTransforms (5,120 bytes) | engine supplied, skinning |

Types 64 to 71 are what the material sets, 130 and up the engine fills per draw. Texture and sampler
slots in the block are 16-byte capture objects:

| class | +0 | +4 | +8 | +12 |
|---|---|---|---|---|
| PShaderParameterCaptureBufferTexture2D | m_parameterType | m_textureBufferIndex | m_samplerState | m_texture (PTexture2D*) |
| PShaderParameterCaptureBufferSampler | m_parameterType (71) | m_textureBufferIndex | m_samplerState (PSamplerState*) | m_unusedPointer |

copybuffer.fx.phyre is the small worked example. Its block is 192 bytes with 11 definitions: float4
values at 128, 144, 160, 176, textures at 16, 32, 48, 80 and samplers at 64, 96, 112.

**Per pass locations.** Each PShaderPass has eight `PShaderPassParameterLocationTypesConstantBuffer`
blocks at 404..572, constants then textures for vertex, fragment, geometry and compute. Each has
`m_parameterStart` and `m_parameterCount` (u16 at 0 and 8) and `m_parameterLocations` at 16, an
array of 16-byte `PShaderParameterCaptureBufferLocationTypeConstantBuffer`: m_offset (u16, in the
material block) at 0, m_constantBufferLocation at 4, m_size at 8, m_type (u8) at 12. Read that as the
copy list from the material block into the stage's cb0. The copy routine itself was not traced.
`PShaderPassBase.m_streamLocations` is the same idea for vertex streams.

Every shipped blob uses at most one constant buffer, cb0. The most is 7 samplers and 9 textures in
one pixel shader. `PShader.m_parameterBufferSize` is not the cb0 size. copybuffer has 48 there while
its vertex shader declares a 64-byte cb0, so what it measures is open.

**Samplers.** PSamplerStateBase: m_minFilter, m_magFilter, m_wrapS, m_wrapT, m_wrapR (u8 at 0..4),
m_lodBias @8, m_maxAnisotropy @12, m_borderColor @16, m_baseLevel @20, m_maxLevel @24, m_flags @28.
`sub_595BF0` creates the D3D11 sampler. `sub_58AB10` maps wrap 1 -> WRAP, 3 -> BORDER, 4 -> MIRROR,
anything else -> CLAMP. `sub_58AA10` maps the filters, forces ANISOTROPIC (0x55) when the global
anisotropy level `flt_C25370` is above 1, and m_flags bit 0x40 selects the comparison filter.

## 6. The state a pass carries

`PShaderPassStateD3D11` at pass+24 is the D3D11 descriptions verbatim, member for member:

| offset in the state | member | type |
|---|---|---|
| 0 | m_importantState | u32, meaning unknown (copybuffer 29) |
| 4 | m_rasterDesc | D3D11_RASTERIZER_DESC, 40 bytes |
| 44 | m_depthDesc | D3D11_DEPTH_STENCIL_DESC, 52 bytes |
| 96 | m_blendDesc | D3D11_BLEND_DESC, 8 + 8 x 32 bytes |
| 376 | m_stencilRef | u8 |

`sub_58D4E0` builds the D3D11 objects from these on load: the rasterizer state (debug name
"RasterState"), a second rasterizer with FillMode 2 (wireframe), the blend state ("BlendState") and
the depth stencil state. 90 distinct state combinations ship.

To D3D12: the enum values are D3D11's, which D3D12 kept unchanged, so every field copies. The one
thing that does not is `ScissorEnable`. D3D12 always scissors, so a pass with ScissorEnable 0 needs
a full target scissor rect. Blend values 12 and 13 do not exist in either API and do not occur.
The phyrechrlitshader lit passes looked at ship `CullMode 2` (front) with `FrontCounterClockwise 0`. That is what the files
say, the winding was not checked against a draw.

**Input layout.** `sub_58D900` builds the D3D11 input layout from the vertex program's
`m_inputLayout`, an array of 20-byte PStreamInputDescD3D11: m_semantic, m_renderType (a
PRenderDataType name), m_semanticIndex, m_d3dFormat (a DXGI format), m_inputSlot. AlignedByteOffset
is always 0, so every element is its own vertex buffer. An element is per instance when its render
type's flag at +8 says so. Formats seen: 2 (R32G32B32A32_FLOAT), 6 (R32G32B32_FLOAT), 16
(R32G32_FLOAT), 30 (R8G8B8A8_UINT). Every vertex blob has exactly one layout across the corpus.

## 7. The programs

All 2,515 distinct blobs are standard DXBC containers, shader model 4.0 (vs_4_0 and ps_4_0), and all
name the same compiler in their RDEF chunk, "Microsoft (R) HLSL Shader Compiler 9.29.952.3111".

FFX.exe also embeds 79 DXBC blobs of its own (0xC2E2E4 onward, the first referenced from 0x595020).
12 of them were built with 9.30.9200.16384 and 67 with 9.29.952.3111, 74 ps_4_0 and 5 vs_4_0. None of
them is any shipped .phyre blob. So "9.30.9200.16384" belongs to the exe's built-in shaders, not to the
.fx.phyre files.

`sub_58D780` creates the vertex shader from m_compiledCode and refuses a profile the device feature
level cannot run ("Unable to compile vertex shader - profile not supported."). `sub_58D1C0`,
`sub_58D350` and `sub_58D030` do the same for pixel, geometry and compute. The call comes from
`PClusterD3D11__loadResourcePayload` at 0x466B66.

**ssao.** All six ssao.fx passes use `ssao_vert`, whose output is `POSITION`, not `SV_Position`.
Nothing can rasterize after it in D3D11 or D3D12, so these passes cannot have drawn anything as
shipped. ssao is also one of the five sources nothing references, which fits.

## 8. Loading on D3D12

D3D12 takes SM 4.0 DXBC as is. Nothing needs translating. `phyrecheck shaders` in projectzanarkand
parses every .fx.phyre, extracts every pass with the C++ reader (`zan::phyre::ShaderLibrary`),
builds a root signature from D3DReflect bindings and creates a pipeline per distinct (VS, PS, input
layout, state). Root signature shape: a root CBV per used b register per stage, then one descriptor
table each for SRVs, samplers and UAVs per stage. The results on 2026-10-12:

| | RTX 4060 Ti (Debug build, debug layer on) | WARP |
|---|---|---|
| distinct pipelines | 3,483 | 3,483 |
| created as the shipped VS + PS pair | 3,477 | 3,477 |
| failed | 0 | 0 |
| distinct root signatures | 32 | 32 |
| blobs not in any created pipeline | 0 of 2,515 | 0 of 2,515 |
| debug layer errors | 0 | no debug layer |

The six ssao pairs are the difference. Their vertex blob is proven in a pipeline of its own with no
pixel shader and the rasterizer unused. Each ssao pixel blob is proven behind a generated stand-in
vertex shader that writes its inputs plus SV_Position. The debug layer gave 3 warnings, all
"SV_TARGET0 writes 2 components" into the R8G8B8A8 test target. That is the test target, not the
shader.

The test uses R8G8B8A8_UNORM colour and D24_UNORM_S8_UINT depth for every pipeline. It proves the
blobs and states are valid D3D12. It does not prove the formats the game renders into.

## 9. The 34 sources FFX.exe names

Render pass types are the PSceneRenderPass m_passType names, entry points the vertex / pixel pairs
from PShaderPassInfo, blobs the distinct DXBC count. The other 76 sources are in
SHADERS_INVENTORY.md.

| source | render pass types | entry points (vs / ps) | blobs |
|---|---|---|---|
| abmapshadercone | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| abmapshaderwave | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| blendframe | BlendFrame | FullscreenVP / FullscreenFP | 2 |
| brightnessshader | BrightBlend | BrightBlendVS / BrightBlendPS | 2 |
| characterbtmtotop | Transparent | CharaterForwardRenderVS / ForwardRenderFP | 2 |
| chry0shadow | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| copybuffer | CopyBuffer, CopyDepthBuffer, CopyBufferWithAlphaBlend, CopyRGBBufferWithAlphaBlend, CopyBufferWithAdditive, CopyRGBBufferWithAdditive, ResolveDepthBuffer2, ResolveDepthBuffer4, ResolveDepthBuffer8 | FullscreenVP / CopyBufferFP, FullscreenVP / CopyDepthBufferFP, FullscreenVP / CopyBufferWithScaleFP, FullscreenVP / CopyRGBBufferWithScaleFP, FullscreenVP / ResolveDepthBuffer2FP, FullscreenVP / ResolveDepthBuffer4FP, FullscreenVP / ResolveDepthBuffer8FP | 8 |
| cutbuffer | CutBuffer | FullscreenVP / CopyBufferFP | 2 |
| hdao | HDAO_BufferGen, HDAO_BufferRender, HDAO_Combine, HDAO_Blur, HDAO_Blur_USM | VS_HDAO / PS_HDAO_BufferGen, VS_HDAO / PS_HDAO_BufferRender, VS_HDAO / PS_HDAO_Combine, VS_HDAO / PS_HDAO_Blur, VS_HDAO_USM / PS_HDAO_Blur_USM | 6 |
| magicfile249 | Transparent | CharaterForwardRenderVS / ForwardRenderFP | 8 |
| magicfile297 | Transparent | CharaterForwardRenderVS / ForwardRenderFP | 8 |
| menudefaultshader | Transparent, ScissorPass, ExtraPass | VFXDefaultVS / ForwardRenderFP, ExtraVS / ExtraFP | 4 |
| menudefaultshaderna | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| menudefaultshadernb | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| menudefaultshadernt | Transparent, ScissorPass | VFXDefaultVS / ForwardRenderFP | 2 |
| menudefaultshaderntnb | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| menudefaultshaderntztest | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| optimizedfontshader | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| pausemenushader | Transparent | VFXDefaultVS / ForwardRenderFP | 2 |
| phyreblur | ApplyBuffer, ApplyBufferWithColor, CopyBufferEx, CopyBufferExWithColor, RadialScratch, ApplyScratch | FullscreenVP / CopyBufferFP, FullscreenWithColorVP / ApplyBufferAdditiveFixFP, FullscreenWithColorVP / ApplyBufferFP, FullscreenVP / GenerateScratchFP | 6 |
| phyrebrokenscreen | CopyTexture, ApplyTexture, CopyTextureWithColor, ApplyTextureWithColor, CopyTextureWithAdditiveColor, CopyColor, ApplyColor, AdditiveColor, PaintColor | FullscreenVP / CopyBufferFP, FullscreenWithColorVP / CopyBufferWithColorFP, FullscreenWithColorVP / CopyBufferWithAdditiveColorFP, FullscreenWithColorVP / CopyPureColorFP, FullscreenVP / PaintPureColorFP | 7 |
| phyrecolorcorrection | ColorCorrection | FullscreenVP / ColorCorrectionFP | 2 |
| phyredepthoffield | GaussianBlur, CalculateFocusDistance, DownsampleColorBuffer, FullDofGPU, NoBlurTiledPass, FullBlurTiledPass, BetweenBlurTiledPass | GaussianBlurVP / GaussianBlurFP, FullscreenVP / CalculateFocusDistanceFP, FullscreenVP / DownsampleColorFP, FullscreenVP / FullDofGPUFP, TiledVP / NoBlurTiledFP, TiledVP / FullBlurTiledFP, TiledVP / BetweenBlurTiledFP | 10 |
| phyredistortionshader | DistortionCopyBuffer | FullscreenVP / CopyBufferFP | 2 |
| phyrefocuseffect | CopyRGBBufferWithAlphaBlend | FullscreenVP / CopyRGBBufferWithScaleFP | 2 |
| phyrefxaa | RenderFXAA, RenderRGBL | FxaaVS / FxaaPS, FxaaVS / PS_RenderRGBL | 3 |
| phyreglow | GenerateGlowMap, RenderGaussianBlurX, RenderGaussianBlurY, RenderGaussianBlurYAdditive, CopyGaussianBlurToScreen | GlowMapFullscreenVP / GlowMapFullscreenFP, GaussianUpscaleXVP / GaussianBlurUpscaleFP, GaussianUpscaleYVP / GaussianBlurUpscaleFP, GaussianUpscaleYVP / GaussianBlurUpscaleAdditiveFP, FullscreenVP / CopyToScreenFP | 7 |
| phyremlaa | RenderMLAA, RenderHorizVertMLAA, RenderNoMLAA, CopyNoMLAA, RenderEdgeDetect, RenderEdgeLength0, RenderEdgeLength | FullscreenVP / ApplyMLAAFP, TiledVP / HorizVertMLAAFP, TiledVP / NoMLAAFP, FullscreenVP / CopyBufferFP, FullscreenVP / EdgeDetectFP, FullscreenVP / RenderEdgeLength0FP, FullscreenVP / RenderEdgeLengthFP | 9 |
| phyretext | RenderText_AlphaBlend, RenderText_AlphaTest, RenderText_SoftEdges, RenderText_HardEdges, RenderText_SoftEdgesAndOutline, RenderText_SoftEdgesAndShadow, RenderText_SoftEdgesAndGlow | TextVP / TextFP, TextVP / TextAlphaTestFP, TextVP / TextSoftEdgesFP | 4 |
| screenfade | ScreenFade, ScreenFadeAdditive, ScreenFadeSubtract | FullscreenVP / CopyBufferFP | 2 |
| shadowblur | ShadowGaussianBlurX_FromDepth, ShadowGaussianBlurY_FromDepth, ShadowGaussianBlurX, ShadowGaussianBlurY, ShadowGaussianBlurY_Adv | GaussianUpscaleXVP / GaussianXBlurUpscaleFP_FromDepth, GaussianUpscaleYVP / GaussianYBlurUpscaleFP_FromDepth, GaussianUpscaleXVP / GaussianXBlurUpscaleFP, GaussianUpscaleYVP / GaussianYBlurUpscaleFP, GaussianUpscaleYVP_Adv / GaussianYBlurUpscaleFP_Adv | 8 |
| smaa | LumaEdgeDetection, ColorEdgeDetection, DepthEdgeDetection, BlendingWeightCalculation, NeighborhoodBlending, Resolve, Separate | DX10_SMAAEdgeDetectionVS / DX10_SMAALumaEdgeDetectionPS, DX10_SMAAEdgeDetectionVS / DX10_SMAAColorEdgeDetectionPS, DX10_SMAAEdgeDetectionVS / DX10_SMAADepthEdgeDetectionPS, DX10_SMAABlendingWeightCalculationVS / DX10_SMAABlendingWeightCalculationPS, DX10_SMAANeighborhoodBlendingVS / DX10_SMAANeighborhoodBlendingPS, DX10_SMAAResolveVS / DX10_SMAAResolvePS, DX10_SMAASeparateVS / DX10_SMAASeparatePS | 11 |
| unsharpmask | USM_Blur, USM_Apply | blur_vert / blur_frag, apply_vert / apply_frag | 4 |
| video | VideoScreen | VideoScreenVP / VideoScreenFP | 2 |

The 63 manifest sources are the vfx* family (vfxdefaultshader, vfxadditiveshader, vfxshapeshader,
vfxsubstract*, vfxzmatadditive* and so on). They all share the render pass set Transparent,
OptimizedTrans, DepthTestDisable, DepthTestDisableWithOptimize, with a few adding Opaque or the
stencil passes SetStencilFlag and EnableStencilTest. Each is 2 or 3 blobs.

## 10. Functions

None of these are named in the IDB yet. Proposed names:

| address | proposed name | evidence |
|---|---|---|
| sub_58D780 | PShaderVertexProgramD3D11__createResources | CreateVertexShader (device vtbl+0x30) on m_compiledCode, the vs50 / vs41 profile check against the feature level, the "Unable to compile vertex shader" string, called from PClusterD3D11__loadResourcePayload 0x466B66 |
| sub_58D900 | PShaderVertexProgramD3D11__createInputLayout | CreateInputLayout (vtbl+0x2C) over the 20-byte PStreamInputDescD3D11 records, AlignedByteOffset 0 |
| sub_58D1C0 | PShaderFragmentProgramD3D11__createResources | CreatePixelShader (vtbl+0x3C), "Unable to compile pixel shader" |
| sub_58D350 | PShaderGeometryProgramD3D11__createResources | the geometry twin of the two above |
| sub_58D030 | PShaderComputeProgramD3D11__createResources | the compute twin |
| sub_58D4E0 | PShaderPassStateD3D11__createStates | CreateRasterizerState (vtbl+0x58) twice, the second with FillMode 2, CreateBlendState (vtbl+0x50), CreateDepthStencilState (vtbl+0x54), debug names "RasterState" and "BlendState" |
| sub_595BF0 | PSamplerStateD3D11__createState | CreateSamplerState (vtbl+0x5C), debug name "PSamplerState" |
| sub_58AA10 | PSamplerStateD3D11__translateFilter | min / mag filter to D3D11_FILTER, anisotropic when flt_C25370 > 1, comparison on flag 0x40 |
| sub_58AB10 | PSamplerStateD3D11__translateWrap | 1 WRAP, 3 BORDER, 4 MIRROR, else CLAMP |
| sub_5BA690 | PRenderer__findShaderForPass | variant m_sceneRenderPassLookup by pass type index, fallback search, context index, returns the PShader at m_shaders + 32 x index |
| sub_4E71D0 | PEffectVariant__findSceneRenderPass | linear search of m_sceneRenderPasses (stride 40) by m_passType |
| sub_4EF440 | PEffect__findContextVariant | binary search of m_contextVariantSwitches (+44 count, +48 elements) |
| sub_AE65D0 | PSceneRenderPassType_Opaque__register | static init, links 0xC169B4 into the list at 0xCA54F8 |
| dword_CA54F8 | g_phyreSceneRenderPassTypeList | head of the pass type registry |
| sub_6C0FC0 | FFX_Vfx_LoadShaderManifest | reads vfxShader_fx.txt, called from FFX_Boot_FirstAsyncLoad 0x6364F1 |
| sub_6C0A70 | FFX_Vfx_ParseShaderProperty | VSP_ flag names to bits |
| sub_6BFE90 | FFX_MaterialText_Parse | texture_name / shader / PARAMETER_BEGIN, no callers |
| sub_69B940 | FFX_ClusterCache_Load | unify name, /Shaders/ flag, PCluster__loadFromFile |
| flt_C25370 | g_ffxAnisotropyLevel | read by sub_58AA10 |

## 11. Unresolved

- What `m_importantState` means.
- What `PShader.m_parameterBufferSize` and `m_parameterBufferFrequenciesRequired` measure.
- The routine that copies the material block into cb0 through the per pass location lists.
- How the lighting context in step 2 of section 4 is built, and whether the PShader count per pass
  is exactly lights 0..m_maxLightCount.
- Whether the five unreferenced sources are reached through a path built at run time.
- What renders into each pass type, which is the format question section 8 leaves open.

## 12. Materials and the cb0 copy (2026-10-13)

MATERIALS.md now covers the material side in full. What it changes here:

- **Section 5, per pass locations, corrected.** `m_parameterStart` and `m_parameterCount` are not
  single u16s. Each is four u16s, one per parameter frequency (scene, material, variant, instance), at
  +0 and +8 of the 24 byte table, and the location array at +16 holds all four runs. `sub_59AB50`
  indexes them with `2 * frequency`.
- **Section 5, the copy routine, found.** `sub_59A650` copies each location of a frequency into the
  program's constant buffer shadow (program +8, sized `m_constantBufferSize`), transposing type 49
  (PMatrix4) and type 48 (4x3) values, and `sub_593510` / `sub_5932B0` upload the whole shadow with
  `UpdateSubresource` and bind it at `m_globalConstantBufferIndex`. The material's source is the
  PParameterBuffer object copied whole from its first byte by `sub_5B7F30`.
- **Section 5 and 11, `PShader.m_parameterBufferSize` and `m_parameterBufferFrequenciesRequired`.**
  The second is a bit mask of the four frequency buffers (0x1 scene, 0x2 material, 0x4 variant, 0x8
  instance) and the first is the size of the instance buffer (`sub_5B5150`). It is not the cb0 size,
  which is the program's `m_constantBufferSize`.
- **Section 4, the extra Opaque / Transparent test.** `sub_5B9A90` skips a material in the Opaque
  pass when its `m_remapFrom` is Transparent and its `m_remapTo` is not, and
  `PRenderer__findShaderForPass` applies the remap before the pass lookup. No shipped material sets a
  remap.
- **Section 4 step 2, the lighting context.** `sub_4EF3F0` builds the key from the context switches
  (only NUM_LIGHTS ships) and `sub_4EEA10` compares a `PNodeContext::m_packedSwitches` by count then
  value. The packed values (0, 0x11, 0x4012, 0x1004013 for phyrechrlitshader) read as i lights for
  index i, which is inference from the values.
- **Section 5, the parameter block.** Definition offsets count from the first byte of the
  PParameterBuffer object, so the first value sits at 16. `m_parameterBufferSize` is the stored object
  rounded up to 16 and equals the variant's tweakable size for every material.

`phyrecheck materials` resolves all 44,479 materials and builds 927,602 per pass cb0 images with no
failure. The proposed names for the functions above are in MATERIALS.md section 9.
