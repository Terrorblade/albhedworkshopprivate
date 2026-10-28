# Command line census: every argv token and dash flag, with its reader

Written 2026-10-27 for Project Zanarkand item 1.2.7. Everything here was read from the FFX.exe
IDB (VA = RVA + 0x400000) through idalib on a private copy, decompiles cross-checked against the
disassembly where a claim rests on one instruction. Names in this doc that were placeholders
before are proposed by `../tools/apply_command_line_names.py` (52 rows, rehearsed on a copy,
nothing skipped).

Companion docs: `BOOT_SEQUENCE.md` (where each reader sits in the boot), `DEBUG_MODE.md` (what the
`debug` token turns on), `MAIN_LOOP.md` section 4 (where the fixed timestep goes),
`INPUT_LAYER.md` (the mapper slots the input log touches), `RENDER_DEVICE.md` section 4 (vsync).

## 0. The answer in one paragraph

The exe reads its command line exactly once, in `WinMain`, with `GetCommandLineW` and
`CommandLineToArgvW`, and builds its own narrow argv. Two parsers read that argv. Phyre's
`Phyre__PApplication__parseCommandLine 0x62ADC0` knows 13 dash flags: `-h`, `-help`, `-?`,
`-script=`, `-log=`, `-input=`, `-timestep=`, `-resolution=`, `-msaa=`, `-vsync=`, `-host=`,
`-fullscreen=`, `-borderless=`. The game itself only ever looks at `argv[1]`, for `debug` or `DEBUG`
(debug mode) and `_ECalm` (the Eternal Calm side story). Every one of them is live in the retail
exe, none is behind the debug gate. Six change what the simulation does: `debug`, `_ECalm`,
`-timestep=`, `-script=`, `-input=` and `-log=` (the last one because it forces a fixed timestep).
The CRT's own argc and argv are built and never read. No registry key is read anywhere, and the
few environment variables that are read are all either dead code, the crash handler, or libvpx's
CPU dispatch.

## 1. How the command line is read, in call order

| step | where | what |
| --- | --- | --- |
| 1 | `_pre_cpp_init 0x94936F` | `__getmainargs(&g_crtArgc 0x193E9D0, &g_crtArgv 0x193E9D4, &g_crtEnvp 0x193E9D8, ...)` at 0x94939F. The only reference to each of the three globals is the `push offset` that hands it to `__getmainargs`. Nothing reads them |
| 2 | `start 0x9493C1` | reads `_acmdln` at 0x9494B2, skips the program name and the spaces after it, and passes the rest as `lpCmdLine` to `WinMain` at 0x9494E7. `WinMain` never reads `lpCmdLine` |
| 3 | `WinMain 0x62EE50` | after the ini load and the single instance mutex: `GetCommandLineW` at 0x62EF84, `CommandLineToArgvW` at 0x62EF8B, then each element through `WideCharToMultiByte(CP_ACP)` into a fresh `FFX_Malloc` string, then `LocalFree` of the wide array. This narrow argv is the only argv anything in the game reads |
| 4 | `WinMain` | copies the ini values into the display block at `app+0x26C` (width +0x26C, height +0x270, +0x284 = `settings+56`, +0x288 = `settings[40] != 0`, +0x298 = `settings[8] == 1`) and `g_phyreBorderlessWindow`, then `if (argc > 1)` (`cmp eax, 1` at 0x62F155) calls `Phyre__PApplication__parseCommandLine(&help, argc, argv, app+0x26C)` at 0x62F163. So every dash flag overrides the ini for this run only, nothing is written back |
| 5 | `WinMain` | `help != 0` prints the usage text and `-? -help`, frees argv and returns 0, before Steam is touched. Otherwise `FFX_SteamApiInit`, then `Phyre__PApplication__run(argv, argc)` |
| 6 | `run 0x629A40` -> `createWindowAndInit 0x629AB0` -> `onInit 0x62A600` | argv and argc are only forwarded. `onInit` consumes the four globals the parser filled (`-timestep=`, `-script=`, `-log=`, `-input=`), section 3 |
| 7 | `FFXApplication__initApplication 0x42F7F0` | stores argv at `app+0x3A4` and argc at `app+0x3A8` (0x42F7FE, 0x42F809) and reads `argv[1] == "_ECalm"` for the window title (string ref at 0x42F826) |
| 8 | `FFX_BootSequence 0x6416C0` | reads `app+0x3A4/0x3A8` for `_ECalm` (0x64177C) and posts `FFX_ParseCommandLineAndInitDebug_tramp 0x636410` with argc and argv to a `JobWorker` (0x641838 and 0x641942) |
| 9 | `FFX_ParseCommandLineAndInitDebug 0x822490` | `debug` / `DEBUG` on `argv[1]` (0x8224A5, 0x8224DA), stores `g_ffxDebugMode` at 0x822509 / 0x822512, then `FFX_InitSubsystems1(argc, argv)` and `FFX_MainInit(argc, argv)` |
| 10 | `FFX_InitSubsystems1 0x820310` | in debug mode with `argc > 1`, `FFX_Dbg_SetUserName 0x821D30(argv[1])` |
| 11 | `FFX_MainInit 0x820860` | not debug and `argv[1] == "_ECalm"` (0x820A43): first event `g_ffxFirstBootSceneEventId` (393) instead of 23 |

