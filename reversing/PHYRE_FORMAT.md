# PhyreEngine cluster format (.phyre)

FINAL FANTASY X HD Remaster ships every model, texture, shader and animation set as a
PhyreEngine 3.9 "cluster" file with the extension .phyre, 47,125 of them inside FFX_Data.vbf
under ffx_data/gamedata/ps3data/. This document is the on-disk layout as the loader in FFX.exe
reads it. Every claim names the function that proves it. Addresses are IDA virtual addresses at
the preferred base 0x400000. Decompiled names that are still sub_XXXXXX in the IDB are quoted as
such, with the names proposed for them at the end. Those names were applied to the IDB on
2026-10-12 after the coordinator re-read the header load, the platform and physics ids, the +144
header class test, the varint reader, the pointer fixup strings and the file open in IDA.

The reference reader is tools/phyre.py in the Project Zanarkand repo. It parses 47,095 of the
47,125 clusters to the last byte with every size, count and fixup cross checked, and rejects the
other 30 for the same reason the game does (section 12).

Related docs: README.md (reflection extraction, phyre_reflection_meta.json), MAP.md (the Phyre
entries), VBF_FORMAT.md (the archive the clusters live in).

## 1. File map

A cluster is one contiguous stream, read front to back by sub_57BCE0 (the PCluster load from a
PStreamReaderFile) and its helpers. Nothing is seekable or indexed, every section's size comes
from the header or from a previous section.

| order | section | size | read by |
|---|---|---|---|
| 1 | PClusterHeader | 84 | sub_57BCE0, sub_57DD30(reader, header, 84) |
| 2 | packed namespace | m_packedNamespaceSize | sub_57BCE0, unpacked by sub_47C8B0 |
| 3 | instance list headers | 36 x m_instanceListCount | sub_57C130 -> sub_57C4A0 |
| 4 | object data | m_totalDataSize | sub_57C130 -> sub_57C940 |
| 5 | user fixup data | m_userFixupDataSize | sub_57C5D0 |
| 6 | user fixup entries | 12 x m_userFixupCount | sub_57C5D0 |
| 7 | header class child counts | 4 x m_headerClassInstanceCount | sub_57B6C0 |
| 8 | header class children | 16 x m_headerClassChildCount | sub_57B6C0 |
| 9 | pointer array fixup stream | m_pointerArrayFixupSize | sub_57ACD0 -> sub_60DAC0 -> sub_60A340 |
| 10 | pointer fixup stream | m_pointerFixupSize | sub_57A6B0 -> sub_60DB20 -> sub_60A750 |
| 11 | array fixup stream | m_arrayFixupSize | sub_57A320 -> sub_60DAC0 -> sub_60A340 |
| 12 | index buffer blob | m_indexBufferSize | sub_465AA0 |
| 13 | vertex buffer blob | m_vertexBufferSize | sub_465AA0 |
| 14 | texture mip chains | derived from the texture objects | sub_465AA0 |

The file ends after the last mip level. The tool checks this on every cluster and it holds for
all 47,095 D3D11 clusters, which is the proof that no section is missing from the table.

All integers are little endian 32 bit unless stated otherwise. The two exceptions are the varint
fixup streams (section 8) and the PAssetReferenceImport payload (section 6).

## 2. PClusterHeader (84 bytes)

PClusterHeaderBase is 72 bytes, constructed by sub_468670. The D3D11 build appends
PClusterHeaderD3D11, 12 bytes, constructed by sub_469980. sub_57BCE0 reads exactly 84 bytes and
only handles m_size == 84 without a resize path being taken (the other branches exist for
headers of a different size and are never hit by shipped files).

| offset | field | value or meaning |
|---|---|---|
| 0 | m_phyreMarker | 0x50485952, the bytes "RYHP" in file order. 0x52594850 means a big endian file, sub_57BCE0 swaps the header with sub_43D6A0 and carries on to the platform check |
| 4 | m_size | 84 |
| 8 | m_packedNamespaceSize | bytes of section 2, must be nonzero |
| 12 | m_platformID | 0x44583131, bytes "11XD" in file order, from sub_464710. Anything else fails with error 6 |
| 16 | m_instanceListCount | number of 36 byte list headers |
| 20 | m_arrayFixupSize | bytes of the array fixup stream |
| 24 | m_arrayFixupCount | decoded 16 byte entries in it, equals the sum of the lists' m_arrayFixupCount |
| 28 | m_pointerFixupSize | bytes of the pointer fixup stream |
| 32 | m_pointerFixupCount | decoded 28 byte entries, sum of the lists' m_pointerFixupCount |
| 36 | m_pointerArrayFixupSize | bytes of the pointer array fixup stream |
| 40 | m_pointerArrayFixupCount | decoded 16 byte entries, sum of the lists' m_pointerArrayFixupCount |
| 44 | m_pointersInArraysCount | total pointer slots living in PArray<T*> storage, sum of the lists' field |
| 48 | m_userFixupCount | 12 byte entries in section 6 |
| 52 | m_userFixupDataSize | bytes of section 5 |
| 56 | m_totalDataSize | bytes of section 4 |
| 60 | m_headerClassInstanceCount | number of instance lists whose class is a header class (section 7) |
| 64 | m_headerClassChildCount | 16 byte entries in section 8 |
| 68 | m_physicsEngineID | 0 or 0x424C4554 ("TELB", Bullet) from sub_5F0490, anything else fails |
| 72 | m_indexBufferSize | PClusterHeaderD3D11, bytes of section 12 |
| 76 | m_vertexBufferSize | bytes of section 13 |
| 80 | m_maxTextureBufferSize | the largest single mip level in section 14, used to size a staging buffer, 0 when there are no textures |

The offsets 48, 52, 60, 64 are read as such by sub_57C5D0 and sub_57B6C0 (`*(header + 48)` and so
on), 28 and 32 by sub_57A6B0, which fixes the field order independently of the reflection dump.

## 3. Packed namespace

The namespace is the reflection data for every class the file uses, so a cluster is self
describing. sub_47F9D0 validates it, sub_47C8B0 unpacks it against the runtime namespace (the
result is the per class resolver table that sub_57B3A0, sub_57B6C0 and the fixup appliers consult).

Header, 32 bytes:

| offset | field |
|---|---|
| 0 | marker 0x01020304 |
| 4 | total size, must equal 32 + 4 x types + 36 x classes + 24 x members + stringSize + extraSize (sub_47F9D0) and m_packedNamespaceSize |
| 8 | type count |
| 12 | class count |
| 16 | member count |
| 20 | string table size |
| 24 | unused, 0 in every shipped file |
| 28 | extra (default data) size, 0 in every shipped file |

Then, back to back: the type table, the class table, the member table, the string table, the
extra data.

Type table: one dword per type, a string table offset. Types are the primitives and the opaque
enums the members use: float, PUInt32, PUInt8, PInt32, PUInt16, bool, PChar, PUInt128, and type
objects such as PRenderDataType or PTextureFormatBase that user fixups resolve by name.

Class table: 36 bytes per class, 9 dwords. Classes are numbered from 1 in the file.

| dword | meaning |
|---|---|
| 0 | super class index, 1 based, 0 for a root |
| 1 | low 28 bits the object size, high 4 bits log2 of the alignment (sub_57B6C0 masks 0xFFFFFFF, sub_57B3A0 shifts by 28 for the allocator) |
| 2 | name, string table offset |
| 3 | member count, the members are the next that many entries of the member table |
| 4, 5, 6 | three words the parse does not consume. 0 for plain structs, 4 4 4 on PCameraProjection, 0 4 4 on its subclasses, 24 24 24 on PClassDescriptor |
| 7 | class flags, a copy of the runtime PClassDescriptor flags word at +144 (section 3.1) |
| 8 | 1 based offset into the extra data for the class defaults, 0 for none |

Member table: 24 bytes per member, 6 dwords, in class order.

| dword | meaning |
|---|---|
| 0 | name, string table offset |
| 1 | type reference (section 3.2) |
| 2 | byte offset inside the object, absolute, there is no vptr in the file image |
| 3 | size. For a trailing fixed array this is the element size (PMatrix4.m_elements is a 4 byte float inside a 64 byte class) |
| 4 | flags. Bit 1 (0x2) marks a pointer member and is the bit the fixup path cares about. Values seen: 0x00, 0x02, 0x08, 0x10, 0x12, 0x16, 0x30 |
| 5 | unused by the loader, 0 |

Strings are NUL terminated ASCII, offsets are from the start of the string table.

### 3.1 Class flags

sub_43B0A0 (Phyre__PClassDescriptor__Init) zeroes the word at +144 of every descriptor, and the
lazy getter of each class ORs its bits in afterwards. The packed class dword 7 carries the same
bits, which is what makes the file readable without the runtime:

| bit | meaning | proved by |
|---|---|---|
| 0x1 | dynamic class, not compiled into the runtime | sub_47C190, no shipped file sets it |
| 0x2 | abstract, PClassDescriptorAbstract | every getter sets it during Init, concrete classes clear it (PNode__GetClassDescriptor 0xAE76D0) |
| 0x4 | header class: each object carries typed children after its fixed part (section 7) | sub_57B6C0 tests +144 & 4 to consume one child count per list. sub_57B3A0 fails with error 6 when a class with this bit is missing at runtime |
| 0x8 | no default constructor, PClassDescriptorWithoutDefaultConstructor | PMaterial__GetClassDescriptor 0xAE5E70 writes `flags & 0xFFFFFFF5 | 8` |
| 0x10 | children may be repacked to their runtime alignment | sub_57B6C0 only moves children when +144 & 0x10 is set, seen on PAnimationClipBinding and PModifierNetworkInfoPacket (0x14) |

Shipped values: PParameterBuffer 0x0C, PAnimationClipBinding and PModifierNetworkInfoPacket 0x14,
PAssetReference, PMaterial, PMeshInstance, the shader program classes 0x08, PCameraProjection,
PClassDescriptor, PDynamicMesh, PParameterBufferBase 0x02. These three header classes are the only
ones in the archive.

The earlier reading that the Init flags argument (stored at +132) marks header classes was wrong.
+132 holds the a7 argument of sub_43B0A0 (4 on PNode and PCameraProjection), and PNode is not a
header class. The header class bit is +144 bit 2, written after Init.

### 3.2 Type references

sub_47E730 resolves a type reference from a member, a user fixup or a header class child: a value
below the type count indexes the type table, anything else is a class, index `ref - typeCount - 1`
into the class table (0 based). Example: with 15 types, ref 3 is types[3], ref 27 is classes[11].

### 3.3 Object layout rules the tables imply

The object image is the 32 bit PC layout. Member offsets are absolute, base class members first
(sub_47C8B0 walks super classes root first). Every pointer is 4 bytes. The two reflected
containers are

- PArray<T>: 8 bytes, m_count at +0 and m_els at +4. The element type is in the class name.
- PString: 4 bytes, m_buffer, a pointer to a NUL terminated string in the array data.
- PSharray<T>: 24 bytes, only seen as PDataBlockD3D11::m_buffers, never patched by a fixup.

## 4. Instance lists

Objects are grouped by class into instance lists. One list holds m_count objects of one class,
with the class's array data behind them. sub_57C4A0 reads m_instanceListCount headers of 36 bytes
(PInstanceListHeader, nine dwords):

| offset | field | rule |
|---|---|---|
| 0 | m_classID | 1 based class index |
| 4 | m_count | objects |
| 8 | m_size | m_objectsSize + m_arraysSize |
| 12 | m_objectsSize | m_count x classSize, larger for a header class (section 7) |
| 16 | m_arraysSize | bytes of PArray and PString storage behind the objects |
| 20 | m_pointersInArraysCount | pointer slots in this list's PArray<T*> storage |
| 24 | m_arrayFixupCount | entries of this list in the array fixup stream |
| 28 | m_pointerFixupCount | entries in the pointer fixup stream |
| 32 | m_pointerArrayFixupCount | entries in the pointer array fixup stream |

The header totals (section 2) are the sums over the lists. A class may own several lists:
PParameterBuffer appears once per material, 282 lists in bsvr01.dae.phyre.

### 4.1 Placing the lists in the object data blob

Section 4 is a single blob of m_totalDataSize bytes. sub_57B3A0 allocates each list with the
bump allocator sub_440CF0, aligned to the class alignment, in list order, and when every class is
compiled into the runtime (the usual case, flags & 0x80000005 == 0) it points the runtime list
straight at the loaded bytes (sub_440BA0). So the file position of list i is

    cursor = alignUp(cursor, align_i)
    listData_i = objectData + cursor
    arrays_i = listData_i + m_objectsSize_i
    cursor += m_size_i

