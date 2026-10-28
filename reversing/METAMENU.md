# metamenu.vbf: the launcher's archive, all 150 files and who opens them

Written 2026-10-28 for Project Zanarkand item 1.12.12. FFX.exe addresses are IDA VAs (image base
0x400000, RVA = VA - 0x400000), read through idalib on a private IDB copy. The launcher is a .NET
assembly, so its code is cited as `Class::Method` with the metadata token, read from IL with the
Windows SDK `ildasm` streamed to memory (nothing written to disk). `[V]` is read this session in the
IL or the disassembly, `[I]` is inferred.

The listing below comes from `../tools/metamenu.py`, which reads the install at run time, rebuilds
every path the launcher can ask for and checks each archive entry against that set:

```
python -I E:\Git\FFXCoopProject\tools\metamenu.py [--vbf PATH] [--md] [--summary]
```

Proposed IDB names are in `../tools/apply_metamenu_names.py` (5 rows, rehearsed on a copy, nothing
skipped).

## 0. The answer

1. **metamenu.vbf is the asset archive of `FFX&X-2_LAUNCHER.exe`**, the WinForms title picker that
   offers FFX, FFX-2, Eternal Calm, Last Mission, the -Will- episode, the 2014 staff credits and the
   settings dialog. It is not the Iggy Flash UI and holds no swf, no Phyre cluster and nothing
   FFX.exe reads. The Iggy movies are 19 `.swf` files in `FFX_Data.vbf` under
   `ffx_data/gamedata/ps3data/flash/`. [V]
2. **150 files: 144 PNG, 2 XML, 2 VAG, 1 OGG, 1 DAT**, 21,885,132 bytes uncompressed in a
   21,709,755 byte archive. 128 of the PNGs are the title screen and its seven buttons in 8
   languages at 2 sizes. [V]
3. **The launcher opens it once, in `Program::Main`, at launcher start**, and reads files out of it
   on its UI thread at five moments: start, Settings open, mute toggle, Settings close after a
   language change, and the Credits 2014 button. Section 3. [V]
4. **149 files are reachable, 1 is never opened**: `menumetamenu/setting_logo.png`. This is a proof
   over the launcher IL plus the shipped XML, not a string scan. Section 5. [V]
5. **FFX.exe never opens metamenu.vbf.** Its single mount is `data\FFX_Data.vbf`, and the
   `/MetaMenu/...` strings it does contain resolve into a separate 15 entry `metamenu/` tree inside
   `FFX_Data.vbf`, read by an orphan. Section 4. [V]
6. **Nothing in it feeds simulation state.** It is presentation for another process that has exited
   before FFX.exe simulates anything. The launcher's two outputs that do reach FFX.exe, the command
   line and `GameSetting.ini`, are built from constants in the launcher IL, not from archive data.
   Section 6. [V]

## 1. Where it lives and the tree

`<install>\data\metamenu.vbf`, next to `FFX_Data.vbf` and `FFX2_Data.vbf`. Same VBF container
(`VBF_FORMAT.md`), header MD5 verifies, 150 entries. Every path starts `metamenu/`. [V]

| subtree | files | what |
| --- | --- | --- |
| `metamenu/ps3data/menumetamenu/<lang>/` | 64 | full size title screen `titlemenu.png` 1280x720 and seven buttons (334x60 or 371x27), 8 per language |
| `metamenu/ps3data/menumetamenu/<lang>/360p/` | 64 | the same 8 at half size, `titlemenu.png` 640x360 |
| `metamenu/ps3data/menumetamenu/` | 8 | `menuactiverects.xml` (layout), `settingsloc.xml` (settings strings), `metamenu.ogg` (BGM), two `.vag` SE, `speaker_on.png`, `speaker_off.png`, `setting_logo.png` |
| `metamenu/ps3data/menucredit/` | 5 | `credits_1280.png`, `scollbar.png`, `button_down/over/up.png` for the credits window |
| `metamenu/ps3data/menucredit/<lang>/` | 8 | `credits_logo.png`, one per language |
| `metamenu/gamedata/ps3data/menucreditstext/` | 1 | `credit.dat`, the staff list |

`<lang>` is one of `ch de fr it jp kr sp us`. The seven buttons are `btn_ffx`, `btn_ffx_2`,
`btn_ffx_2_lm`, `btn_ffx_nagi`, `btn_credits`, `btn_credits_2014` and `btn_setting`.

| type | count | bytes | how classified |
| --- | --- | --- | --- |
| PNG | 144 | 16,693,970 | `89 50 4E 47` magic, size from IHDR. Loaded with `System.Drawing.Image.FromStream` |
| OGG | 1 | 5,140,305 | `OggS`. FMOD `createSound` from memory, looping |
| VAG | 2 | 35,696 | `VAGp`, PS ADPCM. FMOD `createSound` from memory |
| XML | 2 | 11,014 | UTF-8 with BOM, `<?xml`. `XmlDocument.LoadXml` after skipping the 3 BOM bytes |
| DAT | 1 | 4,147 | no known magic. Parsed by the launcher's `GameLauncher.StaffBinParser::Load` into rows and columns of text. Not decoded further |

There is no Phyre cluster in it, so `phyre.py` classes do not apply. [V]

## 2. The code that opens it

