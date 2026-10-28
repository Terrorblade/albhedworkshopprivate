# Movie decode: from a movie id to a frame on screen and a sample in the speakers

PROGRESS 1.9.6 of Project Zanarkand. The decode pipeline of the PC FFX movie player: the file read,
the WebM demux, VP8, Vorbis, the frame queues, the sasa task library, the texture upload and the rule
that keeps sound and picture together. For every stage it says which thread runs it and how that is
known. It then answers three questions: what is presentation only, where the simulation reads movie
progress, and whether the decoder can move the step a movie ends on.

All addresses are VAs (RVA + 0x400000). Read with idalib on a scratch copy of the IDB, nothing renamed
and nothing saved. The container facts come from reading the first 64 KiB chunk of three `.webm`
entries of `FFX_Data.vbf` in memory through `tools/vbf.py`. Nothing was written to disk and no movie
data is in either repo.

Marks: **[V]** read in the binary, **[D]** read in the shipped data, **[I]** inferred.

Related: `FMV_SYNC.md` (the co-op barrier on the movie waits, the skip), `ATEL_MOVIE_SAVE_LIBRARIES.md`
section 1 (the 16 Movie syscall rows), `THREADS_AND_CLOCKS.md` T10, T11, T12 and C2, `VISUAL_CENSUS.md`
section 9 (the FMV frame), `ASYNC_LOAD_REQUESTS.md` (the Video.cgfx load), `VBF_FORMAT.md`,
`MAIN_LOOP.md` (the step).

Proposed names are in `tools/apply_movie_decode_names.py` (section 15). Names in this document that
start with `PVideoPlaybackWin32_`, `CDataFrameQueue__`, `vpx_`, `vorbis_`, `mkvparser__` or
`FFX_FmvMgr_` are mostly those proposals, and the section 15 table maps each to its address.

---

## Summary, read this first

1. **The movies are WebM: VP8 video, Vorbis audio, about 29.97 fps.** 98 `.webm` entries in
   `FFX_Data.vbf` under `ffx_data/gamedata/ps3data/video/` and `metamenu/`. Muxed by Lavf56.19.100,
   TimecodeScale 1 ms, no FrameRate element, DefaultDuration 33366666 ns, audio 48 kHz stereo. Some
   (for example `1_480_new.webm`) have no audio track. The decoders are statically linked libwebm
   mkvparser, libvpx 1.4.0 and libvorbis 1.3.5 with libogg. The sound goes out through FMOD Ex. [V] [D]
2. **Five threads touch a movie, and the step thread does more than anyone expected.** The open,
   the whole container parse, the codec init and the FMOD sound create all run on the step thread,
   inside the movie:0 start opcode. A reading thread demuxes, a decoding thread runs VP8, FMOD's own
   stream thread runs the Vorbis decode inside a PCM callback, and the frame upload runs on the step
   thread (or on the Phyre render thread for videos 46, 72, 32 and looping movies). Section 1. [V]
3. **The master clock of the picture is the simulation step.** One frame is consumed per
   `FFX_MainStep`, and once the 30 frame prebuffer is met the consume **blocks** on an empty queue
   (`WaitForSingleObject(INFINITE)` at 0xA3DFE9). Video never drops a frame and never skips ahead.
   The presented time is `(frames consumed + 1) * 33.37 ms`, a count, not a clock. Section 9. [V]
4. **The decoder has no clock.** The `GetTickCount` stamp at 0xA27F69 goes to `dword_1A84C80`, and
   that store at 0xA27F84 is the only reference to the variable. libvpx runs single threaded because
   the player passes a NULL config (0xA27FBA). That closes `THREADS_AND_CLOCKS.md` C2 and T12. [V]
5. **Sound runs on FMOD's output clock and nothing resyncs it.** Audio is gated on the first consumed
   video frame and paused together with the picture. After that the two run free: video at the step
   rate, audio at 48 kHz real time. Section 9. [V]
6. **The simulation reads movie progress in six places, and after the movie:9 barrier all of them are
   step deterministic on the main path.** The frame index (movie:3, the movie camera, the FMV branch
   of WaitFrames and StepMoveCmd), the first frame byte (movie:9, movie:10), the end test (movie:11),
   IsPlaying (movie:1, the catch-up gate, the menu gate) and the file length (movie:12). Section 10.
   [V]