Completeness. `GetCommandLineW` and `CommandLineToArgvW` each have one call site. There is no
`GetCommandLineA`, no `__p___argc`, `__p___argv`, `__argc`, `__argv` or `__wargv` import, and no
`CommandLineToArgvA`. The narrow argv only travels through the calls above, and a scan of every
instruction for displacement 0x3A4 or 0x3A8 finds the app fields read in exactly three functions
(`initApplication`, `FFX_BootSequence`, and through the job, the two `FFX_Init` functions). The
other hits are unrelated structs (`PInputDevice`, `g_ffxGameSettings` in `FFX_Input__buildActionMap`,
battle units, OEF2 ops). A scan of every narrow and UTF-16 string in the image for `-name`, `/name`
and `name=` shapes with a code reference finds only the parser's strings, plus `-D...` shader
defines and StackWalker's `-nosymbols-` style labels, which are not flags. The only `{name, value}`
pointer table on the path is `g_phyreResolutionNames 0xB3FB50`.

## 2. The flag table

Parsing rules for the dash flags, all from `Phyre__PApplication__parseCommandLine 0x62ADC0`:

- It walks every element including `argv[0]`. An element counts only when its first character is
  `-` or `/`, so `/timestep=0.0166` works as well as `-timestep=0.0166`.
- The tests run in a fixed order and the first match wins: `h`, `help`, `?` (exact `strcmp`), then
  `script=`, `log=`, `input=` (`strncmp`), then `timestep=`, `resolution=`, `msaa=`, `vsync=`,
  `host=`, `fullscreen=`, `borderless=` (`Phyre_StrPrefixValue 0x623DA0`, also `strncmp`). Every name
  is case sensitive. Only the `-resolution=` value is matched case-insensitively.
- An unknown flag is ignored silently. A repeated flag keeps the last value.
- A flag without `=` matches nothing. The usage string says `-vsync<vsync>`, but a bare `-vsync`
  does nothing, only `-vsync=N` is parsed.
- The parser only runs when `argc > 1`.

The argv[1] tokens are plain `strcmp` against `argv[1]` only, case sensitive and exact. So `debug`
and `_ECalm` cannot both be given, and either one must come first, before any dash flag. `-debug`
is not debug mode. `FFX.exe debug -timestep=0.01666` is debug mode with a fixed timestep.

