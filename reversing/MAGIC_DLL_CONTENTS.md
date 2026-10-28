# What the magic DLLs contain: code against data, interpreter or hand code

PROGRESS 1.10.4 of Project Zanarkand. It answers the question in PROGRESS section 15: should the 581
magic DLLs be hand ported from disassembly, run by one interpreter over the shipped PS2 data, or a
mix, and by what rule.

All exe addresses are IDA VAs (RVA + 0x400000). DLL addresses are the DLL's own VAs at its preferred
base 0x10000000. The exe was read with idalib on a scratch copy of the IDB. Nothing was renamed or
saved. The 581 DLLs were read in place with `tools/magicdll.py` and one-off scripts. The few opened in
IDA were copied one at a time into the scratchpad and deleted afterwards. No game data is in either
repo. Proposed exe names are in `tools/apply_dll_contents_names.py`.

Marks: **[V]** read in the binary, **[M]** measured across the 581 DLLs or the whole PS2 tree, **[I]**
inferred.

Related: `MAGIC_DLL.md` (the 947 slot host table, InitMagicPRX), `EFFECT_OVERLAY.md` (the overlay
table, the yonishi header past +0x3F, the step contract), `YONISHI_ENGINE.md` (PROGRESS 1.10.3, the
exe Oef2 interpreter and the ppp particle library, which this doc links to rather than repeats).

---

## Summary, read this first

1. **The DLLs are 95% data.** Of 364,167,680 bytes, function code is 9,807,038 (2.7%), and 1,470,308
   of that is the statically linked CRT. Raw .data is 95.1%. 11.0% of all bytes are whole PS2 `.wd`
   sound banks. Section 3. [M]
2. **There are two kinds of DLL, and the split is exact.** 130 DLLs are yonishi ports: they call
   host 716 `FFX_Magic_Oef2Dispatch` and carry no effect logic at all, only shared glue that hands a
   pointer to their .data to the exe interpreter. The other 451 are hand-written C over about ten
   shared frameworks, with real per-effect code. No DLL is in between. Section 5. [M]
3. **No DLL carries a copy of exe code.** Not by exact hash, not by shape hash, not by 4-gram
   similarity (0 of 3,508 bodies at Jaccard 0.5 or more against 1,837 exe effect-region functions),
   apart from the CRT. What looks like copied engine code is forwarding thunks into the host table.
   Section 4. [M]
4. **The yonishi port's .data IS the PS2 `dat_ov` effect bin.** 100 of the 109 ports that have a
   `dat_ov/mag_NNNN` bin hold it verbatim and contiguous, 8 more are byte-level revisions of it, and
   1 (0345) has diverged. 20 of the 21 ports with no bin of their own embed another id's bin whole.
   The bins carry no relocations, so they are pointer-free. Section 6. [M]
5. **There is no PS2 code to interpret.** `ffx_ps2` holds no MIPS anywhere, so the hand-written 451
   cannot be run from PS2 data. Their data has PS2 parts (sound banks, ATEL blobs, SeSep, texture and
   model blocks shared with the eff bins), but the logic exists only as x86 in the DLL. Section 6.
   [M]
6. **Decision: a mix, by this rule.** A DLL that calls host 716 runs on the recreation's Oef2
   interpreter over its own bin, read in place from the DLL .data. Every other DLL is hand ported.
   That is about 2,750 near-unique function clusters, 1.35 MB of x86, on top of about 394 framework
   functions (57 KB) that are ported once. Particle programs in every DLL go to the shared ppp
   library. Section 7. [M] [I]

## 1. How the sample was chosen

Twenty DLLs, picked after a whole-corpus census rather than at random, so the sample covers every
axis the census found:

- **Every family with more than five members** (section 5.1), plus Yanagi: yonishi 5, Yanagi 2,
  ego 3, fw9 2, fw162 4, fw135 1, fw539 1, fw527 1, fw269 1.
- **Size**, from 24 KB (magic_0052) to the 3.4 MB maximum (magic_0515).
- **Port against non-port**, **stub against non-stub** slot 3 (`EFFECT_OVERLAY.md` section 2), and
  **RNG drawers against not** (host 324 or 307).