and the final cursor equals m_totalDataSize. The tool checks this on every file. The alignments
seen are 1, 4 and 16.

Object j of a list starts at listData + j x classSize. Array data offsets in fixups are relative
to arrays_i.

## 5. Objects reading themselves: the fixup model

The file stores the objects exactly as they sit in memory on a 32 bit PC, with every pointer slot
zero. Three packed streams (section 8) say which slots to patch and with what:

- array fixups patch a PArray<T>::m_els or PString::m_buffer with `arrays_i + dataOffset`
- pointer fixups patch any pointer slot with the address of (list, object, byte offset), or with
  the result of a user fixup
- pointer array fixups give a PArray<T*> a separate zeroed buffer of `count` pointers, which the
  pointer fixups then fill element by element

A fixup names its slot with one dword: bit 31 set means the low 31 bits are a byte offset into the
object, otherwise the dword is a global index into the member table and the slot is that member's
offset (sub_57A320 for arrays, sub_57A280 looks the member up for pointers). Both encodings occur,
raw offsets are the majority (805,398 of 867,908 array fixups across the archive).

## 6. User fixups

A user fixup is a pointer the file cannot express as (list, object): a class descriptor, a
texture format, a render data type, an import. Section 5 is a blob and section 6 has
m_userFixupCount entries of 12 bytes, read by sub_57C5D0:

| dword | meaning |
|---|---|
| 0 | type reference of the pointee |
| 1 | payload size |
| 2 | payload offset into section 5 |

sub_57C5D0 resolves each entry when it reads it: it looks the type up with sub_47E730 and calls
the type's resolver through sub_437980 (the function pointer at +40 of the PType or
PClassDescriptor) with the payload. For every type but one the payload is a NUL terminated name
and the resolver is a lookup by name: PClassDescriptor (class name), PTextureFormatBase (format
name, sub_4C0F10), PRenderDataType ("Vertex", "SkinnableNormal", "ST"), PSceneRenderPassType,
PLightType, PShadowCasterType, PContextSwitch, PAnimationKeyDataType, PModifier.

The exception is PAssetReferenceImport (descriptor 0xC94030). sub_57C5D0 keeps the payload raw
and flags the entry (word 3 of its 16 byte runtime record set to 1), and sub_57A6B0 resolves it
when it applies the pointer fixup that uses it. The payload is always 2 bytes, a big endian u16
index into the cluster's own PAssetReferenceImport instance list (the import whose m_id names the
external asset). This is the only big endian scalar in a D3D11 cluster, inherited from the PS3
tool chain. 24,636 such fixups across the archive, every one 2 bytes and in range.

Pointer fixups refer to user fixups by index (section 8.4). The tool prints them as
`user<PClassDescriptor>'PTexture2D'` or `import #3`.

## 7. Header classes and their children

A header class (flag 0x4) is a class whose objects are followed by a variable block of typed
values. The only shipped ones are PParameterBuffer (the shader constant block of a material,
44,479 lists), PModifierNetworkInfoPacket (982) and PAnimationClipBinding (95). Every shipped
header class list holds exactly one object.

For such a list m_objectsSize exceeds m_count x classSize and the surplus is the children block.
Which lists are header class lists is decided by the class flag, not by the surplus: one
PAnimationClipBinding list in bsvr01.dae.phyre has no children at all and is still counted in
m_headerClassInstanceCount.

sub_57B6C0 reads section 7, one dword per header class list in list order (the number of child
entries for that list), then section 8, 16 bytes per child:

| dword | meaning |
|---|---|
| 0 | type reference of the child (a primitive such as float, or a class) |
| 1 | byte offset from the start of the list's object data (listData_i, not from the object) |
| 2 | flags. Bit 1 (0x2) set means the loader skips the child |
| 3 | element count |

A PParameterBuffer in c001.dae.phyre has 55 children: float x 16 at 528 (a matrix), float x 4 at
992 (a colour) and so on, 1108 bytes after the 16 byte fixed part. The counts in section 7 sum to
m_headerClassChildCount, which the tool checks.

When the runtime class is larger than the file's, sub_57B6C0 prints "Header class '%s' is larger
at runtime than stored in the file" and fails with 9. For child classes with a different runtime
layout it copies each child element through sub_5788F0. Both are runtime concerns, the file
layout is fixed.

## 8. The packed fixup streams

The three streams are the only compressed part of the format. Each is a sequence of groups, and
each group expands to a run of fixed size entries: 16 bytes for array and pointer array fixups
(sub_60A340), 28 bytes for pointer fixups (sub_60A750). A stream holds the entries of every list
in list order, and the decoder is called once per list with that list's count and object count
(sub_60DAC0 for the 16 byte kinds, sub_60DB20 for the 28 byte kind), so a reader must track where
one list's entries end and the next begin. The tool checks that the final position equals the
stream size on every file.

### 8.1 Varints and the first value

Every number is a little endian base 128 varint (sub_60AC00): seven payload bits per byte, low
bits first, the high bit set on every byte but the last. The engine keeps the low 32 bits.

The first varint of every group is the slot dword of section 5 rotated right by one bit, so the
bit 31 raw offset flag costs one bit instead of five bytes. Undo it with
`(v >> 1) | (v << 31)`.

### 8.2 The group header byte

| bits | meaning |
|---|---|
| 0..2 | mode, how object indexes are enumerated (8.3) |
| 3 | 0x08: the per entry count is absent (PString arrays, for the 16 byte kind) or the pointer index is absent (28 byte kind) |
| 4 | 0x10: 28 byte kind, no user fixup field in the tail |
| 5 | 0x20: 28 byte kind, the target list is read once after the slot word and shared by the group |
| 6 | 0x40: 28 byte kind, no target offset in the tail |

The decoders also force two flag bits: bit 0 is always set and bit 1 (0x02) is set when the list
has exactly one object, which makes mode 5 omit the object index.

### 8.3 Modes and index sets

After the header byte and the slot word (and the shared target list when 0x20 is set) comes one
of:

