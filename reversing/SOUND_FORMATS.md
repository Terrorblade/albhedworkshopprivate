# Sound formats: FSB5 banks, FEV event projects and the sound id mapping

PROGRESS 1.9.1 and 1.9.2. What the shipped sound data is, how FFX.exe turns its own SE, BGM and
voice ids into FMOD events and subsounds, which part of FMOD Designer the data actually uses, and
every FMOD import with the game code that calls it. `SOUND_CLASSES.md` has the runtime classes and
threads, `VOICE_TIMING.md` the voice clock. This doc is about the files and the lookups.

Tags as in `SOUND_CLASSES.md`: **[V]** read in the binary (FFX.exe or the shipped fmod_event.dll),
**[D]** measured over the shipped data with the tools below, **[I]** inferred. FFX.exe addresses are
IDA VAs at base 0x400000. fmod_event.dll addresses are VAs at its preferred base 0x10000000.

Every listing in here is a count or a handful of examples. The full listings are tool output,
printed on demand and never saved, because sound and event names are game data.

---

## 1. Summary

1. **Everything is FMOD Ex 4.44.32.** The install ships `fmodex.dll` and `fmod_event.dll` 4.44.32.
   The banks are FSB5 version 1, IMA ADPCM, and the projects are the FMOD Designer RIFF `.fev`
   with `FMT ` value 0x00450000. [V] [D]
2. **FMOD Core 2.03 reads every bank natively, FMOD Studio refuses every project.** The installed
   Studio API 2.03.15 Core opens all 1,260 banks from memory and agrees with an independent header
   parse on all 33,362 subsounds. `Studio::System::loadBankMemory` on a `.fev` returns 69
   `FMOD_ERR_VERSION`, and Core's `createSound` on one returns 19 `FMOD_ERR_FORMAT`. [D]
3. **The event layer is almost empty.** All 33,362 events are simple events, one sound instance,
   one sound definition, one wavetable waveform, 2D. There are no parameters, no layers, no
   envelopes, no user properties, no reverbs and no interactive music data in any project. The
   only categories are `master` and `master/music`. [D]
4. **One event per subsound, exactly.** Every project has one bank, every bank has as many
   subsounds as its project has events, and no subsound is left unreferenced. [D]
5. **The game never addresses an SE or a BGM by name.** SE ids go through per project side tables
   to an event index in the `"nonloop"` or `"loop"` group. BGM tracks go through a 182 entry table
   in FFX.exe to an event index in the `"music"` group. Only voice goes by name, through
   `VoiceIDMapper.txt`. [V] [D]

---

## 2. What ships

Everything is under `ffx_data/gamedata/ps3data/sound_pc/` in `FFX_Data.vbf`. Each `.fev` has one
`_bank00.fsb` next to it. [D]

| folder | projects and banks | events and subsounds | notes |
| --- | --- | --- | --- |
| `music/` | 1 | 89 | the arranged soundtrack, 184.8 minutes |
| `music_ps2/` | 1 | 89 | the original soundtrack, 225.4 minutes, same event order |
| `sfx/us/` | 604 | 7,620 | `%04d.fev` by project number, plus `_common.txt` and `_loop.txt` |
| `sfx/jp/` | 604 | 7,452 | the same 604 numbers, different contents |
| `voice/us/` | 25 | 8,839 | 20 chapter projects, `voice270`, `voice_nagisetsu`, `voice205`, `voice_btl`, `voice_btl_iop` |
| `voice/jp/` | 25 | 9,273 | the same 25 names |
| total | 1,260 | 33,362 | plus `voice/VoiceIDMapper.txt` and `voice/<lang>/VoiceFevMapper.txt` |

Sample rates: 48,000 Hz on 32,985 subsounds, 44,100 on 187, 32,000 on 100, and 35,000 on 90 (set by
a rate chunk because FSB5 has no rate index for it). 22,843 mono, 10,519 stereo. [D]

---

## 3. FSB5 as shipped

`tools/fsb.py` has the field table in its docstring. The short form [D], checked field by field
against FMOD Core by `fsb.py check`:

- 0x3C byte header: `"FSB5"`, version 1, subsound count, header block size, name table size, data
  size, codec (7, IMA ADPCM, in all 1,260 banks), then an 8 byte bank hash at +0x24 and 16 more
  bytes. The +0x24 hash is the one the `.fev` wave bank record carries, so a project can be matched
  to its bank without names.
