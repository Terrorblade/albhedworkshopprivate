# FFX HD Remaster - the PC settings menu (escmenu)

Target: `G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe`
32-bit, preferred base 0x400000. All addresses below are VAs at the preferred base.

Status: in progress. Appended as findings are confirmed.

## Summary so far (read this first)

The HD-only PC settings screen is the "Esc menu". It is an Iggy (Flash) movie,
`/FFX_Data/GameData/PS3Data/flash/escmenu.swf`, but the entry list is NOT authored in
the movie. Native code builds each page at runtime by calling AS3 functions in the
movie, one call per row, with the row type, label and value range passed as arguments.
That is the good case for us: appending a row means making one extra call.

## 1. Finding the menu - the string evidence

Two string clusters in `.rdata` gave it away.

Flash assets (all under `/FFX_Data/GameData/PS3Data/flash/`):

| VA | string |
|----|--------|
| 0xB425A8 | `escmenu.swf`  <- the settings screen |
| 0xB42578 | `pausemenu.swf` |
| 0xB43C5C | `OpeningScreen.swf` |
| 0xB4BF98 | `speed_2.swf` |
| 0xB4BFC8 | `speed_4.swf` |
| 0xB4BFF8 | `freecamera.swf` |
| 0xB4C028 | `time_stop.swf` |
| 0xB4C058 | `invincible.swf` |
| 0xB4C088 | `enemy_0.swf` |
| 0xB4C0B8 | `enemy_1.swf` |
| 0xB4C0E8 | `autobattle.swf` |
| 0xB4C118 | `autobattle_disable.swf` |
| 0xB4C878 / 0xB4CA78 / 0xB4CC78 | `saveload_jp/ch/kr.swf` |

The `speed_2` / `speed_4` assets are the on-screen HUD icons for the HD fast forward
booster (2x and 4x), with `freecamera`, `time_stop`, `invincible`, `enemy_0/1` and
`autobattle` being the rest of the HD booster set. Those are the remaster's extra
features, so the fast forward anchor the task asks about lives in this family.

AS3 callback and function names, 0xB46BD4 - 0xB46F48. These are the native-to-Flash and
Flash-to-native names for the Esc menu:

Flash calls out to native (registered with `IggySetAS3ExternalFunctionCallbackUTF8`):
`EscMenuResume`, `EscMenuQuit`, `EscMenuOnConfirmKey`, `EscMenuVideoSettingRefresh`,
`EscMenuAudioSettingRefresh`, `EscMenuKeyboardSettingRefresh`,
`EscMenuControllerSettingRefresh`, `EscMenuStartKeyEditing`,
`EscMenuStartControllerEditing`, `EscMenuHelpTextRefresh`, `EscMenuButtonClick`,
`EscMenuPopUpConfirmed`, `EscMenuKeyboardSettingIncomplete`,
`EscMenuControllerSettingIncomplete`, `EscMenuOnDefaultKey`.

Native calls into Flash (via `IggyPlayerCallFunctionRS`):
`showEscMenu`, `onEscClose`, `prevItem`, `nextItem`, `leftItem`, `rightItem`,
`onCancelKey`, `confirmSelect`, `keyUp`, `setHelpText`, `showPopUpWithBtn`,
`onDefaultKey`, `playEscSound`, `SetLanguage`, `setHomePage`, `setSysSettingPage`,
`setAudioPage`, `setVideoPage`, `setKeyBindingPage`, `setKeyboardPage`,
`setControllerPage`, `setControlBar`, `setSubTitle`, `setMouseVisible`.

Second cluster, 0xB4C504 - 0xB4C878, the row builders and row widget types:

| VA | string | what it is |
|----|--------|-----------|
| 0xB4C504 | `spinnerButton` | row widget type |
| 0xB4C514 | `addChangeParaPageItem` | adds a row to the "change parameters" page |
| 0xB4C52C | `Function not Available now!` | placeholder popup text |
| 0xB4C548 | `showPopUpStd` | |
| 0xB4C6D8 | `onConfirmKey` | |
| 0xB4C6E8 | `%dx%d` | resolution label format |
| 0xB4C6F0 | `spinnerWithList` | row widget type |
| 0xB4C700 | `addVideoPageItem` | adds a row to the video page |
| 0xB4C714 | `spinner` | row widget type |
| 0xB4C71C | `spinnerWithBar` | row widget type |
| 0xB4C73C | `videoSettingMSAAHDAOPopUpMessageInit` | |
| 0xB4C764..0xB4C7AC | `onMasterVolumeChanged`, `onVoiceVolumeChanged`, `onMusicVolumeChanged`, `onSfxVolumeChanged` | per-slider change callbacks |
| 0xB4C7C0 | `ExitAudioMenu` | |
| 0xB4C7D0 | `addAudioPageItem` | adds a row to the audio page |
| 0xB4C7E8 | `keyboardBindingItem` | row widget type |
| 0xB4C7FC | `addKeyboardPageItem` | |
| 0xB4C810 | `setKeyBinding` | |
| 0xB4C820 | `controllerBindingItem` | row widget type |
| 0xB4C838 | `addControllerPageItem` | |
| 0xB4C850 | `$key$` | label substitution token |
| 0xB4C858 | `addSpecialFeaturePageItem` | adds a row to the HD "special feature" page |

So the menu has at least these pages: home, sys setting, audio, video, key binding
(keyboard + controller), special feature, change parameters. The last two are the
HD-only ones.

## 2. The config file keys

Third cluster, 0xB7C804 - 0xB7CABC. These read as literal key names in a text config
file, and the `[ConfigFile]` diagnostics confirm a line-based key/value text parser.

Keys: `Quality` (0xB7C804), `MasterVolume` (0xB7C814), `VoiceVolume` (0xB7C824),
`MusicVolume` (0xB7C830), `SFXVolume` (0xB7C83C), `Brightness` (0xB7C848),
`VSync` (0xB7C854), `Anisotropic` (0xB7C85C), `Shadow` (0xB7C868),
`TextureQuality` (0xB7C870), `ColorCorrection` (0xB7C888), `PostAA` (0xB7C898),
`UnSharpMask` (0xB7C8A0), `Invincible` (0xB7C8B4), `EncounterRate` (0xB7C8C0),
`ffxKeyBinding` (0xB7C8D0), `ffx2KeyBinding` (0xB7C8E0), `lmKeyBinding` (0xB7C8F0).

Enum value spellings used in that file:
`SM_WINDOW` / `SM_FULLSCREEN` / `SM_BORDERLESS` (0xB7C928..) screen mode,
`VQ_LOW` / `VQ_MEDIUM` / `VQ_HIGH` / `VQ_CUSTOM` (0xB7C954..) overall quality,
`TQ_HIGH` / `TQ_MEDIUM` / `TQ_LOW` (0xB7C9A0..) texture quality,
`PAA_OFF` / `PAA_FXAA` / `PAA_SMAA1..4` (0xB7C9BC..) post AA,
`ER_OFF` / `ER_NORMAL` / `ER_HIGH` (0xB7CA14..) encounter rate.

Parser diagnostics:
0xB7CA30 `[ConfigFile] invalid Resolution at [%s].`
0xB7CA5C `[ConfigFile] %s value invalid [%s].`
0xB7CA98 `[ConfigFile] invalid line at [%s].`
0xB7CABC `[ConfigFile] invalid key at [%s].`

Note what is in that key list and what is not. `Invincible` and `EncounterRate` are
cheat/booster settings and they persist in the same file as the graphics options, so the
file is not purely a video config. There is no `FastForward` key, which is the first
hint that fast forward is a momentary booster (held/toggled in game) rather than a
persisted menu option. Confirming that is next.