- **Kind of effect**: plain spells (Fire, Cura, Curaga, Death), skills (Power Break, Lancet, Scan,
  Cheer, Threaten), aeon entrances (Valefor 148, Anima 152, Shiva 162), overdrives (Energy Blast 151,
  Blitz Ace 393, Delta Attack 515, Element Reels 565), enemy abilities (Photon Spray 345, Single 300,
  Megiddo Flame 382), and the special id 670, game over.
- **Every PS2 match class**: verbatim (0021, 0151, 0670), revised (0148), diverged (0345), header
  only (0098), none (the rest).

Ability names come from the kernel tables (`new_uspc/battle/kernel`, command, item, monmagic1 and
monmagic2, effect id at row +16 and +18), decoded from FFX glyphs. [M]

The whole-corpus figures in sections 3 to 7 are measured on all 581, not on the sample. The sample
is there to show what a row looks like.

## 2. The sample table

Bytes unless the column says KB. "code" is all function bytes in .text. "framework" is bodies shared
by 20 or more DLLs. "bespoke" is everything else that is not CRT, with the count of functions found
in this DLL alone in brackets. "PS2 bin" is the `dat_ov` eff bin found inside .data. "ATEL" is the
slot 7 script blob. "stub" is a slot 3 that does nothing. "RNG" is host 324 plus 307 call sites. [M]

| DLL | effect | family | file KB | code | CRT | framework | bespoke (unique) | .data KB | PS2 bin KB | .wd KB | ATEL | BSS KB | stub | RNG | dat_ov |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0021 | Power Break | yonishi | 448 | 6355 | 2534 | 3469 | 352 (0) | 434 | 420 | 83 | 12220 | 10 | no | 0 | verbatim |
| 0148 | Valefor entrance | yonishi | 1030 | 6796 | 2534 | 3250 | 1012 (0) | 1012 | 1008 | 107 | 1628 | 10 | no | 0 | revised |
| 0151 | Energy Blast | yonishi | 723 | 6373 | 2534 | 3406 | 433 (0) | 708 | 702 | 95 | 3660 | 10 | no | 0 | verbatim |
| 0345 | Photon Spray (enemy) | yonishi | 725 | 6380 | 2534 | 3608 | 238 (0) | 710 | 101 | 233 | 2716 | 10 | no | 0 | diverged, 28% |
| 0670 | game over | yonishi | 296 | 5806 | 2534 | 3098 | 174 (0) | 282 | 280 | - | 1548 | 10 | no | 0 | verbatim |
| 0300 | Single (enemy) | yanagi | 626 | 6329 | 2528 | 1803 | 1998 (9) | 608 | - | 98 | 1096 | 1025 | no | 0 | none |
| 0382 | Megiddo Flame | yanagi | 778 | 8082 | 2528 | 2143 | 3411 (11) | 757 | - | 97 | 2332 | 1025 | no | 0 | none |
| 0003 | Cheer | ego | 305 | 27142 | 2528 | 23600 | 1014 (1) | 262 | - | 90 | 11820 | 2569 | no | 0 | none |
| 0076 | Fire | ego | 364 | 27297 | 2528 | 22773 | 1996 (2) | 321 | - | 91 | 5996 | 2565 | no | 4 | none |
| 0152 | Anima entrance | ego | 1300 | 51497 | 2540 | 23632 | 25325 (7) | 1227 | - | 92 | 1096 | 2583 | no | 9 | none |
| 0009 | Lancet | fw9 | 354 | 15117 | 2546 | 6007 | 6564 (5) | 326 | - | 83 | 2828 | 1432 | yes | 0 | none |
| 0112 | Curaga | fw9 | 450 | 10131 | 2528 | 5893 | 1710 (1) | 428 | - | 101 | 11820 | 1303 | yes | 0 | none |
| 0162 | Shiva entrance | fw162 | 1071 | 40171 | 2534 | 18618 | 19019 (24) | 1012 | - | 98 | 1096 | 1907 | yes | 1 | none |
| 0393 | Blitz Ace | fw162 | 1010 | 29307 | 2546 | 13442 | 13319 (24) | 962 | - | 100 | 11532 | 2038 | yes | 0 | none |
| 0515 | Delta Attack | fw162 | 3354 | 37234 | 2534 | 2921 | 31779 (1) | 3296 | - | 108 | 0 | 68 | no | 18 | none |
| 0565 | Element Reels | fw162 | 1340 | 44268 | 2546 | 14830 | 26892 (0) | 1275 | - | - | 19212 | 1973 | yes | 1 | none |
| 0110 | Cura | fw135 | 300 | 10236 | 2528 | 5774 | 1934 (1) | 278 | - | 101 | 11820 | 1304 | yes | 0 | none |
| 0098 | Death | fw539 | 564 | 13968 | 2546 | 1775 | 9647 (21) | 540 | - | 98 | 4940 | 3 | no | 0 | par/fp.h only |
| 0020 | Scan | fw527 | 419 | 9871 | 2540 | 1313 | 6018 (17) | 400 | - | 97 | 7388 | 22 | no | 0 | none |
| 0052 | Threaten | fw269 | 24 | 10221 | 2307 | 4995 | 2919 (3) | 6 | - | - | 4972 | 340 | yes | 0 | none |