- Each subsound is a u64 (bit 0 more chunks, bits 1-4 rate index, bits 5-6 channels, bits 7-33
  data offset / 32, bits 34-63 length in samples) and optional chunks. Shipped chunk types are 2
  (rate, 90 times) and 3 (loop start and end, 16,886 times).
- The name table is one u32 offset per subsound and then NUL terminated names. Names are the source
  wav stems.

Core and the header parse agree on name, sample count, channels, rate and loop points for every
subsound. The only differences are 8 loop ends, 4 per language (`sfx/*/2547` #7, `2570` #5,
`2571` #1, `4093` #1), where the header's loop end is at or past the sample count and Core reports
`samples - 1`. `fsb.py check` lists them as clamped, not as disagreements. [D]

---

## 4. FEV structure

Decoded from the shipped fmod_event.dll, which reads it field by field, and checked by walking all
1,260 files to their exact chunk ends (`fev.py check`, 0 problems). `tools/fev.py` has the full
grammar in its docstring, this is the outline. [V] for the DLL reads, [D] for the files.

### 4.1 Container

| piece | content | DLL |
| --- | --- | --- |
| `RIFF 'FEV '` | little endian, chunk bodies padded to even sizes | `EventSystemI::load` 0x10034260 branches on `'FEV1'` (the old flat format) or `'RIFF'` (0x10033C30) |
| `FMT ` | a 4 byte chunk, value 0x00450000 in every file | |
| `LIST 'PROJ'` | chunk handlers matched through the table at 0x10050E30 | |
| `OBCT` | u32 n, n x {type, count}, preallocation counts | |
| `PROP` | project name | |
| `LGCY` | u32 pool size, u32 skipped, then the old FEV1 body | 0x10033BD0 |
| `EPRP` | envelope points, n x 3 dwords | 0x10033AC0 |
| `STRR` | u32 n, n offsets, string blob. Every group, event and sound definition name is an index here | 0x10031560 |
| `LANG` | language names, `default` only | 0x10031640 |

### 4.2 The LGCY body

Parsed by 0x10031FE0, which compares the project version against thresholds from 0x080000 up to
0x410000. Every shipped file is 0x450000, so every newer branch is taken and the tool reads that one
shape only.

- project name, then wave banks: `u32 nbanks, u32 nplatforms`, per bank `u32 mode, u32 max
  streams`, per platform `{u8[8] hash, u32}`, then the bank name.
- the category tree (0x1000F270), recursive: name, volume, pitch, max playbacks and behaviour,
  subcategories.
- the event group tree (0x100116A0), recursive: name index, user properties, subgroups, events.
- an event: `u32 type` (the factory at 0x10031BD0 makes 8 complex, 0x10 simple), name index, a
  16 byte GUID, a fixed block of 46 dwords (volume, pitch and their randomisation, priority, max
  playbacks, steal behaviour, mode, 3D distances and cone, 8 speaker levels, rolloff, doppler,
  reverb levels, fade in and out, spawn intensity, pan level), the type's own body through vtable
  +132 (complex 0x10021440, simple 0x10023BE0), then its category names.
- a sound instance (0x1002AE80, 58 bytes): u16 sound definition index, f32 start, f32 length,
  12 more dwords.
- a complex body would add layers, envelopes (0x10020DB0), parameters and user properties
  (0x10034680). None ship.
- sound definition configs (`EventSystemI` vtable 0x10050CD0 +52, 0x1002D8B0, 74 bytes), then
  sound definitions: name index, config index, waveforms. A wavetable waveform is source file name,
  bank index, subsound index and a length in ms.
- reverbs (name plus 33 dwords), then the music data as a `'comp'` chunk. Every project ends with
  an empty 8 byte `'comp'`.

### 4.3 The waveform length is not the subsound length

The waveform's ms length is never longer than the subsound it points at. On 1,890 of 33,362 it is
2 to 9 ms shorter than the FSB sample count says, never more [D]. Encoder padding to the ADPCM block
is the likely reason [I]. It matters for voice, whose wall clock length comes from
`Event::getInfo` (section 9).

---

## 5. The Designer features the game uses

From `fev.py summary` over all 1,260 projects. [D]

| feature | shipped |
| --- | --- |
| event types | 33,362 simple, 0 complex |
| event parameters, layers, envelopes | 0 |
| event and group user properties | 0 |
| reverb definitions | 0 |
| interactive music (`comp` data past the header) | 0 projects |
| categories | `master` and `master/music`, every event in `master` |
| groups | `nonloop` and `loop` in each SFX project, `voice` in each voice project, `music` in each music project, 2,468 in all, no subgroups |
| 2D or 3D | 2D only, mode 0x180008 on every event (0x8 is FMOD_2D, the rest look like Designer's default 3D bits) [I] for the upper bits |
| volume | 1.0 on every SFX and voice event, a per track gain on all 178 music events |
| pitch, fades, reverb send, spawn intensity | all at defaults (0, 0 and 0, 0 and 0, 1.0) |
| max playbacks | 5 on 13,096 SFX events, 30 on 4, 1 on everything else |
| sound definition configs | one per project, play mode 0, no spawning |
| waveforms | one wavetable waveform per sound definition |
| instance loop field | 1 on 31,304 instances, 0 on 2,058, loop count -1 on all [I] for the meaning |
| bank load mode | 0x200 (`FMOD_CREATECOMPRESSEDSAMPLE`) for all 1,208 SFX banks, 0x80 (`FMOD_CREATESTREAM`) for the 52 music and voice banks |

So the game uses FMOD Designer as a container for "play this subsound at this volume, at most N at
once, looping or not", and nothing more.

---

## 6. Sound id to bank and subsound

### 6.1 SE

- **Loading.** The step thread loads SFX projects by number through command 48,
  `FFX_FmodCmd_LoadSeProject` -> `FmodSfx__loadProject 0x70CF10`, which opens
  `SFX/US|JP/%04d.fev` (format strings at 0xB4FB60 and 0xB4FB70). Four step side callers [V]:
  - `sub_887240` loads up to 16 resident projects from its argument or from the list at 0xC54DB0,
    which ships as 0, 1, 2005 (0x887240..0x88726E).
  - `FFX_Se_LoadProjectFromWdChunk 0x81CF90` loads `a1[1]` from a `'WD'` chunk.
  - `sub_81E3B0` loads the int16 at +8 of a header whose +12 size is 1..0x10000.
  - `sub_81E600` walks `"SeSep   "` records and loads each distinct u16 at +17.
- **The side tables.** `SfxProject__ctor 0x70F760` loads the `.fev`, then
  `SfxProject__loadCommonTable 0x70FF50` takes group `"nonloop"` through `EventProject` vtable +8
  (`push offset aNonloop` / `call dword ptr [eax+8]` at 0x70FFD5..0x70FFDB) and `sub_710370` reads
  `<project>_common.txt` as `{u32 sequence, u32 event index}`. `SfxProject__loadLoopTable 0x710780`
  does the same with `"loop"` and `<project>_loop.txt` as `{u32 sequence, u32 n, u32 index[n]}`
  (`sub_710C50`). The lookups (`sub_710290`, `sub_710AE0`) end in `EventGroup` vtable +36,
  `getEventByIndex`. A loop sequence can start several events. [V]
- **Starting.** `FmodSfx__start 0x70C8F0` asks the map projects (the tree at `FmodSfx+0x34`)
  from its last key back, then the resident ones (`FmodSfx+4`). The key is the project number [I]. In each, `SfxProject__findSequence`
  tries the common table, then the loop table. The first project that has the id wins. [V]
- **Battle streaming.** `SFX/US|JP/9999.fev` is loaded separately as the battle streaming project
  (`SOUND_CLASSES.md` section 8). [V]

Shipped: 1,308 side tables, 13,634 rows, 6,774 distinct sequence ids, all resolving to an event in
range [D]. 6,613 of the 6,774 ids appear in more than one project, so **what an SE id plays depends
on which projects are loaded and in which order**, not on the id alone. No map project uses an id of
127 or less. The resident projects use both ranges.

`fev.py seq` prints sequence id -> project, group, event index for every row.

### 6.2 BGM

- `FmodMusic__initData_2 0x708C00` loads `Music/ffx_music.fev` and `Music_PS2/ffx_music_PS2.fev`,
  each through `FmodMusic__initData 0x708D40`, which takes group `"music"` (0x708E29..0x708E2F). [V]
- `FmodMusic__loadTrack 0x709080` turns a track into an event index with `sub_708BE0`, which reads
  `g_ffxFmodBgmEventIndex[track]` at 0xB4F1C8 (`mov eax, ds:g_ffxFmodBgmEventIndex[eax*4]` at
  0x708BED, -1 past 0xB5), then calls `EventGroup` vtable +36 (`call dword ptr [ecx+24h]` at
  0x709158). The same index is used in whichever of the two projects the soundtrack setting picks. [V]
- `FmodMusic__loadBattleTrack 0x708A00` hardcodes event index 6 into slot 16, which is also what the
  table holds for track 16. [V]

The table has 182 entries. Tracks 0 to 9 are -1. 92 tracks have an event, and they cover all 89
music events, so a few events serve more than one track [D]. Track 16 is event 6, `1_09_battle_theme`.
`fev.py bgm` reads the table straight out of FFX.exe and prints track -> event -> subsound with its
length, `--ps2` for the other project.

### 6.3 Voice

- `FmodVoice__initList 0x70AB90` loads `Voice/VoiceIDMapper.txt` (8,730 rows, voice id -> line
  name) and `Voice/US|JP/VoiceFevMapper.txt` (337 rows, map -> project name). Both are `"ML\0\0"`,
  u32 count, then {u32 key, u32 string offset} pairs. [V] [D]
- `FmodVoice__dataChange 0x70A630`, on a map change, unloads the map's voice projects and loads
  every project the mapper lists for the map. The slot is the project name's last two digits,
  `c1 + 2 * (5 * c0 - 264)`, which is `10 * (c0 - '0') + (c1 - '0')`, except that it is 0 on maps
  393..398 (`g_ffxFmodVoiceBusyDisabled`) and on map 270. Each project's `"voice"` group is taken
  through `EventProject` vtable +8 and preloaded (`EventGroup` +4) on three special cases: map 368
  with `voice01`, map 270 with `voice270`, map 127 with `us_voice08`. [V]
- `FmodVoice__prepare 0x70ADD0` maps the voice id to a line name. Ids above 0x33000000, plus
  137630980, 206853380, 678712580, 678716676 and 1284, go to the battle project
  `ffx_<lang>_voice_btl.fev` (loaded once by `FmodVoice__initData 0x70AA30`). Every other id takes
  its slot from the first two characters of the name with the same formula (`movsx` / `lea
  ecx, [eax+eax*4]` / `lea ecx, [ecx-108h]` / `lea ecx, [eax+ecx*2]` at 0x70AEF8..0x70AF08), again
  forced to 0 on the special maps. Then `EventGroup` vtable +32, `getEvent(name)`, and
  `Event::getInfo`, whose `lengthms` becomes the wall clock length. [V]
- The 337 mapper rows name 22 projects per language: 20 chapter projects, `voice270` (map 270)
  and `voice_nagisetsu` (maps 393, 394, 395, 396, 398). Map 397 has no row, so nothing is loaded
  there. `voice205` and `voice_btl_iop.fev` are in no mapper row and no FFX.exe string, so the game
  never loads them. [D] [V]

`fev.py voice --lang us|jp` resolves every VoiceIDMapper row the way the game does:

| | US | JP |
| --- | --- | --- |
| an event | 8,621 | 8,633 |
| a shout subsound (6.4) | 44 | 44 |
| in no project at all | 65 | 53 |

### 6.4 Shouts

Battle shouts are not events. `FmodShout__initData 0x70E2D0` opens
`Voice/<lang>/ffx_<lang>_voice_btl_iop_bank00.fsb` with `createSound`. `FmodShout__play 0x70E480`
gets the line name from the VoiceIDMapper blob that `FmodVoice__playShout 0x70B430` hands it, walks
the subsounds with `getSubSound` and `getName` (0x70E57E, 0x70E5A4) until the name matches, then
`playSound` (0x70E632). The bank has 45 subsounds, 44 of which a VoiceIDMapper id names. [V] [D]

---

## 7. US and JP are different data

Both languages ship the same 604 SFX project numbers, but the contents differ. 0201 has 20 events
in US and 5 in JP, for example. Comparing every sequence id the two languages share in the same
project number, 6,733 resolve in both, 16 of those resolve to subsounds of different lengths (for
example 3,008 ms against 3,141 ms), and 168 more exist in one language only. The voice projects have the
same names but 8,839 events in US against 9,273 in JP, and 65 against 53 voice ids that resolve
nowhere. [D]

---

## 8. The 40 FMOD imports

FFX.exe imports 17 functions from fmod_event.dll and 23 from fmodex.dll. Everything else the game
does with FMOD Designer objects goes through vtables: `EventProject` (vtable 0x100508C0, +0
release, +4 getInfo, +8 getGroup, +32 loadSampleData, +40 cancelAllLoads) and `EventGroup` (vtable
0x100500C8, +4 loadEventData, +32 getEvent, +36 getEventByIndex). The slot names come from the DLL's
own mangled vtable entries. [V]

`-` in the callers column is code with no function. The orphans are named in
`tools/apply_sound_format_names.py`.

### fmod_event.dll (17)

| import | game callers |
| --- | --- |
| `FMOD_EventSystem_Create` | `FfxFmod__init` 0x70684C, orphan wrapper 0x7066A0 |
| `EventSystem::init` | `FfxFmod__init` 0x7068CA |
| `EventSystem::getSystemObject` | `FfxFmod__init` 0x706884 |
| `EventSystem::load` | `FmodMusic__initData` 0x708DE8, `FmodVoice__dataChange` 0x70A81E, `FmodVoice__initData` 0x70AB0C, `SfxProject__ctor` 0x70F7A3 |
| `EventSystem::update` | `FfxFmod__tick` 0x70716B, `FfxFmod__updateEventSystem` 0x707432 |
| `EventSystem::unload` | `FfxFmod__shutdown` 0x707036 |
| `EventSystem::release` | `FfxFmod__shutdown` 0x707058 |
| `Event::start` | `FmodMusic__startDeferred`, `FmodMusic__play` (2), `FmodVoice__startDeferred`, `FmodVoice__start`, `SfxEvent__start` |
| `Event::stop` | `FmodMusic__stop`, `FmodMusic__stopAll`, `FmodVoice__stop`, `SfxEvent__stop` |
| `Event::getState` | 16 sites in `FmodMusic__tickFades`, `startDeferred`, `play`, `stop`, `stopAll`, `isBattleTrackLoading`, `fadeOutAndStop`, `fadeTrackVolume` and `FmodVoice__reapFailedEvent`, `stepVolumeFade`, `startDeferred`, `setPaused`, `start`, `stop`, `setVolume`, `beginVolumeFade` |
| `Event::setCallback` | `FmodMusic__startDeferred`, `play` (2), `stop`, `stopAll`, `FmodVoice__startDeferred`, `start`, `stop`, `SfxEvent__dtor`, `SfxEvent__start` |
| `Event::getInfo` | `FmodVoice__prepare` 0x70B06D, `SfxEvent__isLooping` 0x70ED13, orphan 0x70EB40 |
| `Event::getPaused` | `FmodMusic__fadeOutOthers`, `stop`, `stopAll` |
| `Event::setPaused` | `FmodMusic__crossfadeTo`, `setAllPaused`, `stop` (2), `stopAll` (2), `FmodVoice__setPaused`, `SfxEvent__setPaused`, and 0x708FEB, a chunk of `FmodMusic__tickFades` |
| `Event::getVolume` | `FmodMusic__fadeOutOthers`, `FmodVoice__beginVolumeFade`, `SfxEvent__fadePan`, `fadeVolume`, `stop`, orphan 0x70EC00 |
| `Event::setVolume` | `FmodMusic__setCategoryVolume`, `setMuted`, `setTrackVolume`, `applyPendingVolume`, `FmodVoice__setVolume`, `SfxEvent__setVolume` |
| `Event::getChannelGroup` | `SfxEvent__isLooping`, `SfxEvent__setPan`, orphan 0x70EB90 |

### fmodex.dll (23)

| import | game callers |
| --- | --- |
| `FMOD_Memory_Initialize` | `FfxFmod__init` 0x706839, orphan wrapper 0x7066B0 |
| `FMOD_Memory_GetStats` | `FfxFmod_CheckResult` 0x707A16, 0x70680E (reached only from the orphan 0x6FA260), orphan 0x707A8E |
| `System::setFileSystem` | `FfxFmod__init` 0x706906 |
| `System::setStreamBufferSize` | `FfxFmod__init` 0x7068A5 |
| `System::createSound` | `FmodShout__initData` 0x70E38C, `PVideoPlaybackWin32_openAudio` 0xA275F9 |
| `System::playSound` | `FmodShout__play` 0x70E632, `PVideoPlaybackWin32_openAudio` 0xA2761D |
| `Sound::release` | `FmodShout__initData`, `FmodShout__release`, `PVideoPlaybackWin32_closeAudio` |
| `Sound::getNumSubSounds` | `FmodShout__initData` 0x70E3CB |
| `Sound::getSubSound` | `FmodShout__play` 0x70E57E |
| `Sound::getName` | `FmodShout__play` 0x70E5A4 |
| `Sound::getUserData` | `PVideoPlaybackWin32_fmodPcmReadCallback` 0xA27EF2 |
| `Sound::setUserData` | `PVideoPlaybackWin32_openAudio` 0xA27605 |
| `Sound::getOpenState` | `PVideoPlaybackWin32_closeAudio`, `isPlayerActive`, and 0xA27DCF in `PVideoPlaybackWin32__startReadingThread` |
| `Channel::setPriority` | `PVideoPlaybackWin32_openAudio` 0xA27629 |
| `Channel::setPaused` | `PVideoPlaybackWin32_pause` (2), and 0xA28307 in `PVideoPlaybackWin32__startVideoDecodingThread` |
| `Channel::stop` | `PVideoPlaybackWin32_closeAudio` 0xA2790A |
| `Channel::setVolume` | `FmodShout__setVolume` 0x70E436, `PVideoPlaybackWin32_setVolume` 0xA28340 |
| `Channel::getVolume` | `FmodShout__setVolume` 0x70E419 |
| `Channel::setPan` | `SfxEvent__setPan` 0x70EF54 |
| `Channel::getPan` | orphan 0x70EB90 |
| `Channel::getLoopCount` | `SfxEvent__isLooping` 0x70ECD1 |
| `ChannelGroup::getChannel` | `SfxEvent__isLooping`, `SfxEvent__setPan`, orphan 0x70EB90 |
| `ChannelGroup::getNumChannels` | `SfxEvent__isLooping`, `SfxEvent__setPan` |

Every import has a live caller except `Channel::getPan`, which only the orphan 0x70EB90 calls. The
movie player (`PVideoPlaybackWin32_*`, `MOVIE_DECODE.md`) is the only other FMOD user, through the
same `System` that `FmodManager__getFmodSystem` hands out. There is no pitch, frequency, 3D,
parameter, reverb or DSP call anywhere. [V]

---

## 9. FMOD Studio refuses the files

`fsb.py refuse` loads a `.fev` from memory into the installed FMOD Studio API 2.03.15 (64-bit
`fmodstudio.dll`, header version 0x00020315):

    file   sfx/us/0201.fev, 7538 bytes, starts b'RIFFj\x1d\x00\x00FEV FMT '
    FMOD Studio 20315  Studio::System::loadBankMemory  -> 69 FMOD_ERR_VERSION
    FMOD Core   20315  System::createSound (OPENMEMORY)  -> 19 FMOD_ERR_FORMAT

The music project gives the same two codes. Studio reads `.bank` files only, and the 4.x
Designer `.fev` is a different format, not an older version of the same one. The banks, on the
other hand, open in the same Core with `FMOD_OPENMEMORY | FMOD_CREATECOMPRESSEDSAMPLE`. [D]

---

## 10. Tools

- `tools/fsb.py` - `banks`, `names` (every subsound name per bank, through FMOD Core by default,
  `--via header` for the parser), `summary`, `check` (Core against the parser), `refuse`.
- `tools/fev.py` - `projects`, `events [--names]` (every event with group path, parameters, and the
  bank and subsound it plays), `dump`, `summary`, `check` (all projects parsed, every waveform
  against its FSB5 bank, every side table against its group), `seq`, `bgm [--ps2]`,
  `voice [--lang]`.
- Both read the archive in memory through `tools/vbf.py`. Run them with `python -I`. `fsb.py` needs
  the FMOD Studio API installed (`--sdk` for another path).
- `tools/apply_sound_format_names.py` - the IDB names for this doc's functions and globals, same
  rules as `apply_sound_names.py`, dry run by default.

---

## 11. Co-op and recreation hazards

1. **The soundtrack setting changes lengths.** Same event index, different recordings: track 13 is
   151,579 ms arranged and 37,442 ms PS2, track 16 is 102,661 ms and 201,906 ms. Anything that waits
   for music to end (A2 in `SOUND_CLASSES.md`) ends on different steps on two peers with different
   settings. [D]
2. **The language changes the SE set and some lengths.** Section 7. A4 ("is this SE playing")
   differs between a US peer and a JP peer on 16 shared ids and on 168 one sided ones. Voice differs
   more, by line count and by which ids exist at all. [D]
3. **An SE id is not a sound.** What it plays depends on the loaded project set and its order (map
   projects from the highest key down, then resident). The load set comes from step side commands, so lockstep
   peers agree, but a recreation has to keep both the set and the search order, not a flat id table.
   [V] [D]
4. **A voice id that resolves nowhere is a per language difference.** When `getEvent` fails,
   `FmodVoice__prepare` leaves `+20` NULL, skips `Event::getInfo` and does not touch +108 or +96
   (0x70AF63..0x70B039). 65 US and 53 JP ids do this, plus every voice id on map 397, which has no
   project. What the start path then does with no event decides the busy time, and it is not the
   same as for a line that exists, so peers on different languages can disagree on voice busy.
   [V] for the prepare path, [D] for the counts, [I] for what start does next.
5. **Waveform length against sample length.** The event `lengthms` that voice timing uses is 2 to
   9 ms shorter than the FSB sample count on 1,890 subsounds. A recreation that derives voice length
   from samples can land one 33 ms step late on a line whose length sits near a step boundary.
   Which of the two FMOD Ex reports through `getInfo` is not checked [I].
6. **For the recreation.** FMOD Studio cannot load these projects, and FMOD Ex is 32-bit only. The
   banks load in Core 2.x as they are, and the event layer is small enough to replace with a table
   per project: event index or name -> subsound, volume, max playbacks (1, 5 or 30), loop. The
   music per track volume and the SE max playbacks are the only fields that change what is heard
   or what "playing" reads. Clamp loop ends to `samples - 1` the way Core does.

---

## 12. Names

`tools/apply_sound_format_names.py`, dry run against a copy of the IDB: 15 functions and 2 globals,
0 skipped.

| address | proposed name |
| --- | --- |
| 0x887240 | `FFX_Se_LoadResidentProjects` |
| 0x81E3B0 | `maybe_FFX_Se_LoadProjectFromHeader` |
| 0x81E600 | `FFX_Se_LoadSeSepProjects` |
| 0x710370, 0x710290 | `SfxCommonTable__load`, `SfxCommonTable__getEvent` |
| 0x710C50, 0x710AE0 | `SfxLoopTable__load`, `SfxLoopTable__getEvents` |
| 0x70EB40, 0x70EB90, 0x70EC00 | `maybe_SfxEvent__getNumInstances`, `maybe_SfxEvent__getPan`, `maybe_SfxEvent__getVolume` (orphans) |
| 0x7066A0, 0x7066B0 | `FfxFmod_CreateEventSystemOrphan`, `FfxFmod_MemoryInitializeOrphan` |
| 0x7067F0, 0x6FA260, 0x707A70 | `FfxFmod__getMemoryCurrentAlloc`, `maybe_FmodManager__getMemoryCurrentAlloc`, `FfxFmod_GetMemoryCurrentAllocOrphan` |
| 0xC54DB0 | `g_ffxSeResidentProjects` |
| 0x12FB3C0 | `g_ffxSeSepProjectIds` |

`g_ffxFmodBgmEventIndex 0xB4F1C8` is already in `apply_sound_names.py`.

---

## 13. Open

- The meaning of the sound instance loop field (1 or 0) and the 12 dwords after start and length.
  Only their position is known.
- Whether FMOD Ex's `FMOD_EVENT_INFO.lengthms` comes from the waveform's stored ms length or from
  the subsound. Settles hazard 5.
- What the `"SeSep   "` and `'WD'` records are in the game's own data, and which loader feeds
  `sub_81E3B0`.
- The upper bits of event mode 0x180008.