7. **movie:10 does not play a movie on PC.** `FFX_Fmv_StartVideo` returns 1 only when the sampler
   state create fails, and movie:10's start skips its bookkeeping when it reads 0. So on a normal
   start nothing calls `FFX_Fmv_SetFullscreenMode`, the decode thread is never made and the poll ends
   at once. `FMV_SYNC.md` section 7 and `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.5 read this the other way
   round. Section 14. [V] for the code, [I] for what the three packages look like when run.
8. **`FFX_Fmv_IsDecoderBusy` never looks at the decoder and is always 0.** Section 10.4. [V]
9. **The decoder cannot move the step a movie ends on, with four exceptions**: the render thread path
   (46, 72, 32 and loops), a local pause or focus loss, the local skip, and a file whose frames run
   out before its declared Duration. Section 11. [V] and [I] as marked there.

---

## 1. Threads at a glance

| stage | thread | how it is known | main functions | hands off through |
|---|---|---|---|---|
| id to path | step | `FFX_Fmv_CreatePlayer` is reached from `FFX_Fmv_StartVideo`, which movie:0 and movie:10 start call inside the ATEL VM | `FFX_Fmv_ParseVideoList 0x6D82F0` (boot), `FFX_Fmv_LookupVideoPath 0x6D93E0`, `FFX_Fmv_CreatePlayer 0x6D9C80` | path string at mgr+0x4F0 |
| shader load | AsyncLoad worker | `FFX_Fmv_OnVideoShaderLoaded 0x6DA460` is the callback of `AsyncLoadManager__enqueueWithContext` (`ASYNC_LOAD_REQUESTS.md`) | `FFX_Fmv_OnVideoShaderLoaded`, `FFX_Fmv_BindVideoMaterial 0x6D86E0` | material and mesh at mgr+0x360, mgr+0x388 |
| open, parse, codec init, FMOD create | step | `FFX_Fmv_SetFullscreenMode 0x6427C0` is called from movie:0 start and movie:9 resi, and reaches `PVideoPlaybackWin32__startVideoDecodingThread 0xA27F20` through `FFX_FmvMgr_StartOrResume 0x6D7430` and `FFX_FmvMgr_PlayPath 0x6D8FC0` | `startVideoDecodingThread`, `VbfMkvReader__Open 0xA3E1F0`, `mkvparser__Segment__Load 0x421450`, `vpx_codec_dec_init_ver 0x4094C0`, `PVideoPlaybackWin32_openAudio 0xA273B0` | the player at mgr+0x38 |
| file read and demux | `PVideoPlaybackWin32::_ReadingThread` | created by `Phyre_Thread_Create` in `PVideoPlaybackWin32__startReadingThread 0xA27CF0` with body `PVideoPlaybackWin32___ReadingThread_proc 0xA27880` and that name string | `PVideoPlaybackWin32_readAllBlocks 0xA27650`, `VbfMkvReader__Read 0xA3E2B0` | packet queues at player+0 (video, 4 MiB) and player+0x80 (audio, 1 MiB) |
| VP8 decode | `PVideoPlaybackWin32::_VideoDecodingThread` | created in `startVideoDecodingThread` with body `0xA27B90` and that name string | `PVideoPlaybackWin32_decodeLoop 0xA279B0`, `vpx_codec_decode 0x409680`, `vpx_codec_get_frame 0x4096E0` | decoded frame queue at player+0x40 (30 frames) |
| Vorbis decode | FMOD stream thread inside fmodex.dll | the work is `PVideoPlaybackWin32_fillPcm 0xA27170`, reached only through `PVideoPlaybackWin32_fmodPcmReadCallback 0xA27EE0`, which is stored as `pcmreadcallback` at 0xA275CF. FMOD calls a user stream's read callback from its stream thread [I, upstream behaviour] | `fillPcm`, `vorbis_synthesis_pcmout 0xA90A10`, `vorbis_synthesis 0xA92460` | the FMOD stream buffer |
| texture upload | step, or Phyre render thread | `FFX_Fmv_StepPresentation 0x6D7680` from `FFX_Fmv_StepFromMainStep 0x645DF0` from `FFX_MainStep` at 0x820B2F. When mgr+0x570 is set the same work is queued with `Phyre_PRenderer_QueueCallback 0x5B5880` as `FFX_Fmv_UploadFrameJob 0x6DAE00` | `PVideoPlaybackWin32_ConsumeFrameToTextures 0xA284A0`, `PVideoPlaybackWin32_copyPlane 0xA26FE0` | Y, U, V textures at mgr+0x38C, +0x400, +0x474 |
| draw | recorded on the main thread, executed by the render thread | `FFX_Fmv_DrawFrame 0x6D7570` is called from the scene render `sub_653960` and from 0x642C4B. `MAIN_LOOP.md` and the present notes put execution on the render thread [I] | `FFX_Fmv_DrawFrame` | the frame |

There are no libvpx worker threads and no sasa threads. Sections 4 and 7.

---

## 2. Stage 1: from a movie id to a file read

**The list.** At boot `FFX_Fmv_ParseVideoList 0x6D82F0` (from `FFX_Boot_FirstAsyncLoad`) reads
`FFX_VideoList.txt` for the text region (JP, US or Asia) into a `std::map<int, path>` at mgr+0x6E8.
[V]

**The player create.** `FFX_Fmv_StartVideo 0x641690` calls `FFX_Fmv_CreatePlayer 0x6D9C80` on the
manager singleton (`FFX_Fmv_GetManager 0x6D73A0`). CreatePlayer, on the step thread:

- looks the id up with `FFX_Fmv_LookupVideoPath 0x6D93E0` and copies the path to mgr+0x4F0. A miss
  prints "didn't find FMV" and jumps to the common return at 0x6DA184 (`jmp` at 0x6D9E19). [V]
- clears `g_ffxFmvSkipRequested`, stores the loop flag at mgr+0x6FE, and sets mgr+0x570 when the id
  is 46, 72 or 32 or the movie loops. mgr+0x570 sends the upload to the render thread (section 8).
- builds the three plane textures at mgr+0x38C, +0x400 and +0x474.
- queues `Video.cgfx` through the async loader with the callback `FFX_Fmv_OnVideoShaderLoaded
  0x6DA460`, which runs on the AsyncLoad worker and calls `FFX_Fmv_BindVideoMaterial 0x6D86E0` to bind
  the `YChannel`, `UChannel` and `VChannel` samplers and build the mesh at mgr+0x388.
- calls `FFX_FmvMgr_OpenPlayer 0x6D8F30`, which calls `startReadingThread(mgr+0x38, 0, prebuffer 0,
  audio = sub_6FA200(g_ffxFmodManager))` and sets HasPlayer (mgr+0x6D2) = 1.
- returns 0 at 0x6DA184 (`xor eax, eax`) on success and on the miss. The only nonzero return is the
  result of `sub_58CF80`, the sampler state create, when it fails (`jnz loc_6DA186` at 0x6D9E67,
  which jumps past the `xor`). [V]

So CreatePlayer only starts the reading thread, and that thread sleeps on its wake event. **Nothing is
read and nothing is decoded until the play call.**

**The play call.** movie:0 start calls `FFX_Fmv_SetFullscreenMode(1) 0x6427C0`, which is
`FFX_FmvMgr_StartOrResume 0x6D7430`. The first time it latches the fullscreen byte mgr+0x6FD and,
because a path is set, calls `FFX_FmvMgr_PlayPath 0x6D8FC0(path, loop, fullscreen)`. PlayPath zeroes
the index pair mgr+0x6D8 and +0x6E0, calls `PVideoPlaybackWin32__startVideoDecodingThread 0xA27F20` on
mgr+0x38 (a return of -3, file not opened, raises a message box), and sets IsPlaying (mgr+0x6D0) = 1.
Once a frame is up, StartOrResume only unpauses (`FFX_FmvMgr_Unpause 0x6D8F80`) and restores the volume
from `g_ffxFmvVolume 0xC34420`. That is what movie:9's resi uses it for. [V]

**The read.** `VbfMkvReader` is the libwebm `IMkvReader` over the archive. Vtable 0xB7A9FC: slot 0
`Read(long long pos, long len, unsigned char *buf)` at 0xA3E2B0 (the IDB calls it a destructor, it is
not), slot 1 `Length` at 0xA3E1B0, slot 2 the deleting destructor at 0xA3E0C0. `VbfMkvReader__Open
0xA3E1F0` makes an `FFX_fios_FileHandle` through `FFX_fios_openFile 0x607F40`, which tries the VBF
first and the loose file system on a miss. Reads go through `FFX_fios_FileHandle__read` and
`FFX_VbfStream__readAndAdvance 0x61AA60`, which inflates the 64 KiB zlib chunks (`VBF_FORMAT.md`). The
reader is at player+0x118 and it is used on two threads: the step thread during the parse and the
reading thread after it. Never both at once, because the reading thread is woken only after the parse.
[V]

---

## 3. Stage 2: the Matroska and EBML demux

libwebm `mkvparser`, matched by its asserts, which name `..\third_party\libwebm\mkvparser.cpp` and the
upstream expressions (section 15 has which assert pins which function). Virtuos added an allocator
hook, `FFX_Mkv_SetAllocators 0x41E5B0` (`g_mkvMalloc 0x22FB510`, `g_mkvFree 0x22FB514`). [V]

**On the step thread**, inside `startVideoDecodingThread`:

1. `mkvparser__Segment__CreateInstance 0x41FD50(reader, 0, &segment)`.
2. `mkvparser__Segment__Load 0x421450`, which parses the headers **and every cluster** of the file.
   For a two minute movie that is a long run of small reads and inflates on the step thread, inside
   the movie:0 opcode. [V] for the call, [I] for the cost.
3. The track scan: `GetTracks`, `GetTracksCount`, `GetTrackByIndex`, `Track::GetType`. Type 1 goes to
   player+0x290, type 2 to player+0x294. When audio is off (player+0x298 is 0) the audio track pointer
   is cleared, so its blocks are read and dropped.
4. `fps = VideoTrack::GetFrameRate`, 29.97002997 when that is 0 (it is 0 for every shipped movie, no
   FrameRate element). player+0x108 = 1000 / fps, the frame period in ms as a double. [V] [D]
5. Width and height to player+0x14C and +0x150, the plane offsets and sizes to +0x2C0..+0x2CC and the
   frame size to +0x2D0.
6. player+0xF8 = `Segment::GetDuration() / 1e6`, the container Duration in ms, stored at 0xA28148.
   This is the movie's end time. It is NOT the number of frames in the file. [V]

**On the reading thread**, `PVideoPlaybackWin32_readAllBlocks 0xA27650` walks the parsed clusters
(`Segment::GetFirst/GetNext`, `Cluster::GetFirst/GetNext`, `Block::GetTrackNumber/GetFrameCount/
GetFrame`). A frame of the video track gets a slot in the packet queue at player+0, a frame of the
audio track a slot in the queue at player+0x80, and each slot is filled by `VbfMkvReader__Read`
straight into the queue (`beginWrite`, read, `endWrite`). With no audio queue the audio blocks are read
into the 1 MiB scratch `g_ffxFmvDiscardBlockBuffer 0x1984C80` and thrown away. A looping movie
(player+0xF0) goes back to the first cluster. At the end both packet queues are closed, the reader
state player+0xE8 becomes 2 and the done event player+0x2B4 is set. [V]

The reading thread never waits on time. It runs as fast as the queues let it. [V]

---

## 4. Stage 3: VP8

libvpx 1.4.0 (`"WebM Project VP8 Decoder v1.4.0"` in the iface at 0xB7F728). [V]

**Init, step thread**, in `startVideoDecodingThread`:

```
00A27FA4  call vpx_mem_set_functions          ; malloc calloc realloc free (0xC6B274..0xC6B280)
                                              ; memcpy memset memmove shims 0xA26FB0/0xA26FD0/0xA26FC0