How to read it:

- **For the yonishi rows "bespoke" is not effect logic.** It is slot 0 glue variants that 2 to 19
  ports share, and none of it is unique to one DLL. Section 5.2.
- **0515's 31,779 bespoke bytes with 1 unique function** is because its big phase function, 16 KB,
  is shared in near-identical form with 0611, 0667 and 0668. Exact-unique and near-unique are different counts. Section 7.2 uses the near-unique one.
- **0565's 0 unique** is because the 10 Element Reels DLLs share one body set.
- **0021's `.wd` sits inside its PS2 bin**, it is not extra.
- **BSS** is reserved at load and zeroed, 750 MB virtual across all 581. It is the DLL's own work
  area, Ego class pools and particle heaps.

## 3. Code against data, whole corpus

| part | bytes | share |
| --- | --- | --- |
| all 581 files | 364,167,680 | 100% |
| .text section | about 10.6 M | 2.9% |
| function code | 9,807,038 | 2.7% |
| of that, CRT | 1,470,308 | 0.4% |
| jump tables in .text | 50,800 | under 0.1% |
| .rdata | about 1.1 M | 0.3% |
| .data, raw | about 346 M | 95.1% |
| whole PS2 `.wd` banks inside .data | 40,016,528 | 11.0% |

[M] The `.wd` count is 401 DLLs holding 186 distinct banks, matched whole against `ffx_ps2`.

What the rest of .data is, from the matching in section 6: the eff bins (ports), ATEL blobs (529
of them, the largest header +0x0C offset in every one), SeSep sound sequencing data, and texture,
model and palette blocks byte-identical to blocks in eff bins and in `chr` and `wep` files. The
non-port .data is assembled from the same PS2 asset kinds the eff bins hold, linked straight into
the DLL. [M]

So the job is overwhelmingly reading data in place. The code that needs porting is under 3% of the
bytes and much of it repeats.

## 4. Forwarding thunks and the pppProg tables

Every one of the 581 DLLs carries a block of 17 byte thunks, one per host slot from 39 up [V] [M]:

```
55             push ebp
8B EC          mov  ebp, esp
A1 <tbl>       mov  eax, [g_hostTable]      the DLL's copy of the host table pointer
8B 80 <disp>   mov  eax, [eax + slot*4]
5D             pop  ebp
FF E0          jmp  eax
```

magic_0021 at 0x100022C0: `55 8b ec a1 3c 28 07 10 8b 80 64 0b 00 00 5d ff e0`, slot 0xB64/4 = 729.
[V]

These are the PS2 ppp operator entry points re-pointed at the exe. 575 of the 581 DLLs carry
`pppProg` tables, 127 distinct operator names, and their function columns are these thunks, which
jump to the ppp operators in the exe at 0x720000..0x760000 (`YONISHI_ENGINE.md` section 4, the 273
record DLL table at 0xC3BBB0, host data slot 921). The `.h` headers that `part.exe` wrote into
`ffx_ps2` name 98 pppProg programs, and all 98 exist as exe strings. [M]