## 3. The page builders - one native function per page

Every page of the Esc menu is built by a dedicated native function that calls an AS3
function inside `escmenu.swf` once per row. There is no single master table of rows for
the whole menu, each page builds its own.

| VA | name given | page | how rows are produced |
|----|-----------|------|----------------------|
| 0x6FCCA0 | `FFX_EscMenu_BuildVideoPage` | Video | 12 inline calls to `addVideoPageItem` |
| 0x6FF770 | `FFX_EscMenu_BuildAudioPage` | Audio | calls `addAudioPageItem`, uses `spinnerWithBar` |
| 0x700DA0 | `FFX_EscMenu_BuildKeyboardPage` | Keyboard | calls `addKeyboardPageItem` |
| 0x701FA0 | `FFX_EscMenu_BuildControllerPage` | Controller | calls `addControllerPageItem` |
| 0x702660 | `FFX_EscMenu_BuildSpecialFeaturePage` | Special Feature | loop over a real table, 5 entries |
| 0x6FC3F0 | `FFX_EscMenu_BuildChangeParaPage` | Change Parameters | 3 inline calls to `addChangeParaPageItem` |
| 0x65F450 | `FFX_EscMenu_LoadMovie` | - | the only xref to the `escmenu.swf` path string |
| 0x68E6F0 | `FFX_EscMenu_ShowSysSettingPage` | - | the only xref to `setSysSettingPage` |

### The AS3 call helpers

Rows are added through two thin variadic wrappers I named:

- 0x68DE50 `FFX_Iggy_CallAS3_4Args(this, funcName, arg0..arg3)`
- 0x68DEF0 `FFX_Iggy_CallAS3_5Args(this, funcName, arg0..arg4)`

Each argument is a 16-byte IggyValue passed by value, so in the decompiler each shows up
as four consecutive ints. Values are built with:

- 0xA28D70 `IggyValue <- char* string`
- 0xA28D30 `IggyValue <- int`
- 0x68E180 `FFX_Text_GetLocalizedById(buf, msgId)` then 0x68E270 `IggyValue <- buf`
- 0x6412F0 `FFX_Text_FetchMessage(msgId, buf, len)` for the other path

### Confirmed row signatures

`addChangeParaPageItem(rowIndex:int, widgetType:String, label:String, helpText:String)`
with `widgetType` always `"spinnerButton"`. Label msg ids 694, 701, 708 and a shared
help msg id 712.

`addSpecialFeaturePageItem(rowIndex:int, label:String, hintWithKeyName:String,
descPart1:String, descPart2:String)`. The description msg id is split on the literal
`@el@` into two halves, which is how the movie gets a two-line description.

### The one real table: g_escSpecialFeatureTable at 0xC3A2C0

5 entries, stride 24 bytes, loop bound `0x78 / 24 = 5`. Field layout:

| offset | meaning | values across the 5 rows |
|--------|---------|--------------------------|
| +0x00 | index into the FFX key-binding array at config+0x60 | 28, 33, 34, 29, 31 |
| +0x04 | label message id | 657, 660, 663, 666, 676 |
| +0x08 | message id of the `$key$` hint template | 656 for all 5 |
| +0x0C | description message id, split on `@el@` | 658, 661, 664, 667, 677 |
| +0x10 | always 0, unused by the builder | 0 |
| +0x14 | a further message id, not read by this builder | 659, 662, 665, 669, 678 |

Important: field 0 is NOT a settings value index, it is a key-binding action index. The
builder does `FFX_Str_ReplaceToken(hint, "$key$", keyNameOf(config[0x60 + 4*idx]))`. So
the Special Feature page is a read-only hotkey cheat sheet for the HD boosters, not a
page of editable values. That is why fast forward has no persisted config key.

## 4. The config file - reader, writer and exact layout

### The object

`g_ffxGameSettings` at **0x22FB504** is a pointer to the single config object. The whole
PC options set lives in that one object. Layout, confirmed from both the parser and the
serializer:

| byte offset | dwords | contents |
|-------------|--------|----------|
| +0x000 | 24 | the 24 setting values, in the same order as the key-name table |
| +0x060 | 81 | FFX key-binding array (this is the `+96` the Special Feature page indexes) |
| +0x1A4 | 81 | FFX-2 key-binding array |
| +0x2E8 | 81 | Last Mission key-binding array |
| +0x42C | 1 | 81, the binding count |
| +0x430 | - | the config file path as a wide string (`wchar_t`, used directly by `_wfopen`) |
| +0x638 | - | three bytes zeroed on a successful read, a "loaded" flag area |

### The 24 keys, in order

Key-name pointer table at **0xC25010** (a duplicate copy sits at 0xC31B38, the exe has
several copies of this pointer run). Index here is both the config dword index and the
`i` in the parser loop:

0 `Language`, 1 `Resolution`, 2 `ScreenMode`, 3 `Quality`, 4 `Mute`, 5 `MasterVolume`,
6 `VoiceVolume`, 7 `MusicVolume`, 8 `SFXVolume`, 9 `Brightness`, 10 `VSync`,
11 `Anisotropic`, 12 `Shadow`, 13 `TextureQuality`, 14 `MSAA`, 15 `ColorCorrection`,
16 `PostAA`, 17 `UnSharpMask`, 18 `HDAO`, 19 `Invincible`, 20 `EncounterRate`,
21 `ffxKeyBinding`, 22 `ffx2KeyBinding`, 23 `lmKeyBinding`.

Language code table follows at 0xC25070: `jp en fr de it es kr ch none`.

So there are 24 settings and yes, they are one contiguous block of 24 dwords at the
start of the object, plus the three key-binding arrays right after.

### Reader

- **0x401CB0** `FFX_Config_LoadFile`. `_wfopen(this+0x430, L"rb")`, reads the whole file
  into a stack buffer via `alloca`, calls the parser. If the open fails it calls
  0x402B00 (set defaults) and then 0x401D80 (write the file out), so a missing file is
  created on first run.
- **0x401E00** `FFX_Config_ParseFile(this, char *text)`. Splits on lines, each line must
  contain `=`. Key is matched case-insensitively against the 24 names.
  - A line with no `=` prints `[ConfigFile] invalid line at [%s].` and is skipped.
  - **An unrecognised key prints `[ConfigFile] invalid key at [%s].` and is skipped, it
    is not an error and parsing continues.** This matters a lot for us, see below.
  - Recognised keys go to 0x401930, the per-key value parser and validator.
  - A 24-byte presence bitmap is kept. After the loop, any key that was absent, and also
    the video sub-settings when `Quality != VQ_CUSTOM`, is overwritten from a preset
    table `dword_B7CBA8` which is 24 rows by 4 columns of dwords, column chosen by the
    `Quality` value (LOW/MEDIUM/HIGH/CUSTOM). That is the quality-preset mechanism.
  - If any of the three key-binding sections was missing it copies defaults from
    `dword_C8A538` (FFX), `dword_C8A3F0` (FFX-2) and `dword_C8A2A8` (Last Mission),
    81 entries each, and sets the count to 81.
- Called once from `_WinMain` at 0x62EEF4.

### Writer

- **0x401D80** `FFX_Config_SaveFile`. `_wfopen(this+0x430, L"wb")`, calls the serializer
  twice (once with a null buffer to measure the length, once to fill an `alloca` buffer),
  `fwrite`, `fclose`.