00A27FBA  push 9                              ; VPX_DECODER_ABI_VERSION
00A27FBC  push 0                              ; flags
00A27FBE  push 0                              ; cfg = NULL
00A27FC0  call vpx_codec_vp8_dx
00A27FC5  push eax
00A27FC6  lea  eax, [edi+130h]                ; the codec context at player+0x130
00A27FCD  call vpx_codec_dec_init_ver
```

With `cfg` NULL the decoder's private config stays zeroed, so `max_threads` is 0 when
`vp8_create_decoder_instances 0x40AE10` (the IDB calls it `vp8_dx_init`) reaches
`vp8_decoder_create_threads`, and no worker is created. [V] for the call site and the copy of
`oxcf->max_threads` into pbi+15704, [I] that upstream 1.4.0 leaves it 0 on a NULL config.

**Decode, `PVideoPlaybackWin32::_VideoDecodingThread`.** `PVideoPlaybackWin32_decodeLoop 0xA279B0`:

1. `beginRead` on the packet queue (blocks while empty and open), `vpx_codec_decode`, `endRead`.
2. `vpx_codec_get_frame`, then `beginWrite` of the frame size on the decoded queue at player+0x40
   (blocks while full), copy planes 0, 1, 2 of the `vpx_image_t` (+36, +40, +44, strides +52, +56,
   +60) into the slot as packed I420, `endWrite`.
3. Exit on the stop flag player+0xEC or on a closed, drained packet queue. On exit it closes the
   decoded queue and clears the decoder alive byte player+0x154. [V]

No time is read on this thread either. Its pace is set by the decoded queue, which holds 30 frames
(`prebuffer * (frameSize + 4)`). [V]

---

## 5. Stage 4: Vorbis, and where the samples go

libvorbis 1.3.5 (`"Xiph.Org libVorbis I 20150105"`) on libogg. Virtuos turned libogg's allocators into
pointers (`g_oggMalloc 0xC8B248` and its three siblings) set by `FFX_Ogg_SetAllocators 0x42E1D0`. [V]

**Open, step thread**, `PVideoPlaybackWin32_openAudio 0xA273B0`, called from `startVideoDecodingThread`
when there is an audio track:

1. `vorbis_info_init`, `vorbis_comment_init`, then the three header packets split out of the
   Xiph laced `CodecPrivate` (`Track::GetCodecPrivate 0x427B80`, 3953 bytes in the sampled files) and
   fed to `vorbis_synthesis_headerin` three times through the `ogg_packet` at player+0x160.
2. `vorbis_synthesis_init` (dsp at player+0x1B0), `vorbis_block_init` (block at player+0x220).
3. The audio packet queue at player+0x80, 1 MiB.
4. FMOD `System::createSound(0, 0x4C8, &exinfo)`. 0x4C8 is OPENUSER | CREATESTREAM | SOFTWARE | 2D.
   exinfo: cbsize 0x88, length = duration * rate * 2 * channels bytes, numchannels, defaultfrequency,
   format 2 (PCM16), decodebuffersize 0x1000, `pcmreadcallback = PVideoPlaybackWin32_fmodPcmReadCallback`
   (stored at 0xA275CF). Then `playSound` and `setPriority(0)`. Sound at player+0x29C, channel at
   +0x2A0. [V]

**Decode, FMOD's stream thread.** The callback 0xA27EE0 fetches the player from `Sound::getUserData`
and calls `PVideoPlaybackWin32_fillPcm 0xA27170`, which:

- returns at once while the prebuffer byte player+0x2A8 is set, so the stream plays silence until the
  first video frame is consumed [V for the return, I that FMOD outputs silence for an unfilled read].
- otherwise fills player+0x2A4 (4096) frames: `vorbis_synthesis_pcmout`, float * 32767 + 0.5, floor,
  clip to int16 (printing "Clipping in frame %ld" when it clips), interleave, `vorbis_synthesis_read`.
- when the dsp is dry pops a packet with `CDataFrameQueue__beginRead` on the audio queue, then
  `vorbis_synthesis` and `vorbis_synthesis_blockin`, `endRead`. That `beginRead` blocks while the
  audio queue is empty and open, so **the FMOD stream thread can wait on the reading thread**. [V]

**Where the PCM goes.** Into the FMOD stream's decode buffer, then FMOD's mixer, then the output
device. The step never reads any of it. [V] for no step reader, [I] for FMOD's internals.

Teardown is `PVideoPlaybackWin32_closeAudio 0xA278F0`: stop the channel, release the sound (it spins
on `Sleep(1)` until the stream reports ready), clear the Vorbis state, free the audio queue. [V]

---

## 6. Stage 5: CDataFrameQueue

A blocking single producer, single consumer ring of length prefixed frames, 64 bytes per queue object,
vtable 0xB7A9E4. The player has three: packets at +0, decoded frames at +0x40, audio packets at +0x80.
[V]

| offset | field |
|---|---|
| +0x00 | vtable |
| +0x04 | buffer |
| +0x08 | read offset |
| +0x0C | write offset |
| +0x10 | pending write end |
| +0x14 | pending read end |
| +0x18 | initialised |
| +0x1C | space event (auto reset, starts set) |
| +0x24 | data event (auto reset) |
| +0x2C | open |
| +0x30 | capacity |
| +0x34 | popped count |
| +0x38 | pushed count |
| +0x3C | free function |

| function | address | what it does |
|---|---|---|
| `CDataFrameQueue__init` | 0xA3DDF0 | (capacity, maxFrame, alloc, free). Events through `sub_430050`, which is `CreateEventA` |
| `CDataFrameQueue__beginWrite` | 0xA3DEC0 | reserve n + 4 bytes, write the size word. Blocks on the space event while full and open |
| `CDataFrameQueue__endWrite` | 0xA3DF00 | commit, ++pushed, set the data event |
| `CDataFrameQueue__beginRead` | 0xA3DD90 | front slot. Blocks on the data event while empty and open, returns 0 only when closed and drained |
| `CDataFrameQueue__endRead` | 0xA3DDC0 | pop, ++popped, set the space event |
| `CDataFrameQueue__count` | 0xA3DDE0 | pushed minus popped |
| `CDataFrameQueue__close` | 0xA3DEA0 | open = 0, set both events, so a blocked side wakes and gets 0 |
| `CDataFrameQueue__release` | 0xA3DF20 | free the buffer, close the events |
| `CDataFrameQueue__waitForSpan` | 0xA3DFB0 | the shared wait |

The wait is the load-bearing part, because the step thread calls it:

```
00A3DFD0  mov  eax, [ebp+arg_C]
00A3DFD3  mov  eax, [eax]               ; the other side's offset
00A3DFD5  cmp  ebx, eax
00A3DFD7  jbe  short loc_A3DFDC
00A3DFD9  add  eax, [esi+30h]           ; unwrap by capacity
00A3DFDC  cmp  [edi], eax
00A3DFDE  jb   short loc_A3DFFC         ; fits, done
00A3DFE0  cmp  byte ptr [esi+2Ch], 0    ; closed?
00A3DFE4  jz   short loc_A3DFF0         ; yes, return 0
00A3DFE6  mov  ecx, [ebp+arg_10]
00A3DFE9  call sub_4302A0               ; WaitForSingleObject(event, INFINITE)
00A3DFEE  jmp  short loc_A3DFD0
```

There is no timeout and no try variant. [V]

---

## 7. Stage 6: the sasa task library

`sa_task.c` (`..\program\lib\src\`), the PS2 cooperative task group library. **It is not part of the
PC movie pipeline.** [V]

- `sa_task_createGroup 0x77A1A0` and the group allocator at 0x77A1D0 (`TASK_GRUPE_MAX` is 8) keep a
  table of groups. `sa_task_runGroup 0x77A420` calls `nullsub_88`, so a group never runs anything.
- The task primitives are `SG_Printf` stubs: start 0x77A480, 0x77A490, 0x77A4A0, 0x77A4B0, 0x77A4C0,
  and the sleep or wait 0x77A4E0, which the ROM read paths call 20 times and which returns at once.
- The PS2 movie player `movie_str__f77BA70` (`movie_str.c`, with `movie_cd.c` and `movie_debug.c`)
  is referenced only as the body passed to the stub 0x77A480 at 0x77DE20..0x77DE35, which is code
  outside any function with no reference to it. So the whole PS2 movie path is dead on PC.
- `aSasa 0xB59F10` is a developer name in a name table at 0xC4990C, not a library tag.

`FFX2_RECON.md` says the FFX-2 build keeps the movie and CD streaming code under `..\sasa\lib\src\`.
The FFX build has the same shape and the same dead end. Nothing in sections 2 to 9 calls into sa_task.
[V]

---

## 8. Stage 7: the texture upload and the present

**The step.** `FFX_MainStep` calls `FFX_Fmv_StepFromMainStep 0x645DF0` at 0x820B2F, at the top of the
step, before the suspend check. That runs the skip poll (`FFX_Fmv_PollSkipButtons 0x6D9460`) and
`FFX_Fmv_StepPresentation 0x6D7680`. [V]

`FFX_Fmv_StepPresentation`, step thread:

1. Runs only while HasPlayer (mgr+0x6D2) and (no frame shown yet, mgr+0x6E4 == 0, or not paused,
   mgr+0x6FC == 0). 0x6D768A..0x6D76AD.
2. **Keep presenting** while `PVideoPlaybackWin32_isPlayerActive 0xA27C50` and (loop mgr+0x6FE, or no
   frame shown, or (`getCurrentTime < getEndTime` and no skip)). 0x6D76B7..0x6D7701.
   `isPlayerActive` is: decoder alive (player+0x154), or an audio track exists and FMOD's open state
   is not READY, or a decoded frame is still queued.
3. Builds a 0x24 byte job of pointers (player, the three textures, &index, &prev, &shown, &IsPlaying,
   &busy at mgr+0x6F0).
4. When mgr+0x570 is set (videos 46, 72, 32 and looping movies) the job goes to the Phyre render
   thread with `Phyre_PRenderer_QueueCallback(FFX_Fmv_UploadFrameJob)` at 0x6D7798. Otherwise inline:
   if IsPlaying and `ConsumeFrameToTextures` returns 1, then prev = index, index =
   `(int)((float)getCurrentTime() * 29.97f * 0.001f)` (0x6D77D6..0x6D7823), and shown = 1 at 0x6D7828.
5. **The first frame hold.** After the upload, if a frame is shown, the fullscreen latch mgr+0x6FD is
   set, `g_ffxFmvScriptRunning` is 0 and the player is not paused, it calls
   `PVideoPlaybackWin32_pause 0xA27E90` and sets mgr+0x6FC = 1 (0x6D7834..0x6D785D). movie:0 sets
   ScriptRunning to 0, so a movie:0 movie stops on its first frame until movie:9's resi sets
   ScriptRunning = 1 and calls `SetFullscreenMode(0)`, which unpauses it. This answers
   `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.7's last open item. [V]