Before 2026-10-27 `tools/magicdll.py` looked only at `call`, so it never saw these. With `jmp reg`
counted the scan finds 41,116 tail jump sites, 39,612 in thunks something references (an `E8` target
or a relocated address). 246 host slots are reached only through a referenced thunk: 219 are ppp
operators in 0x720000..0x760000, 9 are Yanagi (0x925000..0x931000), 3 are Oef2 range, and 15 are
elsewhere, chiefly hosts 728..740, the Oef2 sub-command helpers at 0xA6DBE0..0xA76B60 (731 is
`u2_part_work_scale`). [M]

magic_0021's Oef2 opcode table at header +0x7C, and +0x78, are nothing but these thunks (hosts
728..740 and 911, to 0xA6DBE0..0xA76B60, 0x7F5F70 and 0x7FA6D0). So the per unit DLL table that
`YONISHI_ENGINE.md` section 5.1 mentions holds no DLL code either. [V]

The exe side of this is 1.10.3's, and this doc does not attribute it. The point here is only that
the DLLs carry thunks, not copies.

## 5. Interpreter or hand code

### 5.1 Families

Clustering the DLLs on shared function bodies (exact body hash with relocations and rel32 zeroed)
gives these families. A family is named after one member DLL or after the class names it carries.
Only ego, yonishi and Yanagi were read closely enough to describe. [M]

| family | DLLs | what it is | slot 3 stubs | RNG drawers |
| --- | --- | --- | --- | --- |
| yonishi | 130 | ports, Oef2 over the bin | 0 | 0 |
| ego | 117 | Ego class system, `EgoTask`, `EgoVec`, `EgoNode` and so on | 0 | 60 |
| fw9 | 103 | | 91 | 21 |
| fw162 | 85 | holds the sampled aeon and overdrive non-ports | 81 | 31 |
| fw135 | 42 | | 42 | 8 |
| yanagi | 40 | continuation chains over the exe Yanagi task engine | 0 | 0 |
| fw539 | 27 | | 0 | 10 |
| fw527 | 20 | | 0 | 6 |
| fw269 | 7 | | 7 | 1 |
| fw183 | 5 | | 0 | 0 |
| fw662 | 2 | | 0 | 0 |
| single | 3 | 0065, 0127, 0197 share no framework | 3 | 0 |

All 224 slot 3 stubs are in the hand-written families. All 137 RNG drawers are too. No yonishi or
Yanagi DLL draws the battle RNG. [M]

### 5.2 The 130 yonishi ports are pure interpreter

- **The rule is exact.** All 130 call host 716 `FFX_Magic_Oef2Dispatch 0x80CC00`. None of the
  other 451 does. Three non-ports (0350, 0380, 0605) hold the overlay table VA for an unrelated
  reason and were miscounted before, `EFFECT_OVERLAY.md` 2.3 now says so. [M]
- **They have no per-effect code.** In the five sampled ports 0 functions are unique to the DLL.
  Corpus-wide, only 19 port bodies (1,271 bytes) are unique to one port, and every one is a slot 0
  variant or a 17 byte thunk. [M]
- **Slot 0 only wires the bin to the exe.** magic_0021 slot 0 at 0x10001690 allocates 1 MB through
  host 803, passes `&bin` at 0x10006FE0, which is exactly where `eff_0021.bin` starts in .data, to
  host 718 `FFX_Oef2_SetFromUnit 0x7E3E30` and host 719 `FFX_Oef2_GetSubTable 0x7E3810`, and to
  host 715 `FFX_Effect_InitUnit 0x7FDA80` as (1, ..., 512, 0, handle). Slot 3 and 4 then call 716
  and host 721 `opp_main 0x80BD50`. [V]
- **One debug override.** If `*(host748 + 16)` and `*(host746 + 64) & 0x10`, slot 0 takes the bin
  pointer from `*(host746 + 168)` instead, a PS2 era hot reload hook. On a retail run the flags are
  clear. [V] [I]

So a port is data for the exe's own Oef2 interpreter (`YONISHI_ENGINE.md` section 5) and nothing
else.

