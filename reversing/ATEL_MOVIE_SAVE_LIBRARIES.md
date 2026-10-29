# The ATEL Movie and Save syscall libraries

2026-10-26. PROGRESS 1.8.10, the Movie and Save half. Library 11 of `g_ffxAtelSysFuncLibs 0x1328558`
is `g_ffxAtelSysFuncLib11_Movie 0xC40E30` and library 12 is `g_ffxAtelSysFuncLib12_Save 0xC52DD8`,
both registered by `FFX_Atel_Init 0x86D6D0` in the order 13.2 of `ATEL_CONTEXT.md` gives. Every row is
16 bytes of {start, poll, float getter, int getter}, the ABI is `ATEL_CONTEXT.md` 11.7 and 13. This note
names every row, marks the dead ones, and says for each row whether its result or the step it completes
on can differ between two machines running the same inputs. `../tools/apply_movsave_lib_names.py` puts
the names in the IDB. All addresses are IDA VAs at base 0x400000.

A script operand picks the library in bits 12..15 and the function in bits 0..11, so a Movie call is
operand 0xBNNN and a Save call 0xCNNN. [V]

Read only throughout. The handlers were decompiled through idalib on a scratch copy of the IDB, with
nothing renamed and nothing saved. The census walked the install with `../tools/ebp.py` and the
recreation's `tools/mon_ai.py` loader, and no script bytes were copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper, an engine field with no traced reader), [D] counted in shipped data. The tables carry V or I
per row.

The big one first: **the Movie table has 16 rows, not 146.** Section 1.3 has the proof. Everything
the older notes say about movie:16 to movie:145 is about sa_menu code that no script can reach.

## 1. Movie, library 11

### 1.1 Short version

- **16 rows, 10 used, 6 dead.** 869 call sites, all in 42 event packages. No monster AI, battle field
  file or system_01 calls Movie. No row is null. [D] [V]
- **The table ends at 0xC40F30.** What sits there is `off_C40F30`, a flat table of 4 byte function
  pointers that the sa_menu code indexes, followed by menu globals and more pointer tables. The 449
  positional names `FFX_AtelSys_Movie_016_start` .. `FFX_AtelSys_Movie_145_resi` the IDB carried were
  on functions that are not syscall handlers. They have been cleared (the apply script's
  `--clear-false-movie-rows`, section 3), and section 5 proposes real names for the ones the notes
  use. [V]
- **movie:3 is covered once movie:9 is gated.** It returns the presented frame index, and 219 calls in
  23 packages use it in `while movie:3 < N: wait 1` loops. The index is the count of frames the main
  step has consumed from the decode queue. An earlier version of this note said a step consumes
  nothing when the queue is empty, which made those loops end on a per machine step. That is wrong:
  the consume blocks (`MOVIE_DECODE.md` section 6), and a movie:0 movie holds on its first frame until
  movie:9's resi. So after the `FMV_SYNC.md` barrier on movie:9 both machines consume one frame per
  step from the same step on, and the loops agree. The exception is the render thread upload of
  videos 46, 72, 32 and looping movies (`MOVIE_DECODE.md` section 11). [V] [D] (corrected 2026-10-27)
- **movie:9 and movie:10 wait for the first frame, not the end.** `FFX_Fmv_FirstFrameShownOrStopped` reads
  mgr+1764, which is set after the first consumed frame. The player buffers 30 frames before it hands
  out the first one, so these waits end on a per machine step, which is what the `FMV_SYNC.md` barrier
  absorbs. On PC movie:10 never starts a decode at all, see 1.5. [V]
- **movie:0 with flag 0x2000 keeps the movie from stopping itself.** It sets mgr+0x720 through
  `sub_6437F0(1)`. 30 of the 165 movie:0 calls pass it. `FMV_SYNC.md` says that byte is only ever
  written to 0, which is wrong. [V] [D]
- **No Movie row touches `maybe_g_ffxMovieStreamFromHdd`.** The writers older notes put at movie:143
  and movie:145 are sa_menu functions. [V]

### 1.2 The census [D]

Sources: 397 event packages, 361 monster AI blocks, 816 battle field files and system_01. Every Movie
call is in an event package. Library totals over the same sources, for scale: Core 1,861,120, Came
801,131, Ch 253,293, Btl 127,268, MapFx 50,989, Sg 25,955, Sys 7,332, Movie 869, Save 328.

| row | calls | packages | form | note |
|---|---|---|---|---|
| 0 | 165 | 41 | 0x58, 2 args | flags 0x500 x81, 0x400 x45, 0x2500 x23, 0 x7, 0x2000 x3, one each of 0x10D00, 0x12100, 0x2100, 0x12900, 0x100, 0x12500. 30 calls carry 0x2000 |
| 1 | 152 | 33 | 0x58 | |
| 2 | 5 | 1 | 0x35 | test11 only |
| 3 | 219 | 23 | 0x35 | 183 of the calls are followed by a push of an immediate and a compare, 17 by a variable, 19 by a store |
| 4 | 167 | 32 | 0x58 | |
| 9 | 116 | 41 | 0x58 | |
| 10 | 7 | 3 | 0x58, 2 args | ikai0800, test01, test11. Video ids 104, 152, 154, flags 1152 x2, 0, 2048 x2, 6144 x2 |
| 11 | 34 | 26 | 0x58 | |
| 12 | 2 | 2 | 0x35 | lchb1200 and test25, stored to a variable and compared with movie:3 |
| 13 | 2 | 2 | 0x58 | kino0200 (at movie frame 329) and zkrn0600 |

Rows 5, 6, 7, 8, 14 and 15 have no caller. The usual package shape is movie:0, movie:9, a run of
`movie:3 < N` loops that time the scene to the movie, then movie:11, movie:4 and movie:1. lchb1200
makes about 120 movie:3 reads.

### 1.3 Why the table is 16 rows [V]

- Rows 0..15 point at one contiguous run of handlers, 0x76E910 to 0x76ECD0, and the function right
  after them is `FFX_Fmv_AbortForSceneChange 0x76ECE0`.
- 0xC40F30, which would be row 16, is the label `off_C40F30`. `sa_menu_start 0x770430` (the IDB used
  to call it `FFX_AtelSys_Movie_136_resf`) stores that address into `dword_C416D0` at 0x770540, and
  `sub_774860` indexes it as a flat array of 4 byte function pointers by the high word of its argument.
- The targets past 0xC40F30 have one argument, non ATEL signatures, for example
  `FFX_SaveData_GetGil`, `FFX_SaveData_TestCharAbilityBit(a1, 12288 + k)` and
  `FFX_SaveData_GetCaptureCount`. 0x772790, once the IDB's "Movie_019_start", is a jmp thunk to
  `FFX_SaveData_GetCaptureCount`.
- 0xC416A8..0xC416B8 are jump targets of the menu glue (`sub_8BF050`, `sub_8BEF50`, `sub_8BEFF0`,
  `j_sa_menu_stop`), 0xC416BC..0xC416DC are menu globals (`dword_C416BC` is written with 1, 4, 7, 10,
  13, and the "row 137" values 2, 0x30, 0x2F00, 0x38FC are `dword_C416C0..CC`), and 0xC416E0 and
  0xC41720 are further tables with their own xrefs.
- Nothing references an address between 0xC40E30 and 0xC40F30 except the table base, so nothing ever
  treated the tail as rows.

Of the 449 false names, 101 are start, 101 poll, 99 float and 99 int. Every one sits exactly where
the pointer at 0xC40E30 + 16 * NNN + 4 * slot points, which is how the earlier pass produced them, and
448 of them carry the repeatable comment "ATEL script syscall handler. Reached as library 11 (Movie)
function NNN". The IDB comment on the table itself says "145 entries".

The true shapes: start+poll+int on rows 0, 1, 4, 8, 9, 10 and 11 (7 rows), int only on the other 9.
No float getter, no null row.

### 1.4 Names that were already there

None of the 16 rows had a descriptive name in the IDB, every slot is positional. The one exception
`ATEL_CONTEXT.md` 13.5 lists, the Movie 0 poll misnamed `__RTC_NumErrors_800`, is already positional
in this copy, and the apply script takes it over if the real IDB still has it.

ebp.py's names: (11, 2) `movie_stub2` is `movie_status_resulti` by its own print, (11, 11)
`movie_force_destroy` is right for the getter only, (11, 12) `movie_end_frame_result` is right. (11,
135), (11, 136), (11, 143) and (11, 144) are not rows. `sa_menu_stop 0x770340`, `loc_76F3B0`
("sa_menu_init") and `hdd_install_end 0x7792C0` are good names for menu functions, not for syscalls.

Two helper names under the rows are misleading:

- `FFX_Fmv_FirstFrameShownOrStopped 0x645DC0` returns mgr+1764 while a movie is playing and 1 otherwise, and
  mgr+1764 means "a frame has been shown" (`FMV_SYNC.md`, correction of 2026-10-13). So it reads 1
  from the first frame on. A name like `FFX_Fmv_FirstFrameShownOrStopped` would say what it does. Its
  IDB comment, and the one on `FFX_Fmv_StepPresentation 0x6D7680` that calls mgr+1764 "playback
  complete", still carry the old reading.
- `FFX_Fmv_StopPlaying` sits at 0x6DAC40, which is a chunk of `sub_643E50 0x643E50`, not a function
  start. The function the movie:4 poll calls is `sub_643E50`, which only clears mgr+1744.

### 1.5 The families