The launcher is `FFX&X-2_LAUNCHER.exe`, a .NET Framework assembly named `GameLauncher` with
namespaces `GameLauncher`, `VBFTool` and an FMOD Ex C# wrapper. It carries its own VBF reader,
`VBFTool.VirtuosBigFileReader`, independent of FFX.exe's native one. [V]

### 2.1 The mount and the reader

| step | method (token) | what |
| --- | --- | --- |
| mount | `Program::Main` (0x060003C7) IL_0098..IL_00b6 | `new VirtuosBigFileReader()`, then `LoadBigFileFile(Path.Combine(Application.StartupPath, "data\\MetaMenu.vbf"))`, before `InitGlobalConfig` and before the main form exists. A `VirtuosBigFileException` is caught and shown in a MessageBox |
| header | `VirtuosBigFileReader::LoadBigFileFile` (0x060003D7) | reads the whole header into arrays (md5 keys, name offsets, block starts, sizes, offsets, string table, block list), builds `Dictionary<string,int> mMD5ToIndex`, recomputes the header MD5 and throws on a mismatch. Closes the file |
| lookup | `VirtuosBigFileReader::GetFileContents` (0x060003D8) | `path.ToLower()`, MD5 of its UTF-8 bytes, hex with `X02`, dictionary lookup. Then `File.Open` on the archive per call, seeks, reads each 64 KiB block, inflates with `DeflateStream` or copies a raw block, returns a `byte[]`. A miss returns null |
| wrapper | `Program::LoadVBFData` (0x060003A5) | `mBigFileReader.GetFileContents(path)`. **The only caller of `GetFileContents`** |
| images | `Program::LoadVBFImage` (0x060003A4) | `mVBFImageCache` lookup by path, else `LoadVBFData` then `Image.FromStream`, and cache it. So an image is read from the archive at most once per launcher run |
| text | `Program::LoadVBFUTF8Text` (0x060003A6) | `LoadVBFData`, then `Encoding.UTF8.GetString(data, 3, len - 3)`. Not cached, re-read on every call |

Lookup is the same key rule as FFX.exe's (`md5` of the lowercased path, no root prefix), which is
why `tools/vbf.py` reads this archive unchanged. [V]

### 2.2 The path builders

| builder (token) | output |
| --- | --- |
| `Program::get_LanguagePath` (0x060003AA) | `mLanguage`, except `en` -> `us` and `es` -> `sp` |
| `Program::get_IsSmall` (0x060003AD) | `mScreenHeight < 720`. `mScreenHeight` is `Screen.PrimaryScreen.WorkingArea.Height`, set once in `InitGlobalConfig`, so it is the work area (taskbar excluded), not the display mode |
| `Program::get_XmlNodeSelectorCondition` (0x060003A7) | `[(not(@language) or @language='<mLanguage>') and not(@small)]`, with the `and not(@small)` part dropped when small |
| `Program::get_LangAndSizePath` (0x060003A8) | `<mLanguage>/360p/` or `<mLanguage>/`. **No caller**, dead |

`mLanguage` is set in `Program::InitGlobalConfig` (0x060003C2) from `GetUserDefaultLangID`: 0x404,
0xC04 or 0x1404 give `ch`, primary language 0x0C `fr`, 0x07 `de`, 0x10 `it`, 0x0A `es`, 0x11 `jp`,
0x12 `kr`, anything else `en`. `Program::LoadIniFile` (0x060003BD) then replaces it with the
`Language` key of `GameSetting.ini` when that is not empty and writes it back. Simplified Chinese
(0x804) therefore falls to `en`. [V]

`MenuActiveRects.xml` holds a `Background` and seven `ActiveRect` nodes. Each has an `Image` child
twice, a `small="true"` one first (`360p/<name>.png`) and a plain one (`<name>.png`), plus
geometry, and each `ActiveRect` has an `OnClick` method name. With the selector above, the small
image wins when `get_IsSmall` is true (first in document order) and the plain one otherwise. The
image path is then `MetaMenu/PS3Data/MenuMetaMenu/` + `get_LanguagePath()` + `/` + the Image text.
[V]

| ActiveRect image | OnClick | what the click does |
| --- | --- | --- |
| `btn_FFX.png` | `MainForm::OnFFXClicked` | `RunExeAndExit("FFX.exe", "")` |
| `btn_FFX_nagi.png` | `MainForm::OnSideStoryClicked` | `RunExeAndExit("FFX.exe", "_ECalm")`, Eternal Calm (`COMMAND_LINE.md`) |
| `btn_FFX_2.png` | `MainForm::OnFFX2Clicked` | `RunExeAndExit("FFX-2.exe", "")` |
| `btn_FFX_2_lm.png` | `MainForm::OnLastMissionClicked` | `RunExeAndExit("FFX-2.exe", "FFX2_LASTMISSION")` |
| `btn_credits.png` | `MainForm::OnCreditClicked` (0x06000387) | `RunExeAndExit("FFX&X-2_Will.exe", "")` |
| `btn_credits_2014.png` | `MainForm::OnCredit2014Clicked` (0x06000388) | `new CreditForm().ShowDialog()`, the staff credits from this archive |
| `btn_setting.png` | `MainForm::OnConfigClicked` (0x0600037D) | `new ConfigWindow().ShowDialog()`, which hosts a `ConfigForm` |