- **0x4022F0** `FFX_Config_Serialize(this, char *out)`. Returns the length. Loops
  `i` from 0 to 0x17 and emits one `key=value` line per key. For each key it looks at
  `off_C89F48[8*i]`, a 24 by 8 table of enum-name string pointers, and writes the
  symbolic spelling (`VQ_HIGH`, `SM_BORDERLESS`, `PAA_SMAA2`, `ER_NORMAL`...) when the
  key has one, otherwise a plain number. Key 1 (`Resolution`) is special cased and
  written as two 16-bit halves. Keys 21, 22, 23 are the three key-binding arrays and are
  serialised as a packed string by 0x4016E0.

**The writer emits exactly those 24 keys and nothing else.** So an extra key a mod adds
to the file by hand survives one read (as a warning) but is deleted the next time the
game saves. See the extensibility verdict below for the way round that.

## 5. The row signature for the editable pages, read from the code

The Video page row call is 6 IggyValue arguments through 0x6FCAF0
`FFX_Iggy_CallAS3_6Args`:

```
addVideoPageItem( rowIndex:int,
                  widgetType:String,     // "spinner" | "spinnerWithList" | "spinnerWithBar"
                  label:String,          // localized text, fetched by message id
                  currentValueText:String,
                  choices:Array,         // AS3 array of strings, built by 0x6FCBC0
                  barMax:int )           // 50 for the brightness bar, -1 when not a bar
```

Helpers used to build those values:
- 0xA28D30 `IggyValue <- int`
- 0xA28D70 `IggyValue <- char*`
- 0xA28DD0 `IggyValue <- AS3 array ref`
- 0x6FCBC0 `FFX_Iggy_MakeStringArray(player, char **strings, count)`
- 0xA28D10 get the Iggy player from the menu object

The 12 Video rows, read straight out of 0x6FCCA0, with the config dword each one edits:

| row | widget | label msg id | config key index | key name |
|-----|--------|--------------|------------------|----------|
| 0 | spinnerWithList | 561 | 1 | Resolution (choices built from the mode list, `%dx%d`) |
| 1 | spinner | 563 | 2 | ScreenMode, choices msg 565/566/567 |
| 2 | spinnerWithBar | 568 | 9 | Brightness, bar max 50 |
| 3 | spinner | 570 | 10 | VSync, choices msg 573/572 |
| 4 | spinner | 585 | 13 | TextureQuality, choices 587/588/589, remapped through {2,1,0} |
| 5 | spinner | 574 | 11 | Anisotropic, literal choices "1x" "2x" "4x" "8x" |
| 6 | spinner | 580 | 12 | Shadow, choices 579/582/583/584, remapped through {0,3,2,1} |
| 7 | spinner | 590 | 14 | MSAA, choices 592/593/594/595, values {0,2,4,8} found by linear search |
| 8 | spinner | 598/599 | 15 | ColorCorrection |
| 9..11 | spinner | - | 16, 17, 18 | PostAA, UnSharpMask, HDAO |

The current value of each row is read as `*(g_ffxGameSettings + 4*keyIndex)` and the
value the user picked is cached in the menu object at `escMenu + 4*(41 + row)`.

## 6. The input and commit path - this is the useful hook

Every Flash-to-native entry point is registered in one place:
**0x68FD40 `FFX_EscMenu_RegisterAS3Callbacks`**, which calls
0xA28E90 `FFX_Iggy_RegisterAS3Callback(name, fn)` once per name. The full map:

| AS3 name | native handler |
|----------|---------------|
| `EscMenuResume` | 0x68FE60 `FFX_EscMenu_OnResume` |
| `EscMenuQuit` | 0x68FB00 |
| `EscMenuOnConfirmKey` | 0x6FF1E0 `FFX_EscMenu_OnConfirmKey` |
| `EscMenuVideoSettingRefresh` | 0x6FF4B0 `FFX_EscMenu_OnVideoSettingRefresh` |
| `EscMenuAudioSettingRefresh` | 0x6FF660 `FFX_EscMenu_OnAudioSettingRefresh` |
| `EscMenuKeyboardSettingRefresh` | 0x701680 |
| `EscMenuControllerSettingRefresh` | 0x701E60 |
| `EscMenuStartKeyEditing` | 0x7013E0 |
| `EscMenuStartControllerEditing` | 0x701BE0 |
| `EscMenuHelpTextRefresh` | 0x68E5B0 |
| `EscMenuButtonClick` | 0x68F610 `FFX_EscMenu_OnButtonClick` |
| `EscMenuPopUpConfirmed` | 0x68F8F0 |
| `EscMenuKeyboardSettingIncomplete` | 0x701660 |
| `EscMenuControllerSettingIncomplete` | 0x701E40 |
| `EscMenuOnDefaultKey` | 0x68F6F0 |
| `playEscSound` | 0x68F9E0 |
| `onMasterVolumeChanged` (off_C39638) | 0x6FF5B0 |
| `onVoiceVolumeChanged` (off_C39650) | 0x6FF4F0 |
| `onMusicVolumeChanged` (off_C39668) | 0x6FF530 |
| `onSfxVolumeChanged` (off_C39680) | 0x6FF570 |
| `ExitAudioMenu` | 0x6FF5F0 |

### The commit

`EscMenuVideoSettingRefresh` is the "the user left the Video page, here are all 12
values" callback. The handler 0x6FF4B0 just marshals the AS3 args into a 0x30 byte
block and schedules **0x6FF290 `FFX_EscMenu_CommitVideoSettings`** on the right thread.
That function is the real commit and it does, in order:

1. `FFX_Config_SetValue(cfg, keyIndex, value)` for key indices
   1, 2, 9, 10, 13, 11, 12, 14, 15, 16, 17, 18.
2. Sets dirty flags on the Phyre application object at `+21`, `+22`, `+23`, `+24` so the
   renderer picks the change up next frame.
3. Calls `sub_62BEF0()` if the resolution or screen mode changed.
4. Calls `FFX_Config_RecomputeQualityPreset(cfg)` so the Quality row re-reads as
   LOW/MEDIUM/HIGH or falls to CUSTOM.
5. Caches the 12 values back into the menu object.

**0x402A00 `FFX_Config_SetValue(cfg, keyIndex, value)` is just `cfg[keyIndex] = value`
and it is the single universal setter for all 24 options.** It has only 19 callers, all
of them in the config loader or in the Esc menu commit handlers. That is the hook point
to use for "a setting changed, broadcast it". Hooking it gives you the key index and the
new value with no guessing.

Audio works the same way but changes live: `onMasterVolumeChanged` and friends call
0x6FF5B0 / 0x6FF4F0 / 0x6FF530 / 0x6FF570, each of which calls `FFX_Config_SetValue`
once as the slider moves.

### When the file is actually written

`FFX_Config_SetValue` only touches memory. `FFX_Config_SaveFile` (0x401D80) is called
from `FFX_EscMenu_LoadMovie`, `FFX_EscMenu_OnResume`, 0x68E350, 0x68FCF0 and the
`FFEscMenu` destructor. So the file is written when the Esc menu opens and when it
closes, not per keystroke.

Other things I named on the way:
- 0x401CB0 `FFX_Config_LoadFile`, called once from `_WinMain` at 0x62EEF4
- 0x402B00 `FFX_Config_SetDefaults`
- 0x401930 `FFX_Config_ApplyKeyValue`, the per-key value parser and validator
- 0x405AF0 `FFX_Input_KeyCodeToName`, a bounds-checked lookup into
  `g_keyCodeNames` at 0xC8ABE0, 128 char* entries