### 5.3 The 451 others are hand code

- **Ego** (117) is a small class system: records of a 16 byte name, a kind, a parent and a method
  table. magic_0003 Cheer is one 898 byte phase machine at 0x10006540 on top of 23 KB of shared
  Ego code. Ego DLLs draw the RNG most (60 of 117). [V] [M]
- **The fwNNN families** each share a block of framework bodies, and the effect sits in functions
  on top. In fw9, Curaga's setup at 0x10001410 is 1,165 bytes found in no other DLL. [V] [M]
- **Yanagi** (40) DLLs are chains of small state functions over the exe's Yanagi task engine
  (`pppYanagi.c` in the FFX-2 build), hosts 430..451. magic_0300 creates a task through host 434,
  inits a program through 436, and each phase pushes the next with 449 or waits N frames with 451.
  Host 437 steps the program and calls `FFX_Btl_EffectAdvanceState` when it ends. Host 436 reads the
  DLL's data through the same `FFX_Oef2_SetFromUnit` and `FFX_Oef2_GetSubTable(unit, 8, i)` as the
  ports, then relocates its particle set. So Yanagi is half interpreted: the program data is read
  like a port's, the phase logic is hand code. [V]

### 5.4 Nothing is a copy of the exe

Every non-CRT DLL function was fingerprinted three ways and compared with all 49,160 IDB functions
[M]:

- exact: bytes with relocated dwords and `E8`/`E9` rel32 zeroed,
- shape: displacements and immediates dropped, host slot markers kept,
- fuzzy: token 4-gram Jaccard with MinHash LSH, 48 hashes in 12 bands of 4.

Only CRT matches. Fuzzy found 0 of 3,508 candidate bodies at J 0.5 or above against the 1,837 exe
functions in the effect regions. As a control, the same detector scores the 0515 and 0611 phase
functions at J 0.88 with 7 of 12 bands colliding, so it does find near copies when there are some.
[M]

## 6. The PS2 data

### 6.1 dat_ov against the ports

`ffx_ps2/ffx/yonishi_data/dat_ov/` has 112 `mag_NNNN` ids, 116 bins counting the `_2` files. [M]

| class | count | ids |
| --- | --- | --- |
| port, bin verbatim and contiguous in .data | 100 | |
| port, bin differs by 1 to 8 bytes | 5 | 0170, 0277, 0278, 0280, 0595 |
| port, bin rebuilt with inserted bytes, 99.9% of 64 byte blocks match | 3 | 0148, 0154, 0569 |
| port, diverged, 28% of blocks match | 1 | 0345 |
| no DLL | 2 | 0000, 0777 |
| header only (`par/fp.h`), DLL is hand code | 1 | 0098 |

The 21 ports with no dat_ov bin of their own: 20 embed another id's bin whole. 0125 has 0124, 0131
has 0130, 0137 has 0136, 0139 has 0138, 0141 has 0140, 0143 has 0142, 0236 has 0235, 0238 has 0237,
0306, 0593, 0604 and 0615 have 0151, 0326 has 0021, 0327 has 0319, 0328 has 0320, 0329 has 0321,
0418 has 0175, 0422 has 0283 and 0283_2, 0607 has 0170_2, 0608 has 0154_2. 0296 is only a near
match, to 0265. [M]

**There are 0 relocations inside the 100 verbatim bins.** The bin is position independent, every
internal reference is an offset, so it can be read in place from the DLL image. [M]

### 6.2 What an eff bin is

A PS2 asset pack, not a program: a section offset table (texture, clut, obj, model, vib, sound,
camera, particle, as the `.ha` files name them), a SeSep block and a whole WD bank. `eff_0021.bin`
holds `wave0325.wd` at bin + 0x1040. The Oef2 script the exe interprets lives in it. [V] [M]

### 6.3 No PS2 code anywhere

0 `jr ra` words (0x03E00008) in any dat_ov file, and no file in the whole 17,913 file, 1.6 GB
`ffx_ps2` tree has three or more. So for the 451 hand-written DLLs there is no PS2 form of the logic
to interpret, only the assets it uses. [M] Where the PS2 effect code went is not known, presumably overlays that
this tree does not include. [I]