The exe and parameter strings come from the `PathConfig` string resource read by
`InitGlobalConfig` (`/Paths/FFX`, `/Paths/SideStory`, ...). `OnClick` is resolved by name with
`Type.GetMethod(name, 52)`, and the seven names above are all the XML holds. [V]

### 2.3 Threads

Everything above runs on the launcher's main thread, which is `[STAThread] Program::Main` running
`Application.Run(new MainForm())`. Every archive read is synchronous on that thread. The only other
thread is a thread pool item, `MainForm::CloseAfterSeStopped` (0x06000376), queued by
`MainForm::RunExeAndExit` (0x06000382) after a successful `ShellExecute`. It polls the SE channel
every 50 ms and then closes the main form, so the launcher exits right after starting a game. FMOD's
own mixer thread plays the BGM and SE from memory. [V]

## 3. Which file is read when

| group | files | read by | when |
| --- | --- | --- | --- |
| layout | `menuactiverects.xml` | `MainForm::InitializeActiveRect` (0x06000374) | launcher start (`MainForm::.ctor` 0x06000375, first statement after the base ctor), and again from `OnConfigClicked` when the `Language` ini key changed |
| title screen and buttons | 128 PNGs under `menumetamenu/<lang>/` and `<lang>/360p/` | `MainForm::InitializeActiveRect` through `LoadVBFImage` | same times. Per run it reads 8 of them, one language and one size. A language change in Settings reads the 8 of the new language |
| audio | `metamenu.ogg`, `metamenu_sfx_comfirme.vag`, `metamenu_sfx_move.vag` | `MainForm::InitializeFmodResources` (0x06000378) | launcher start, last statement of `MainForm::.ctor`. The BGM starts in `MainForm::MainForm_Load` |
| settings strings | `settingsloc.xml` | `MainForm::MainForm_Load` (0x06000380), `ConfigForm::ConfigForm_Load` (0x0600000A), `MainForm::OnConfigClicked` | start (window title), Settings open (dialog labels and font), Settings close after a language change (window title) |
| speaker icons | `speaker_on.png`, `speaker_off.png` | `ConfigForm::LoadConfiguration` (0x06000007), `ConfigForm::pictureBox2_Click` (0x0600000E) | Settings open picks one by the `Mute` ini key, clicking the speaker loads the other |
| credits window | `credits_1280.png`, `scollbar.png`, `button_down/over/up.png`, `<lang>/credits_logo.png` | `CreditForm::.ctor` (0x0600001F) | Credits 2014 button. The images are scaled in code when `get_IsSmall` [I from the IL shape] |
| staff list | `credit.dat` | `CreditForm::.ctor` -> `StaffBinParser::Load` | Credits 2014 button |
| unused | `setting_logo.png` | nothing | never |

Nothing is loaded on a booster toggle or any in-game event, because the launcher is gone by then.
A language change only matters inside the launcher's own Settings dialog. [V]

## 4. What FFX.exe does with it: nothing

### 4.1 The proof [V]

- `FFX_InitFileSystem 0x6795B0` mounts `data\FFX_Data.vbf` (string `0xB44D78`, pushed at
  0x6795EB) through `FFX_VbfManager__mountArchive 0x61C150`. That function has exactly one caller,
  the `call` at 0x6795F7, by IDA xref and by a full `.text` rel32 scan, and no absolute reference in
  any segment. `FFX_VbfArchive__open 0x61DAC0` likewise has one caller, 0x61C1DD inside the mount.
  So FFX.exe mounts one archive, ever.
- The only `.vbf` string in FFX.exe, ASCII or UTF-16, is `data\FFX_Data.vbf`. No string names
  `metamenu.vbf`, `MenuMetaMenu`, `MenuCredit`, `titlemenu`, `btn_`, `credit.dat`,
  `MenuActiveRects`, `SettingsLoc`, `speaker_` or `scollbar` (case-insensitive scan of the whole
  file). The same holds for `FFX-2.exe` (mounts `data\FFX2_Data.vbf`) and `FFX&X-2_Will.exe`
  (mounts `data\FFX_Data.vbf`).
- The loose-file fallback cannot reach it either, since it is a single file the game would have to
  mount, not a folder of loose files.

### 4.2 The MetaMenu paths FFX.exe does contain

FFX.exe holds three strings `/MetaMenu/GameData/PS3Data/Video/JP/timestamp_JP.txt` (0xB4A068),
`.../Asia/timestamp_%s.txt` (0xB4A0A8) and `.../US/timestamp_%s.txt` (0xB4A0F4). After
`FFX_fiosUnifyFilename` they become keys `metamenu/gamedata/ps3data/video/...`, which live in
**`FFX_Data.vbf`**, not in metamenu.vbf. `FFX_Data.vbf` has a 15 entry `metamenu/` tree:
`video/{us,jp,asia}/sidestory.webm` (1.80, 1.86 and 1.86 GB, the -Will- episode) and 12
`timestamp_XX.txt` files (`us`: de en fr it sp, `jp`: ch en jp kr, `asia`: ch en kr). metamenu.vbf
has no `video/` entries at all. [V]