6. **The stop.** When the keep presenting test fails, a frame was shown and mgr+0x720 (the 0x2000
   flag of movie:0) is clear: index = 65534, IsPlaying = 0, mgr+0x710 = 1 (0x6D786C..0x6D788B). [V]

`PVideoPlaybackWin32_ConsumeFrameToTextures 0xA284A0` (the Phyre interface calls it `updateTextures`):

```
00A284D9  cmp  dword ptr [esi+0E4h], 1     ; state must be 1 (playing)
00A284E0  jnz  loc_A28847                   ; else return 0
00A284E6  cmp  byte ptr [esi+2A8h], 0      ; still prebuffering?
00A284ED  jz   short loc_A2850A
00A284EF  lea  ecx, [esi+40h]
00A284F2  call CDataFrameQueue__count
00A284F7  cmp  eax, [esi+158h]             ; 30
00A284FD  jb   loc_A28847                   ; fewer than 30 decoded: return 0
00A28503  mov  byte ptr [esi+2A8h], 0      ; prebuffer met, audio may start
   ...    resize the three textures on a size change (3 buffers each)
   ...    Src = CDataFrameQueue__beginRead(player+0x40)      ; BLOCKS when empty
   ...    map each texture, PVideoPlaybackWin32_copyPlane
00A28826  inc  dword ptr [esi+100h]        ; consumed++
00A2882F  call CDataFrameQueue__endRead
00A28843  mov  al, 1
```