| flag | syntax | reader (VA) | sets | effect | retail live | simulation visible |
| --- | --- | --- | --- | --- | --- | --- |
| `debug`, `DEBUG` | exactly `argv[1]` | `FFX_ParseCommandLineAndInitDebug 0x822490` | `g_ffxDebugMode 0x133C910` (store 0x822509, else 0x822512), and through `FFX_InitSubsystems1` `g_ffxDbgUserName 0xC494A8` = a copy of `argv[1]` | everything in `DEBUG_MODE.md`: `FFX_LoadEventIdTable` in `FFX_MainInit`, the first event is 0 with no title request, the SG debug GUI and HUD pages. The user name only feeds PS2 dev host paths (`host0:/home/%s/battle/...`, `/ffx/proj/proj/setting/%s.txt`, `$USER$` in `FFX_Asset_ResolvePathWithDebugOverrides`) and can only ever be `debug` or `DEBUG` | yes, no gate | yes: start event 0 instead of 23, and every debug feature that steps |
| `_ECalm` | exactly `argv[1]` | `FFXApplication__initApplication 0x42F7F0` (title), `FFX_BootSequence 0x6416C0` (0x64177C), `FFX_MainInit 0x820860` (0x820A43) | nothing stored, `argv[1]` is re-read each time | title "FINAL FANTASY X ETERNAL CALM", no `OpeningScreen.swf` (30 blank frames instead), first event 393 (scene1.ebp, the first of the 393..399 boot scenes) instead of 23. Ignored for the start event in debug mode, which `FFX_MainInit` tests first | yes. The launcher `FFX&X-2_LAUNCHER.exe` carries `<SideStoryParam>_ECalm</SideStoryParam>` | yes: a different first event |
| `-h`, `-help`, `-?` | bare | `parseCommandLine` | the `help` byte on `WinMain`'s stack | usage text, frees argv, `WinMain` returns 0 | yes | no, the process exits |
| `-script=PATH` | rest of the element | `parseCommandLine` (0x62AEB9) | `g_phyreScriptPathArg 0xCC9CFC` | `onInit` reads the file (`Phyre__PApplicationScriptBuffer__ctorFromFile 0x61FC20`). If it loaded, the frame callback `app+0x268` becomes `Phyre__PApplication__scriptFrameTick 0x62C6F0` instead of `frameTick`, and the script drives every frame. Section 3 | yes | yes, it is the input and the frame driver |
| `-log=PATH` | rest of the element | `parseCommandLine` (0x62AEDF) | `g_phyreInputLogPathArg 0xCC9D08` | `onInit` opens a writer into `app+0x29C`. If `app+0x3C` is 0 it is set to 0.016659999 ("INFO: Set fixed timestep to %f in logging mode", store at 0x62A722). The first line written is `0:Timestep:%f`. Every `frameTick` then calls `Phyre__PApplication__recordInputLog 0x629E40` before the device latch. `onExit` closes it | yes | yes, through the forced fixed timestep. The recording itself is not an input |
| `-input=PATH` | rest of the element | `parseCommandLine` (0x62AF05) | `g_phyreInputReplayPathArg 0xCC9D0C` | only when neither `-log=` nor `-script=` is set (0x62A6F6, 0x62A797). `onInit` reads the last frame number N of the log (`Phyre__InputLog_GetLastFrameNumber 0x6285B0`) and runs the synthesized script `InputSource(PATH)\nFrames(N+1)\n` | yes | yes, it replays input and the logged timestep |
| `-timestep=SECONDS` | `atof` | `parseCommandLine` (`fstp` at 0x62AF28) | `g_phyreFixedTimeStepArg 0xCC9D10`, a float, default 0 | `onInit` copies it into `app+0x3C` (`fld` at 0x62A665). When `app+0x3C > 0` `frameTick` uses it as the frame delta instead of the clock (`MAIN_LOOP.md` section 4). The `WinMain` sleep limiter still runs on the wall clock | yes | yes, this is the pacing lever |
| `-resolution=NAME` | `480i 480p 576i 576p 720i 720p 1080i 1080p`, any case | `parseCommandLine` through `Phyre_LookupNamedValue 0x6255D0` and `g_phyreResolutionNames 0xB3FB50` | `app+0x26C` width, `app+0x270` height, `app+0x28C` resolution index: 720x480 (0), 720x576 (1), 1280x720 (2), 1920x1080 (3). An unknown name prints "The window output resolution is unsupported" and changes nothing | the window size and every render target sized from the app (`FFX_Frame_RenderScene`, `FFX_Frame_DrawPausedSnapshot`) | yes | no reader on the step side was found |
| `-msaa=N` | `atoi` | `parseCommandLine` | `app+0x284` | read by the render target builder `sub_693D70` (0x693DEC), which fails with 19 unless N is 0, 2, 4, 8 or 16. The swap chain itself is always single sampled, see section 5 | yes | no |
| `-vsync=N` | `atoi` | `parseCommandLine` | `app+0x288 = (N != 1)` | the byte goes into `g_phyreWindowRecord+9` and from there into the renderer's vsync byte at device creation (0x595AE6), so it sets the boot `Present` interval. `-vsync=0` means on, `-vsync=1` means off, any other N means on although the log line says OFF. The settings menu later overrides it through `app+0x18`. The byte is also mirrored, inverted, into `byte_CC9CF4` every frame, which only an uncalled getter reads | yes | no for the step. Without a fixed timestep it changes the wall clock pace and therefore dt |
| `-host=HWND` | `atoi` | `parseCommandLine` (0x62B0D3) | `g_phyreHostWindowArg 0xCC9CE0` | only while `g_phyreHostedWindowMode 0xCC9CD2` is set: `SetParent` the game window into that HWND and send it `PHYREMSG_SENDHWND`. A tool embedding hook | yes, inert unless hosted | no |
| `-fullscreen=N` | `atoi` | `parseCommandLine` | `app+0x298 = (N == 1)` | `Phyre__PWindowWin32Base__create` switches the desktop mode with `CDS_FULLSCREEN` and a popup style. Not an exclusive swap chain | yes | no |
| `-borderless=N` | `atoi` | `parseCommandLine` | `g_phyreBorderlessWindow 0xCC9CD1 = (N != 0)` | borderless window style | yes | no |