The reader is `0x6D98E0`, which IDA has as loose code with no function. It switches on the master
version (0 JP, 1 or 2 US, 3 Asia), sprintf's the path, unifies it, `FFX_File_Open`s it, prints
`Virtuos Error: Cannot locate the video timestamp file!!!.` on a miss, then parses `a,b` lines and
appends them to the list at `this+0x724`. **It has no caller**: no IDA xref, no rel32 call or jump,
no absolute pointer in any segment. Its neighbour `0x6DA550`, which opens the per-language
`.../event/obj_psv/cr/credits/credits.bin` and installs the `cr/credits` ftc font, is orphaned the
same way. Both were already listed as orphans in `ASYNC_LOAD_REQUESTS.md`. [V]

`FFX&X-2_Will.exe` contains the same three timestamp strings plus
`/MetaMenu/GameData/PS3Data/Video/{JP,Asia,US}/SideStory.webm` and `Credits_2014`, so this is code
shared with the -Will- player, compiled into FFX.exe and never called. [V for the strings, I for the
sharing]

### 4.3 The two places FFX.exe touches the launcher

| VA | what | live |
| --- | --- | --- |
| 0x625275 in `Phyre__PWindowWin32Base__wndProc` | `WM_CLOSE`: `PathCombineW(exe dir, L"FFX&X-2_LAUNCHER.exe")`, `ShellExecuteW(..., SW_SHOWNORMAL)`. Closing the game window relaunches the launcher, which then opens metamenu.vbf in its own process | yes [V] |
| 0x42F6D0 | builds `<exe dir>\FFX&X-2_LAUNCHER.exe` into `0xC8FF38` with `PathCombineA`, or returns `0xC8FAF0` when its first byte is set | no caller by xref or rel32 or absolute scan, no function [V] |

## 5. Never opened

**`metamenu/ps3data/menumetamenu/setting_logo.png` (21,432 bytes) is never opened.** [V]

The proof is a closure over the IL, not a string scan:

1. The archive object `Program::mBigFileReader` is read only in `Main` (the mount) and in
   `Program::LoadVBFData`.
2. `LoadVBFData` is called only by `LoadVBFImage`, `LoadVBFUTF8Text`,
   `MainForm::InitializeFmodResources` (3 constants) and `CreditForm::.ctor` (`credit.dat`).
3. `LoadVBFImage` is called only by `ConfigForm::LoadConfiguration` and
   `ConfigForm::pictureBox2_Click` (the two speaker constants), `CreditForm::.ctor` (five constants
   and `MenuCredit/<lang>/credits_logo.png`) and `MainForm::InitializeActiveRect` (the XML `Image`
   values). `LoadVBFUTF8Text` is called with the two XML constants only.
4. So the reachable set is those constants, `credits_logo.png` for the 8 language dirs, and the 16
   `Image` values of `MenuActiveRects.xml` for the 8 language dirs. `tools/metamenu.py` builds that
   set from the archive's own XML and finds 149 entries in it and one outside, `setting_logo.png`.
   No path in the set is missing from the archive.

The one input that is data rather than IL is `MenuActiveRects.xml`, and it ships inside the same
archive, so the result holds for the shipped install. Nothing on the IL side builds a path from
`SettingsLoc.xml` (its nodes are `Font`, `Title`, `ConfigTitle` and `Text` with `Control` or `Key`
attributes, used as label text only). [V]

Reachable but not read on a given run: of the 128 title screen PNGs a run reads the 8 for one
language and one size, plus 8 more per language change in Settings. The size never changes within
a run. [V]

## 6. Simulation or presentation

The recreation is checked step by step by a hash over simulation state. None of metamenu.vbf can
reach it, because every file is consumed by a separate process that exits before FFX.exe runs a
step.

| group | files | call |
| --- | --- | --- |
| title screen, buttons, credits images, speaker icons | 143 PNG | presentation, launcher only |
| BGM and SE | 1 OGG, 2 VAG | presentation, launcher only |
| `menuactiverects.xml` | 1 | presentation. It does pick which button runs which `On*Clicked`, but those handlers and their exe and parameter strings are IL constants and resources |
| `settingsloc.xml` | 1 | presentation, label text. The ini values the Settings dialog writes (`Language`, `Resolution`, `ScreenMode`, `Quality`, `Mute`, `MasterVolume`) come from combobox keys hard-coded in `ConfigForm::ConfigForm_Load`, not from this file |
| `credit.dat` | 1 | presentation, launcher only |
| `setting_logo.png` | 1 | unused |

What does reach FFX.exe from the launcher is the command line (`_ECalm` changes the first event,
`COMMAND_LINE.md`) and `GameSetting.ini` (the language picks the text and master tree,
`ASSET_PATHS.md` section 2). Both are launcher behaviour fixed in its IL, and both are inputs the
recreation reads directly. A recreation can ignore metamenu.vbf entirely, or rebuild the launcher as
a separate front end, without touching the hash. [V]

## 7. For the recreation

- FFX.exe's own boot does not depend on the launcher. The relaunch on `WM_CLOSE` is the only link,
  and PROGRESS.md already lists it as not reproduced.
- If item 16.7 wants "the HD prompts and remaster UI", those are the Iggy movies under
  `ffx_data/gamedata/ps3data/flash/` in `FFX_Data.vbf` (`escmenu.swf`, `pausemenu.swf`,
  `openingscreen.swf`, `saveload_*.swf`, `box*.swf`, the booster toasts `autobattle*.swf`,
  `speed_2/4.swf`, `invincible.swf`, `enemy_0/1.swf`, `chocobo.swf`, `freecamera.swf`,
  `time_stop.swf`), not metamenu.vbf.