| mode | encoding |
|---|---|
| 0 | one tail per object, objects 0 .. count-1 in order |
| 1 | runs until every object has an entry: a submode byte, one tail, then an index set in that submode. Every index in the set gets a copy of the tail |
| 2 | an explicit index set, then one tail per index |
| 3 | an exclusion index set, then one tail per index |
| 4 | a bitmap index set, a tail after each index |
| 5 | a varint n, then n entries, each an object index varint (omitted when the single object bit is set) and a tail |
| 6 | an arithmetic progression index set, a tail after each index |

Index sets (sub_60CE70, sub_60CC20, sub_60CAC0, sub_60D000) use the same numbers:

| set | encoding |
|---|---|
| 2 | varint n, then n indexes |
| 3 | varint n, then n excluded indexes in ascending order, the set is every other object |
| 4 | ceil(objectCount / 8) bytes, bit i (byte i >> 3, bit i & 7) selects object i |
| 6 | varints start, step, n, the set is start + k x step |

An index is one byte when the list has fewer than 0x100 objects, otherwise a varint.

The archive uses every mode and set: the breakdown of 700,000 odd groups is in the tool's
`all` report. Mode 0 with no count (0x08) is the common array case, mode 0 with 0x58 the common
pointer case.

### 8.4 The entries

16 byte entry (array and pointer array fixups), sub_60A340:

| field | source |
|---|---|
| slot word | the group's first varint, un-rotated |
| object index | from the mode |
| count | varint in the tail unless 0x08 |
| data offset | varint in the tail, relative to the list's array data |

28 byte entry (pointer fixups), sub_60A750 with the tail read by sub_60E730:

| field | source |
|---|---|
| slot word | the group's first varint, un-rotated |
| object index | from the mode |
| user fixup | unless 0x10: varint u, 0 means none, else user fixup u-1 and the next three fields are skipped |
| target object | varint |
| target list | varint unless 0x20, then the group's shared value |
| target offset | varint unless 0x40, bytes into the target object |
| pointer index | varint unless 0x08 |

The pointer index means two things. For an element of a PArray<T*> it is the element's position
in the separate pointer buffer (sub_57A6B0 writes `buffer + 4 x index`). For a plain pointer slot
that is the m_els of a PArray<T> whose elements are objects of another instance list, it is the
element count and sub_57A6B0 hands it to the array setter (vtable +32) together with the target:
PParameterBuffer::m_tweakableShaderParameterDefinitions in c001.dae.phyre is count 55, target
PShaderParameterDefinition list 38 object 0, pointer index 55.

### 8.5 Applying them

- sub_57A320 (array fixups): slot = arrays_i + dataOffset. For PString only the pointer is
  written, which is why those groups carry no count.
- sub_57ACD0 (pointer array fixups): allocates one buffer of 4 x m_pointersInArraysCount bytes per
  cluster, hands each PArray<T*> `count` consecutive slots of it in fixup order, and writes the
  buffer address into the slot. The lists' m_pointersInArraysCount fields are the per list sums of
  these counts, which the tool checks.
- sub_57A6B0 (pointer fixups): slot = address of (target list, target object) + target offset, or
  the resolved user fixup. sub_5765E0 maps (list, index) to the object.

## 9. The D3D11 resource payload

Two corrections from 2026-10-12 are in section 15.7: no standalone PIndexDataBlock ships (the index
block is embedded in PMeshSegmentD3D11 at +48), and PTextureFormat__ctor stores five values, not four.

sub_465AA0 (the PClusterD3D11 payload loader) runs after the fixups, reading from the same stream:

1. m_indexBufferSize bytes: one blob holding every index buffer. Each PIndexDataBlock object
   (file class PIndexDataBlock, 60 bytes, deriving PIndexDataBlockD3D11 then PIndexDataBlockBase)
   addresses its slice with m_offsetInIndexBuffer at +44 and m_dataSize at +52. sub_561E90 creates
   the D3D11 buffer from the slice.
2. m_vertexBufferSize bytes: one blob holding every vertex buffer. PDataBlock (64 bytes, deriving
   PDataBlockD3D11 then PDataBlockBase) uses m_offsetInVertexBuffer at +48 and m_dataSize at +56,
   with m_stride at +0, m_elementCount at +4 and m_streams (a PArray<PVertexStream>) at +8
   describing the layout. sub_5620C0 creates the buffer.
3. The mip chains of every texture object, in this order: all PTexture2D lists, then PTexture3D,
   then PTextureCubeMap, list order within each, object order within a list. There is no size
   field, the reader derives each level from the object:
   - m_format at +0 (PTextureCommonBase), a pointer patched by a user fixup of type
     PTextureFormatBase whose payload is the format name
   - m_mipmapCount at +12, the chain holds m_mipmapCount + 1 levels
   - m_width at +28 and m_height at +32 (PTexture2DBase), m_depth at +36 for a 3D texture. A cube
     map has one PTextureCubeMapBase::m_size at +28 for both
   - level bytes = w x h x bitsPerPixel / 8 (x depth for 3D), with w and h rounded up to a
     multiple of 4 for block compressed formats, each dimension halving per level down to 1
   - a cube map stores six full chains, one per face

Formats: the PTextureFormat static objects are built by the init stubs between 0xAE49E0 and
0xAE5000 through sub_5844A0(name, id, flags, four DXGI formats), and sub_4C0C00 turns the id into
bits per pixel (+12) and a flags word (+16, bit 0 = block compressed). The archive uses five:
DXT5 (id 17, 8 bpp, block), DXT1 (15, 4 bpp, block), ARGB8 (12, 32 bpp), DXT3 (16, 8 bpp, block)
and L8 (0, 8 bpp). The full id table is in tools/phyre.py (TEXTURE_FORMAT_IDS and
BITS_PER_PIXEL_BY_ID). The getter the reflection meta records for m_format (0x4C0A10) is a
PSamplerState function, the texture format getter is Phyre__PType__PTextureFormatBase at
0x4C0B10.

Example, chr/pc/c801/tex/d3d11/c801.dds.phyre: PTexture2D DXT1 8x8, m_mipmapCount 3, levels of
32 + 8 + 8 + 8 = 56 bytes at file offset 2664, file size 2720.

## 10. What a 64 bit reader has to translate

Everything the loader does in place depends on the 32 bit layout, so a 64 bit port cannot point a
runtime object at the file bytes the way sub_440BA0 does. The list:

- Every pointer slot is 4 bytes and zero on disk. PArray<T> is 8 bytes (count, pointer),
  PString 4. The member offsets and class sizes in the namespace are the 32 bit ones. A reader
  must either keep the 32 bit image and resolve fixups into its own handles (what the Zanarkand
  reader does, ObjectRef = list + index), or lay every class out again from the member table
  with 8 byte pointers and apply the fixups to the new offsets.
- Class alignment comes from the packed class (1, 4 or 16), not from the runtime type.
- Header class children offsets are 32 bit list relative offsets. They stay valid in the file
  image.
- Fixup slot words are 32 bit member offsets. Resolve them through the member table (section 5)
  rather than through runtime types.
- The pointer index of a PArray<T*> element addresses a buffer of 4 byte slots in the original.
- The PAssetReferenceImport payload is big endian.
- m_maxTextureBufferSize is a staging buffer hint sized in 32 bit bytes, harmless.
- The user fixup resolvers are runtime lookups by name. A reader keeps the names.

## 11. Results over the archive

`python -I tools/phyre.py all` over FFX_Data.vbf (2026-10-12, 16 worker processes, 9 seconds):

- tried 47,125, parsed 47,095, rejected 30 (section 12)
- 11,770,046,810 bytes, 171,974 instance lists, 3,597,315 objects
- by file kind: .dds 44,364 (single textures), .dae 1,314 (models and maps), .fx 793 (shaders),
  .ags 623 (sprite and texture animation sets), .fgen 1 (the bitmap font)
- textures: 44,365, DXT5 29,455, DXT1 13,511, ARGB8 1,062, DXT3 335, L8 2. 44,255 PTexture2D and
  110 PTextureCubeMap, no PTexture3D
- user fixup types: PClassDescriptor 54,892, PTextureFormatBase 44,365, PAssetReferenceImport
  24,636, PRenderDataType 11,544, PSceneRenderPassType 3,566, PLightType 601, PShadowCasterType
  601, PContextSwitch 601, PAnimationKeyDataType 258, PModifier 70
- no dynamic classes, no namespace extra data
- header class lists: PParameterBuffer 44,479, PModifierNetworkInfoPacket 982,
  PAnimationClipBinding 95 (one without children)
- the most populated classes (objects, files): PShaderParameterCaptureBufferLocationTypeConstantBuffer
  1,427,260 in 793, PShaderParameterDefinition 409,885 in 2,058, PSamplerState 293,281,
  PVertexStream 241,667, PDataBlock 234,142, PMatrix4 159,189, PAssetReference 118,551 in 46,427,
  PNode 101,465 in 1,270, PString 62,510, PSkinBoneRemap 49,543, PTexture2D 44,255, PMaterial
  44,197, PMeshSegment 39,310, PMesh 12,235, PMeshInstance 11,255, PAnimationClip 94,
  PBitmapFont 1

The full report with every class and every fixup encoding count is written by `--report`.

## 12. The 30 rejected clusters