So before the prebuffer is met a step consumes nothing and carries on, and after it a step consumes
exactly one frame, waiting for the decoder if it has to. [V]

`FFX_Fmv_UploadFrameJob 0x6DAE00` does the same consume and index write on the render thread, with
`InterlockedCompareExchange` on mgr+0x6F0 around it. [V]

**The present.** `FFX_Fmv_DrawFrame 0x6D7570` draws the mesh with the three textures. It returns 9 and
draws nothing when there is no player, no frame shown or the skip flag set. Callers are the scene render
`sub_653960` and 0x642C4B. [V]

**A second player.** The gfx context holds another `PVideoPlaybackWin32` at gfx+0xDE8 for looping
texture videos: loaded by `sub_66FB70` ("Load Texture Video"), stepped by `sub_654960`, which queues the
render thread job `sub_672070`, stopped by `sub_657A50` and torn down by `sub_670760`. It has the same
threads and the same queues. Nothing in the simulation reads it. [V]

---

## 9. Stage 8: the A/V sync rule

**The master clock of the picture is the simulation step.** One `FFX_MainStep`, one consumed frame.
The presented time is `getCurrentTime 0xA27BD0 = (consumed + 1) * period`, truncated to whole ms:

```
v1 = (double)(unsigned int)(*(_DWORD *)(this + 256) + 1) * *(double *)(this + 264);
```

With the period 33.3667 ms and the float constants in the index write, the index equals the number of
frames consumed for any movie shorter than about nine hours. [V] for the code, [I] for the arithmetic,
which is `floor(floor((k + 1) * 33.3667) * 0.0299699996)`.

**Drop or wait: always wait.** The consume blocks on an empty queue (section 6). There is no frame drop,
no catch-up skip and no comparison with any clock. A slow decoder makes the step slower, it does not
make the movie skip. [V]

**Audio runs on FMOD's output clock.** It starts when the first frame clears the prebuffer byte, it is
paused and resumed with the picture (`PVideoPlaybackWin32_pause` toggles both the state and
`Channel::setPaused`), and it runs at 48 kHz from then on. [V]

**No resync.** No code compares the FMOD position with the frame count, and the only time value the
player stores (`dword_1A84C80`) is never read. If the step rate falls below 29.97 Hz the picture falls
behind the sound and stays behind. `FFX_StepPacing_AllowCatchUp 0x81FD30` refuses catch-up while a
movie plays, except for the camera restore movies (46, 72, 32, 70) and `g_ffxFmvScriptBlocking`, so a
slow frame is not made up afterwards. [V] for the code, [I] for how audible the drift gets.

**The three coupling points**, which is all there is:

1. The start gate: audio waits for the first consumed frame (player+0x2A8).
2. The shared pause: state 2 stops both.
3. The first frame hold until movie:9 (section 8 step 5).

A weaker coupling: the reading thread fills both packet queues in file order, so a picture that stops
consuming eventually starves the sound (the decoded queue fills, the decoder stops, the 4 MiB packet
queue fills, the reader stops, the 1 MiB audio queue drains and `fillPcm` blocks). With these sizes
that takes many seconds. [V] for the chain, [I] for the time.

---

## 10. What the simulation reads

### 10.1 Presentation only

None of these is read by any step function. [V]

- The reading thread, the decoding thread, libvpx, the packet and frame queues, the frame bytes.
- The Vorbis state, the FMOD sound and channel, every PCM sample.
- The plane textures, the material, the mesh, `FFX_Fmv_DrawFrame`, the Video.cgfx load.
- `dword_1A84C80`, the `GetTickCount` stamp (read by nothing at all).
- The texture video player at gfx+0xDE8.
- `dword_C40E2C` (movie:13), which only gates a scene pass over the movie
  (`ATEL_MOVIE_SAVE_LIBRARIES.md` 1.5).

### 10.2 What is read, and when it is written

| state | written by (thread) | read by in the step | ties to |
|---|---|---|---|
| frame index mgr+0x6D8 | `StepPresentation` (step) or `UploadFrameJob` (render, mgr+0x570) | movie:3 through `FFX_Fmv_GetFrameIndex 0x6410D0`, `FFX_Fmv_ApplyMovieCamera 0x76EDC0` (at `FFX_MainStep` 0x82112A and from movie:9 resi), `FFX_Fmv_FramesSinceMark 0x645D90` (index minus prev) in `FFX_AtelOp_WaitFrames_poll 0x85C6B0` and `FFX_Atel_StepMoveCmd` while ScriptRunning and `FFX_Fmv_CurrentMovieNeedsCameraRestore 0x76EFC0` | movie:3 row, the movie camera |
| prev index mgr+0x6E0 | same | `FFX_Fmv_FramesSinceMark` | WaitFrames FMV branch |
| shown mgr+0x6E4 | same, 1 after the first upload | movie:9 and movie:10 polls through `FFX_Fmv_PlaybackFinished 0x645DC0` (IsPlaying ? shown : 1), `FFX_Fmv_IsFinishedOrSkipped 0x6DA240` | movie:9, 10, 11 rows |
| IsPlaying mgr+0x6D0 | `PlayPath` (step), the stop branch (step), the skip (step), movie:4 poll (step) | movie:1 poll (`FFX_Fmv_IsPlaying 0x641CF0`), `FFX_StepPacing_AllowCatchUp`, `FFX_Fmv_IsIdleOrSkippable 0x76ED10` in `FFX_MenuSys_PollOpenAndStep` and `FFX_Field_StepMenuOpenRequest`, `IsFinishedOrSkipped` | movie:1, 11 rows, the menu gate, the catch-up gate |
| end test | `getCurrentTime >= getEndTime`, both from the player | `FFX_Fmv_IsFinishedOrSkipped` (skip, or not IsPlaying, or shown and current >= end) | movie:11 row |
| length in frames | `FFX_FmvMgr_GetLengthFrames 0x6D91D0` = `(int)(Duration ms * 29.97 / 1000)` | movie:12 | movie:12 row |
| paused mgr+0x6FC | `StepPresentation` (step), the pause toggles (main, focus loss) | `StepPresentation` itself, `FFX_Fmv_IsPaused 0x645DB0` | the hold |