- A native launcher would need: the 8 language dirs and the `en -> us`, `es -> sp` mapping, the
  work-area-under-720 switch to `360p/`, the seven buttons and their command lines, the settings
  keys, and the BGM loop. `InitializeFmodResources` calls `setLoopPoints(0x156F, 1, 0x1CA3C, 1)`
  on the BGM after `setLoopCount(-1)`. Time unit 1 is `FMOD_TIMEUNIT_MS`, so the loop runs from
  5.487 s to 117.308 s. [V]

## 8. Corrections to other docs

- `projectzanarkand/MAP.md` section 4.16 (`data` row) and the archive table call metamenu.vbf "the
  Flash UI assets". **Wrong.** It is the launcher's PNG, XML, OGG, VAG and DAT set. The Flash UI is
  `FFX_Data.vbf` `ffx_data/gamedata/ps3data/flash/`. Not edited here, MAP.md is integrated by the
  caller.
- `projectzanarkand/PROGRESS.md` item 16.7 "The HD prompts and remaster UI from `metamenu.vbf`"
  points at the wrong archive for the same reason. Not edited here.
- `VBF_FORMAT.md` said everything the game loads lives in one of the three archives. metamenu.vbf
  is the launcher's, which is now said there.
- `ASSET_PATHS.md` 1.1 was right that FFX.exe never opens metamenu.vbf. It now also says that the
  `/MetaMenu/` paths in FFX.exe go to `FFX_Data.vbf`'s own `metamenu/` tree.
- `ASYNC_LOAD_REQUESTS.md` called `0x6D98E0` "the MetaMenu timestamp reader", which is right, and
  now says which archive its paths resolve into.
- `MOVIE_DECODE.md` says 98 `.webm` entries in `FFX_Data.vbf` under `video/` and `metamenu/`. That
  is correct (95 plus the 3 `sidestory.webm`), no change.

## 9. Proposed names

`../tools/apply_metamenu_names.py`, rehearsed with `--apply --comments` on the IDB copy, 5 renamed,
0 skipped, 0 failed. All five addresses had no name or a placeholder.

| VA | kind | name |
| --- | --- | --- |
| 0x6D98E0 | code, no function | `FFX_Fmv_LoadMetaMenuTimestamps_ORPHAN` |
| 0x6DA550 | code, no function | `FFX_Fmv_LoadCreditsText_ORPHAN` |
| 0x42F6D0 | code, no function | `FFX_App_GetLauncherPath_ORPHAN` |
| 0xC8FF38 | data | `g_ffxLauncherPathBuf` |
| 0xC8FAF0 | data | `maybe_g_ffxLauncherPathOverride` |

The launcher methods keep their own .NET names, which are real and unobfuscated.

## 10. Known unknowns

- `credit.dat` is parsed by `StaffBinParser` and was not decoded. It only matters for a native
  rebuild of the credits window.
- The launcher was read statically, not run. The load order in section 3 is the IL order.
- The -Will- player's live copy of the timestamp reader was not traced inside `FFX&X-2_Will.exe`,
  only matched by its strings.
- `0xC8FAF0` has no direct write. An indirect write through a computed pointer was not ruled out,
  hence the `maybe_`.

## 11. The full listing

Generated by `python -I tools/metamenu.py --md`. Sizes are uncompressed bytes. "when" names the
language code and the size condition for the title screen rows, see sections 2.2 and 3.