**Start.** movie:0 and movie:10 both pop (videoId, flags) and call `FFX_Fmv_StartVideo`. movie:0
passes 0 as the second argument and sets fullscreen mode 1, movie:10 passes `flags & 0x80` and does
its bookkeeping (fullscreen mode 0, ScriptRunning, `g_ffxFmvScriptBlocking`) only when StartVideo
returns nonzero. An earlier version of this note read that as "only on success". It is the other way
round: `FFX_Fmv_CreatePlayer` returns 0 on success and on a missing file, and nonzero only when the
sampler state create fails, and StartVideo passes that through (neg/sbb/neg at 0x6416AF). So on every
normal start movie:10 skips `SetFullscreenMode`, which is the only path to the decode thread, and its
poll finishes at once because Blocking is 0. movie:10 shows no movie on PC. `MOVIE_DECODE.md`
section 14. (corrected 2026-10-27)

`FFX_Fmv_IsStartPending`, in the waits below, is constant 0 in the shipped game
(`MOVIE_DECODE.md` 10.4).

**Waits.** Four polls block:

| row | done when | what it means |
|---|---|---|
| 1 | not IsPlaying and not DecoderBusy | the movie has stopped, then it destroys the player |
| 9 | not DecoderBusy and `FFX_Fmv_FirstFrameShownOrStopped` | the first frame is up, or the movie stopped |
| 10 | not Blocking, or not DecoderBusy and `FFX_Fmv_FirstFrameShownOrStopped` | same as 9, after its own start |
| 11 | `FFX_Fmv_IsFinishedOrSkippedGlobal` | the movie ran out or the player pressed skip |

movie:4 is the stop: its poll clears IsPlaying, restores the camera and returns 1 (done, yield) on the
first poll, and its getter destroys the player.

**Reads.** movie:3 the frame index, movie:12 the length in frames, both through `FFX_Fmv_GetManager`.

**The scene overlay.** `dword_C40E2C` (initialized to 1) is set by movie:0, 1, 4 and 10 and by
`FFX_Fmv_AbortForSceneChange`, and cleared only by movie:13. `sub_653960` tests it at 0x653BFD before
it draws the scene pass over the movie. Render state only.

**Stubs.** Rows 2, 5, 6, 7, 8, 14 and 15 print a "Virtuos Warning: Movie on Windows, PS3 & PS Vita is
not impelemented yet! Call back by the movie_..." line and return 0. Those prints give the PS2 names.
The movie:8 poll returns 0, so a script that called it would hang there.

### 1.6 Lockstep hazards

The lockstep column uses these flags:

- `fmvclock` hands the presented frame index to the script.
- `fmvwait` a wait that finishes on the decode or presentation state of this machine.
- `fmv` changes player state, the result itself does not differ.
- `render` only sets render state.

**How the frame index is made.** `FFX_Fmv_StepPresentation 0x6D7680` runs once per `FFX_MainStep`.
When the player has a decoded frame queued, `PVideoPlaybackWin32_ConsumeFrameToTextures 0xA284A0`
pops it and bumps the player's consumed count (player +256), and the step then writes mgr+1752 =
`(int)(sub_A27BD0() * 29.97 / 1000)`, where `sub_A27BD0` is (consumed + 1) times the frame period
(player +264). For videos 32, 46 and 72 the same work runs on the Phyre render thread, in
`FFX_Fmv_UploadFrameJob 0x6DAE00`. So the index is not a wall clock reading, it is a count of
consumed frames, but a step only consumes a frame when one is ready:

- `PVideoPlaybackWin32__startReadingThread` sets the prebuffer count at player +344, 30 when the
  caller passes 0, and `PVideoPlaybackWin32__startVideoDecodingThread` sets player +0x2A8 at 0xA27F44.
  While that byte is set, ConsumeFrameToTextures returns 0 until the queue holds 30 frames. So the
  first frame appears after the decode thread has done 30 frames of work, which takes a different
  number of main steps on different machines. [V]
- After that, `sub_A3DD90` (`CDataFrameQueue__beginRead`) BLOCKS on an empty open queue
  (`WaitForSingleObject(INFINITE)` at 0xA3DFE9), so the step waits for the decoder and still consumes
  exactly one frame. A decode stall makes that step longer, it does not delay the index. An earlier
  version of this bullet said the step consumes nothing. (corrected 2026-10-27, `MOVIE_DECODE.md`
  section 6) [V]
- On the render thread path the index is written off the step thread at present time, so the step
  that reads it sees whatever the render thread last wrote. [V]

**Per row verdict.** "Result" is whether the value handed back can differ, "completion" whether the
step it finishes on can.

| row | result | completion | why |
|---|---|---|---|
| movie:0 | same | same | the poll is 5. It starts the decode thread, which is where the later variance comes from |
| movie:1 | same | **differs** | waits for IsPlaying and DecoderBusy, end of decode and the local skip |
| movie:2 | same | same | stub |
| movie:3 | same after the movie:9 barrier, **differs** on the render thread path | same | the consumed frame count, one per step after movie:9's resi releases the first frame hold. Videos 46, 72, 32 and loops write it on the render thread (corrected 2026-10-27) |
| movie:4 | same | same | done on the first poll |
| movie:9 | same | **differs** | first frame, after the 30 frame prebuffer |
| movie:10 | same | same | on PC the start skips its bookkeeping on every normal start, so the poll ends at once on every machine (corrected 2026-10-27, 1.5) |
| movie:11 | same | **differs** | end of movie or the local skip button |
| movie:12 | same for the same file | same | the duration from the file header (player +0xF8, written at 0xA28148). A peer with a different movie file, another language folder or a replaced movie, reads a different length |
| movie:13 | same | same | render only |

**movie:3 and the barrier.** `FMV_SYNC.md` puts the barrier on the four polls. A script that does
`while movie:3 < 120: wait 1` and then moves an actor finishes that loop when its own machine has
consumed 120 frames. An earlier version of this paragraph said the barrier does nothing there. It
does, through movie:9: the movie holds on its first frame until movie:9's resi, the barrier makes that
resi run on the same step on both machines, and from then on each step consumes exactly one frame
because the consume blocks. So the loops agree on the main thread path. What is left is the render
thread upload of 46, 72, 32 and looping movies, a local pause or focus loss, and a file with fewer
frames than its Duration. `MOVIE_DECODE.md` section 11. (corrected 2026-10-27)

**The 0x2000 flag.** `sub_6437F0 0x6437F0` writes mgr+0x720 at 0x643800 (`mov [eax+720h], bl`). movie:0
calls it with 1 when `flags & 0x2000`, movie:4's start calls it with 0. While it is set,
`FFX_Fmv_StepPresentation` does not clear IsPlaying when the movie runs out, and the summary in
`FMV_SYNC.md` section 4 says the skip path honours the same guard. So movie:1 would never finish on
such a movie until something else clears IsPlaying, and the packages that pass 0x2000 end it with
movie:3 loops, movie:11 and movie:4 instead. For co-op this means the 30 flagged starts lean on movie:3,
the one read the barrier does not cover. The
`FMV_SYNC.md` statement that mgr+1824 is only ever written to 0 missed this writer because it writes a
register, not an immediate. [V]

**The HDD stream byte.** `maybe_g_ffxMovieStreamFromHdd 0x133C913` is per machine (core:465 reads it,
`ATEL_CORE_LIBRARY.md`). No Movie row 0..15 reads or writes it. Its writers include
`FFX_Menu_ApplySaveMovieOption` at 0x8724DB, `hdd_install_check`, `tklib__f88D710`,
`FFX_SaveFile_CommitLoad` and two sa_menu functions, `FFX_SaMenu_SetMovieStreamFromHdd 0x773C60` (once
`FFX_AtelSys_Movie_145_poll`) and `FFX_SaMenu_DetectHddInstallMode 0x7792E0` (once
`FFX_AtelSys_Movie_143_start`, which clears the byte and puts it back). Section 5 has both. [V]

**Clock reach.** movie:0 start, movie:9 getter and movie:10 start reach `GetTickCount` through
`FFX_Fmv_SetFullscreenMode`, `sub_6D7430`, `sub_6D8FC0` and
`PVideoPlaybackWin32__startVideoDecodingThread`. The value goes to `dword_1A84C80` and nothing reads
it, so it is not pacing anything (corrected 2026-10-27, `MOVIE_DECODE.md` section 9). The variance
reaches the script only through the prebuffer before movie:9. [V]

### 1.7 Open

- Whether decode stalls after the prebuffer happen often enough to matter on real hardware, and how
  big the gap between two machines gets at the first frame. A log of mgr+1752 against the step count
  on two machines would settle both.
- The flag bits of movie:0 other than 0x2000. They go into the work area (`*work = flags`,
  `work[1] = videoId`) and the getter prints them, nothing else reads them in the handler. Whether
  `FFX_Fmv_StartVideo` or the player reads the work area later is not traced.
- What `sub_6F6F80`, called from the camera restore, does.
- Settled 2026-10-27: mgr+1789, which `sub_6D7430` sets and `FFX_Fmv_StepPresentation` tests with
  `g_ffxFmvScriptRunning == 0` before it pauses the player (`sub_A27E90`), is how a movie:0 movie holds
  on its first frame until movie:9 sets ScriptRunning (0x6D7834..0x6D785D). `MOVIE_DECODE.md` section
  8. [V]

### 1.8 The table

Columns: index, call sites, packages, source groups (e event, m monster AI, b battle field, s
system_01), the call form (0x35 leaves the result on the stack, 0x58 pops it into R), arguments in
push order, result, whether it blocks, the names, meaning, lockstep flags, mark.