## 7. Decision

### 7.1 The rule

**A mix.** For each DLL:

1. **If it calls host 716, it is a yonishi port.** The recreation runs it on its own Oef2
   interpreter and opp, the 1.10.3 engine, over the bin that slot 0 hands the exe. Read the bin in
   place from the DLL's .data (PORTING_RULES A1 and G1, the bin is pointer-free so nothing needs
   relocating). The DLL .data is the authority because it is the PC revision: 9 of the 109 differ
   from dat_ov and 0345 differs a lot. dat_ov is a cross-check only. No per-DLL code is written.
   130 DLLs.
2. **Otherwise it is hand ported from the disassembly.** First port the shared framework once per
   family (Ego, fw9 and fw135, fw162, fw269, fw527, fw539, Yanagi glue, fw183, fw662), then the
   per-effect functions on top. The Yanagi family also needs the exe Yanagi engine, hosts 430..451,
   which reads its program data the same way the ports do. 451 DLLs.
3. **Particle programs go to the shared ppp library in both cases.** A pppProg row is a thunk to an
   exe operator, so it is a table of names and operator ids to bind, not code to port.
4. **Assets are read in place in both cases.** The .data is assembled from PS2 asset kinds (WD banks,
   SeSep, ATEL, texture and model blocks). Their readers belong to the shared format code, not to
   any one DLL.

### 7.2 Size of the hand port

Distinct non-CRT bodies across all 581: 4,089, 1.76 MB. [M]

| part | bodies | bytes |
| --- | --- | --- |
| framework, in 20 or more DLLs | 394 | 57 KB |
| effect bodies | 3,701 | 1.70 MB |
| of those, distinct shapes | 3,039 | |
| of those, in exactly one DLL | 2,583 | |
| near-duplicate clusters, J 0.8 or above | 2,797 | |
| of those, outside yonishi | 2,751 | 1.35 MB |
| of those, 32 bytes or less | 162 | |

So the hand port is about **2,750 functions, 1.35 MB of x86**, after merging near copies, plus the
394 framework functions once. The largest single items are the 16 KB phase functions shared by 0515,
0611, 0667 and 0668, and 9 KB ones in 0434 and 0610. [M]

At a rough 10 to 15 bytes of x86 per line of C, 1.35 MB is about 90,000 to 135,000 lines. Treat that
as an order of magnitude. The real count depends on how much of the near-copy merging survives a
port, and on how many phase functions collapse into data driven tables once the frameworks are
understood. [I]

## 8. The claims that carry the decision

1. **Port test is host 716 and it is exact.** 130 of 581 call `FFX_Magic_Oef2Dispatch 0x80CC00`
   through the host table at 0xC64CE8 slot 716. No other DLL does. `tools/magicdll.py survey`. [M]
2. **A port's .data is its PS2 bin.** magic_0021 slot 0 at 0x10001690 passes 0x10006FE0 to hosts
   718, 719 and 715, and `eff_0021.bin` starts at exactly 0x10006FE0. 100 of 109 such bins are
   verbatim, with 0 relocations inside. [V] [M]
3. **The DLL's "engine code" is thunks.** `55 8b ec a1 3c 28 07 10 8b 80 64 0b 00 00 5d ff e0` at
   magic_0021 0x100022C0, one per slot from 39 up in every DLL. 219 of the 246 slots reached only
   through them are exe ppp operators. [V] [M]
4. **No exe code is copied into any DLL.** 0 non-CRT matches by exact, shape or 4-gram MinHash, with
   a positive control at J 0.88 (0515 against 0611). [M]
5. **No MIPS in ffx_ps2.** 0 `jr ra` in dat_ov, no file in 17,913 with three or more. [M]

## 9. Doc and tool fixes made

- `EFFECT_OVERLAY.md` 2.3: the yonishi header is in **130** DLLs, not 132, and all 130 call 716, not
  131 of 132. magic_0350 and magic_0380 hold the table VA only as a loop bound (`cmp edi, table`),
  magic_0605's +0x7C is an unrelated global. "The other 449" is 451.