Two pieces of dead code sit next to the parser:

- `Phyre__PApplication__parseOneArgument_ORPHAN 0x62B150` is a single argument copy of the same
  parser (thiscall, `retn 8`). It has no function, no code reference and no data reference. It is why
  every flag string has two cross references.
- `Phyre_GetFixedTimeStepArg 0x623D10`, `Phyre_GetInputReplayPathArg 0x623D40`,
  `Phyre_GetInputLogPathArg 0x623D90` and `Phyre_GetScriptPathArg 0x623E90` return the four globals
  and have no caller.

The debug user name, for completeness. `g_ffxDbgUserName 0xC494A8` defaults to the empty string
0xB8FBEB. `FFX_Dbg_GetAssetPathUserName 0x820620` returns "sugimoto" when `g_ffxDbgUseSugimotoUser
0x12FB8F8` is set, but `FFX_InitSubsystems1` clears that byte at 0x820332 and nothing sets it.
`sub_7FF960`, called from `FFX_Rand_SeedCosmeticStreams`, sets `byte_232278D` when the name starts
with "yonishi", which it never can. On the PS2 `argv[1]` was presumably the developer's name, on
PC the same slot has to hold the debug token.

## 3. The script and input log harness behind -script=, -log= and -input=

The 16 commands, registered by `Phyre__InputLog_RegisterCommands 0x62BB60` into 72 byte rows
`{fn, name[64]}` and looked up with `_stricmp`:

| command | handler | does |
| --- | --- | --- |
| `Frames(n)` | `Phyre__InputLog_CmdFrames 0x623660` | runs n frames. Each one applies the loaded log rows whose frame number has come up, then calls `frameTick` |
| `Timestep(f)` | `Phyre__InputLog_CmdTimestep 0x625720` | `app+0x3C = f`. `f <= 0` goes back to the clock |
| `Framerate(label)` | `Phyre__InputLog_CmdFramerate 0x6235D0` | prints the measured frame rate |
| `MinFramerate()` | `Phyre__InputLog_CmdMinFramerate 0x624900` | nothing |
| `RenderFrame(file)` | `Phyre__InputLog_CmdRenderFrame 0x624D70` | render and screenshot |
| `Keypress(c)`, `KeyDown(c)`, `KeyUp(c)` | `0x624890`, `0x624810`, `0x624850` | keyboard slot 0 key from `byte_C30E08[c & 0x7F]`. Keypress is down, one app update (vtbl+28), up |
| `Buttonpress(b)`, `ButtonDown(b)`, `ButtonUp(b)` | `0x623060`, `0x622D40`, `0x622ED0` | pad slot 0 (`mapper+0x38`) button state. Names: Circle Cross Triangle Square Start Select Up Down Left Right. No shoulder buttons, no sticks, and nothing at all when no pad is bound in slot 0 |
| `Mousepress(x,y)` | `0x624910` | mouse button and position for one app update |
| `InputSource(file)` | `Phyre__InputLog_CmdInputSource 0x624410` | loads a log through `Phyre__InputLog_LoadInputSource 0x62B5F0` into `g_phyreInputLogEntries 0xCC9D18` (count `0xCC9D14`, cursor `0xCC9CF8`) |
| `Expect()` | `0x623460` | nothing in this build |
| `Echo(text)` | `0x623310` | prints text |
| `Quit()` | `0x624D30` | `app+0x39 = 1` |