| movie | uses | files | src | form | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 165 | 41 | e | 58 | videoId:int, flags:int | int 0 | no | `FFX_AtelSys_Movie_000_StartMovie_start`, `FFX_AtelSys_Movie_000_StartMovie_poll`, `FFX_AtelSys_Movie_000_StartMovie_resi` | start: g_ffxFmvCurrentVideoId = videoId, FFX_Fmv_StartVideo(videoId, 0), FFX_Fmv_SetFullscreenMode(1), flags & 0x2000 sets mgr+0x720 through sub_6437F0(1) so the natural end and the skip no longer stop the movie, dword_C40E2C = 1, g_ffxFmvScriptRunning = 0. The poll returns 5, the getter is a print stub | fmv | V |
| 1 | 152 | 33 | e | 58 |  | int 0 | yes | `FFX_AtelSys_Movie_001_WaitMovieStopped_start`, `FFX_AtelSys_Movie_001_WaitMovieStopped_poll`, `FFX_AtelSys_Movie_001_WaitMovieStopped_resi` | start zeroes the work word. poll: 0 while FFX_Fmv_IsPlaying or FFX_Fmv_IsStartPending, then g_ffxFmvScriptRunning = 0, dword_C40E2C = 1, sub_63DF30 destroys the player and it returns 5. getter: when FFX_Fmv_CurrentMovieNeedsCameraRestore (videos 32, 46, 70, 72 with the gfx byte) clears the matrix override, sets the movie screen mode 0 and calls sub_6F6F80, then g_ffxFmvCurrentVideoId = -1 | fmvwait | V |
| 2 | 5 | 1 | e | 35 |  | int 0 | no | `FFX_AtelSys_Movie_002_MovieStatus_resi` | movie_status_resulti, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |
| 3 | 219 | 23 | e | 35 |  | int frame | no | `FFX_AtelSys_Movie_003_GetMovieFrame_resi` | FFX_Fmv_GetFrameIndex 0x6410D0 -> mgr+1752, which FFX_Fmv_StepPresentation and FFX_Fmv_UploadFrameJob write as (int)(player time ms * 29.97 / 1000) after each consumed frame. 65534 once the movie stops | fmvclock | V |
| 4 | 167 | 32 | e | 58 |  | int 0 | yield | `FFX_AtelSys_Movie_004_StopMovie_start`, `FFX_AtelSys_Movie_004_StopMovie_poll`, `FFX_AtelSys_Movie_004_StopMovie_resi` | start: sub_6437F0(0) clears mgr+0x720. poll: sub_643E50 clears mgr+1744 IsPlaying, then the camera restore, VideoId -1, g_ffxFmvScriptRunning = 0, dword_C40E2C = 1, returns 1 (done, yield). getter: sub_63DF30 destroys the player |  | V |
| 5 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Movie_005_MoviePause_resi` | movie_pause, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |
| 6 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Movie_006_MovieChannel_resi` | movie_ch, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |
| 7 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Movie_007_MovieVolume_resi` | movie_vol, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |
| 8 | 0 | 0 |  |  |  | int 0 | forever | dead `FFX_AtelSys_Movie_008_MovieStartDebug_start`, dead `FFX_AtelSys_Movie_008_MovieStartDebug_poll`, dead `FFX_AtelSys_Movie_008_MovieStartDebug_resi` | movie_start_debug_init, _exec and _resulti print stubs. The poll returns 0, so a call would never finish |  | V |
| 9 | 116 | 41 | e | 58 |  | int 0 | yes | `FFX_AtelSys_Movie_009_WaitMovieFirstFrame_start`, `FFX_AtelSys_Movie_009_WaitMovieFirstFrame_poll`, `FFX_AtelSys_Movie_009_WaitMovieFirstFrame_resi` | poll: 0 while FFX_Fmv_IsStartPending or FFX_Fmv_FirstFrameShownOrStopped is 0, else 5. PlaybackFinished is mgr+1764 while a movie plays, and mgr+1764 is set after the FIRST consumed frame, so this waits for the movie to appear (or to stop). getter: g_ffxFmvScriptRunning = 1, FFX_Fmv_SetFullscreenMode(0), FFX_Fmv_ApplyMovieCamera | fmvwait | V |
| 10 | 7 | 3 | e | 58 | videoId:int, flags:int | int 0 | yes | `FFX_AtelSys_Movie_010_StartMovieWaitFirstFrame_start`, `FFX_AtelSys_Movie_010_StartMovieWaitFirstFrame_poll`, `FFX_AtelSys_Movie_010_StartMovieWaitFirstFrame_resi` | start: FFX_Fmv_StartVideo(videoId, flags & 0x80) and only when it returns nonzero, which is only a failed sampler create, FFX_Fmv_SetFullscreenMode(0), g_ffxFmvScriptRunning = 1, dword_C40E2C = 1, g_ffxFmvScriptBlocking = 1. So a normal start skips them and no decode starts (corrected 2026-10-27, 1.5). poll: 0 while Blocking and (decoder busy or no first frame), else 5 through _RTC_NumErrors_803, a return 5 stub | fmvwait | V |
| 11 | 34 | 26 | e | 58 |  | int 0 | yes | `FFX_AtelSys_Movie_011_WaitMovieEndOrSkip_start`, `FFX_AtelSys_Movie_011_WaitMovieEndOrSkip_poll`, `FFX_AtelSys_Movie_011_WaitMovieEndOrSkip_resi` | poll: FFX_Fmv_IsFinishedOrSkippedGlobal, 5 when the movie ended or g_ffxFmvSkipRequested is set. getter: for video 52 prints 'Force destroy fmv player for movieId:%d' and calls sub_63DF30, then tail calls the movie:1 getter (camera restore, VideoId -1) | fmvwait | V |
| 12 | 2 | 2 | e | 35 |  | int frames | no | `FFX_AtelSys_Movie_012_GetMovieLengthFrames_resi` | movie_end_frame_resulti. sub_6D91D0: (int)(sub_A27C40 duration ms * 29.97 / 1000), the length of the open file in frames, 0 with no player | fmv | V |
| 13 | 2 | 2 | e | 58 |  | int 0 | no | `FFX_AtelSys_Movie_013_HideSceneOverMovie_resi` | dword_C40E2C = 0. sub_653960 at 0x653BFD tests it and draws the scene pass over the movie only while it is set, every other Movie row sets it back to 1 | render | V |
| 14 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Movie_014_MovieFrameDrawPath3_resi` | movie_op_frame_draw_acc_path3, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |
| 15 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Movie_015_MovieFramePal_resi` | movie_frame_pal_resulti, prints a PS2 name through SG_Printf_STUB and returns 0 |  | V |

## 2. Save, library 12

### 2.1 Short version

- **94 rows, 31 used, 62 dead, 1 null (10).** 328 call sites in 60 event packages, nothing in
  monster AI, battle field files or system_01. Row 94 would be the string
  `"/ffx/proj/event/header/eventid.bin"` that `sub_879760` reads, so 94 is right. Shapes: 90 int only,
  2 float only (71, 72), 1 start+poll (36), 1 null. [D] [V]
- **The library is mostly a developer kit.** Most of the used rows are in test packages (test01,
  test02, test20 the title, test22, testbattle, testpub*) and 14 rows are gated on
  `FFX_IsDevHarness 0x887D10`, which returns 0 in retail. The name "Save" comes from the dev save RAM
  dump in rows 0 and 1.
- **save:47 is the hazard.** It is the indoor test by camera height, the same body as core:530, and it
  is the form the shipped maps use: 29 calls in 11 packages against core:530's 2. The test compares a
  threshold with the camera's y every frame and feeds the result into the save block. With a camera
  per player it differs between machines. [V] [D]
- **No Save row touches a memory card, the save slot list, the system settings or any per machine
  file in retail.** The only file I/O is host0 dev reads and writes behind the dev gate, and asset
  loader reads from the install. The option flags rows (2, 13, 19, 20) work on bits inside the save
  block. [V]
- **Four rows hand a raw pointer to the script** (55, 69, 84, 88). Two are used, only in test22. [V]

### 2.2 The census [D]

| row | calls | packages | where |
|---|---|---|---|
| 2 | 1 | 1 | test02 |
| 3 | 7 | 4 | sysfp, test12, test15, test23 |
| 7 | 9 | 9 | test01, test20, test23, test25, test31, test32, test35, test36, test40 |
| 9 | 16 | 5 | startmap0, test02, test20, test25, testbattle |
| 11 | 1 | 1 | sysfp |
| 12 | 2 | 2 | sysfp, test13 |
| 13 | 1 | 1 | test02 |
| 19, 20 | 2 each | 1 | test02, as expressions |
| 24 | 1 | 1 | test20 |
| 34 | 1 | 1 | test20 |
| 36 | 5 | 4 | lchb0200, lchb0300, lchb0400, lchb0700 |
| 37 | 4 | 2 | test02, testbattle |
| 40 | 1 | 1 | test01 |
| 42 | 8 | 2 | test02, testbattle |
| 44 | 20 | 4 | test02, test20, test23, testbattle |
| 47 | 29 | 11 | bika0000, bjyt0200, bjyt0800, bsil0000, bsil0100, bsil0300, cdsp0000, genk0900, scene1, soundtest, testpub1 |
| 48 | 12 | 5 | cdsp0000, kino0200, kino0500, mihn0300, mihn0600 |
| 49 | 1 | 1 | sysfp |
| 54 | 12 | 6 | bsil0400, mcyt0600, omeg0100, test01, testpub4, znkd0300 |
| 59, 60 | 2 each | 2 | test02, testbattle |
| 81 | 9 | 5 | test01, test20, test22, test23, test26 |
| 82 | 21 | 13 | bjyt0200, bjyt0400, five bsil maps, gameover, memochek, scene3, test22, testpub1 and more, as an expression |
| 83..88 | 1, 1, 1, 2, 1, 8 | 1 | test22 |
| 91 | 145 | 11 | dbg_nagi0000, kami0300, mihn0000..0800, mtgz0100, nagi0000 |

The title, test20, calls save:7, 9, 24, 34, 44 and 81. Of those only 9 and 81 do anything in retail
(24 and 34 are dev gated, 7 sets two debug screen globals, 44 returns its argument).

### 2.3 Names that were already there

Every slot of the 94 rows is positional in the IDB. ebp.py has six names:

- (12, 0) `atelsaveram_read` and (12, 1) `atelsaveram_write` are swapped. save:0 opens
  `atelsaveram000520.dat` with `sub_879C60` and writes the block with `sub_879E50`, save:1 opens it
  with `sub_879CF0` and reads it back with `sub_879DE0` (ReadFile). [V]
- (12, 11) `yiLoadBattleFieldParticleNo`, (12, 12) `yiLoadFieldParticleNo`, (12, 86)
  `saveram_read_group` and (12, 87) `saveram_copy_to` are right, from the handlers' own prints.

The IDB comments on rows 71 to 78 (raw save block access) and 87 (the transplant) are right.
`std::_Get_future_error_what` in row 69 is an identical COMDAT the linker folded into the slot, not a
real use of the C++ library.

### 2.4 The families

**Dev gated, a no-op in retail (14 rows).** 0 and 1 the host0 save RAM dump and restore, 4, 5, 6 the
SgCapture start, arm and stop, 11 and 12 the host0 particle set loads, 21 a host0 file into the sound
code, 24 a sound call on a variable, 34 the debug GUI draw switch, 39 and 40 the key item clear and
give, 42 a report switch, 52 a debug GUI byte. Each one returns its usual value without running its
body.

**Loads in the step.** 3 and 25 `FFX_Ev_LoadSceneBackgroundAlt` and `FFX_Ev_LoadSceneBackground`, 49
the same with `FFX_Map_LoadMapSync`, 36 the field reload through `FFX_Btl_ReturnToField(1, a, scene)`
after freezing the actors and releasing their CHRs, 60 `FFX_SaveData_GrantAllAbilities` (was `FFX_Btl_LoadPlayerData`).

**Option flags.** 2 and 19 bit 0x08 (the map name alternative), 13 and 20 bit 0x20 (copied into each
new message window by `FFX_MesWin_SetTextSource`).

**Single slot globals.** 7 `dword_133085C`/`dword_1330860` (read only by the debug screen), 9
`byte_132711F` (the localized event directory mode), 54 `dword_CEC1F0` (the placed effects gate, the
same as core:550 and mapfx:12), 81 and 82 `byte_1330832` (the demo flag: `sub_88E880` picks the
FFXDEMO paths, and the field menu and the save menu read it), 63 to 66 `byte_1330092` (sound output
mode) and `byte_1330093`, 61 the signal flags `dword_1326B70` (core:378 and 379 in one row).

**Sound.** 14, 16, 17, 18, 22, 23, 26 to 31, 38, 45, 46, 58, 62, 67, 90. Most forward to the same
helpers as Core and Btl rows (core:213 PrepareVoice, btl:101 StartVoice, core:216 voice busy, core:223
and 224 SE start and stop, core:403). `sub_8866B0` plays the SE that a kind table (0 to 7, or 255 for
a direct id) holds for a slot 0..15. All dead.

**Save block access.** 71 to 78 get and set a float, dword, word or byte at any offset (the setters
pop the value as a float), 87 the whole block memcpy from a pointer.

**Pass through and debug.** 32, 33, 44 return their argument, 43 returns 0, 79 and 80 return 1, 83
drops four values, 91 and 92 print "DEBUG:%d/%d" into a nullsub and return the argument. save:91's 145
calls are a trace left in the mihn maps.

### 2.5 Lockstep hazards

The lockstep column uses these flags:

- `camread` reads the local camera into simulation state.
- `stall` a synchronous load inside the step. The result is the same on both machines, only the
  wall time differs.
- `ptr` hands a raw address to the script.
- `disk` reads the install through the asset loader or host0.
- `dev` behind the dev gate, nothing runs in retail.
- `local` depends on the language.

**save:47, the camera height test.** `FFX_AtelSys_Save_047_resi 0x878E80` pops the float threshold, then
the int enable, writes ctx+0x208 and bit 0 of ctx byte +2, the same writes as core:530. `sub_871E50`
then sets `byte_1326B83 = (threshold <= FFX_Came_GetPos(1).y)` every frame while the bit is set, and
that byte feeds save +40, the ambient volume (63 or 127) and the stopping of the placed effect groups
(`MAP_SECTIONS.md` 4). Save +40 is inside the save block, so a peer with a different camera height
writes a different block and the desync hash sees it. Verdict: differs whenever the two cameras differ
in height across the threshold. Either replicate the camera position this test reads, or evaluate the
test against an agreed camera (the host's, or the camera of the character the area belongs to). [V]

**Per row verdict for the used rows.**

| row | result | completion | why |
|---|---|---|---|
| save:2, 13 | same | same | set option bits inside the save block. A peer that changes the same option in its own config menu diverges through the block, not through these rows |
| save:3, 25, 49 | same | same | synchronous loads, a stall only |
| save:7, 9, 81 | same | same | script driven single slot globals |
| save:11, 12, 24, 34, 40, 42 | same | same | dev gated |
| save:19, 20 | same while the blocks agree | same | read option bits from the block |
| save:36 | same | same | reload in place, done on the first poll |
| save:37 | same | same | copies script text into the save block name |
| save:44, 91 | same | same | return the argument |
| save:47 | **differs** | same | the camera height test above |
| save:48 | same | same | ctx+0x1F4, simulation state. No shipped window reads R after it |
| save:54 | same | same | the placed effects gate |
| save:59 | **differs by language** | same | `FFX_Debug_FullItem` has a language 11 case. test02 and testbattle only |
| save:60 | same | same | reload of ply_data |
| save:82 | same | same | the demo flag, set by script |
| save:84, 88 | **differs** | same | heap and script memory addresses. test22 only |
| save:83, 85, 86, 87 | same | same | test22's group read and transplant. 86 reads the install, so the same install gives the same bytes |

**Per machine files.** None in retail. Rows 0, 1, 11, 12 and 21 are the only ones that open files, all
on host0 and all behind the dev gate. 56, 57 and 86 read through `FFX_GetAssetLoader`, which reads the
install. Nothing opens a memory card, a save slot, the system settings or the Steam folders. The
option flags these rows touch live in the save block, the same block a joiner gets in the transplant.
[V]

**A raw pointer in a script variable.** If a script stores the result of save:84 or save:88 in a
variable the desync hash covers, the hash differs between machines although nothing is wrong. Only
test22 does it.

### 2.6 Open

- What `FFX_SndIop_SendSoundRoot` (save:24, dev gated) and `sub_67A060` (save:38) do in the sound code. Both rows
  are dead or gated.
- Which of the sound kind tables behind `sub_8866B0` is which (music, jingle, ambient). All dead rows.
- Option bit 0x20. `FFX_MesWin_SetTextSource` copies it into window +0x22 together with
  `dword_C59558`, which `sub_8AF6A0` sets from a per event table. It reads like a per window text mode
  but no reader of +0x22 was traced.
- Whether ctx+0x1F4 (save:48) is the same counter for every context. Its value is discarded by every
  shipped call, so it does not matter for lockstep.

### 2.7 The table

Columns as section 1.8.

| save | uses | files | src | form | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 |  |  |  | int 0/1 | no | dead `FFX_AtelSys_Save_000_DevDumpSaveBlock_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Opens host0 atelsaveram000520.dat for writing (sub_879C60) and writes 8684 bytes of the save block (sub_879E50) | disk dev | V |
| 1 | 0 | 0 |  |  |  | int 0/1 | no | dead `FFX_AtelSys_Save_001_DevLoadSaveBlock_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Opens the same file for reading (sub_879CF0) and reads 0x21EC bytes over the save block (sub_879DE0) | disk dev | V |
| 2 | 1 | 1 | e | 58 | on:int | int 0 | no | `FFX_AtelSys_Save_002_SetMapNameAltFlag_resi` | sub_785F40: option flags bit 0x08 = on, the flag FFX_SaveData_GetMapNameAltFlag reads to pick the place name string |  | V |
| 3 | 7 | 4 | e | 58 | sceneId:int | int 0 | no | `FFX_AtelSys_Save_003_LoadSceneBackgroundAlt_resi` | FFX_Ev_LoadSceneBackgroundAlt 0x86DCA0: FFX_SaveData_SetSceneAndSub then FFX_Map_LoadFieldMapSync, a synchronous load inside the step | stall | V |
| 4 | 0 | 0 |  |  | a:int, b:int | int 1 | no | dead `FFX_AtelSys_Save_004_DevCaptureStart_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A4F0(a, b), the SgCapture start | dev | V |
| 5 | 0 | 0 |  |  | a:int | int 1 | no | dead `FFX_AtelSys_Save_005_DevCaptureArm_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A580(a) | dev | V |
| 6 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_006_DevCaptureStop_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A570 | dev | V |
| 7 | 9 | 9 | e | 58 | a:int, b:int | int a | no | `FFX_AtelSys_Save_007_SetDebugScreenParams_resi` | sub_88EBF0: dword_133085C = a, dword_1330860 = b. Only maybe_FFX_Atel__debugScreen reads them (sub_88D6B0, sub_88D6C0), sub_88E880 inits 23 and 0 |  | V |
| 8 | 0 | 0 |  |  | var:int | int 1 | no | dead `FFX_AtelSys_Save_008_ResolveVarRet1_resi` | FFX_Atel_ResolveVarAddress(var) and the address is dropped |  | V |
| 9 | 16 | 5 | e | 58 | mode:int | int mode | no | `FFX_AtelSys_Save_009_SetEventDirMode_resi` | sub_873E40: byte_132711F = mode, the localized event directory mode sub_873AA0 reads. FFX_Atel_Init also writes it. The substitution it picks is dead on PC (ASSET_PATHS.md 7.2) |  | V |
| 10 | 0 | 0 |  |  |  | int 0 | no | null | null row, no handler. Polls 5 and returns 0 |  | V |
| 11 | 1 | 1 | e | 58 | id:int | int 1 | no | `FFX_AtelSys_Save_011_DevLoadBattleParticleSet_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. yiLoadBattleFieldParticleNo, FFX_MapParticle_LoadBattleSet(id) from host0 f_particle b%03d | disk dev | V |
| 12 | 2 | 2 | e | 58 | id:int | int 1 | no | `FFX_AtelSys_Save_012_DevLoadMapParticleSet_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. yiLoadFieldParticleNo, FFX_MapParticle_LoadMapSet(id) from host0 f_particle m%03d | disk dev | V |
| 13 | 1 | 1 | e | 58 | on:int | int on (0/1) | no | `FFX_AtelSys_Save_013_SetOptionFlag20_resi` | sub_785E90: option flags bit 0x20 = on. FFX_MesWin_SetTextSource copies that bit into each new message window (+0x22) |  | V |
| 14 | 0 | 0 |  |  | a:int, id:int, c:int, d:int | int a | no | dead `FFX_AtelSys_Save_014_SoundQueueCommand_resi` | sub_81E4E0(a, id, c, d): bumps the dword_12F4E7C command counter, sub_67A440(id, d & 0x7F, c & 0x7F), sets dword_12F4F64 bit 0 |  | I |
| 15 | 0 | 0 |  |  | var:int | int 0 | no | dead `FFX_AtelSys_Save_015_ResolveVarRet0_resi` | as save:8 and returns 0 |  | V |
| 16 | 0 | 0 |  |  | voiceId:int | int 1 | no | dead `FFX_AtelSys_Save_016_PrepareVoice_resi` | sub_887570(voiceId), the same voice prepare call as core:213 and btl:100 |  | I |
| 17 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_017_StartVoice_resi` | sub_887630, the prepared voice start of btl:101 |  | I |
| 18 | 0 | 0 |  |  |  | int bool | no | dead `FFX_AtelSys_Save_018_IsVoiceBusy_resi` | sub_8864B0() != 0, the voice busy test of core:216 |  | V |
| 19 | 2 | 1 | e | 35 |  | int bool | no | `FFX_AtelSys_Save_019_GetMapNameAltFlag_resi` | FFX_SaveData_GetMapNameAltFlag() == 1, option flags bit 0x08 |  | V |
| 20 | 2 | 1 | e | 35 |  | int bool | no | `FFX_AtelSys_Save_020_GetOptionFlag20_resi` | sub_7851B0, option flags bit 0x20 |  | V |
| 21 | 0 | 0 |  |  | pathVar:int | int 1 | no | dead `FFX_AtelSys_Save_021_DevLoadFileToSound_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Reads the host0 file named by a script variable into an actor pool block and hands it to sub_886EE0 | disk dev | V |
| 22 | 0 | 0 |  |  | seId:int | int 1 | no | dead `FFX_AtelSys_Save_022_PlaySe_resi` | sub_886E80(seId), the core:223 SE start |  | I |
| 23 | 0 | 0 |  |  | seId:int | int 1 | no | dead `FFX_AtelSys_Save_023_StopSe_resi` | sub_886EB0(seId), the core:224 SE stop |  | I |
| 24 | 1 | 1 | e | 58 | var:int | int 1 | no | `FFX_AtelSys_Save_024_DevSoundFromVar_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. FFX_SndIop_SendSoundRoot(address of var) | dev | V |
| 25 | 0 | 0 |  |  | sceneId:int | int 0 | no | dead `FFX_AtelSys_Save_025_LoadSceneBackground_resi` | FFX_Ev_LoadSceneBackground 0x86DD30, the plain form of save:3, a synchronous load | stall | V |
| 26 | 0 | 0 |  |  | slot:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_026_PlayKindSe1_resi` | sub_8866B0(1, slot, b, c): plays the SE that sound kind table 1 holds for slot 0..15 |  | I |
| 27 | 0 | 0 |  |  | slot:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_027_PlayKindSe0_resi` | sub_8866B0(0, slot, b, c) |  | I |
| 28 | 0 | 0 |  |  | slot:int, b:int | int 1 | no | dead `FFX_AtelSys_Save_028_PlayKindSe5_resi` | sub_8866B0(5, slot, b, 0) |  | I |
| 29 | 0 | 0 |  |  | slot:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_029_PlayKindSe2_resi` | sub_8866B0(2, slot, b, c) |  | I |
| 30 | 0 | 0 |  |  | slot:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_030_PlayKindSe3_resi` | sub_8866B0(3, slot, b, c) |  | I |
| 31 | 0 | 0 |  |  | slot:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_031_PlayKindSe4_resi` | sub_8866B0(4, slot, b, c), kind 4 uses the dword_132FF3C mask |  | I |
| 32 | 0 | 0 |  |  | x:int | int x | no | dead `FFX_AtelSys_Save_032_PassThroughA_resi` | returns its argument |  | V |
| 33 | 0 | 0 |  |  | x:int | int x | no | dead `FFX_AtelSys_Save_033_PassThroughB_resi` | returns its argument |  | V |
| 34 | 1 | 1 | e | 58 | on:int | int 1 | no | `FFX_AtelSys_Save_034_DevSetDebugGuiDraw_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. FFX_SetDebugGuiDrawEnabled(on) | dev | V |
| 35 | 0 | 0 |  |  | actorId:int, joint:int, xVar:int, yVar:int, zVar:int | int 1 | no | dead `FFX_AtelSys_Save_035_GetJointPosToVars_resi` | maybe_FFX_Ch_GetJointWorldMatrix on the actor's CHR and the translation row into three script variables, zeros with no CHR |  | V |
| 36 | 5 | 4 | e | 58 | a:int, ignored:int | int 0 | yield | `FFX_AtelSys_Save_036_ReloadFieldInPlace_start`, `FFX_AtelSys_Save_036_ReloadFieldInPlace_poll` | start: FFX_Field_FreezeActorsToScratch, maybe_FFX_Atel_ReleaseAllActorChrs, FFX_Btl_ReturnToField(1, a, current scene). The poll returns 1 (done, yield) | stall | V |
| 37 | 4 | 2 | e | 58 | messageId:int | int 0/1 | no | `FFX_AtelSys_Save_037_SetCharName0FromMessage_resi` | copies message text messageId over FFX_SaveData_GetCharName(0) when it is shorter than sub_784FE0(0) |  | V |
| 38 | 0 | 0 |  |  | var:int, ignored:int | int 1 | no | dead `FFX_AtelSys_Save_038_SoundQueryToVar_resi` | sub_886A00(address of var): dword_133009C = sub_67A060(address) |  | I |
| 39 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Save_039_DevClearKeyItems_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_906B30(-1) and clears 26 key item flags from 0xA004 | dev | V |
| 40 | 1 | 1 | e | 58 |  | int 1 | no | `FFX_AtelSys_Save_040_DevGiveKeyItems_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_88E790 for 26 key items from 0xA004 | dev | V |
| 41 | 0 | 0 |  |  | flags:int, b:int, a:int | int a or b | no | dead `FFX_AtelSys_Save_041_SelectByFlagBit1_resi` | computes the actor yaw difference through sub_88BC20 and drops it, then returns a when flags & 2, else b |  | V |
| 42 | 8 | 2 | e | 58 | on:int | int on | no | `FFX_AtelSys_Save_042_DevReport_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_88EBE0(on) sets byte_1330871 and sub_885A80 when on | dev | V |
| 43 | 0 | 0 |  |  |  | int 0 | no | dead `FFX_AtelSys_Save_043_ReturnZero_resi` | returns 0 |  | V |
| 44 | 20 | 4 | e | 58 | x:int | int x | no | `FFX_AtelSys_Save_044_PassThroughC_resi` | returns its argument |  | V |
| 45 | 0 | 0 |  |  | slot:int | int 1 | no | dead `FFX_AtelSys_Save_045_PlayKindSe6_resi` | sub_8866B0(6, slot, 0, 0) |  | I |
| 46 | 0 | 0 |  |  | slot:int | int 1 | no | dead `FFX_AtelSys_Save_046_PlayKindSe7_resi` | sub_8866B0(7, slot, 0, 0) |  | I |
| 47 | 29 | 11 | e | 58 | enable:int, heightY:float | int enable (0/1) | no | `FFX_AtelSys_Save_047_SetIndoorCameraHeightTest_resi` | ctx+0x208 = heightY and ctx byte +2 bit 0 = enable, the same body as core:530. sub_871E50 then sets byte_1326B83 = (heightY <= FFX_Came_GetPos(1).y) every frame, which feeds save +40, the ambient volume and the placed effect groups | camread | V |
| 48 | 12 | 5 | e | 58 |  | int counter | no | `FFX_AtelSys_Save_048_GetContextStepCounter_resi` | returns ctx+0x1F4 of the current context. Every shipped call is the 0x58 form and no window reads R afterwards |  | V |
| 49 | 1 | 1 | e | 58 | sceneId:int | int 0 | no | `FFX_AtelSys_Save_049_LoadSceneMap_resi` | sub_86DCD0: FFX_SaveData_SetSceneAndSub then FFX_Map_LoadMapSync, synchronous | stall | V |
| 50 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_050_TouchThreadChannel_resi` | FFX_Atel_GetActorThreadChannel on the caller and drops the result |  | V |
| 51 | 0 | 0 |  |  |  | int byte | no | dead `FFX_AtelSys_Save_051_GetDebugGuiByte_resi` | sub_830320 returns byte_13007E8, the debug GUI root window state |  | V |
| 52 | 0 | 0 |  |  | ignored:int | int 1 | no | dead `FFX_AtelSys_Save_052_DevSetDebugGuiByte_resi` | dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_830330(1) | dev | V |
| 53 | 0 | 0 |  |  | chr:int | int bool | no | dead `FFX_AtelSys_Save_053_PartyCharHasPendingScript_resi` | maybe_FFX_Atel_PartyCharHasPendingScript(chr), as mapfx:88 |  | V |
| 54 | 12 | 6 | e | 58 | on:int | int on (0/1) | no | `FFX_AtelSys_Save_054_SetPlacedEffectsEnabled_resi` | sub_729050: dword_CEC1F0 = on, the gate FFX_MapFx_UpdatePlacedEffects 0x729AC0 tests first, the same as core:550 and mapfx:12 |  | V |
| 55 | 0 | 0 |  |  | id:int | int pointer | no | dead `FFX_AtelSys_Save_055_GetMapNameString_resi` | returns a pointer into the dword_25D16C4 string table, row 2 * id (+1 under the map name alt flag) | ptr | V |
| 56 | 0 | 0 |  |  | group:int, file:int, var:int | int 1 | no | dead `FFX_AtelSys_Save_056_AssetLoadToVar_resi` | FFX_GetAssetLoader: slot 4 (group), then slot 0 (file, address of var) | disk | V |
| 57 | 0 | 0 |  |  | file:int | int | no | dead `FFX_AtelSys_Save_057_AssetQuery_resi` | FFX_GetAssetLoader slot 2 (file) | disk | V |
| 58 | 0 | 0 |  |  | a:int, b:int, c:int | int 1 | no | dead `FFX_AtelSys_Save_058_VoiceStartRaw_resi` | sub_8875D0: sub_67A4E0(a, b, c or 0), sub_81EE20(a), then sets the busy bit 0 in qword_132FF14 |  | I |
| 59 | 2 | 2 | e | 58 |  | int 1 | no | `FFX_AtelSys_Save_059_DebugFullItem_resi` | FFX_Debug_FullItem: fills the item list (with a language 11 case) and opens a menu | local | V |
| 60 | 2 | 2 | e | 58 |  | int 1 | no | `FFX_AtelSys_Save_060_LoadPlayerData_resi` | FFX_SaveData_GrantAllAbilities (was FFX_Btl_LoadPlayerData), ORs every ability from ply_save.bin rows 18 and 19 into the save block, see BOOSTERS.md | stall | V |
| 61 | 0 | 0 |  |  | bit:int, on:int | int on or 0 | no | dead `FFX_AtelSys_Save_061_SetSignalFlagTo_resi` | on: sub_86FA40(bit) sets bit of dword_1326B70 (core:378), off: sub_863860(bit) clears it (core:379) |  | V |
| 62 | 0 | 0 |  |  | seId:int | int | no | dead `FFX_AtelSys_Save_062_PlaySeOrRamp_resi` | sub_886680(seId), the core:403 body |  | I |
| 63 | 0 | 0 |  |  | mode:int | int mode | no | dead `FFX_AtelSys_Save_063_SetSoundOutputMode_resi` | sub_887180: clamps to 0..2, sub_81EA00(mode), byte_1330092 = mode. FFX_Menu_ApplySaveSoundOptions calls the same |  | V |
| 64 | 0 | 0 |  |  |  | int mode | no | dead `FFX_AtelSys_Save_064_GetSoundOutputMode_resi` | sub_886430 returns byte_1330092 |  | V |
| 65 | 0 | 0 |  |  | on:int | int on | no | dead `FFX_AtelSys_Save_065_SetSoundFlag1330093_resi` | sub_8871C0: byte_1330093 = clamp(on, 0, 1) |  | V |
| 66 | 0 | 0 |  |  |  | int | no | dead `FFX_AtelSys_Save_066_GetSoundFlag1330093_resi` | sub_886440 returns byte_1330093 |  | V |
| 67 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_067_StopAllSound_resi` | sub_886DB0: sub_81E5D0(-1), as FFX_Map_ReturnToTitle does |  | I |
| 68 | 0 | 0 |  |  | x:int | int 0 | no | dead `FFX_AtelSys_Save_068_NopMapOutA_resi` | nullsub_130(x), a stub the map out code also calls |  | V |
| 69 | 0 | 0 |  |  | code:int | int pointer | no | dead `FFX_AtelSys_Save_069_FutureErrorText_resi` | std::_Get_future_error_what(code), a library string pointer, an identical COMDAT folded into the slot | ptr | V |
| 70 | 0 | 0 |  |  | x:int | int 0 | no | dead `FFX_AtelSys_Save_070_NopMapOutB_resi` | nullsub_132(x) |  | V |
| 71 | 0 | 0 |  |  | offset:int | float | no | dead `FFX_AtelSys_Save_071_GetSaveFloat_resf` | the float at save block + offset, unchecked |  | V |
| 72 | 0 | 0 |  |  | offset:int, value:float | float value | no | dead `FFX_AtelSys_Save_072_SetSaveFloat_resf` | writes a float at save block + offset |  | V |
| 73 | 0 | 0 |  |  | offset:int | int | no | dead `FFX_AtelSys_Save_073_GetSaveDword_resi` | the dword at save block + offset |  | V |
| 74 | 0 | 0 |  |  | offset:int, value:float | int value | no | dead `FFX_AtelSys_Save_074_SetSaveDword_resi` | writes (int)value as a dword at save block + offset |  | V |
| 75 | 0 | 0 |  |  | offset:int | int | no | dead `FFX_AtelSys_Save_075_GetSaveWord_resi` | the signed word at save block + offset |  | V |
| 76 | 0 | 0 |  |  | offset:int, value:float | int value | no | dead `FFX_AtelSys_Save_076_SetSaveWord_resi` | writes (int)value as a word |  | V |
| 77 | 0 | 0 |  |  | offset:int | int | no | dead `FFX_AtelSys_Save_077_GetSaveByte_resi` | the signed byte at save block + offset |  | V |
| 78 | 0 | 0 |  |  | offset:int, value:float | int value | no | dead `FFX_AtelSys_Save_078_SetSaveByte_resi` | writes (int)value as a byte |  | V |
| 79 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_079_ReturnOne_resi` | returns 1 |  | V |
| 80 | 0 | 0 |  |  |  | int 1 | no | dead `FFX_AtelSys_Save_080_NopReturnOne_resi` | calls sub_76F090 only when sub_76F060 (return 0) is set, so returns 1 and does nothing |  | V |
| 81 | 9 | 5 | e | 58 | on:int | int on (0/1) | no | `FFX_AtelSys_Save_081_SetDemoMode_resi` | sub_88EC10: byte_1330832 = on. sub_88E880 picks the FFXDEMO paths while it is set. The title (test20) calls it with 0 |  | V |
| 82 | 21 | 13 | e | 35 |  | int bool | no | `FFX_AtelSys_Save_082_GetDemoMode_resi` | sub_88D6D0 returns byte_1330832, also read by FFX_Field_StepMenuOpenRequest, sub_86BAE0 and the sub_888xxx save menu modules |  | V |
| 83 | 1 | 1 | e | 35 | a:int, b:int, c:int, d:int | int 0 | no | `FFX_AtelSys_Save_083_Drop4Ret0_resi` | pops four values and returns 0 |  | V |
| 84 | 1 | 1 | e | 35 | size:int | int pointer | no | `FFX_AtelSys_Save_084_AllocActorPoolBlock_resi` | FFX_Tk_AllocSectorRounded(size), a raw heap pointer into the script | ptr | V |
| 85 | 1 | 1 | e | 58 | pointer:int | int 1 | no | `FFX_AtelSys_Save_085_FreeTkBlock_resi` | FFX_Tk_Free(pointer), the free for save:84 |  | V |
| 86 | 2 | 1 | e | 58 | group:int, index:int, dest:int | int 1 | no | `FFX_AtelSys_Save_086_ReadAssetGroup_resi` | saveram_read_group, prints 'Read: group = %d/ index = %d/adrs = 0x%x' and sub_88D140: asset loader slots 4, 5, 0 and 2, a load into dest | disk | V |
| 87 | 1 | 1 | e | 58 | pointer:int | int 1 | no | `FFX_AtelSys_Save_087_CopySaveBlockFrom_resi` | saveram_copy_to, memcpy(&g_ffxSaveData, pointer + 64, 0x68C0), the whole block transplant with nothing re-derived |  | V |
| 88 | 8 | 1 | e | 35 | var:int | int pointer | no | `FFX_AtelSys_Save_088_GetVarAddress_resi` | FFX_Atel_ResolveVarAddress(var), a raw pointer into script memory | ptr | V |
| 89 | 0 | 0 |  |  | ability:int, on:int | int old | no | dead `FFX_AtelSys_Save_089_SwapInnateAbilityFlag_resi` | returns FFX_SaveData_TestInnateAbilityFlag(ability) and sets it to on through sub_785D40 |  | V |
| 90 | 0 | 0 |  |  | a:int, b:int | int 1 | no | dead `FFX_AtelSys_Save_090_PlayKindSe255_resi` | sub_8866B0(255, a, 0, b), the direct SE form through sub_8867E0 |  | I |
| 91 | 145 | 11 | e | 58 | x:int | int x | no | `FFX_AtelSys_Save_091_DebugPrintDec_resi` | sprintf 'DEBUG:%d/%d' of the thread id and x into nullsub_123, returns x |  | V |
| 92 | 0 | 0 |  |  | x:int | int x | no | dead `FFX_AtelSys_Save_092_DebugPrintHex_resi` | sprintf 'DEBUG:%d/0x%x' into nullsub_123, returns x |  | V |
| 93 | 0 | 0 |  |  | lang:int | int lang | no | dead `FFX_AtelSys_Save_093_SetTextLanguage_resi` | FFX_MesWin_SetTextLanguage(lang) | local | V |

## 3. The apply script

`../tools/apply_movsave_lib_names.py` covers both libraries and follows the Came, Ch and Core scripts:

- It reads both tables from the IDB, Movie with 16 rows and Save with 94, and for every row in its
  NAMES tables gives each non null slot `FFX_AtelSys_Movie_NNN_<Role>_<slot>` or
  `FFX_AtelSys_Save_NNN_<Role>_<slot>`. `--no-slot-suffix` drops the slot on single slot rows.
- It only renames a slot whose name is a positional name of that row (with or without the zero
  padding, or the bare `FFX_AtelSys_<Lib>_NNN` on a single slot row), a `sub_`/`loc_`/`nullsub_`
  placeholder, or the exact misname `__RTC_NumErrors_800` on the Movie 0 poll. It skips a slot that is
  not a function start, a function in more than one slot of the same table, and a name already used
  elsewhere.
- Dead rows are left positional unless `--include-dead`. Save row 10 is null and not listed.
- `--clear-false-movie-rows` removes the positional Movie names 16..145, and the false repeatable
  comment on the same functions, but only where the name is exactly `FFX_AtelSys_Movie_NNN_<slot>` and
  the pointer at 0xC40E30 + 16 * NNN + 4 * slot is that function. The functions go back to `sub_`
  names, so whoever names the sa_menu code later starts clean. It does not touch the table's own data
  comment, which still says "145 entries" and wants rewriting by hand.
- Dry run by default, and it never saves.

Runs on the scratch copy:

- Default: 54 to rename (22 Movie slots over the 10 used rows, 32 Save slots over the 31 used rows), 0
  skipped, 0 already named.
- `--include-dead --clear-false-movie-rows`: 124 to rename (30 Movie, 94 Save), 0 skipped, 449 false
  Movie names to clear, 448 with the false comment.
- The same with `--apply`, headless so nothing is saved: renamed 124, cleared 449, failed 0.

## 4. Corrections to other notes

Applied on 2026-10-26, ebp.py's names too. Its Movie count stays 146 on purpose, see its comment.

- `ATEL_CONTEXT.md` 13.2: Movie has 16 rows, not 146, and the shapes row should read 9 int only and 7
  start+poll+int with no null. The "rows that are not handlers" paragraph, `FFX_AtelSys_BareRet`, the
  Movie 16 start and the Movie 135 float slot are all about the sa_menu pointer table. 13.3: past the
  end of Movie is `off_C40F30`, not `"128M"`. 13.5: the Movie 16 and `sa_menu_init` rows come out of
  the syscall list.
- `tools/ebp.py`: `LIB_INFO[11]` should be `(0xC40E30, 16, 13)`. The names (11, 135), (11, 136), (11,
  143) and (11, 144) are not syscalls, and (12, 0) and (12, 1) are swapped. (11, 2) is
  `movie_status`.
- `MINIGAMES_TIMED.md` line 153: "by `movie:143` and `movie:145`" are sa_menu functions, no script can
  reach them. Line 208: "the `movie:136` start handler" likewise. The 145 record count and 123 blocking
  rows for Movie come from the false tail.
- `BLITZBALL.md` lines 122, 603 and 672: `FFX_AtelSys_Movie_136_start` is not an ATEL syscall handler,
  so "the only clock-reaching ATEL syscall handler" is gone and a Movie:136 byte pattern means nothing.
  movie:0, movie:9 and movie:10 do reach `GetTickCount`, through the decode thread start.
- `ASSET_PATHS.md` line 169, `ASYNC_LOAD_REQUESTS.md` line 165 and `THREADS_AND_CLOCKS.md` line 426 use
  the `FFX_AtelSys_Movie_136_*` names as function references. The functions are real, they are sa_menu
  code.
- `FMV_SYNC.md` section 4: mgr+1824 is written with 1 by `sub_6437F0` from movie:0 (flag 0x2000), not
  only with 0. And movie:9 and movie:10 wait for the first frame, not for the end, which follows from
  that note's own 2026-10-13 correction of mgr+1764.

## 5. sa_menu functions past the Movie table

Applied to the live IDB on 2026-10-26: 38 of the names below (sa_menu_stop and hdd_install_end already had
theirs, and `sa_menu_init` 0x76F3B0 is still a chunk of sub_8BF050, so it is not named yet).
`FFX_AtelSys_BareRet` is now `FFX_SaMenu_NoAction`. The three tables are labelled `g_ffxSaMenuGetters`
(`off_C40F30`), `g_ffxSaMenuActions16` (`off_C416E0`) and `g_ffxSaMenuActions10` (`off_C41720`).

2026-10-26. Name proposals for the code that used to carry the false Movie names, read through idalib
on a scratch copy of the IDB. Nothing was renamed. The same proposals are a Python `NAMES` dict in the
session scratchpad (`samenu_names.py`) for whoever applies them.

### 5.1 What sa_menu is

It is menu module 11, which `MENU_SYSTEM.md` calls the save frontend. `FFX_Module_SwitchGameMode
0x8AA780` registers `off_C5A08C` as module 11 and starts it for game mode 0x400000 at draw priority -16.
The module's callbacks are `sub_8BEF50` (step), `sub_8BEFF0` (draw, sets a 512x416 view first),
`sub_8BF050` (init), `sub_8BF070` (stores the start argument in `dword_186A234`) and `j_sa_menu_stop`.
They call into the sa_menu code through five pointers at 0xC416A8..0xC416B8: init, update, draw, stop
and start. [V]

`sa_menu_start` takes a mode. Modes 1 to 4 and the default pick different menu files
(`dword_C416BC`, `dword_C416C0`, `dword_C416D8`), and mode 4 is the HDD install menu, which also loads
files 15, 16, 17 and 13 and calls `sub_779000` to measure free space on `hdd0:`. Then it stores three
tables the data driven menu calls into:

- `off_C40F30` into `dword_C416D0`: 478 getters of one int argument, 0xC40F30..0xC416A4. `sub_774860`
  calls them for text tokens (the index is the token's high word), `sub_775530` for item enable checks,
  `sub_776940` from the item scripts. "k" below is (address - 0xC40F30) / 4.
- `off_C416E0` into `dword_C416D4`: 16 actions, called by `sub_776940`.
- `off_C41720` into `dword_C416DC`: 10 actions, then `0x100` and 0, called by `sub_776940`.

None of it is reachable from an ATEL script. Several actions write the save block (world flags, gil),
so the module matters to co-op as a menu, not as a syscall library. [V]

### 5.2 The getter families, not named one by one

- 239 getters are `FFX_SaveData_TestCharAbilityBit(a1, n)` with n from 0x3000 to 0x3120.
- 170 read a field of record a1 in `dword_112A034` (472 byte records, loaded from menu file 0 by
  `sa_menu_start`). 10 of them turn a u16 field into an item name through `sub_7767A0`
  (`FFX_Btl_GetItemRow`), 113 into a text through `sub_774720` (`sub_8C19A0`), the rest return the raw
  byte, word or dword.
- k=2, 3 and 4 return dwords +0x50, +0x58 and +0x54 of `FFX_SaveData__getCharRecord(a1)`. Those fields
  are not identified.

A name per flag or per field offset would only restate the operand, so these stay `sub_` until the menu
data says what each token shows.

### 5.3 Proposed names

| VA | where | was | proposed | evidence | mark |
|---|---|---|---|---|---|
| 0x76F3B0 | 0xC416A8 init | Movie 135 float | `sa_menu_init` | prints `"sa_menu_init\n"`, zeroes the menu globals. It is a `loc_` inside a chunk of `sub_8BF050`, so the chunk has to come off and a function be made first | V |
| 0x76F470 | 0xC416AC update | Movie 135 int | `FFX_SaMenu_Update` | called only by `sub_8BEF50` in state 3, every frame. Runs the `dword_112A014` task, reads `FFX_MenuSys_GetHeld/Pressed/Repeat`, walks the 312 byte item records, runs item scripts through `sub_776940`, ends in `TOMenuTermHelp` | V |
| 0x76F850 | 0xC416B0 draw | Movie 136 start | `FFX_SaMenu_Draw` | called only by `sub_8BEFF0` after it sets the view. Draws the item text through `sub_774CB0` (the `cos(t*8)*2` wobble, `THREADS_AND_CLOCKS.md` A21), prints `"w=%d h=%d\n"` | V |
| 0x770340 | 0xC416B4 stop | Movie 136 poll | `sa_menu_stop` (already) | prints `"sa_menu_stop()\n"`, frees the menu buffers | V |
| 0x770430 | 0xC416B8 start | Movie 136 float | `sa_menu_start` | prints `"sa_menu_start\n"`, called by `sub_8BEF50` with the module argument, stores the three tables (5.1), loads the `help` FTC | V |
| 0x7722A0 | k=0 | Movie 16 start | `FFX_SaMenu_GetBattlesSinceMapLoad` | `sub_784F50() - FFX_Map_GetSave3D14AtLoad()`. `sub_784F50` returns `g_ffxBattleCount 0x11307A4`, save +0x3D14, and the other is the same word captured at map load | V |
| 0x772310 | k=1 | Movie 16 poll | `FFX_SaMenu_GetBattleCount` | thunk to `sub_784F50` | V |
| 0x7724C0 | k=5 | Movie 17 poll | `FFX_SaMenu_GetItemCount` | `FFX_SaveData_GetItemCount(a1)` | V |
| 0x7724F0 | k=6 | Movie 17 float | `FFX_SaMenu_TestKeyItemFlag` | `FFX_SaveData_TestKeyItemFlag(a1)` | V |
| 0x772550 | k=7 | Movie 17 int | `FFX_SaMenu_GetGil` | `FFX_SaveData_GetGil()` | V |
| 0x7725B0 | k=8 | Movie 18 start | `FFX_SaMenu_GetWorldFlagByte` | byte a1 of `FFX_SaveData_GetWorldFlags3DCC()` | V |
| 0x772620 | k=9 | Movie 18 poll | `FFX_SaMenu_TestWorldFlagBit` | bit a1 of the same block | V |
| 0x7726D0 | k=10 | Movie 18 float | `FFX_SaMenu_GetScenarioWord` | `FFX_SaveData_GetScenarioWord()` | V |
| 0x772730 | k=11 | Movie 18 int | `FFX_SaMenu_GetSaveWordEA` | `sub_86C4B0`, save word +0xEA, the reader core:526 uses | V |
| 0x772790 | k=12 | Movie 19 start | `FFX_SaMenu_GetCaptureCount` | `jmp FFX_SaveData_GetCaptureCount` (`MONSTER_ARENA.md` section 4) | V |
| 0x7727F0 | k=13 | Movie 19 poll | `FFX_SaMenu_IsInstallItemEnabled` | `byte_112A530[12 * a1]`, the per item flag `sub_778FB0` sums the install size over | V |
| 0x7729D0 | k=58 | Movie 30 float | `FFX_SaMenu_IsCursorItem` | returns `dword_112A078`, which `FFX_SaMenu_Update` sets to "this record is the cursor record" while it walks the items | I |
| 0x772970 | k=103 | Movie 41 int | `FFX_SaMenu_IsCrossPressed` | `dword_112A08C & 0x40`, the pressed mask. 0x40 is cross in the PS2 pad layout `sub_775530` decodes (0x1000 up, 0x2000 right, 0x4000 down, 0x8000 left) | I |
| 0x772800 | k=284 | Movie 87 start | `FFX_SaMenu_IsCirclePressed` | `dword_112A08C & 0x20`, circle in the same layout | I |
| 0x772900 | k=286 | Movie 87 float | `FFX_SaMenu_IsCharInParty` | `FFX_SaveData_IsCharInParty(a1) == 1` | V |
| 0x7793A0 | k=136 | Movie 50 start | `FFX_SaMenu_GetHddInstallMode` | returns `dword_112A518`, set by `FFX_SaMenu_DetectHddInstallMode` | V |
| 0x779290 | k=137 | Movie 50 poll | `FFX_SaMenu_GetHddInstallPercent` | `100 * dword_112A7AC / dword_112A524`, capped at 100, 0 with no total | V |
| 0x773530 | k=138 | Movie 50 float | `FFX_SaMenu_GetInstallItemSectors` | `sub_778960`, `dword_112A52C[3 * a1] / 2048` | V |
| 0x779880 | k=139 | Movie 50 int | `FFX_SaMenu_GetHddInstallState` | maps `byte_112A50B`, which `hdd_thread_main` writes: 2 to 1, -1 to 2, -2 to 3, else 0 | V |
| 0x778FB0 | k=140 | Movie 51 start | `FFX_SaMenu_GetHddInstallSizeMb` | sums `dword_112A52C` over the enabled items, then `((((sum >> 20) + 255) >> 7) + 2) << 7`. `sub_779000` compares the same value `>> 7` with free space | I |
| 0x773B80 | k=141 | Movie 51 poll | `FFX_SaMenu_GetHddCheckStatus` | `sub_777E90`, 2 when `byte_112A50A` is set, else `byte_112A509` from `hdd_install_check`. The reader core:578 uses | V |
| 0x773780 | k=310 | Movie 93 float | `FFX_SaMenu_GetMonsterName` | `FFX_Btl_GetMonsterNameById` into record a1 +408 and returns it | V |
| 0x773B90 | off_C416E0 0..7, 10..12, 14, 15 | Movie 139..142 | `FFX_SaMenu_NoAction` | a bare `ret`, now `FFX_AtelSys_BareRet` with a Movie comment that should go. These 13 slots are its only references | V |
| 0x773800 | off_C416E0 8 | Movie 141 start | `FFX_SaMenu_SetWorldFlagByte` | `FFX_SaveData_GetWorldFlags3DCC()[a1] = a2` | V |
| 0x773880 | off_C416E0 9 | Movie 141 poll | `FFX_SaMenu_SetWorldFlagBit` | clears bit a1, sets it when a2 != 0 | V |
| 0x773940 | off_C416E0 13 | Movie 142 poll | `FFX_SaMenu_SetInstallItemEnabled` | thunk to `sub_779590`, `byte_112A530[12 * a1] = a2` | V |
| 0x7792E0 | off_C41720 0 | Movie 143 start | `FFX_SaMenu_DetectHddInstallMode` | also called by `libmscd__f76C560` at 0x76CC00. Clears `maybe_g_ffxMovieStreamFromHdd` and puts it back after, resets the install state, sets `dword_112A518` from `sub_778980() & 0xF` (3 on PC), prints `"hdd_install_mode=%d\n"` | V |
| 0x7792C0 | off_C41720 1 | Movie 143 poll | `hdd_install_end` (already) | prints `"hdd_install_end()\n"` | V |
| 0x773990 | off_C41720 2 | Movie 143 float | `FFX_SaMenu_SpendGil` | thunk to `FFX_SaveData_SpendGil(a1)` | V |
| 0x7739C0 | off_C41720 3 | Movie 143 int | `FFX_SaMenu_CloseWithResult1` | once only (`byte_112A038`): result `dword_112A058 = 1`, value `dword_112A054 = a1`. `TOMenuTermHelp` hands the pair to `FFX_SaveLoadMenu_SetResult` | V |
| 0x773A30 | off_C41720 4 | Movie 144 start | `FFX_SaMenu_CloseWithResult0` | the same with result 0 | V |
| 0x773AA0 | off_C41720 5 | Movie 144 poll | `FFX_SaMenu_CloseWithResult3` | the same with result 3 | V |
| 0x7789A0 | off_C41720 6 | Movie 144 float | `hdd_format` | prints `"<hdd format>\n"`, calls `sub_62F780` (0 on PC) with `"hdd0:"`, prints `"cannot format disk %d\n"` on failure. ebp.py already uses the name | V |
| 0x779120 | off_C41720 7 | Movie 144 int | `FFX_SaMenu_HddInstallStart` | unless `byte_112A50B` is set: zeroes the progress, allocates two 0x100800 byte buffers, sets `dword_112A018 = sub_7797A0` (the copy loop), registers `hdd_thread_main` through `FFX_Ps2Thread_SetHddCallback_dead` and signals it | V |
| 0x773B10 | off_C41720 8 | Movie 145 start | `FFX_SaMenu_PlaySound` | `sub_886E80(a1 \| 0x80000000)`, the call the menu makes with an item's sound id (item +242, +244, +248), ending in `sub_67A440(id, 127, 63)` | I |
| 0x773C60 | off_C41720 9 | Movie 145 poll | `FFX_SaMenu_SetMovieStreamFromHdd` | `maybe_g_ffxMovieStreamFromHdd = (a1 != 0)`, mounts `hdd0:` once through `sub_779490` when set, then `sub_88E930` passes `pfs0:ffx44` and `pfs0:ffx45`, or an empty string, to `sub_81D470` and `sub_81D3C0` | V |

The "was" column is the false positional name the address carried, for finding old references.

### 5.4 Open

- Which menu file each mode loads, and so which screens use the getter families. The record table at
  `dword_112A034` has a monster name at +408 and a gil amount at +404 (8000 for id 249, and k=252
  returns the gil left after paying it), which reads like a shop or a fee list, not a save screen. [I]
- Who asks for game mode 0x400000, and with which argument. `FFX_MenuSys_StepFrame` is the only caller
  of `FFX_Module_SwitchGameMode`, the requests reaching it are not traced.
  Answered 2026-10-28 (`MENU_SYSTEM.md` third pass): scripts, through sg:29. 0x40400003 in 13
  non-test events, each right after `core:525` sets save word +0xEA, and in 12 of them followed by
  sg:29 `0x40800000 | core:506()`, which opens a module 22 menu demo with the sa_menu's result.
  0x40400000 or two script variables from nagi0700, and 0x40400004 from test20. Module 11 is not the
  save screen, `MENU_SYSTEM.md` row 11 is corrected.
- The fields at char record +0x50, +0x54 and +0x58.