Every writer on the main path is the step thread, so every read in this table sees a value that was
produced on the same machine by a deterministic number of step calls, **provided** each step consumed
one frame. Section 11 is about when it does not.

### 10.3 How this connects to `FMV_SYNC.md` and the Movie rows

- `FMV_SYNC.md` puts a barrier on the polls of movie:1, 9, 10 and 11. With the first frame hold the
  movie:9 barrier is stronger than that note assumed: both machines leave movie:9 on the same step,
  run movie:9's resi on that step, and unpause on that step. From then on each step consumes one frame
  on both machines, so **movie:3 and every other index reader agree without a barrier of their own**,
  on the main thread path. That replaces the movie:3 verdict in `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.6,
  which said the index differs per machine because an empty queue lets the step consume nothing. It
  does not: an empty queue blocks the step. The hazard that remains is the render thread path.
- Before movie:9, the prebuffer decides the step of the first frame, and that differs per machine.
  Only movie:9's own poll and anything a script reads between movie:0 and movie:9 can see it. The
  census pattern is movie:0, movie:9, then the `movie:3 < N` loops, so the variance lands on the
  barrier. [D] for the pattern, as listed in `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.2.
- The movie:11 end test, `(consumed + 1) * period >= Duration`, is step deterministic after movie:9 for
  the same reason. Its barrier is still needed for the skip, which is local input.
- movie:1 waits for IsPlaying, which the stop branch clears on the same deterministic step, or the skip.
- movie:12 reads the file header. Same on both machines with the same files.

### 10.4 `FFX_Fmv_IsDecoderBusy` is constant 0

`FFX_Fmv_IsDecoderBusy 0x641CE0` is `FFX_Fmv__isDecoderBusy 0x6DA2A0` on the global manager. That
scans the manager's command list (count at mgr+4, commands at mgr+0xC) for a 1, and also returns true
when mgr+0x704 is 0. In the shipped game:

- The list is pushed only by `FFX_FmvMgr_PushCommand 0x6D7D90`, whose only caller is the per frame
  command processor `FFX_FmvMgr_ProcessCommands 0x6D8CB0` itself (its case 3 pushes a 4). Nothing
  outside ever starts the chain, so the list stays empty. [V]
- mgr+0x704 is written only with 1: by the constructor at 0x6D6B73 and by the unreferenced
  `maybe_FFX_FmvMgr_PlayFromJob 0x6DA9B0` at 0x6DAB58. That was checked by a scan of all of `.text`
  for displacement 0x704 on the manager. [V]

So the term is 0 in every poll that reads it. It costs nothing, but every note that calls it "the
decode side drain" is describing something that is not there. The proposed name is
`FFX_Fmv_IsStartPending`.

---

## 11. Can the decoder's own clock change the step a movie ends on?

**It has no clock to change it with.** No time read reaches the decoder (section 9), and the only
input from the decoder to the step is whether a frame is ready, which the step waits for. So, on the
main thread path, with the movie:9 barrier in place:

- first frame hold released on step S on both machines,
- one frame consumed on every step from S on,
- the stop on the first step where `(consumed + 1) * period >= Duration`.

The end step is S plus a constant of the file. The decoder decides how long those steps take in wall
time, not how many there are. **No, on the main path.** [V]

The exceptions, each of which can move the end step on one machine:

1. **The render thread path**, mgr+0x570, videos 46, 72, 32 and looping movies. The consume and the
   index write run in `FFX_Fmv_UploadFrameJob` when the render thread gets to the job, so the value the
   step reads lags by however many jobs are still queued, and the end test in the next
   `StepPresentation` reads the player's count as the render thread left it. 46, 72 and 32 are also
   the movies with a movie camera (`FFX_Fmv_CurrentMovieNeedsCameraRestore`), whose
   `FFX_Fmv_ApplyMovieCamera` drives `FFX_Came_SetMatrixOverride` from the index every step. [V] for
   the path, [I] for the size of the lag.
2. **A local pause.** `FFX_Fmv_TogglePause 0x6DA4B0` is reached from
   `CWindowEventHandler__onFocusLost` and `FFX_Frame_StepSuspendOverlay`, and
   `FFX_Fmv_TogglePauseGlobal 0x645DE0` from the Esc menu, `FFX_MenuSys_StepFrame` and the cutscene
   pause overlay (`FFX_Cutscene_EnterPauseOverlay`). While paused `StepPresentation` returns early, so a
   machine that loses focus mid movie stops consuming while its peer carries on. [V]
3. **The skip.** `FFX_Fmv_PollSkipButtons` is local input. `FMV_SYNC.md` section 4 already makes it a
   command. The seek it asks for, `PVideoPlaybackWin32_jumpTo 0xA27E10`, is a stub that returns 1.
   [V]
4. **Frames that run out before the Duration.** If the file holds fewer frames than `Duration / period`
   the decoder finishes, the queue drains, `beginRead` returns 0 instead of blocking, and the step
   stops consuming while the end test is still false. The movie then ends when `isPlayerActive` goes
   false, which needs FMOD's open state to read READY, a wall clock event on FMOD's thread. [V] for the
   path, [I] that any shipped file is short. Checking means counting the video blocks of every movie
   against its Duration, which can be done from the cluster headers with the same in-memory reader.

Before movie:9, the step of the first frame does depend on decode speed, through the 30 frame
prebuffer. That is the variance `FMV_SYNC.md`'s movie:9 barrier absorbs.

---

## 12. Co-op hazards

1. **Focus loss and the pause menus pause the movie on one machine only** (exception 2). The fix has
   the same shape as the skip: make the movie pause an ordered command, or make the co-op build never
   pause the player on a local event.
2. **The render thread path breaks index agreement** for 46, 72, 32 and loops (exception 1), and those
   drive a camera in the step. For co-op these movies need either the inline path (clear mgr+0x570
   and accept the step thread upload) or the index carried as a host value.
3. **movie:10 is effectively a no-op on PC** (section 14). Both machines agree on that, so it is not a
   desync, but the kit's arming and `MovieWasInvolved` reasoning about movie:10 in `FMV_SYNC.md` 7 rests
   on the opposite reading and should be revisited.