A script is a sequence of `Name(args)` commands (`Phyre__PApplicationScript__parseCommand
0x6276E0`). `Phyre__PApplication__scriptFrameTick 0x62C6F0` runs one command per pump iteration
through `Phyre__PApplicationScript__step 0x627480`.

A log is one record per line, `#` starts a comment, and every record begins with the frame number
`F`, which is `app+0x34`, the `frameTick` count:

```
F:KB:device:key:value          keyboard state change, device = keyboard slot 0..6, key 0..255
F:IK:action:+:value            a mapped keyboard action went down (- for up)
F:Name:args                    any other identifier that names a command, run at frame F
0:Timestep:0.016660            what -log= writes first
```

The recorder `Phyre__PApplication__recordInputLog 0x629E40` only walks the seven keyboard slots at
`mapper+0x80` (the loop runs `ecx` from 0x80 at 0x629E61 to 0x9C at 0x62A06F), semantics 28 to 283,
comparing the device's current value against its latched copy at `device+280`. The 18 pad slots
at `mapper+0x38` and every analog axis are never looked at. On replay, `Phyre__InputLog_ApplyEntry
0x626070` writes a KB row into the keyboard's current state cache at `device+24+key`, which
`Phyre__PInputMapper__latchDeviceStates 0x628320` only overwrites when the physical key changes, so
a replayed key holds.

## 4. What this means for the oracle harness

The recreation is checked step by step against the original, so the original has to be run with a
known input and a known delta. What the command line offers:

- **The flags that change the simulation**: `debug` (start event 0 and the debug systems),
  `_ECalm` (start event 393), `-timestep=` (the delta), `-script=` and `-input=` (the input and the
  frame driver), `-log=` (forces a fixed timestep). Nothing else is simulation visible once the
  delta is fixed. With a fixed timestep, `-vsync=` and the window flags only change how fast wall
  clock time passes, which the step no longer reads.
- **Nothing on the command line seeds the RNG, starts a battle or picks an arbitrary map.** The RNG
  is seeded from the wall clock in `FFX_Btl_Init` during `FFX_MainInit` (`BOOT_SEQUENCE.md`), and
  the start event is 23, 0 under `debug` or 393 under `_ECalm`. Those have to be set through the
  kit, not argv.
- **Use `-timestep=`, always.** It is the only way to take the clock out of the frame delta.
- **The shipped recorder cannot record a controller.** It records keyboards only, proved from the
  recorder's slot loop above. A `-log=` run played on a pad replays nothing of the pad, and analog
  movement is lost even from the keyboard side because the sticks are never recorded. The kit's
  own log, taken where `INPUT_LAYER.md` says the mask and the four axes are final, is the better
  oracle input. `-script=` can still drive the original with keyboard keys and the ten pad names.
- **The timestep in a log is written with `%f`, six decimals.** The log mode default 0.016659999
  survives the round trip (both are the float 0x3C887A8D), but a hand picked `-timestep=0.0166666`
  replays as 0.016667, a different float. Pick a value exact at six decimals, or let `-log=` pick it.
- **`-input=` is dropped when `-log=` or `-script=` is also given.** `-log=` and `-script=` together
  record a scripted run.
- **Log frame numbers are `frameTick` counts, not `g_ffxMainStepCounter`.** The boot frames that
  `FFX_BootSequence` presents itself are not counted.