- 0xC25010 `g_configKeyNames`, 0xC25070 `g_configLanguageCodes`,
  0xB7CBA8 `g_configQualityPresetTable`, 0xB7CB90 `g_configPresetCompareMask`,
  0xC89F48 `g_configEnumValueNames`, 0xC8A538/0xC8A3F0/0xC8A2A8 the three default
  key-binding tables

## 7. Where the config file actually lives

Built in `_WinMain` at 0x62EE8D onward:

1. `SHGetFolderPathW(hwnd=0, csidl=5, ...)` -> CSIDL_PERSONAL, the user's My Documents.
2. Appends the wide literal at 0xB40EB8:
   `\Square Enix\FINAL FANTASY X&X-2 HD Remaster\GameSetting.ini`
3. `FFX_Alloc(0x63C)` for a 1596-byte settings object.
4. `FFX_GameSettings__loadIni(obj, path)` at **0x401880** - badly named, it is the
   constructor. It seeds the 24 option dwords from column 0 of
   `g_configQualityPresetTable`, zeroes the three 0x144-byte (81 dword) binding arrays at
   +96, +420 and +744, and copies the wide path to +1072.
5. Stores the pointer in `g_ffxGameSettings` (0x22FB504) and calls
   `FFX_Config_LoadFile`.

So the file is a plain text INI at
**`%USERPROFILE%\Documents\Square Enix\FINAL FANTASY X&X-2 HD Remaster\GameSetting.ini`**

It is shared between the three games in the package, which is why the key list has
`ffxKeyBinding`, `ffx2KeyBinding` and `lmKeyBinding` side by side. I did not find any
registry use for these options and no Steam cloud call on this path, but I only looked
at this code path, I did not audit the Steam integration, so treat "no cloud" as
unverified.

Settings object layout, now fully confirmed from the constructor and both the parser and
the serializer:

```
+0x000  int   values[24]      // the 24 options, index == index in g_configKeyNames
+0x060  int   ffxBindings[81]
+0x1A4  int   ffx2Bindings[81]
+0x2E8  int   lmBindings[81]
+0x42C  int   bindingCount    // 81
+0x430  wchar path[...]       // the GameSetting.ini path, used directly by _wfopen
+0x638  byte  firstRunFlags[3]
        total size 0x63C
```

## 8. Fast forward - where it really is

The task's premise was that fast forward is a setting in this menu. It is not. It is a
hotkey-driven booster and nothing about it is persisted. Here is the chain.

The five HD boosters are polled every frame in
**0x657140 `FFX_Frame_UpdateBoostersAndOverlays`**, which reads input action bitmasks:

| action mask | action index | default key | booster | runtime state global |
|-------------|--------------|-------------|---------|---------------------|
| 0x10000 | 28 | F1 | **fast forward, 1x / 2x / 4x** | **`g_boosterSpeedIndex` 0xCE82B4** |
| 0x20000 | 29 | F4 | auto battle | `g_boosterAutoBattle` 0x133D6E0 |
| 0x40000 | 30 | unbound | nothing uses it | - |
| 0x80000 | 31 | F5 | a toggle on FFXApp+69068, messages 752/753 | FFXApp+69068 |
| 0x100000 | 32 | unbound | FMV skip | - |
| 0x200000 | 33 | F2 | invincibility | `g_boosterInvincible` 0x12FB7CC |
| 0x400000 | 34 | F3 | encounter rate 0 off / 1 normal / 2 high | `g_boosterEncounterRate` 0xC421D8 |

The mask for a booster action index `i` is `1 << (i - 12)`. That pattern holds for all
of them and ties the Special Feature page rows (28, 33, 34, 29, 31) and the Keyboard
page booster rows (same five indices) to these handlers.

### The fast-forward anchor, precisely

- **`g_boosterSpeedIndex` at 0xCE82B4**, an int, values 0, 1, 2.
- Cycled by **0x6F7470 `FFX_Booster_UpdateSpeedHotkey`** on a press of action mask
  0x10000.
- Applied by **0x6F7340**, the dt scaler, as
  `dt * g_boosterSpeedMultipliers[g_boosterSpeedIndex]` where
  **`g_boosterSpeedMultipliers` at 0xC38E1C = {1.0f, 2.0f, 4.0f}**. I read those three
  floats out of the data section, they are not inferred. An existing comment on 0x6F7340
  in the database (from earlier project work) says the result is stored back into
  `FFX_MainStep`'s own dt argument at 0x820B01, so the multiplier increases the step
  count, which is why both machines must agree on this value for lockstep.
- Gated by `g_boosterEnabled` (0xCE82BC), with `g_boosterSpeedApplied` (0xCE82B8) as the
  applied/not-applied edge flag and `g_boosterSpeedIggyIcon` (0xCE82C0) as the HUD icon
  handle.
- Reset to 0 by `FFX_Booster_Enable` (0x6F72F0) and `FFX_Booster_Disable` (0x6F72A0).

Other names I set: 0x6F7580 `FFX_Booster_PollAutoBattleKey`,
0x6F75C0 `FFX_Booster_PollEncounterRateKey`,
0x6F76C0 `FFX_Booster_PollInvincibleKey`,
0x6F7900 `FFX_Booster_HideIcon`, 0x6F7970 `FFX_Booster_ShowIcon`,
0x6F78B0 `FFX_Booster_GetHud`, `g_boosterHud` at 0xCE82E4 (a 0x40-byte singleton created
by 0x6F77F0 from `FFX_EscMenu_LoadMovie`, holding the nine booster HUD swf paths from
0x6F7700).

### A loose end worth knowing about

The config file has `Invincible` (key index 19) and `EncounterRate` (key index 20), and
the serializer writes them, but I could not find anything in FFX.exe that reads
`cfg[19]` or `cfg[20]`, and the runtime booster states are completely separate globals
that only the hotkey handlers write. Method note: I checked every xref to
`g_ffxGameSettings` and looked 8 instructions past each load for offsets +0x4C and +0x50
and for `FFX_Config_SetValue` with key 19 or 20, and found nothing. That is a bounded
search, not a proof, so call it likely rather than certain. The plausible explanation is
that those two keys belong to FFX-2.exe, which shares this file. Do not treat them as
free scratch space for mod data.

## 9. Is the entry list extensible? Yes for rows, no for pages

### Where every page gets built

**0x68DFD0 `FFEscMenu__ctor`**, called from `FFEscMenu__createSingleton` (itself called
from `FFX_EscMenu_LoadMovie`), populates the whole menu once:

```
FFX_EscMenu_RegisterAS3Callbacks()
sub_68F400(this)
FFX_EscMenu_SetAllPageLabels(this)        // 0x68E6F0
FFX_EscMenu_BuildVideoPage_Entry(this)    // 0x6FF020 -> 0x6FCCA0
FFX_EscMenu_BuildAudioPage(this)          // 0x6FF770
FFX_EscMenu_BuildControllerPage(this)     // 0x701FA0
FFX_EscMenu_BuildKeyboardPage(this)       // 0x700DA0
FFX_EscMenu_BuildSpecialFeaturePage(this) // 0x702660
FFX_EscMenu_BuildChangeParaPage(this)     // 0x6FC3F0
```

There is a second copy of that sequence at **0x68F3C0**, which I defined as a function
and named `FFX_EscMenu_RebuildAllPages`. It was not recognised as a function before, and
it has no xrefs IDA can see, so it is probably reached through a vtable or a function
pointer. It is the same list minus the callback registration, so it looks like the
language-change or reset path.

### The rows are extensible

Every row of every editable page is one AS3 call with the row index as the first
argument. Nothing in native code holds a maximum. Three independent pieces of evidence
that the movie handles a variable number of rows:

1. On the Video page, row 0 (Resolution) is inside `if (modeList != 0)`, so the page
   emits either 11 or 12 rows depending on the machine, and the indices it passes are
   1..11 in the short case. The movie therefore tolerates a missing index, which means
   it builds rows from what it is told rather than filling fixed slots.
2. The Resolution row's choice array length is however many display modes the adapter
   reports, built at runtime by `FFX_Iggy_MakeStringArray`.
3. The pages already differ wildly in length, 3 for Change Parameters, 5 for Special
   Feature, 12 for Video, 23 for Keyboard, and all are scrollable.

So `addVideoPageItem(12, "spinner", "My setting", "Off", ["Off","On"], -1)` issued right
after the original builder returns should give a 13th row. **I want to be honest that
this is an inference about the ActionScript inside escmenu.swf, not something I read.
The AS3 is in the asset file, not the exe. It is a well-supported inference but it is
the one thing that has to be tested before anyone budgets on it.**

### Reading the new row back

The movie returns all the row values for a page as an AS3 array.
**0x6FED60 `FFX_EscMenu_ReadVideoRowValues`** pulls them out with
`IggyValuePathSetArrayIndex` and `IggyValueGetS32RS` in a loop with the bound hardcoded
as `i < 12`. A mod bumps that bound (it is a `cmp esi, 0Ch`, a one byte patch) or hooks
the function, then reads index 12 for the extra row. The commit function
`FFX_EscMenu_CommitVideoSettings` copies 12 values into the menu object, so that loop
bound needs the same treatment.

### The pages are NOT extensible without editing the swf

`FFX_EscMenu_SetAllPageLabels` shows the structure is fixed by the AS3 function arity:
`setHomePage` takes exactly 5 labels, `setSysSettingPage` 3, `setKeyBindingPage` 2,
`setControlBar` 3, `setSubTitle` 4. Those counts live in the movie, not in a table. You
cannot add a sixth top-level entry or a new page from native code alone. Editing
escmenu.swf is possible in principle but it is an Iggy-compiled Flash asset, which is a
much bigger job and a much bigger compatibility risk.

### The only real table, if you prefer a data patch to a code hook

`g_escSpecialFeatureTable` at 0xC3A2C0, 5 entries of 24 bytes, documented in section 3.
To extend it a mod would have to repoint four separate absolute operands
(`dword_C3A2C0`, `+C4`, `+C8`, `+CC` are each encoded separately at 0x7027B3, 0x702708,
0x70272A, 0x702761) and change the loop bound 0x78. That is five small patches against
one hook on the builder's return, so the hook is the better route even here.

## 10. Verdict and the co-op split

### Can the mod put its settings in the game's own menu?

Yes, as extra rows on an existing page. The cheapest honest plan:

1. Detour the return of `FFX_EscMenu_BuildVideoPage` (0x6FCCA0) or, better, the tail of
   `FFEscMenu__ctor` (0x68DFD0), and issue `addVideoPageItem` calls for row indices 12,
   13 and so on, using the already-identified helpers `FFX_Iggy_CallAS3_6Args`
   (0x6FCAF0), `FFX_Iggy_MakeStringArray` (0x6FCBC0), and the IggyValue builders at
   0xA28D30 / 0xA28D70 / 0xA28DD0. Labels can be literal strings, they do not have to
   come from the message table, since `addSpecialFeaturePageItem` and the keyboard page
   both pass plain `char*` through 0xA28D70.
2. Hook `FFX_EscMenu_ReadVideoRowValues` (0x6FED60) and
   `FFX_EscMenu_CommitVideoSettings` (0x6FF290) to read and act on the extra indices.