4. **A missing file on one machine.** `CreatePlayer` returns 0 on the miss, so no caller can tell. That
   machine gets no HasPlayer, no IsPlaying, movie:9 and movie:11 pass at once and movie:3 returns
   whatever the index was left at (PlayPath, which zeroes it, is never reached). The barrier holds the
   waits, but `movie:3 < N` loops after the barrier would end at once on that machine if the stale index
   is large, for example 65534 from the last movie's stop. [V] for the paths, [I] for the stale value,
   not traced through `FFX_Fmv_DestroyPlayer`.
5. **The open runs on the step thread.** `Segment::Load` parses the whole file inside movie:0. In
   lockstep this is a long step on each machine, and the faster one waits for the slower at the next
   input exchange. Deterministic, but a visible hitch. [I] for its size.
6. **A slow decoder stalls its own step.** The blocking consume means the machine with the slower
   decoder runs slower steps, and lockstep makes both machines run at the slower pace. The picture stays
   in step, the sound does not (hazard 7).
7. **Sound drifts from the picture and is never pulled back.** Any lockstep delay, network wait or slow
   step during a movie lets FMOD's 48 kHz run ahead of the 29.97 Hz picture. Audible on a long movie
   with a slow peer. [I]
8. **The kit's `FmvDecoderBusy()` and `MovieWasInvolved()`'s decoderBusy term read a constant 0.**
   Harmless, but it is not the drain signal the notes say it is.
9. **Textures are mapped from the step thread** on the main path while the render thread may draw the
   previous buffer. Each texture has 3 buffers, which reads as the designed answer. [I]

## 13. Recreation notes

- **One frame per step, block on empty, is the contract to keep.** It is what makes the movie index a
  step count. A recreation can decode anywhere, but it must hand the step exactly one frame per step
  after the hold, and wait rather than skip.
- **Keep the first frame hold** and release it from movie:9's resi. Let the prebuffer finish off the
  step and only report "first frame shown" to the step on the step where it is consumed.
- **Move the open and the cluster parse off the step**, and have movie:0 wait for "opened" the way the
  prebuffer already makes movie:9 wait. That keeps the opcode's step semantics and removes the hitch.
- **Drop the render thread upload path**, or make it publish the index only through the step.
- **Audio may follow the step** (resample or drop to the frame count) for a recreation that wants no
  drift. That is a presentation decision, the step reads none of it.
- **movie:10.** Decide whether to reproduce the PC behaviour (no movie) or the intended one (play and
  wait for the first frame). The three callers are ikai0800, test01 and test11
  (`ATEL_MOVIE_SAVE_LIBRARIES.md` 1.2). [D]
- **Duration, not frame count, ends a movie.** Read it from the container, in ms, and end on
  `(consumed + 1) * period >= Duration` with period 1000 / 29.97002997 when the track has no frame rate.

---

## 14. Corrections to other notes