| # | path | type | detail | bytes | loaded by | when |
|---|---|---|---|---|---|---|
| 1 | `metamenu/gamedata/ps3data/menucreditstext/credit.dat` | DAT | launcher staff table | 4147 | CreditForm::.ctor (StaffBinParser::Load) | Credits 2014 button |
| 2 | `metamenu/ps3data/menucredit/button_down.png` | PNG | 32x32 | 3694 | CreditForm::.ctor | Credits 2014 button |
| 3 | `metamenu/ps3data/menucredit/button_over.png` | PNG | 32x32 | 3781 | CreditForm::.ctor | Credits 2014 button |
| 4 | `metamenu/ps3data/menucredit/button_up.png` | PNG | 32x32 | 3470 | CreditForm::.ctor | Credits 2014 button |
| 5 | `metamenu/ps3data/menucredit/ch/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language ch |
| 6 | `metamenu/ps3data/menucredit/credits_1280.png` | PNG | 1100x560 | 909523 | CreditForm::.ctor | Credits 2014 button |
| 7 | `metamenu/ps3data/menucredit/de/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language de |
| 8 | `metamenu/ps3data/menucredit/fr/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language fr |
| 9 | `metamenu/ps3data/menucredit/it/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language it |
| 10 | `metamenu/ps3data/menucredit/jp/credits_logo.png` | PNG | 355x169 | 82897 | CreditForm::.ctor | Credits 2014 button, language jp |
| 11 | `metamenu/ps3data/menucredit/kr/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language kr |
| 12 | `metamenu/ps3data/menucredit/scollbar.png` | PNG | 6x12 | 188 | CreditForm::.ctor | Credits 2014 button |
| 13 | `metamenu/ps3data/menucredit/sp/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language es |
| 14 | `metamenu/ps3data/menucredit/us/credits_logo.png` | PNG | 355x169 | 81897 | CreditForm::.ctor | Credits 2014 button, language en |
| 15 | `metamenu/ps3data/menumetamenu/ch/360p/btn_credits.png` | PNG | 167x30 | 5584 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 16 | `metamenu/ps3data/menumetamenu/ch/360p/btn_credits_2014.png` | PNG | 204x14 | 5416 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 17 | `metamenu/ps3data/menumetamenu/ch/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 18 | `metamenu/ps3data/menumetamenu/ch/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 19 | `metamenu/ps3data/menumetamenu/ch/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 20 | `metamenu/ps3data/menumetamenu/ch/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 21 | `metamenu/ps3data/menumetamenu/ch/360p/btn_setting.png` | PNG | 204x14 | 2573 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 22 | `metamenu/ps3data/menumetamenu/ch/360p/titlemenu.png` | PNG | 640x360 | 383324 | MainForm::InitializeActiveRect | start, language ch, work area height < 720 |
| 23 | `metamenu/ps3data/menumetamenu/ch/btn_credits.png` | PNG | 334x60 | 17742 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 24 | `metamenu/ps3data/menumetamenu/ch/btn_credits_2014.png` | PNG | 371x27 | 11075 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 25 | `metamenu/ps3data/menumetamenu/ch/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 26 | `metamenu/ps3data/menumetamenu/ch/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 27 | `metamenu/ps3data/menumetamenu/ch/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 28 | `metamenu/ps3data/menumetamenu/ch/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 29 | `metamenu/ps3data/menumetamenu/ch/btn_setting.png` | PNG | 371x27 | 6725 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 30 | `metamenu/ps3data/menumetamenu/ch/titlemenu.png` | PNG | 1280x720 | 1382824 | MainForm::InitializeActiveRect | start, language ch, work area height >= 720 |
| 31 | `metamenu/ps3data/menumetamenu/de/360p/btn_credits.png` | PNG | 167x30 | 2896 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 32 | `metamenu/ps3data/menumetamenu/de/360p/btn_credits_2014.png` | PNG | 204x14 | 7870 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 33 | `metamenu/ps3data/menumetamenu/de/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 34 | `metamenu/ps3data/menumetamenu/de/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 35 | `metamenu/ps3data/menumetamenu/de/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 36 | `metamenu/ps3data/menumetamenu/de/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 37 | `metamenu/ps3data/menumetamenu/de/360p/btn_setting.png` | PNG | 204x14 | 3711 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 38 | `metamenu/ps3data/menumetamenu/de/360p/titlemenu.png` | PNG | 640x360 | 383779 | MainForm::InitializeActiveRect | start, language de, work area height < 720 |
| 39 | `metamenu/ps3data/menumetamenu/de/btn_credits.png` | PNG | 334x60 | 18492 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 40 | `metamenu/ps3data/menumetamenu/de/btn_credits_2014.png` | PNG | 371x27 | 12289 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 41 | `metamenu/ps3data/menumetamenu/de/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 42 | `metamenu/ps3data/menumetamenu/de/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 43 | `metamenu/ps3data/menumetamenu/de/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 44 | `metamenu/ps3data/menumetamenu/de/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 45 | `metamenu/ps3data/menumetamenu/de/btn_setting.png` | PNG | 371x27 | 9444 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 46 | `metamenu/ps3data/menumetamenu/de/titlemenu.png` | PNG | 1280x720 | 1426871 | MainForm::InitializeActiveRect | start, language de, work area height >= 720 |
| 47 | `metamenu/ps3data/menumetamenu/fr/360p/btn_credits.png` | PNG | 167x30 | 5584 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 48 | `metamenu/ps3data/menumetamenu/fr/360p/btn_credits_2014.png` | PNG | 204x14 | 5984 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 49 | `metamenu/ps3data/menumetamenu/fr/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 50 | `metamenu/ps3data/menumetamenu/fr/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 51 | `metamenu/ps3data/menumetamenu/fr/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 52 | `metamenu/ps3data/menumetamenu/fr/360p/btn_ffx_nagi.png` | PNG | 167x30 | 2906 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 53 | `metamenu/ps3data/menumetamenu/fr/360p/btn_setting.png` | PNG | 204x14 | 6745 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 54 | `metamenu/ps3data/menumetamenu/fr/360p/titlemenu.png` | PNG | 640x360 | 384377 | MainForm::InitializeActiveRect | start, language fr, work area height < 720 |
| 55 | `metamenu/ps3data/menumetamenu/fr/btn_credits.png` | PNG | 334x60 | 25927 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 56 | `metamenu/ps3data/menumetamenu/fr/btn_credits_2014.png` | PNG | 371x27 | 14075 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 57 | `metamenu/ps3data/menumetamenu/fr/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 58 | `metamenu/ps3data/menumetamenu/fr/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 59 | `metamenu/ps3data/menumetamenu/fr/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 60 | `metamenu/ps3data/menumetamenu/fr/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 61 | `metamenu/ps3data/menumetamenu/fr/btn_setting.png` | PNG | 371x27 | 10447 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 62 | `metamenu/ps3data/menumetamenu/fr/titlemenu.png` | PNG | 1280x720 | 1428953 | MainForm::InitializeActiveRect | start, language fr, work area height >= 720 |
| 63 | `metamenu/ps3data/menumetamenu/it/360p/btn_credits.png` | PNG | 167x30 | 2996 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 64 | `metamenu/ps3data/menumetamenu/it/360p/btn_credits_2014.png` | PNG | 204x14 | 6051 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 65 | `metamenu/ps3data/menumetamenu/it/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 66 | `metamenu/ps3data/menumetamenu/it/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 67 | `metamenu/ps3data/menumetamenu/it/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 68 | `metamenu/ps3data/menumetamenu/it/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 69 | `metamenu/ps3data/menumetamenu/it/360p/btn_setting.png` | PNG | 204x14 | 7060 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 70 | `metamenu/ps3data/menumetamenu/it/360p/titlemenu.png` | PNG | 640x360 | 385810 | MainForm::InitializeActiveRect | start, language it, work area height < 720 |
| 71 | `metamenu/ps3data/menumetamenu/it/btn_credits.png` | PNG | 334x60 | 20284 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 72 | `metamenu/ps3data/menumetamenu/it/btn_credits_2014.png` | PNG | 371x27 | 14773 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 73 | `metamenu/ps3data/menumetamenu/it/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 74 | `metamenu/ps3data/menumetamenu/it/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 75 | `metamenu/ps3data/menumetamenu/it/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 76 | `metamenu/ps3data/menumetamenu/it/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 77 | `metamenu/ps3data/menumetamenu/it/btn_setting.png` | PNG | 371x27 | 11077 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 78 | `metamenu/ps3data/menumetamenu/it/titlemenu.png` | PNG | 1280x720 | 1387138 | MainForm::InitializeActiveRect | start, language it, work area height >= 720 |
| 79 | `metamenu/ps3data/menumetamenu/jp/360p/btn_credits.png` | PNG | 167x30 | 5584 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 80 | `metamenu/ps3data/menumetamenu/jp/360p/btn_credits_2014.png` | PNG | 204x14 | 5416 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 81 | `metamenu/ps3data/menumetamenu/jp/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 82 | `metamenu/ps3data/menumetamenu/jp/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 83 | `metamenu/ps3data/menumetamenu/jp/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 84 | `metamenu/ps3data/menumetamenu/jp/360p/btn_ffx_nagi.png` | PNG | 167x30 | 6275 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 85 | `metamenu/ps3data/menumetamenu/jp/360p/btn_setting.png` | PNG | 204x14 | 3672 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 86 | `metamenu/ps3data/menumetamenu/jp/360p/titlemenu.png` | PNG | 640x360 | 384876 | MainForm::InitializeActiveRect | start, language jp, work area height < 720 |
| 87 | `metamenu/ps3data/menumetamenu/jp/btn_credits.png` | PNG | 334x60 | 17742 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 88 | `metamenu/ps3data/menumetamenu/jp/btn_credits_2014.png` | PNG | 371x27 | 11075 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 89 | `metamenu/ps3data/menumetamenu/jp/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 90 | `metamenu/ps3data/menumetamenu/jp/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 91 | `metamenu/ps3data/menumetamenu/jp/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 92 | `metamenu/ps3data/menumetamenu/jp/btn_ffx_nagi.png` | PNG | 334x60 | 20058 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 93 | `metamenu/ps3data/menumetamenu/jp/btn_setting.png` | PNG | 371x27 | 7590 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 94 | `metamenu/ps3data/menumetamenu/jp/titlemenu.png` | PNG | 1280x720 | 1219995 | MainForm::InitializeActiveRect | start, language jp, work area height >= 720 |
| 95 | `metamenu/ps3data/menumetamenu/kr/360p/btn_credits.png` | PNG | 167x30 | 5584 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 96 | `metamenu/ps3data/menumetamenu/kr/360p/btn_credits_2014.png` | PNG | 204x14 | 5416 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 97 | `metamenu/ps3data/menumetamenu/kr/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 98 | `metamenu/ps3data/menumetamenu/kr/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 99 | `metamenu/ps3data/menumetamenu/kr/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 100 | `metamenu/ps3data/menumetamenu/kr/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 101 | `metamenu/ps3data/menumetamenu/kr/360p/btn_setting.png` | PNG | 204x14 | 2626 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 102 | `metamenu/ps3data/menumetamenu/kr/360p/titlemenu.png` | PNG | 640x360 | 383326 | MainForm::InitializeActiveRect | start, language kr, work area height < 720 |
| 103 | `metamenu/ps3data/menumetamenu/kr/btn_credits.png` | PNG | 334x60 | 17742 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 104 | `metamenu/ps3data/menumetamenu/kr/btn_credits_2014.png` | PNG | 371x27 | 11075 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 105 | `metamenu/ps3data/menumetamenu/kr/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 106 | `metamenu/ps3data/menumetamenu/kr/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 107 | `metamenu/ps3data/menumetamenu/kr/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 108 | `metamenu/ps3data/menumetamenu/kr/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 109 | `metamenu/ps3data/menumetamenu/kr/btn_setting.png` | PNG | 371x27 | 6779 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 110 | `metamenu/ps3data/menumetamenu/kr/titlemenu.png` | PNG | 1280x720 | 1382699 | MainForm::InitializeActiveRect | start, language kr, work area height >= 720 |
| 111 | `metamenu/ps3data/menumetamenu/menuactiverects.xml` | XML | UTF-8 with BOM | 3096 | MainForm::InitializeActiveRect | start, and on Settings close when the language changed |
| 112 | `metamenu/ps3data/menumetamenu/metamenu.ogg` | OGG | Ogg Vorbis | 5140305 | MainForm::InitializeFmodResources | start, looping BGM |
| 113 | `metamenu/ps3data/menumetamenu/metamenu_sfx_comfirme.vag` | VAG | PS ADPCM | 26016 | MainForm::InitializeFmodResources | start, confirm SE |
| 114 | `metamenu/ps3data/menumetamenu/metamenu_sfx_move.vag` | VAG | PS ADPCM | 9680 | MainForm::InitializeFmodResources | start, cursor move SE |
| 115 | `metamenu/ps3data/menumetamenu/setting_logo.png` | PNG | 640x63 | 21432 | never | no path in the launcher reaches it |
| 116 | `metamenu/ps3data/menumetamenu/settingsloc.xml` | XML | UTF-8 with BOM | 7918 | MainForm::MainForm_Load, ConfigForm::ConfigForm_Load, MainForm::OnConfigClicked | start (window title), Settings open (labels), Settings close when the language changed |
| 117 | `metamenu/ps3data/menumetamenu/sp/360p/btn_credits.png` | PNG | 167x30 | 2944 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 118 | `metamenu/ps3data/menumetamenu/sp/360p/btn_credits_2014.png` | PNG | 204x14 | 5984 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 119 | `metamenu/ps3data/menumetamenu/sp/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 120 | `metamenu/ps3data/menumetamenu/sp/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 121 | `metamenu/ps3data/menumetamenu/sp/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 122 | `metamenu/ps3data/menumetamenu/sp/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 123 | `metamenu/ps3data/menumetamenu/sp/360p/btn_setting.png` | PNG | 204x14 | 4702 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 124 | `metamenu/ps3data/menumetamenu/sp/360p/titlemenu.png` | PNG | 640x360 | 385137 | MainForm::InitializeActiveRect | start, language es, work area height < 720 |
| 125 | `metamenu/ps3data/menumetamenu/sp/btn_credits.png` | PNG | 334x60 | 19812 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 126 | `metamenu/ps3data/menumetamenu/sp/btn_credits_2014.png` | PNG | 371x27 | 13155 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 127 | `metamenu/ps3data/menumetamenu/sp/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 128 | `metamenu/ps3data/menumetamenu/sp/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 129 | `metamenu/ps3data/menumetamenu/sp/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 130 | `metamenu/ps3data/menumetamenu/sp/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 131 | `metamenu/ps3data/menumetamenu/sp/btn_setting.png` | PNG | 371x27 | 11846 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 132 | `metamenu/ps3data/menumetamenu/sp/titlemenu.png` | PNG | 1280x720 | 1386653 | MainForm::InitializeActiveRect | start, language es, work area height >= 720 |
| 133 | `metamenu/ps3data/menumetamenu/speaker_off.png` | PNG | 32x32 | 542 | ConfigForm::LoadConfiguration, ConfigForm::pictureBox2_Click | Settings open when muted, mute toggled |
| 134 | `metamenu/ps3data/menumetamenu/speaker_on.png` | PNG | 32x32 | 327 | ConfigForm::LoadConfiguration, ConfigForm::pictureBox2_Click | Settings open when not muted, mute toggled |
| 135 | `metamenu/ps3data/menumetamenu/us/360p/btn_credits.png` | PNG | 167x30 | 5584 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 136 | `metamenu/ps3data/menumetamenu/us/360p/btn_credits_2014.png` | PNG | 204x14 | 5416 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 137 | `metamenu/ps3data/menumetamenu/us/360p/btn_ffx.png` | PNG | 167x30 | 4624 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 138 | `metamenu/ps3data/menumetamenu/us/360p/btn_ffx_2.png` | PNG | 167x30 | 4939 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 139 | `metamenu/ps3data/menumetamenu/us/360p/btn_ffx_2_lm.png` | PNG | 167x30 | 6759 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 140 | `metamenu/ps3data/menumetamenu/us/360p/btn_ffx_nagi.png` | PNG | 167x30 | 5781 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 141 | `metamenu/ps3data/menumetamenu/us/360p/btn_setting.png` | PNG | 204x14 | 3672 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 142 | `metamenu/ps3data/menumetamenu/us/360p/titlemenu.png` | PNG | 640x360 | 383884 | MainForm::InitializeActiveRect | start, language en, work area height < 720 |
| 143 | `metamenu/ps3data/menumetamenu/us/btn_credits.png` | PNG | 334x60 | 17742 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 144 | `metamenu/ps3data/menumetamenu/us/btn_credits_2014.png` | PNG | 371x27 | 11075 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 145 | `metamenu/ps3data/menumetamenu/us/btn_ffx.png` | PNG | 334x60 | 14631 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 146 | `metamenu/ps3data/menumetamenu/us/btn_ffx_2.png` | PNG | 334x60 | 15202 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 147 | `metamenu/ps3data/menumetamenu/us/btn_ffx_2_lm.png` | PNG | 334x60 | 21431 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 148 | `metamenu/ps3data/menumetamenu/us/btn_ffx_nagi.png` | PNG | 334x60 | 19522 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 149 | `metamenu/ps3data/menumetamenu/us/btn_setting.png` | PNG | 371x27 | 7590 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
| 150 | `metamenu/ps3data/menumetamenu/us/titlemenu.png` | PNG | 1280x720 | 1219281 | MainForm::InitializeActiveRect | start, language en, work area height >= 720 |