- **Steam does not eat the arguments.** `FFX_SteamApiInit` ignores what
  `SteamAPI_RestartAppIfNecessary(0)` returns and the game carries on with the same argv.

## 5. Environment variables and the registry

No registry API is imported (ADVAPI32 brings only `GetUserNameA` and the `Crypt*` hash calls) and no
`Software\`, `HKEY_` or `RegOpenKey` string exists in the image, narrow or wide.

| variable | reader | reached |
| --- | --- | --- |
| `SCE_PHYRE` | `Phyre_ReadScePhyreEnv_ORPHAN 0x42FF80` | never, no caller |
| `CG_BIN_PATH`, then `PATH` (`_dupenv_s`, `_putenv_s`) | `Phyre_AddCgBinPathToPath_ORPHAN 0x430F80` -> `Phyre_AppendToPathEnv 0x431080` | never, no caller |
| any `%VAR%` in a path | `Phyre_ExpandEnvVarsInPath_ORPHAN 0x60FB00` -> `Phyre_GetEnvVar 0x42FD10` | never, no caller |
| `_NT_SYMBOL_PATH`, `_NT_ALTERNATE_SYMBOL_PATH`, `SYSTEMROOT`, `SYSTEMDRIVE`, `ProgramFiles`, plus `GetUserNameA` | StackWalker `sub_687E90` and `sub_687850`, loading dbghelp | only on the crash and stack dump path (its two callers are not inside defined functions, inferred) |
| `VPX_SIMD_CAPS`, `VPX_SIMD_CAPS_MASK` | libvpx CPU dispatch `0x40D190`, `sub_40D2C0`, `sub_41AFE0`, `sub_41B320` | when a movie decoder is created (`maybe_create_decompressor 0x40A680`). Picks SIMD code paths only, not simulation visible |
| Lua `os.getenv`, `LUA_PATH` / `LUA_CPATH` | `lua_os_getenv 0x95F460`, `loadlib_setpath 0x961B10` | Lua never runs a script (`LUA_MODULE.md`) |

The bundled DLLs (`fmodex`, `fmod_event`, `iggy_w32`, `steam_api`) were not looked at.

## 6. Corrections to other docs

- `RENDER_DEVICE.md` section 4 said the `-vsync=` argument writes `g_ffxGameSettings[40]`. It does
  not, it writes `app+0x288`. It also said the game starts unsynced until the first frame services
  the dirty flag. In fact `FFX_Phyre_CreateDeviceAndSwapChain` stores its seventh argument, the
  window record's byte +9 (`app+0x288`), into `renderer+0x131C` at 0x595AE6, so the boot interval is
  the ini's VSync setting or the `-vsync=` override. Hex-Rays drops that argument from the
  decompile, the disassembly shows it.
- `RENDER_DEVICE.md` sections 1 and 2 called `+4` of the window record the msaa count. It is the
  constant 1 that `createWindowAndInit` writes at 0x629ADF and 0x629B17, so the swap chain is always
  single sampled. The msaa setting reaches the render targets through `app+0x284` instead.
- `FFX_GAME_NOTES.md` listed `-script=`, `-input=` and `-timestep=` as the record and replay flags
  and left out `-log=`, which is the recorder. It also named `0x62C730`
  `Phyre__InputScript__buttonFromName`. That function sets a pad slot 0 button state by name, it is
  renamed `Phyre__InputScript__setPadButtonByName`.
- `BOOT_SEQUENCE.md` step 6 quoted the usage string as the flag list. The usage string names five
  flags, the parser knows thirteen. Step 6 now points here.

## 7. Open

- How fios resolves a relative `-script=` or `-log=` path. Both files are opened in `onInit`, before
  `FFX_InitFileSystem` sets the data root in `initApplication`. Use absolute paths until 1.2.6 has
  run one.
- Whether a `-log=` / `-input=` round trip replays to the same `g_ffxMainStepCounter` and state
  hash. That is item 1.2.6 and needs the game running. Async loads finish on wall clock time even
  with a fixed timestep, which is the first thing to watch.
- What `byte_CC9CF4` was for. It is written by `frameTick` and by the two input poll helpers and
  read only by a getter with no caller.