| note | what it said | what the binary says |
|---|---|---|
| `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.5 and the movie:10 row | movie:10 does its bookkeeping only when the start succeeds | `FFX_Fmv_StartVideo` (neg/sbb/neg at 0x6416AF) returns 1 only when the sampler create fails, so movie:10 skips the bookkeeping on every normal start (`jz` at 0x76EBB4) |
| `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.6 | `sub_A3DD90` returns null on an empty queue and the step consumes nothing, so movie:3 differs per machine and is outside the barrier | it blocks (0xA3DFE9). After the movie:9 barrier the index is step deterministic on the main path, the hazard is the render thread path |
| `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.6 "clock reach" | the `GetTickCount` is the decode thread's own pacing | the value is stored and never read |
| `ATEL_MOVIE_SAVE_LIBRARIES.md` 1.7 | the first frame hold through mgr+1789 is [I] | it is what happens, section 8 step 5 [V] |
| `FMV_SYNC.md` 2 and 7 | movie:10 sets ScriptRunning and Blocking only on success, the asymmetry is a missing file | inverted, see above. A missing file still returns 0 |
| `FMV_SYNC.md` 2 | the decode thread has a `GetTickCount` timebase | the decoder has no clock |
| `FMV_SYNC.md` correction 2026-10-13 and `VISUAL_CENSUS.md` 9 | mgr+1764 is stored at 0x6D7775 | 0x6D7775 stores the pointer `&mgr+0x6E4` into the job, the byte store is at 0x6D7828 (`mov byte ptr [eax], 1`) |
| `THREADS_AND_CLOCKS.md` T11, T12 and C2 | decode "against its own clock", the vpx thread count unresolved, the tick "used internally" | no clock, cfg is NULL, no workers, the tick is dead |
| `MAIN_LOOP.md` step 3 and the counter table | the FMV path draws and returns, no simulation, the step counter stalls during FMV | `FMV_SYNC.md` section 1: the test is the suspend state, the simulation runs through a movie |
| `tools/apply_movsave_lib_names.py` row 10 | "only on success" | as above |
| IDB comments on `FFX_Fmv_StartVideo`, `FFX_Fmv_PlaybackFinished`, `FFX_Fmv_IsDecoderBusy`, `FFX_Fmv_StepPresentation`, `FFX_Fmv_CreatePlayer` | inverted return, "this movie is over", "decode side drain", "playback complete", "GetTickCount timebase" | replacement text in the apply script's COMMENTS table |

The notes themselves carry a dated correction pointing here. The IDB comments are left for the apply
script.

---

## 15. Names

`tools/apply_movie_decode_names.py`: 98 NAMES, 15 RENAMES, 14 GLOBALS and 5 COMMENTS. A dry run on a
copy of the IDB reports 132 changes and no collisions, and an `--apply` rehearsal (closed without
saving) applies 131, the difference being the thunk 0x6DADF0 that IDA renames by itself.

How the third party code was matched:

| library | evidence |
|---|---|
| libwebm mkvparser | the asserts name `mkvparser.cpp` with an expression only one upstream function asserts (`pReader` and `pos >= 0` is `Segment::CreateInstance`, `m_clusters == NULL` then `m_clusterSize == 0` is `Segment::Load`, `m_timecodeScale >= 1` is the inlined `SegmentInfo::GetDuration`, `idx < m_frame_count` is `Block::GetFrame`, `m_entries[idx] == pCurr` is `Cluster::GetNext`). The one line accessors by return type and by how the player uses them |
| libvpx 1.4.0 | the version string in the iface `vpx_codec_vp8_dx` returns, `ver == 9` and `abi_version == 5` checks, the `VPX_CODEC_INVALID_PARAM` (8) returns and the iface slots each wrapper calls, the 28 byte 7 pointer block of `vpx_mem_set_functions` |
| libvorbis 1.3.5 | the vendor strings, the struct sizes (`codec_setup_info` 0xE50, `vorbis_block` 0x70), the `OV_EBADHEADER`, `OV_ENOTVORBIS` and `OV_EINVAL` returns, the call order in `openAudio` and `fillPcm` |
| libogg | the bit mask table and the 256 byte `BUFFER_INCREMENT` realloc |

The main ones, by stage:

| address | proposed | current |
|---|---|---|
| 0xA3E2B0 | `VbfMkvReader__Read` | `VbfMkvReader__scalar_deleting_dtor_1` |
| 0xA3E1F0 | `VbfMkvReader__Open` | `sub_A3E1F0` |
| 0x41FD50 | `mkvparser__Segment__CreateInstance` | `mkvparser__f41FD50` |
| 0x421450 | `mkvparser__Segment__Load` | `mkvparser__f421450` |
| 0x425200 | `mkvparser__Segment__GetDuration` | `mkvparser__f425200` |
| 0x428DD0 | `mkvparser__VideoTrack__GetFrameRate` | `sub_428DD0` |
| 0x4094C0 | `vpx_codec_dec_init_ver` | `sub_4094C0` |
| 0x409680 | `vpx_codec_decode` | `sub_409680` |
| 0x4096E0 | `vpx_codec_get_frame` | `sub_4096E0` |
| 0x40AE10 | `vp8_create_decoder_instances` | `vp8_dx_init` |
| 0xA273B0 | `PVideoPlaybackWin32_openAudio` | `sub_A273B0` |
| 0xA27170 | `PVideoPlaybackWin32_fillPcm` | `sub_A27170` |
| 0xA27EE0 | `PVideoPlaybackWin32_fmodPcmReadCallback` | `sub_A27EE0` |
| 0xA27650 | `PVideoPlaybackWin32_readAllBlocks` | `sub_A27650` |
| 0xA279B0 | `PVideoPlaybackWin32_decodeLoop` | `sub_A279B0` |
| 0xA27BD0 | `PVideoPlaybackWin32_getCurrentTime` | `sub_A27BD0` |
| 0xA27C40 | `PVideoPlaybackWin32_getEndTime` | `sub_A27C40` |
| 0xA27C50 | `PVideoPlaybackWin32_isPlayerActive` | `sub_A27C50` |
| 0xA27E10 | `PVideoPlaybackWin32_jumpTo` | `sub_A27E10` |
| 0xA27E90 | `PVideoPlaybackWin32_pause` | `sub_A27E90` |
| 0xA3DD90 | `CDataFrameQueue__beginRead` | `sub_A3DD90` |
| 0xA3DEC0 | `CDataFrameQueue__beginWrite` | `sub_A3DEC0` |
| 0xA3DFB0 | `CDataFrameQueue__waitForSpan` | `sub_A3DFB0` |
| 0x6D7430 | `FFX_FmvMgr_StartOrResume` | `sub_6D7430` |
| 0x6D8FC0 | `FFX_FmvMgr_PlayPath` | `sub_6D8FC0` |
| 0x6D8F30 | `FFX_FmvMgr_OpenPlayer` | `sub_6D8F30` |
| 0x6D8CB0 | `FFX_FmvMgr_ProcessCommands` | `sub_6D8CB0` |
| 0x641CE0 | `FFX_Fmv_IsStartPending` | `FFX_Fmv_IsDecoderBusy` |
| 0x6DA2A0 | `FFX_FmvMgr_IsStartPending` | `FFX_Fmv__isDecoderBusy` |
| 0x645DC0 | `FFX_Fmv_FirstFrameShownOrStopped` | `FFX_Fmv_PlaybackFinished` |
| 0x1A84C80 | `g_ffxFmvDecodeStartTick` | `dword_1A84C80` |

The kit's address files use the old names `FmvIsDecoderBusy` and `PlaybackFinished`. The renames do
not touch them.

---

## Appendix A. The player, `PVideoPlaybackWin32`, at mgr+0x38

| offset | field |
|---|---|
| +0x000 | video packet queue (CDataFrameQueue, 4 MiB) |
| +0x040 | decoded frame queue (prebuffer * (frame size + 4)) |
| +0x080 | audio packet queue (1 MiB) |
| +0x0C0 | reading thread handle |
| +0x0D0 | reading thread quit request |
| +0x0E4 | state: 1 playing, 2 paused, 3 stopping |
| +0x0E8 | reader state, 2 when the file is done |
| +0x0EC | decoder stop request |
| +0x0F0 | loop |
| +0x0F8 | Duration in ms (u64) |
| +0x100 | frames consumed (u32) |
| +0x108 | frame period in ms (double) |
| +0x118 | `VbfMkvReader` |
| +0x130 | `vpx_codec_ctx_t` |
| +0x14C, +0x150 | width, height |
| +0x154 | decoder thread alive |
| +0x158 | prebuffer frame count (30) |
| +0x160 | `ogg_packet` |
| +0x184 | Vorbis channel count (in `vorbis_info`) |
| +0x1B0 | `vorbis_dsp_state` |
| +0x220 | `vorbis_block` |
| +0x290, +0x294 | video track, audio track |
| +0x298 | audio enabled |
| +0x29C, +0x2A0 | FMOD sound, channel |
| +0x2A4 | PCM frames per fill (4096) |
| +0x2A8 | prebuffering, also the audio start gate |
| +0x2AC | reader wake event |
| +0x2B4 | reader done event |
| +0x2C0..+0x2CC | plane offsets and sizes |
| +0x2D0 | decoded frame size |

## Appendix B. The manager fields this note uses

| offset | field |
|---|---|
| +0x004, +0x00C | command list count and entries (always empty) |
| +0x038 | the player |
| +0x360 | Video.cgfx cluster |
| +0x388 | mesh instance |
| +0x38C, +0x400, +0x474 | Y, U, V textures |
| +0x4F0 | movie path |
| +0x570 | upload on the render thread (46, 72, 32, loop) |
| +0x6D0 | IsPlaying |
| +0x6D2 | HasPlayer |
| +0x6D8 | frame index, 65534 after the stop |
| +0x6DC | video id |
| +0x6E0 | previous index |
| +0x6E4 | a frame has been shown |
| +0x6E8 | the id to path map |
| +0x6F0 | render job busy word |
| +0x6FC | paused |
| +0x6FD | fullscreen latch |
| +0x6FE | loop |
| +0x704 | start ok, only ever 1 |
| +0x710 | stopped |
| +0x720 | stop suppressed (movie:0 flag 0x2000) |

## Open

- Whether any shipped movie has fewer video frames than its Duration says (exception 4).
- The size of the render thread lag for 46, 72 and 32, and whether `ApplyMovieCamera` ever reads a
  value that differs between two machines in practice. A log of mgr+0x6D8 against the step count on
  two machines would settle it.
- What the three movie:10 packages look like when run on PC.
- Whether `FFX_Fmv_DestroyPlayer` resets the index (hazard 4).