ffx_data/gamedata/ps3data/magic/magic_0668/tex/d3d11/*.dds.phyre, 30 files, are PS3 clusters that
were never converted: the marker is byte swapped and the swapped header reads m_size 88 and
m_platformID 0x47434D00 ("GCM" and a NUL in file order). sub_57BCE0 swaps the header with
sub_43D6A0, compares m_platformID with sub_464710 and returns error 6. The game cannot load them
either. The tool reports them as "foreign platform cluster". Nothing in the archive references
them through a PAssetReferenceImport (the other magic_0668 textures are D3D11).

## 13. Corrections to earlier notes

- MAP.md calls PStreamReaderFile__vf03 (0x576990) the cluster loader. It is only the file open
  (it calls FFX_fios_openFile). The cluster parse is sub_57BCE0, reached from sub_4406C0.
- phyre_reflection_meta.json records 0x4C0A10 as the tdfn of m_format. That is a PSamplerState
  getter, the texture format getter is 0x4C0B10.
- The "Init flag 4 marks a header class" reading in earlier notes is wrong, see section 3.1.
- Header class membership is not "m_objectsSize != m_count x classSize", one shipped list breaks
  that rule. It is class flag 0x4.

## 14. Functions and the names proposed for them

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x57BCE0 | sub_57BCE0 | PCluster__loadFromStream | reads 84 header bytes, marker and platform checks, drives everything below |
| 0x4406C0 | sub_4406C0 | PCluster__loadFromFile | opens the PStreamReaderFile and calls 0x57BCE0 |
| 0x43D6A0 | sub_43D6A0 | PClusterHeader__byteSwap | swaps the 21 header dwords on a swapped marker |
| 0x47F9D0 | sub_47F9D0 | PPackedNamespace__isValid | marker 0x01020304 and the size equation |
| 0x47C8B0 | sub_47C8B0 | PPackedNamespace__unpack | type, class and member loops with strides 4, 36, 24 |
| 0x4786A0 | sub_4786A0 | PNamespaceUnpacker__ctor | |
| 0x47E730 | sub_47E730 | PPackedNamespace__resolveTypeRef | type below the count, class above it |
| 0x57C130 | sub_57C130 | PClusterLoad__readBody | calls the list, data, user fixup, child and fixup readers in order |
| 0x57C4A0 | sub_57C4A0 | PClusterLoad__readInstanceListHeaders | 36 bytes per list |
| 0x57B3A0 | sub_57B3A0 | PClusterLoad__createInstanceLists | aligned bump allocation, header class surplus, error 6 on a missing header class |
| 0x57C940 | sub_57C940 | PClusterLoad__readObjectData | |
| 0x57C5D0 | sub_57C5D0 | PClusterLoad__readUserFixups | 12 byte entries, resolver call, PAssetReferenceImport kept raw |
| 0x437980 | sub_437980 | PType__resolveUserFixup | calls the resolver at +40 |
| 0x57B6C0 | sub_57B6C0 | PClusterLoad__readHeaderClassChildren | counts then 16 byte children, +144 & 4 |
| 0x576C70 | sub_576C70 | PHeaderClassChild__byteSwap | 16 byte entry swap |
| 0x576D20 | sub_576D20 | PUserFixup__byteSwap | 12 byte entry swap |
| 0x57ACD0 | sub_57ACD0 | PClusterLoad__applyPointerArrayFixups | |
| 0x57A6B0 | sub_57A6B0 | PClusterLoad__applyPointerFixups | 28 byte entries, pointer index use |
| 0x57A320 | sub_57A320 | PClusterLoad__applyArrayFixups | bit 31 raw offset |
| 0x57A280 | sub_57A280 | PFixupMemberCache__find | |
| 0x60DAC0 | sub_60DAC0 | PFixupStream__unpack16 | |
| 0x60DB20 | sub_60DB20 | PFixupStream__unpack28 | |
| 0x60A340 | sub_60A340 | PFixupUnpacker__decode16 | modes and flags of section 8 |
| 0x60A750 | sub_60A750 | PFixupUnpacker__decode28 | |
| 0x60E730 | sub_60E730 | PFixupUnpacker__readPointerTail | |
| 0x60AC00 | sub_60AC00 | PFixupUnpacker__readVarint | |
| 0x60CE70, 0x60CC20, 0x60CAC0, 0x60D000 | sub_ | PFixupUnpacker__readIndexSet{Explicit,Exclusion,Bitmap,Progression} | |
| 0x465AA0 | sub_465AA0 | PClusterD3D11__loadResourcePayload | index blob, vertex blob, mip chains |
| 0x4647A0 | sub_4647A0 | PClusterHeaderD3D11__copyBufferSizes | |
| 0x468670 | sub_468670 | PClusterHeaderBase__ctor | writes the marker |
| 0x469980 | sub_469980 | PClusterHeaderD3D11__ctor | |
| 0x464710 | sub_464710 | Phyre__getPlatformID | returns 0x44583131 |
| 0x5F0490 | sub_5F0490 | Phyre__getPhysicsEngineID | returns 0x424C4554 |
| 0x57DD30 | sub_57DD30 | PStreamReader__readExact | |
| 0x5765E0 | sub_5765E0 | PInstanceList__getObject | |
| 0x5844A0 | sub_5844A0 | PTextureFormat__ctor | |
| 0x4C0C00 | sub_4C0C00 | PTextureFormatBase__init | id to bpp and block flag |
| 0x4C0F10 | sub_4C0F10 | PTextureFormat__findByName | |
| 0x561E90 | sub_561E90 | PIndexDataBlockD3D11__createBuffer | |
| 0x5620C0 | sub_5620C0 | PDataBlockD3D11__createBuffer | |
| 0x43B0A0 | Phyre__PClassDescriptor__Init | keep | zeroes +144 |

## 15. Meshes and textures as the D3D11 loader uses them (2026-10-12)

Written for Project Zanarkand items 5.3 and 5.5. The readers are src/engine/phyre/Mesh.cpp and
Texture.cpp there, and "phyrecheck meshes" and "phyrecheck textures" run them over the whole archive.

### 15.1 Mesh layout

- PMesh (56 bytes): m_meshSegments +0 PArray<PMeshSegment>, m_skeletonMatrices +8 PArray<PMatrix4>,
  m_skeletonBounds +16 PArray<PSkeletonJointBounds>, m_defaultPose +24 PArray<PMatrix4>,
  m_matrixNames +32 PArray<PString>, m_matrixParents +40 PArray<PInt32>, m_defaultMaterials +48
  PMaterialSet, whose m_materials is a PSharray<PMaterial*> (m_count +48, m_u +52).
- A PArray of objects is m_count at the slot and a pointer fixup at slot + 4 that lands on the first
  element object, with pointerIndex equal to the count. Element i is the object i places after it in
  the same list. A PArray of primitives is an array fixup at slot + 4.
- PSharray<PMaterial*>: a count of 1 keeps the pointer inline in m_u, more go through a pointer array
  fixup, and the pointer fixups' pointerIndex is the slot (they are not stored in slot order). 981 of
  the 12,266 meshes state a count over a pointer array with no pointer fixups at all, every slot
  null. In bsil03 they are the 25 odd meshes 1 to 49, and only their PAssetReference points at them,
  no PMeshInstance or PModifierNetworkDynamicMesh does.
- PMeshSegment (PMeshSegmentBase 40 bytes first): m_materialIndex +0 into the mesh's material set,
  m_matrixIndex +4, m_skinBones +8 PArray<PSkinBoneRemap>, m_primitiveType +16, m_modeIndex +24,
  m_segmentRenderOrder +28, m_clothmodelIndex +32, m_clothIndex +36. The PMeshSegmentD3D11 part holds
  the vertex blocks (a PArray<PDataBlock>) and an embedded PIndexDataBlockD3D11 at +48, so the index
  block's m_offsetInIndexBuffer is segment +92 and its m_dataSize segment +100.
- PSkinBoneRemap is two PUInt16, m_hierarchyMatrixIndex then m_skeletonMatrixIndex.
  PSkeletonJointBounds is min[3], m_hierarchyMatrixIndex, size[3] and a pad, 32 bytes. Over the
  archive every remap's skeleton index lands in the bounds table and
  bounds[remap.skeleton].hierarchyMatrixIndex equals remap.hierarchy.
- c001 (Tidus): 1 PMesh, 13 segments of 8 blocks each, 104 blocks tiling the 806,748 byte vertex
  blob exactly, indices tiling the 59,172 byte index blob, 8,769 vertices, 127 remaps, 53 skeleton
  matrices and bounds, 79 hierarchy matrices. Segments 6, 7, 10, 11 and 12 carry cloth indices
  (cloth model 0 or 1, cloth 0 to 2). modeIndex is 0 for segments 0 to 3 and 1 for the rest.

### 15.2 Data types, streams and indices

- sub_55BC40 maps a data type id (0 to 50) to its byte size. sub_4D6C00 maps it to a reflected
  component type and count: 0..3 float x1..4, 4..7, 12..15 and 40..43 PUInt16, 8..11 PUInt32,
  16..19 PUInt8 x1..4, 28..31 PInt32, 32..35 PInt16, 48 float x12, 49 float x16, 50 PUInt32.
- The archive's vertex streams use four types: 1 (Float2, ST sets 0 to 2, stride 8), 2 (Float3,
  Vertex, Normal, Tangent, Binormal and the Skinnable ones, stride 12), 3 (Float4, Color and
  SkinWeights, stride 16) and 19 (UInt8x4, SkinIndices, stride 4). Every PDataBlock holds one stream
  and stride x elementCount equals m_dataSize.
- There is no stream to DXGI table in FFX.exe. The format comes from each .fx's input layout:
  ST is R32G32_FLOAT (16), Vertex and Normal R32G32B32_FLOAT (6), Color and SkinWeights
  R32G32B32A32_FLOAT (2), SkinIndices R8G8B8A8_UINT (30).
- Index format, the cmp/setnz at 0x592C95 in sub_592C60: m_type 8 gives R32_UINT (42), anything else
  R16_UINT (57). Every shipped segment uses type 12, two bytes.
- Topology, sub_58AB60 on m_primitiveType: 0 and 5 POINTLIST, 1 LINELIST, 2 TRIANGLELIST, 3
  TRIANGLESTRIP, 4 TRIANGLESTRIP with a "FAN_NOT_SUPPORTED" warning, else 0. Every shipped segment
  is 2.
- sub_592C60 sets the index buffer with the offset at [ib+2Ch] and draws DrawIndexed(count, 0, 0).
  sub_592DE0 is the instanced variant (vtable +80).

### 15.3 What the loader does to the buffers

- PDataBlockD3D11__createBuffer and PIndexDataBlockD3D11__createBuffer copy the blob slice verbatim
  into an immutable buffer (usage 1). Bind flags are 9 for vertices (VB and SRV) and 10 for indices
  (IB and SRV). Each also creates a BUFFEREX shader resource view, R32_FLOAT (41) over vertices and
  R32_UINT (42) over indices, NumElements = m_dataSize >> 2.
- PClusterD3D11__loadResourcePayload zeroes m_offsetInIndexBuffer (+44 of the index block, segment
  +92) and PDataBlock m_offsetInVertexBuffer (+48) after each create, because every block now owns
  its own buffer. The value in the file is the slice offset, the value at runtime is 0.

### 15.4 Textures

- PTextureFormat__ctor(name, id, flags, a5 to a9) stores a5..a9 at +20, +24, +28, +32, +36.
  sub_58FC20 creates the Texture2D with Format = +24. By id: L8 0 and A8 1 -> 61, LA8 2 and RG8 3
  -> 49, L16 4 and A16 5 -> 57, LA16 6 and RG16 7 -> 36, RGBA8 11 -> 28, ARGB8 12 -> 87, A2RGB10 13
  -> 24, RGBA16 14 -> 12, DXT1 15 -> 71, DXT3 16 -> 74, DXT5 17 -> 77, BC4 18 -> 80, BC5 19 -> 83,
  RGBA16F 28 -> 10, RGBA32F 29 -> 2, R16F 30 and L16F 31 -> 54, R32F 32 and L32F 33 -> 41, RG16F 34
  and LA16F 35 -> 34, RG32F 36 and LA32F 37 -> 16, DEPTH16 38 -> 53, DEPTH24 39 and DEPTH24S8 40 ->
  44, DEPTH32 41 -> 39, R32 47 -> 42, UNKNOWN 48 -> 0. For the depth formats +28, +32 and +36 differ
  (DEPTH16 55, 56, 56, DEPTH24 45, 46, 46, DEPTH32 40, 41, 41) and look like the DSV, SRV and RTV
  formats. That reading is not verified.
- ARGB8 is B8G8R8A8_UNORM, so the file's bytes are BGRA in memory and nothing swaps channels. No
  shipped texture is swizzled or tiled.
- sub_58FC20 creates MipLevels = m_mipmapCount + 1, except 1 when m_maxMipLevel (+16) is 0 and
  m_textureFlags (+20) bit 1 is clear. Row pitch = (block ? alignUp(w, 4) x 4 : w) x bpp / 8, slice
  pitch 0. Usage 2 when the dynamic argument is set, BindFlags 8. sub_590800 (cube) uses the same
  MipLevels rule with ArraySize 6 and MiscFlags 4, and walks the face pointers by the created level
  count, so a cube forced to one level would read the wrong data for faces 1 to 5. No shipped cube
  hits that. sub_5903A0 (3D) was not decoded.
- Over the archive: flags 0 on 30,157 textures (m_maxMipLevel == m_mipmapCount), flags 0x2 on
  14,208. No texture has m_maxMipLevel 0 with a chain, so the one level rule never fires.

### 15.5 Texture quality drops top levels

sub_5947D0 returns the D3D11 render interface's +0x1340, sub_58E000 sets it and reloads the
clusters. Phyre__PApplication__onInit (0x62AA0F) and the frame tick (0x627BCC) pass
g_ffxGameSettings +0x34, which is GameSetting.ini key 13, TextureQuality (TQ_HIGH 0, TQ_MEDIUM 1,
TQ_LOW 2). PClusterD3D11__loadResourcePayload then, per texture:

- 2D: when m_mipmapCount >= skip and skip != 0, w' = w >> skip, h' = h >> skip, and while skip != 0
  and either is below 4, both double and skip drops by one. The lowered skip is a single local, so
  it carries over to later textures of the same cluster, cube and 3D included. No shipped cluster
  holds more than one texture, so the carry never shows.
- Dropped levels are read into the same staging spot and discarded. The object is resized through
  Phyre__PTexture2D__setFormatAndSize(w >> skip, h >> skip, format, m_mipmapCount - skip), whose
  return the loader ignores. It refuses (3) when the level count is nonzero and 1 << it exceeds the
  larger side. No shipped texture would be refused at skip 1 or 2.
- Cube: no 4x4 clamp, sub_4C7260(size >> skip, format, m_mipmapCount - skip).
- Archive counts: skip 1 drops one level from 30,151 textures, skip 2 drops two from 30,106 and one
  from 45 (the clamp). The 14,214 single level textures keep everything at both.

### 15.6 Archive totals (phyrecheck, 47,095 clusters parsed, 30 foreign)

- Meshes: 1,266 clusters, 12,266 PMesh (all in .dae clusters), 39,497 segments, 235,397 vertex
  blocks and streams, 23,256,840 vertices, 28,506,639 indices, 49,543 remaps, 34,195 joint bounds,
  62,497 hierarchy matrices. In every cluster the mesh blocks tile the vertex blob byte for byte
  with no overlap. 4,084 segments are skinned, and in each the largest SkinIndices byte is the last
  palette entry. One segment has no indices. Two znkd09 segments store a maximum index of 137 over
  indices that reach 113, still inside the vertex count.
- No standalone PIndexDataBlock list exists anywhere in the archive, all 39,497 index blocks are
  embedded in segments. 5,016 PDynamicDataBlock objects are not segment data.
- Textures: 44,365 decode, 300,252 subresources, 9,562,557,912 texel bytes. 2D DXT5 29,452, DXT1
  13,404, ARGB8 1,062, DXT3 335, L8 2, cube DXT1 107, DXT5 3. 278 are not a power of two, 6 block
  compressed ones are not a multiple of 4 (for example a 1x256 DXT5 under help_). Every level's size
  agrees with the DXGI layout and the chain ends exactly at the end of the file.
- bsil03 as one map: 140 .dds.phyre (tex 66, fp 42, texswap 32), 1,267 levels, 10,900,016 bytes,
  and the model cluster imports 65 textures. The import id "PS3Data/<dir>/<name>.dds" maps to
  "ffx_data/gamedata/ps3data/<dir>/d3d11/<name>.dds.phyre" lowercased. That mapping was read off
  the data, the resolver in FFX.exe was not traced.

### 15.7 Corrections

- Section 9 says each PIndexDataBlock object is a 60 byte file class PIndexDataBlock. No such list
  ships. The 60 byte PIndexDataBlockD3D11 lives inside PMeshSegmentD3D11 at +48, so its
  m_offsetInIndexBuffer and m_dataSize are segment +92 and +100.
- Section 9 says the init stubs pass "four DXGI formats". PTextureFormat__ctor stores five values,
  +20 to +36, and +24 is the one a Texture2D is created with.
- The 12,235 PMesh figure in Project Zanarkand's PROGRESS is low, the archive holds 12,266.

### 15.8 Names proposed

Applied to the IDB on 2026-10-12, all but 0x4D6C00, whose role is still uncertain.

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x55BC40 | sub_55BC40 | Phyre__PDataType__getSize | 51 entry id to byte size switch |
| 0x4D6C00 | sub_4D6C00 | Phyre__PDataType__getComponentType | id to PType and count, low confidence on the role |
| 0x58AB60 | sub_58AB60 | PRenderInterfaceD3D11__primitiveTopology | m_primitiveType switch, the FAN_NOT_SUPPORTED string |
| 0x592C60 | sub_592C60 | PRenderInterfaceD3D11__drawIndexed | index format at 0x592C95, index buffer offset, DrawIndexed |
| 0x592DE0 | sub_592DE0 | PRenderInterfaceD3D11__drawIndexedInstanced | the vtable +80 twin |
| 0x58FC20 | sub_58FC20 | PTexture2DD3D11__createResource | D3D11_TEXTURE2D_DESC from the object and format +24 |
| 0x590800 | sub_590800 | PTextureCubeMapD3D11__createResource | ArraySize 6, MiscFlags 4 |
| 0x5903A0 | sub_5903A0 | PTexture3DD3D11__createResource | the 3D create, not decoded |
| 0x5947D0 | sub_5947D0 | PRenderInterfaceD3D11__getTextureMipSkip | returns +0x1340 |
| 0x58E000 | sub_58E000 | PRenderInterfaceD3D11__setTextureMipSkip | stores +0x1340 and reloads, fed TextureQuality |
| 0x4C7260 | sub_4C7260 | PTextureCubeMap__setFormatAndSize | the cube twin of 0x4C40C0 in the skip path |

## 16. Raw offset fixups, scene objects and fonts (2026-10-13)

Project Zanarkand 5.2 and 5.6. The full write up is SCENE_OBJECTS.md, this section only records what
changes the format notes above.

- Correction: a raw offset fixup (dword 0 bit 31) is not the slot. `PPackedClass__findMemberAtRawOffset
  0x47D3E0` adds each class's packed dword 5 (layout word 1) and takes the member covering the sum,
  class by class along the chain, so PNode's 100,251 name fixups (raw 76) patch m_name at 80. The
  camera names (raw 204) still land at 204 because the PCamera class that owns m_name has layout 0.
  Every class with a zero layout, which is every mesh, material and texture class, is unaffected.
  Section 8 and `slot_of` / `Cluster::SlotOf` treat the raw value as the slot. Verified, static and data.
- PWorldMatrix m_matrix is a PMatrix4x3 stored as `[c1 c0.x | c2 c0.y | c3 c0.z]` (three floats of a
  column, then one element of column 0). `PMatrix4x3__toMatrix4 0x451080`. Verified, static and data.
- Correction 2026-10-13 (vptr shift): a member's table offset is within an object of the class that
  owns it. In an object of a derived class it moves by the derived class's layout word 1 minus the
  owner's. Layout word 1 counts the bytes a vptr puts in front of a base that has none. PCamera is a
  plain class (layout 0, 332 bytes) and PCameraProjection adds a vptr (layout 4, 340 bytes, m_aspect
  at 336), so in every camera object the PCamera subobject starts at +4: m_localToWorldMatrix +180,
  m_nearPlane +184, m_farPlane +188, m_name +208 (`FFX_Gfx_FindGlobalCamera2 0x6519F0` reads the
  name there, checked in IDA), the fog members +212 on. A raw offset counts no vptr, so the slot is
  the raw offset plus the list class's layout word 1: camera names at raw 204 patch +208, not +204
  as the note above said. PCamera inside a camera is the only base this moves in the shipped data
  (scanned over a map, a character and an effect cluster). The near and far planes read before this
  were a zero pad and the real near. Found by the 6.3.2 map render agent, rule applied in
  `Namespace::ObjectOffset` (Zanarkand Cluster.h) and `Namespace.object_offset` (tools/phyre.py).
- Every stored node world in the archive (14,625) equals, bit for bit, the local matrices composed up
  the parent chain the way `PNode__composeWorldMatrix 0x506660` does it. The loader never recomputes it.
- No cluster holds a PWorld, PLight or PSpriteCollection object.
- A map's model cluster is changed after load by `FFX_Gfx_AdoptMapCluster 0x65AF60`: every mesh
  instance world is multiplied by diag(-2.5, -2.5, 1) then translate(-256, -224, 0) on a field map, or
  by diag(10, 10, 10) on a battle map, where the battle path also scales camera translations by 10.
- The one PBitmapFont (tuffy.fgen.phyre) is the debug text font. The menu font is FFX's font 0x3C00,
  DXT5 page textures under menu_us/base_ftc (menu/base_ftc for Japanese text) plus an FTCX width table.