- `EFFECT_OVERLAY.md` 5: a note that its counts predate the tail jump fix.
- `tools/magicdll.py`: `HostCallScan` now records `jmp reg` tail calls (`FF E0` and kin) under the same
  provenance test as calls, counted as `tail_jump`. `_record_call` returns whether it recorded a
  slot. The `cmd_ovl` text says 130.
- `MAGIC_DLL.md`: summary item 2, the section 7 figures table (a tail jump column: 860 distinct
  slots, 115,728 sites, 892 called or copied), a paragraph on the thunks, and section 8 item 3 (640
  becomes 892, with the caveat that a referenced thunk says wired in, not called at run time).
- The exe IDB names on hosts 449 and 450, `maybe_FFX_DispList_PushDwordChecked 0x92ECB0` and
  `maybe_FFX_DispList_PushDword 0x92EE20`, are wrong. They push and set a Yanagi task's state
  function. Renames proposed in `tools/apply_dll_contents_names.py`, with names for 0x925140,
  0x925170, 0x925210, 0x925440, 0x9254F0, 0x92EDE0, 0x92EE00, 0x92EE60 and 0x92EE90. Not applied.

Left for the PROGRESS owner, not edited here: 1.10.2's count of unnamed host slots was made against
the call-only scan and should be redone against 892 reached and 55 never reached.

## 10. Addresses

| what | address |
| --- | --- |
| `g_ffxMagicHostApiTable`, 947 slots | 0xC64CE8 |
| host 715 `FFX_Effect_InitUnit` | 0x7FDA80 |
| host 716 `FFX_Magic_Oef2Dispatch` | 0x80CC00 |
| host 718 `FFX_Oef2_SetFromUnit` | 0x7E3E30 |
| host 719 `FFX_Oef2_GetSubTable` | 0x7E3810 |
| host 721 `opp_main` | 0x80BD50 |
| host 434 Yanagi create task, proposed `FFX_Yanagi_CreateTask` | 0x925170 |
| host 436 Yanagi init program, proposed `FFX_Yanagi_InitProgram` | 0x925210 |
| host 437 Yanagi step program, proposed `FFX_Yanagi_StepProgram` | 0x925440 |
| host 438 set the ppp heap, proposed `maybe_FFX_Yanagi_SetPppHeap` | 0x9254F0 |
| host 449 push state, proposed `FFX_YanagiTask_PushState` | 0x92ECB0 |
| host 450 set state, proposed `FFX_YanagiTask_SetState` | 0x92EE20 |
| host 451 wait N frames, proposed `FFX_YanagiTask_WaitFrames` | 0x92EE60 |
| the ppp DLL operator table, host data 921 | 0xC3BBB0 |
| magic_0021 slot 0 | 0x10001690 |
| magic_0021 `eff_0021.bin` in .data | 0x10006FE0 |
| magic_0021 first host thunk shown | 0x100022C0 |
| magic_0003 Cheer phase machine | 0x10006540 |
| magic_0112 Curaga setup | 0x10001410 |

The Yanagi task layout from hosts 449..451: state slots at +12 + 4 * depth, depth at +172 (0..7),
a per depth frame counter at +44 + 16 * depth, flag bit 2 at +0 set by the wait's pop. [V]

## 11. Residuals

- **The non-port .data was identified by matching, not by reading every byte.** WD banks, eff bin
  blocks, chr and wep blocks, ATEL and SeSep cover most of it. What is left was not classified one
  by one. [M] [I]
- **0345 diverged.** Whatever changed between the PS2 bin and the PC one was not traced. The rule
  reads the PC one, so it does not matter for the port, but it matters for anyone diffing against
  PS2. [M]
- **The families named fw539, fw527, fw183 and fw662 were not read** past their sample members.
  Their framework split is from body sharing alone.
- **The 63 DLLs whose task dispatch the scanner cannot reach** (`EFFECT_OVERLAY.md` 5) were not
  revisited after the tail jump fix.
- **Flag bit 2 on a Yanagi task** has no known reader yet.
- **The size estimate counts bytes and clusters, not porting effort.** A 16 KB phase function that
  is one long switch over data may port as a table, and a 300 byte function full of host calls may
  not.