3. For persistence, do not add keys to GameSetting.ini naively. The reader tolerates
   unknown keys (it prints `[ConfigFile] invalid key` and moves on, so nothing breaks)
   but `FFX_Config_Serialize` emits only the 24 known keys, so the mod's line is deleted
   on the next save. Two clean options: hook `FFX_Config_Serialize` to append extra lines
   after the 24 (the game's own reader will ignore them harmlessly), or just use a
   separate mod ini. The separate file is simpler and cannot corrupt anything.

Rough size: a day or two of work for the plumbing, assuming step 1's assumption about
the movie holds. If it does not hold, fall back to adding rows to the Special Feature
page (a plain list of text rows, the least likely of all of them to have a hardcoded
slot count) or to a Win32 window after all.

### Which existing settings are local and which are shared

Using the project rule that settings are local unless they change the game:

Local, must never be synced:
- `Language` (0), `Resolution` (1), `ScreenMode` (2), `Quality` (3), `Mute` (4),
  `MasterVolume` (5), `VoiceVolume` (6), `MusicVolume` (7), `SFXVolume` (8),
  `Brightness` (9), `VSync` (10), `Anisotropic` (11), `Shadow` (12),
  `TextureQuality` (13), `MSAA` (14), `ColorCorrection` (15), `PostAA` (16),
  `UnSharpMask` (17), `HDAO` (18), and all three key-binding arrays. That is all 24
  persisted options except 19 and 20. None of them touch simulation.

Shared, must match on both machines:
- `g_boosterSpeedIndex` (0xCE82B4). Not optional. The comment already in the database on
  0x6F7340 says the multiplier feeds back into the main step's dt and therefore the step
  count, so if one side is at 2x and the other at 1x the two simulations diverge
  immediately. This is the single most important booster to broadcast, and arguably it
  should be host-authoritative rather than merely synced.
- `g_boosterEncounterRate` (0xC421D8). Changes whether and how often encounters fire,
  read by 0x780D10. Divergent values mean one side enters a battle and the other does
  not.
- `g_boosterInvincible` (0x12FB7CC). Read by the damage path at 0x792A90. Game changing.
- `g_boosterAutoBattle` (0x133D6E0). Read by roughly thirty battle AI functions. It
  decides who issues commands, so it must match.
- The two persisted keys `Invincible` (19) and `EncounterRate` (20) would be shared if
  anything read them, but see the loose end in section 8, nothing in FFX.exe appears to.

The mod's own settings split the same way. Pad assignment for the second character is
local. Anything that changes how the simulation steps or how battle decisions are made
is shared.

### Biggest risk or unknown

Whether escmenu.swf's `addVideoPageItem` really appends rather than filling a fixed
number of pre-placed row instances. Everything on the native side says it appends, and
the variable row count on the Video page is good evidence, but the proof is in the
ActionScript inside the Iggy asset and I could not read that from the exe. It is a
twenty minute experiment in the running game: detour `FFX_EscMenu_BuildVideoPage`'s
return, make one extra `addVideoPageItem` call with index 12, open the menu and look.
Falsifiable, cheap, and it decides the whole approach.

Second risk, much smaller: the Esc menu saves GameSetting.ini when it opens and when it
closes, so any hook that appends lines to the serializer runs more often than you might
expect, and a buggy one corrupts the user's graphics settings. Keep mod settings in a
separate file unless there is a real reason not to.

---

# Second pass: the pieces a mod actually has to call

Written while building `loader\workshop\include\ffx\addresses\EscMenu.h`,
`include\ffx\EscMenu.h` and `src\ffx\EscMenu.cpp`. Everything in sections 1 to 10 above was
spot-checked against the IDB before it went into a header. Nothing above was found to be
wrong. One labelling slip: section 3's table calls 0x68E6F0
`FFX_EscMenu_ShowSysSettingPage` and section 9 calls the same address
`FFX_EscMenu_SetAllPageLabels`. The second is right and is what the IDB says. The address
itself is correct in both places.

Also confirmed correct, because it looks like a typo and is not: `g_ffxGameSettings` really
is at **0x22FB504**. It sits in the big uninitialised tail of `.data`, which runs
0xC0A000..0x25D7000, so `is_loaded` reports false for it and it is easy to mistake for a bad
address.

## 11. The IggyValue, settled

16 bytes. This is the thing most likely to be got wrong, so here is the evidence from both
directions.

```
+0x00  DWORD   tag      3 = bool, 4 = number, 5 = string, 9 = array ref
+0x04  DWORD   never written by any builder and never read by any consumer
+0x08  double  the number, OR the char* for a string, OR the array ref
+0x0C  DWORD   the string's byte length, string tag only
```

**Writing side.** `FFX_IggyValue_SetNumber` 0xA28D30 is five instructions:

```
mov dword ptr [eax], 4      ; the tag
fild [ebp+arg_4]            ; the int
fstp [ebp+arg_0]            ; narrowed to a 32-bit FLOAT in the pointer's own stack slot
fld  [ebp+arg_0]
fstp qword ptr [eax+8]      ; widened back and stored as a DOUBLE at +8
```

So the number is a double at +0x08, and the game's own builder rounds anything above 2^24 on
the way. Fine for a row index, not fine for a handle or a hash.
`FFX_IggyValue_SetString` 0xA28D70 writes tag 5, the pointer to +0x08 and `strlen` to +0x0C,
and **does not copy the string**, so the buffer has to outlive the call.
`FFX_IggyValue_SetArrayRef` 0xA28DD0 writes tag 9 and the ref to +0x08.
`FFX_IggyValue_SetBool` 0xA28DB0 writes tag 3 and the widened byte to +0x08. None of the
four touches +0x04.

**Reading side, independently.** `FFX_EscMenu_OnMasterVolumeChanged` 0x6FF5B0 does
`(int)*(double *)(args + 8)` to get the new slider value out of the AS3 argument array. Same
16 bytes, same double at +0x08, from the opposite direction. That also pins the AS3 callback
signature as `int __cdecl (FFEscMenu *menu, const IggyValue *args)`.

**Size.** All twelve `addVideoPageItem` call sites do `add esp, 68h`, and 0x68 is 8 bytes of
`(menu, name)` plus 6 * 16.

The kit's struct was compiled with a negative-array size check on `sizeof`, `offsetof(tag)`,
`offsetof(reserved)`, `offsetof(payload)` and `offsetof(payload.string.length)`, so the C++
side is proven to match, not assumed to.

## 12. The AS3 call helpers, and the better one nobody was using

Section 3 found the 4-arg and 5-arg wrappers and section 5 the 6-arg one. There are six, one
per arity, and they are all thin forwarders:

| VA | name | stack bytes the caller pops |
|----|------|-----------------------------|
| 0x68DD40 | `FFX_Iggy_CallAS3_1Arg` | 0x18 |
| 0x68DD80 | `FFX_Iggy_CallAS3_2Args` | 0x28 |
| 0x68DDD0 | `FFX_Iggy_CallAS3_3Args` | 0x38 |
| 0x68DE50 | `FFX_Iggy_CallAS3_4Args` | 0x48 |
| 0x68DEF0 | `FFX_Iggy_CallAS3_5Args` | 0x58 |
| 0x6FCAF0 | `FFX_Iggy_CallAS3_6Args` | 0x68 |

All six are `int __cdecl (IggyMenu *menu, const char *as3Name, IggyValue a0 .. aN-1)`. The
IggyValues go **by value, 16 bytes each, in declaration order, lowest stack address first**,
and the caller cleans up: 8 + 16*N, which is exactly the `add esp` column. Verified at
fourteen call sites, `add esp, 68h` at all twelve Video rows plus 0x58 at the Special Feature
loop and 0x48 at the Change Parameters rows.

Reading the argument order out of MSVC's listing is backwards and that is worth knowing,
because it is how a reader gets the order wrong. Each value is emitted as `mov reg, esp` /
`sub esp, 10h` / four stores, with the register pointing at the block the PREVIOUS `sub`
allocated. So the first block written in the listing is the LAST argument. Row 0 of the Video
page proves it: `FFX_IggyValue_SetNumber(&v, -1)` at 0x6FCDA6 is the first block written and
ends up as `barMax`, argument 6, while `FFX_IggyValue_SetNumber(&v, 0)` at 0x6FCEAC is the
last block written and ends up as `rowIndex`, argument 1.

### The one a mod should use instead

**0xA28BA0 `FFX_IggyMenu_CallAS3Function`** is what all six forward to, and it is not
variadic:

```
int __thiscall (IggyMenu *this, void *resultPath, const char *name,
                int argCount, const IggyValue *args, int reserved)
```

`retn 14h`, so the callee pops the five stack arguments. Body:
`IggyPlayerCreateFastNameUTF8` in a `Sleep(5)` retry loop until it succeeds, then
`IggyPlayerCallFunctionRS(player, resultPath, fastName, argCount, args)`. The player comes
from the same `**(this + 0x6C)` expression as the getter.

Every one of the game's own call sites passes `resultPath = 0` and `reserved = 1`, confirmed
at both extremes: `FFX_Iggy_CallAS3_6Args` pushes `(0, name, 6, &values, 1)` and
`FFX_EscMenu_ShowUnsavedChangesPopUp` pushes `(0, "onCancelKey", 0, 0, 1)` for the
zero-argument case. Since `argCount` and `args` are ordinary parameters, a mod can call any
AS3 function with any number of arguments and never build a 104-byte variadic stack frame.
The kit's `ffx::CallAS3` does exactly this.

### Getting the player, and the choice array

- **0xA28D10 `FFX_IggyMenu_GetPlayer`**, `__thiscall`, is `mov eax,[ecx+6Ch]; mov eax,[eax]`.
  So the `iggy_player` handle is `*(void **)(*(void **)(menu + 0x6C))`. Confirmed three ways:
  this function, 0xA28EE0 which feeds the same expression to `IggyPlayerGotoFrameRS`, and
  0xA28BA0 which inlines it. The kit reimplements the two derefs rather than calling the
  getter, so it needs no prologue check.
- **0x6FCBC0 `FFX_Iggy_MakeStringArray`** is a constructor, not a plain function:
  `IggyArrayRef *__thiscall (IggyArrayRef *out, void *player, const char **strings, int count)`,
  `retn 0Ch`. `IggyArrayRef` is 8 bytes, `{ player, arrayRef }`. **Feed the SECOND dword to
  `FFX_IggyValue_SetArrayRef`** - the first is the player handle the constructor stashed, and
  passing that instead hands the movie a pointer where it expects an array reference. The
  strings are copied into the movie by `IggyValueSetStringUTF8RS`, so unlike a string
  IggyValue they do not have to outlive the call.
  **Nothing frees these arrays.** `IggyValueRefFree` appears only on the read-back path in
  `FFX_EscMenu_ReadVideoRowValues`, so the game already leaks one AS3 array per Video page
  build. Build a mod's choice array once and keep the ref.

### Localized labels need a 0x110-byte buffer, not 0x100

`FFX_Text_GetLocalizedById` (0x68E180) fills `ecx` with up to 0x100 bytes of text, and
`FFX_IggyValue_SetLocalizedText` (0x68E270) then builds a string IggyValue at
**textObj + 0x100** and copies the 16 bytes out. So the caller's buffer must be 0x110 bytes.
Every page builder in the game declares 272 bytes for exactly this reason. A mod does not
have to use any of it, since a plain `char*` literal through `FFX_IggyValue_SetString` works,
which is what the Special Feature and keyboard pages do for key names.

### The deferred-call queue

`FFX_IggyMenu_QueueDeferred2` 0xA28E40,
`char __thiscall (IggyMenu *this, void *ctx, void *fn, int arg, void *cleanupFn)`, is how an
AS3 callback gets real work onto a safe point. It walks 5 slots of 20 bytes from `this + 0`,
takes the first with a zero byte at `slot + 4`, and fills `+0x08 ctx`, `+0x0C fn`,
`+0x10 arg`, `+0x14 cleanupFn`. **It returns 0 when all five slots are taken and says
nothing**, so anything a mod queues here has to check the return. 0xA28DF0 is the
three-field variant.

## 13. The FFEscMenu object layout

Read out of the constructor's field writes and cross-checked against every user of each
field.

```
+0x000  vftable, and the deferred-call queue shares these bytes: 5 slots of 20, each
        with its in-use byte at slot+4, so slot 0 effectively runs +0x04..+0x13
+0x06C  void **  one more deref gets the iggy_player
+0x070  BYTE     visible, EscMenuIsOpen's first test
+0x09C  a second vftable, Phyre IWindowEvent
+0x0A0  BYTE     1
+0x0A4  int[12]  the cached Video page row values
+0x0D4  int      display mode count
+0x0D8  int      selected mode index, -1 until the page builds
+0x0DC  void *   display mode list, stride 28, width dword at +0, height dword at +4
+0x0E4  int      -1 until open, EscMenuIsOpen's second test, >= 0 means open
+0x234  int      -1
```

**0xA4 + 12*4 is 0xD4.** The row cache ends exactly where the display mode count begins, so
a 13th cached row value overwrites the mode count. That is the concrete reason to hook
`FFX_EscMenu_CommitVideoSettings` rather than widen its copy loop, and it is in the kit as a
comment on `EscMenuFields::VideoRowCacheCount`.

The Resolution config value is packed as `(width << 16) | height`, read off
`FFX_EscMenu_CommitVideoSettings` which does
`(word[modeEntry] << 16) | word[modeEntry + 4]`.

## 14. Widening the Video page: all five bytes, and why not to

Section 9 found two of the hardcoded 12s. There are three, plus two buffer sizes.

| VA | RVA | bytes now | patched | what it bounds |
|----|-----|-----------|---------|----------------|
| 0x6FEDCD | 0x2FEDCD | `83 FE 0C` | `83 FE 0D` | `FFX_EscMenu_ReadVideoRowValues` loop |
| 0x6FF46A | 0x2FF46A | `83 F8 0C` | `83 F8 0D` | `FFX_EscMenu_CommitVideoSettings` cache copy |
| 0x6FF212 | 0x2FF212 | `83 F9 0C` | `83 F9 0D` | `FFX_EscMenu_OnConfirmKey` unsaved-changes compare |
| 0x6FF1E5 | 0x2FF1E5 | `6A 30` | `6A 34` | `FFX_EscMenu_OnConfirmKey` scratch, 12 ints -> 13 |
| 0x6FF4B5 | 0x2FF4B5 | `6A 30` | `6A 34` | `FFX_EscMenu_OnVideoSettingRefresh` scratch |

The immediate byte is the last byte of each, so the one-byte patch addresses are 0x6FEDCF,
0x6FF46C, 0x6FF214, 0x6FF1E6 and 0x6FF4B6. **Nothing was applied, this is documentation.**

Miss either `push 30h` and the read-back loop runs one element past a 48-byte heap block.
Get all five right and the commit still writes a 13th int over the display mode count, per
section 13. So the honest recommendation in the kit is: hook the three functions, do not
patch the bounds, and hold the extra row's value in the mod rather than in the game's cache.
The byte table is in `addresses\EscMenu.h` so a mod can verify the bytes it is hooking.

The Special Feature page's equivalent is one bound, `cmp ebx, 78h` at 0x7029C4 with the
immediate at 0x7029C6, where 0x78 is 5 entries of 24 bytes.

## 15. Which page to append to, and the real reason

Section 10 guessed Special Feature as the fallback because it is "a plain list of text rows".
That is right about the cost and it misses the thing that actually decides it.

| | Video | Special Feature | Change Parameters |
|-|-------|-----------------|-------------------|
| arguments per row | 6 | 5 | 4 |
| needs a choice array | yes for `spinnerWithList` | no | no |
| needs a widget type string | yes | no | yes, `spinnerButton` |
| row source | 12 inline calls | a 5-entry table | 3 inline calls |
| **does native get the value back** | **yes** | **no** | press only |
| hardcoded counts to deal with | three 12s and two 0x30s | one table bound | none |

**Recommendation: it depends on what the row is for, and the deciding factor is the callback
list, not the argument count.**

- A row the player **changes** goes on the **Video** page. It is the only page with both an
  editable spinner and a native read-back path, `EscMenuVideoSettingRefresh` ->
  `FFX_EscMenu_ReadVideoRowValues` -> `FFX_EscMenu_CommitVideoSettings`. There is no
  `addSpecialFeature...Refresh` name anywhere in `FFX_EscMenu_RegisterAS3Callbacks`, so the
  movie never reports a Special Feature row's state to native. A toggle there would be a
  picture of a toggle.
- A row that only **tells the player something** ("host: connected, 2 players") goes on
  **Special Feature**. Five plain strings, no array to build and leak, no bounds to widen,
  and the builder is a single table walk so a detour on its return is trivial.
- A row that is a **button** is worth considering on **Change Parameters**: four arguments,
  `spinnerButton`, no array, and presses arrive through `EscMenuButtonClick` (0x68F610) and
  `EscMenuOnConfirmKey`. Only three rows exist today so there is the least existing machinery
  to disturb. This one was not traced end to end, so treat it as a lead.

Best of both: put the mod's settings on Video where they can be edited, and a status line on
Special Feature where it costs nothing.

## 16. Does the row index have to be contiguous? Answered: native never knows

Section 9 inferred that the movie appends from the fact that the Video page emits 11 or 12
rows. Here is the harder half of the answer.

**No native code anywhere reads a row count back from the movie, and it could not.** Three
independent pieces:

1. **The import list.** FFX.exe imports exactly 41 `iggy_w32` entry points and not one of
   them returns an array length or a property. The value side is `IggyValueGetS32RS`,
   `IggyValueGetStringUTF8RS`, `IggyValueSetStringUTF8RS`, `IggyValueRefCreateArray`,
   `IggyValueRefFree`, `IggyValueRefFromTempRef`, `IggyValuePathFromRef`,
   `IggyValuePathMakeArrayRef` and `IggyValuePathSetArrayIndex`. There is no
   `GetArrayLength`, no generic property getter. The game is physically unable to ask the
   movie how long an array is.
2. **The read-back loop.** `FFX_EscMenu_ReadVideoRowValues` sets index 0..11 and calls
   `IggyValueGetS32RS` twelve times with the bound as an immediate. It does not look at what
   the movie sent, it tells the movie which indices to hand over.
3. **The page-open call.** `setVideoPage` is
   `FFX_Iggy_CallAS3_1Arg(menu, "setVideoPage", title)`. One string, the page title from
   message id 547. No row count in either direction, and the same is true of `setAudioPage`,
   `setKeyboardPage` and `setControllerPage`.

So on the native side row indices are whatever we say they are, there is no validation, no
contiguity requirement and no count to keep in step. That proves native is count-blind. It
does **not** prove the movie appends, because the ActionScript is still in the Iggy asset and
still unread. What it does mean is that the only thing that can go wrong is inside
`escmenu.swf`, which narrows the twenty minute experiment in section 10 to exactly the right
question: issue one `addVideoPageItem` with index 12 and look.

## 17. The audio row table, 0xC39628

Found while chasing the volume callbacks, which read their config key index out of data
rather than from a literal. 4 entries of 24 bytes, 0xC39628..0xC39688, where the next
structure (a third copy of the 24 config key-name pointers) begins.

| offset | meaning | values |
|--------|---------|--------|
| +0x00 | channel index | 0, 1, 2, 3 |
| +0x04 | label message id | 559, 553, 555, 557 |
| +0x08 | bar maximum | 100 for all four |
| +0x0C | config key index | 5 MasterVolume, 6 VoiceVolume, 7 MusicVolume, 8 SFXVolume |
| +0x10 | AS3 callback name | `onMasterVolumeChanged`, `onVoice...`, `onMusic...`, `onSfx...` |
| +0x14 | the live-apply function | 0x67A530, 0x67A5B0, 0x67A550, 0x67A590 |

The field order is why this looked wrong at first: `FFX_EscMenu_OnMasterVolumeChanged` reads
`dword_C39634`, which is row 0 + 0x0C, so a reader who assumes the key index is field 0 lands
the table 12 bytes late and the last row's tail reads as garbage. The table starts at
0xC39628 and the layout above is the one that makes all four rows consistent.

## 18. The config object, two refinements

Both from the constructor rather than from the parser, so they are first hand.

- The path at **+0x430** really is used raw: `_wfopen(this + 1072, L"rb")`. The field runs to
  the flag at +0x638, which is 260 wide characters.
- Section 4 called +0x638 "three bytes zeroed on a successful read". More precisely it is a
  **WORD at +0x638 set to 0x0101** and a **BYTE at +0x63A set to 1** by the constructor, both
  zeroed by `FFX_Config_LoadFile` the moment `_wfopen` succeeds. So it is a "we never read a
  file, these are defaults" marker, and a mod can test it to tell a first run from a
  returning player.
- `FFX_Config_SetValue` 0x402A00 is
  **`int __thiscall (void *cfg, int keyIndex, int value)`, `retn 8`**, not `__cdecl`. `ecx` is
  the cfg object and every one of its callers loads it from `g_ffxGameSettings` first. Worth
  pinning down because a kit that declared it `__cdecl` would corrupt the stack.

## 19. The booster globals, with their readers

The three that `addresses\MainLoop.h` does not already carry. Fast forward
(`BoosterSpeedIndex`) is there, because it changes the step count rather than anything about
the menu.

| global | VA | RVA | who reads it | verdict |
|--------|----|-----|--------------|---------|
| `g_boosterAutoBattle` | 0x133D6E0 | 0xF3D6E0 | 31 instructions in 22 distinct battle functions, nearly all `cmp ..., 1` | MUST MATCH, it decides who issues commands |
| `g_boosterEncounterRate` | 0xC421D8 | 0x8421D8 | one gameplay site, the encounter roll at 0x780D73 | MUST MATCH, one side enters a battle and the other does not |
| `g_boosterInvincible` | 0x12FB7CC | 0xEFB7CC | the damage path, four reads in 0x792A90 | MUST MATCH, different HP from the first hit |

All three are written in exactly one place, `FFX_Frame_UpdateBoostersAndOverlays` 0x657140,
from a hotkey. None is persisted and none has a config key, so a bare store from a mod is
safe: nothing caches them and nothing overwrites until the player presses the key again. That
is why the kit's setters are plain writes rather than calls.

And the other half of the co-op answer, now expressible as data rather than prose: all 24
persisted keys are presentation only, so `ffx::ConfigKeyAffectsSimulation` returns false for
every one of them. The method behind that: the 24 value dwords have exactly one writer,
`FFX_Config_SetValue`, whose callers are the file parser and the Esc menu commit handlers, and
every reader of a value dword is a renderer or audio path. Indices 19 and 20 have no reader at
all, see the loose end in section 8. The three key-binding arrays are per-player by
definition.

## 20. What went into the kit

- `loader\workshop\include\ffx\addresses\EscMenu.h` - 91 RVAs, all verified against the IDB,
  grouped by what a caller is trying to do. Includes the five patch-site bytes as named
  constants so a mod can check them before hooking. Deliberately does not redeclare
  `EscMenu`, `EscMenuIsOpen` or the fast forward globals, which `addresses\MainLoop.h` owns,
  nor `InputKeyCodeToName` and `KeyCodeNames`, which `addresses\Input.h` owns.
- `loader\workshop\include\ffx\EscMenu.h` and `src\ffx\EscMenu.cpp` - the typed layer. The
  24-key enum, the config object and menu object accessors, `ReadConfigValue` /
  `WriteConfigValue` through the game's own setter, `ConfigKeyName`, `ConfigFilePath`,
  `ConfigKeyAffectsSimulation`, the three booster reader/writer pairs, the `IggyValue` struct
  with its builders, `CallAS3`, `MakeChoiceArray`, `AddVideoPageRow`,
  `AddSpecialFeaturePageRow`, `VideoRowsBuilt`, `RegisterAS3Callback` and `LogEscMenuState`.
- Both compile clean at `/W4 /WX` for x86, standalone and alongside `ffx\Addresses.h`,
  `ffx\MainLoop.h` and `ffx\GameState.h`. The `IggyValue` layout is held in place by
  compile-time size and offset checks rather than by a comment.
- IDB: renamed `FFX_IggyMenu_CallAS3Function`, `FFX_IggyMenu_GetPlayer`,
  `FFX_IggyValue_SetNumber` / `SetString` / `SetBool` / `SetArrayRef` / `SetLocalizedText`,
  `FFX_IggyMenu_QueueDeferred1` / `QueueDeferred2`, `FFX_IggyMenu_SetVisible`, the four
  `FFX_EscMenu_On*VolumeChanged`, `FFX_EscMenu_OnExitAudioMenu`,
  `FFX_EscMenu_ShowUnsavedChangesPopUp` and `g_escAudioRowTable`. Comments on all of the above
  plus `FFEscMenu__ctor`, `FFX_EscMenu_SetAllPageLabels`, `FFX_EscMenu_RegisterAS3Callbacks`,
  `FFX_EscMenu_ReadVideoRowValues`, `FFX_EscMenu_CommitVideoSettings`,
  `FFX_EscMenu_OnConfirmKey`, `FFX_EscMenu_BuildSpecialFeaturePage`,
  `FFX_Iggy_CallAS3_6Args`, `FFX_Iggy_MakeStringArray`, `FFX_Config_SetValue`,
  `FFX_GameSettings__loadIni` and the three booster globals. Saved.

## 21. Biggest remaining risk, unchanged and now narrower

Still the same single unknown as section 10: whether `escmenu.swf`'s `addVideoPageItem`
appends a row or fills one of a fixed number of pre-placed row instances. Section 16 removes
every native-side way that could go wrong, which means the whole plan now rests on one line of
ActionScript nobody has read. The experiment is unchanged and cheap: detour the return of
`FFX_EscMenu_BuildVideoPage` (RVA 0x2FCCA0), call
`ffx::AddVideoPageRow(12, kWidgetSpinner, "Co-op", "Off", 0, -1)`, open the menu and look. If
a 13th row appears the plan is sound. If it does not, fall back to a status row on the Special
Feature page, which has the same unknown but a much smaller payload riding on it, and put the
editable settings in a Win32 window after all.

Second risk, smaller but easy to hit: `ffx::CallAS3` is only safe while the menu object
exists, and the only moments that are known to be safe are inside a detour on a page builder
or on the constructor's tail. Calling it from an arbitrary frame was not tested.
