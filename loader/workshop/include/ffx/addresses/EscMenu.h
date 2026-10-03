#pragma once

#include <windows.h>

// The Esc menu and the PC config file.
//
// This is the HD-only PC settings screen. It is an Iggy (Flash) movie,
// /FFX_Data/GameData/PS3Data/flash/escmenu.swf, but the row list is NOT authored
// in the movie. Native code builds every page at runtime by calling an AS3
// function once per row, with the row index, the widget type, the label and the
// value range all passed as arguments. That is why a mod can add its own rows:
// one extra call per row, no asset editing.
//
// WHY A CO-OP MOD CARES, in the order the user will notice:
//
//   1. It is the only in-game UI surface the mod can reach without shipping new
//      Flash assets. Everything a player has to set before a session starts -
//      host or join, which pad drives which character - belongs here rather than
//      in a Win32 window the game would draw over.
//   2. None of the 24 persisted options affect the simulation, so the config file
//      never has to be synced. That is a real finding, not an assumption, and it
//      is spelled out per key in ffx/EscMenu.h.
//   3. The four things in this area that DO affect the simulation are the HD
//      boosters, and three of them live here because MainLoop.h does not already
//      have them. The fast forward speed index is the fourth and it is in
//      addresses/MainLoop.h as BoosterSpeedIndex, because it changes the step
//      count rather than anything about the menu.
//
// Opening the Esc menu is also the real pause: see EscMenuIsOpen and EscMenu in
// addresses/MainLoop.h, which this file deliberately does not redeclare.
//
// The derivation, the evidence and the byte patches are in
// reversing\SETTINGS_MENU.md.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The singleton and its construction.
		//
		// The menu object is created once, lazily, by FFX_EscMenu_LoadMovie, and the
		// constructor populates EVERY page in one pass before the movie is ever shown.
		// Nothing rebuilds a page on navigation, so a mod gets exactly one chance per
		// movie load to add its rows, and the natural place to take it is the tail of
		// the constructor.
		//
		// The singleton pointer itself is EscMenu in addresses/MainLoop.h.
		// ---------------------------------------------------------------------------
		const DWORD EscMenuCtor = 0x0028DFD0;         // FFEscMenu__ctor, __thiscall
		const DWORD EscMenuLoadMovie = 0x0025F450;    // the only xref to escmenu.swf's path
		const DWORD EscMenuRebuildPages = 0x0028F3C0; // same build sequence minus the callbacks

		// ---------------------------------------------------------------------------
		// The page builders, one native function per page. All __thiscall(FFEscMenu *).
		//
		// Video is the only page with BOTH an editable spinner and a native read-back
		// path, so it is the one to use for a row the user changes. Special Feature is
		// the cheapest to build but it is a read-only text list, see the note on
		// SpecialFeatureLoopBound below.
		// ---------------------------------------------------------------------------
		const DWORD BuildVideoPage = 0x002FCCA0;           // 12 inline addVideoPageItem calls
		const DWORD BuildVideoPageEntry = 0x002FF020;      // wrapper the ctor actually calls
		const DWORD BuildAudioPage = 0x002FF770;           // addAudioPageItem, spinnerWithBar
		const DWORD BuildKeyboardPage = 0x00300DA0;        // addKeyboardPageItem
		const DWORD BuildControllerPage = 0x00301FA0;      // addControllerPageItem
		const DWORD BuildSpecialFeaturePage = 0x00302660;  // table walk, 5 rows, text only
		const DWORD BuildSpecialFeatureThunk = 0x00302560; // jmp thunk the ctor calls
		const DWORD BuildChangeParaPage = 0x002FC3F0;      // 3 inline addChangeParaPageItem calls
		const DWORD SetAllPageLabels = 0x0028E6F0;         // setHomePage, setVideoPage, setSubTitle

		// ---------------------------------------------------------------------------
		// The Iggy bridge. THIS IS THE PART THAT LETS A MOD ADD A ROW.
		//
		// IggyMenuCallAS3Function is the one to use. It takes the argument count and an
		// IggyValue array as ordinary parameters, so it can make a call of any arity.
		// The six fixed-arity wrappers below exist only because the game's own callers
		// found them convenient, and each one is a worse deal for a mod: the IggyValues
		// go by value on the stack, 16 bytes each, which means a caller has to get a
		// variadic cast exactly right for no benefit. They are listed so a hook on the
		// game's own row calls can find them.
		//
		// IggyMenuGetPlayer is needed for the choice array, because
		// IggyMakeStringArray wants the raw iggy_player handle rather than the menu.
		// ---------------------------------------------------------------------------
		const DWORD IggyMenuCallAS3Function = 0x00628BA0; // __thiscall, count+array, retn 14h
		const DWORD IggyMenuGetPlayer = 0x00628D10;       // __thiscall, *(void**)(menu+0x6C) deref
		const DWORD IggyRegisterAS3Callback = 0x00628E90; // __thiscall(menu, name, fn)
		const DWORD IggyMenuSetVisible = 0x00628EF0;      // __thiscall(menu, char), writes +0x70

		// IggyValue builders. Each writes 16 bytes and sets the tag. See IggyValue in
		// ffx/EscMenu.h for the layout and how it was confirmed.
		const DWORD IggyValueSetNumber = 0x00628D30;   // tag 4, double at +0x08
		const DWORD IggyValueSetString = 0x00628D70;   // tag 5, char* at +0x08, strlen at +0x0C
		const DWORD IggyValueSetBool = 0x00628DB0;     // tag 3, byte widened into +0x08
		const DWORD IggyValueSetArrayRef = 0x00628DD0; // tag 9, array ref at +0x08

		// The choice list for a spinnerWithList row. Constructor shaped, so the "this"
		// pointer is an 8-byte out struct and the array ref is its SECOND dword.
		const DWORD IggyMakeStringArray = 0x002FCBC0; // __thiscall, retn 0Ch

		// Deferred work. The menu cannot safely commit from inside an AS3 callback, so
		// the callbacks park a function here and it runs later on the right thread.
		// There are only 5 slots and a full queue returns 0 silently.
		const DWORD IggyMenuQueueDeferred1 = 0x00628DF0; // (ctx, fn, arg)
		const DWORD IggyMenuQueueDeferred2 = 0x00628E40; // (ctx, fn, arg, cleanupFn)

		// The fixed-arity wrappers. int __cdecl (menu, as3Name, IggyValue x N), and the
		// caller pops 8 + 16*N bytes. Listed for completeness and for hooking.
		const DWORD CallAS3_1Arg = 0x0028DD40;  // add esp, 0x18
		const DWORD CallAS3_2Args = 0x0028DD80; // add esp, 0x28
		const DWORD CallAS3_3Args = 0x0028DDD0; // add esp, 0x38
		const DWORD CallAS3_4Args = 0x0028DE50; // add esp, 0x48
		const DWORD CallAS3_5Args = 0x0028DEF0; // add esp, 0x58
		const DWORD CallAS3_6Args = 0x002FCAF0; // add esp, 0x68, the Video page one

		// ---------------------------------------------------------------------------
		// Localized text, for a row label that should follow the player's language.
		//
		// TextGetLocalizedById needs a buffer of at least 0x110 bytes, not 0x100: the
		// first 0x100 is the text and the next 16 are the IggyValue scratch that
		// IggyValueSetLocalizedText reads back out. Every one of the game's own page
		// builders declares 272 bytes for exactly this reason.
		//
		// A mod does not have to use any of this. A plain char* literal through
		// IggyValueSetString works, which is how the keyboard page passes key names.
		// ---------------------------------------------------------------------------
		const DWORD TextGetLocalizedById = 0x0028E180;      // __thiscall(buf, msgId)
		const DWORD TextFetchMessage = 0x002412F0;          // __cdecl(msgId, buf, len)
		const DWORD IggyValueSetLocalizedText = 0x0028E270; // __thiscall(textObj, IggyValue *out)

		// ---------------------------------------------------------------------------
		// The Flash-to-native callbacks, all int __cdecl (FFEscMenu *, const IggyValue *).
		//
		// Every one of these is registered in one place, EscMenuRegisterCallbacks, and
		// nothing ever unregisters. This list is the complete set of ways the movie can
		// reach native code, which matters because a mod's new row either reuses one of
		// these or registers a name of its own.
		// ---------------------------------------------------------------------------
		const DWORD EscMenuRegisterCallbacks = 0x0028FD40;

		const DWORD OnResume = 0x0028FE60;                      // EscMenuResume
		const DWORD OnQuit = 0x0028FB00;                        // EscMenuQuit
		const DWORD OnConfirmKey = 0x002FF1E0;                  // EscMenuOnConfirmKey
		const DWORD OnVideoSettingRefresh = 0x002FF4B0;         // EscMenuVideoSettingRefresh
		const DWORD OnAudioSettingRefresh = 0x002FF660;         // EscMenuAudioSettingRefresh
		const DWORD OnKeyboardSettingRefresh = 0x00301680;      // EscMenuKeyboardSettingRefresh
		const DWORD OnControllerSettingRefresh = 0x00301E60;    // EscMenuControllerSettingRefresh
		const DWORD OnStartKeyEditing = 0x003013E0;             // EscMenuStartKeyEditing
		const DWORD OnStartControllerEditing = 0x00301BE0;      // EscMenuStartControllerEditing
		const DWORD OnHelpTextRefresh = 0x0028E5B0;             // EscMenuHelpTextRefresh
		const DWORD OnButtonClick = 0x0028F610;                 // EscMenuButtonClick
		const DWORD OnPopUpConfirmed = 0x0028F8F0;              // EscMenuPopUpConfirmed
		const DWORD OnKeyboardSettingIncomplete = 0x00301660;   // EscMenuKeyboardSettingIncomplete
		const DWORD OnControllerSettingIncomplete = 0x00301E40; // EscMenuControllerSettingIncomplete
		const DWORD OnDefaultKey = 0x0028F6F0;                  // EscMenuOnDefaultKey
		const DWORD PlayEscSound = 0x0028F9E0;                  // playEscSound
		const DWORD OnMasterVolumeChanged = 0x002FF5B0;         // onMasterVolumeChanged, key 5
		const DWORD OnVoiceVolumeChanged = 0x002FF4F0;          // onVoiceVolumeChanged, key 6
		const DWORD OnMusicVolumeChanged = 0x002FF530;          // onMusicVolumeChanged, key 7
		const DWORD OnSfxVolumeChanged = 0x002FF570;            // onSfxVolumeChanged, key 8
		const DWORD OnExitAudioMenu = 0x002FF5F0;               // ExitAudioMenu

		// Not a callback, but the thing OnConfirmKey schedules: the "you have unsaved
		// changes" popup, or an immediate onCancelKey when nothing changed.
		const DWORD ShowUnsavedChangesPopUp = 0x002FF060;

		// ---------------------------------------------------------------------------
		// The Video page read-back and commit. The two functions to hook for an extra
		// row, and the three places the row count 12 is hardcoded.
		// ---------------------------------------------------------------------------

		// __thiscall(FFEscMenu *, int *outValues, const IggyValue *as3Arg). Pulls the
		// 12 row values the movie hands back. It never asks the movie how long the
		// array is, and it could not: FFX.exe imports 41 iggy_w32 entry points and not
		// one of them returns an array length.
		const DWORD ReadVideoRowValues = 0x002FED60;

		// __thiscall(FFEscMenu *, const int *values). The real commit. Calls
		// ConfigSetValue for key indices 1, 2, 9, 10, 13, 11, 12, 14, 15, 16, 17, 18,
		// sets four renderer dirty flags, then caches the 12 values back into the menu
		// object at EscMenuLayout::VideoRowCache.
		const DWORD CommitVideoSettings = 0x002FF290;

		// ---------------------------------------------------------------------------
		// Hardcoded loop bounds and buffer sizes, FOR DOCUMENTATION.
		//
		// These are the exact immediate bytes a mod would have to change to make the
		// Video page carry a 13th row. They are here so a mod can VERIFY the bytes
		// before hooking, not so it can patch blind. Patching is the worse option of
		// the two: the 12-int cache in the menu object is exactly 12 ints wide and the
		// display mode count sits immediately after it, so a 13th cached value
		// overwrites someone else's field. Hook the three functions instead.
		//
		//   RVA            current  patched  what it bounds
		//   0x002FEDCF     0x0C     0x0D     ReadVideoRowValues loop, cmp esi, 0Ch
		//   0x002FF46C     0x0C     0x0D     CommitVideoSettings cache copy, cmp eax, 0Ch
		//   0x002FF214     0x0C     0x0D     OnConfirmKey dirty compare, cmp ecx, 0Ch
		//   0x002FF1E6     0x30     0x34     OnConfirmKey scratch, push 30h -> 13 ints
		//   0x002FF4B6     0x30     0x34     OnVideoSettingRefresh scratch, push 30h
		// ---------------------------------------------------------------------------
		const DWORD VideoRowBoundRead = 0x002FEDCF;     // one byte, the 0x0C of 83 FE 0C
		const DWORD VideoRowBoundCommit = 0x002FF46C;   // one byte, the 0x0C of 83 F8 0C
		const DWORD VideoRowBoundDirty = 0x002FF214;    // one byte, the 0x0C of 83 F9 0C
		const DWORD VideoRowBufferConfirm = 0x002FF1E6; // one byte, the 0x30 of 6A 30
		const DWORD VideoRowBufferRefresh = 0x002FF4B6; // one byte, the 0x30 of 6A 30

		// The Special Feature page's bound, for comparison. 0x78 is 5 entries of 24
		// bytes, so this one is a table size rather than a row count, and the table
		// address is encoded four separate times in the builder. Hooking the builder's
		// return beats five patches.
		const DWORD SpecialFeatureLoopBound = 0x003029C6; // one byte, the 0x78 of 83 FB 78

		// ---------------------------------------------------------------------------
		// The config object and the file.
		//
		// One object, 0x63C bytes, holding all 24 options plus the three key-binding
		// arrays plus the path to its own file. ConfigObjectPtr is a POINTER TO it, not
		// the object, and it is NULL until WinMain has run.
		//
		// The file is a plain text INI at
		//   %USERPROFILE%\Documents\Square Enix\FINAL FANTASY X&X-2 HD Remaster\GameSetting.ini
		// shared with FFX-2 and Last Mission, which is why the key list has three
		// separate key-binding sections.
		//
		// FOR A CO-OP MOD: do not add keys to this file. The reader tolerates an
		// unknown key (it prints "[ConfigFile] invalid key" and carries on) but the
		// serializer emits exactly the 24 it knows, so a mod's line is deleted the next
		// time the game saves, which is every time the Esc menu opens or closes. Use a
		// separate mod ini.
		// ---------------------------------------------------------------------------
		const DWORD ConfigObjectPtr = 0x01EFB504; // void **, the single config object

		const DWORD ConfigSetValue = 0x00002A00;               // __thiscall(cfg, key, value)
		const DWORD ConfigLoadFile = 0x00001CB0;               // __thiscall(cfg)
		const DWORD ConfigSaveFile = 0x00001D80;               // __thiscall(cfg)
		const DWORD ConfigParseFile = 0x00001E00;              // __thiscall(cfg, char *text)
		const DWORD ConfigSerialize = 0x000022F0;              // __thiscall(cfg, char *out)
		const DWORD ConfigSetDefaults = 0x00002B00;            // __thiscall(cfg)
		const DWORD ConfigApplyKeyValue = 0x00001930;          // per-key parse and validate
		const DWORD ConfigConstruct = 0x00001880;              // __thiscall(cfg, wchar *path)
		const DWORD ConfigRecomputeQualityPreset = 0x00002A20; // __thiscall(cfg)
		const DWORD ConfigSerializeBindings = 0x000016E0;      // packs a binding array to text

		// The key-code to display-name lookup the Special Feature page uses for its
		// $key$ substitution is InputKeyCodeToName in addresses/Input.h, with its table
		// as KeyCodeNames there. That area owns both, so they are not repeated here.

		// ---------------------------------------------------------------------------
		// Static data behind the menu and the file.
		// ---------------------------------------------------------------------------

		// 24 char* key names, index == the config dword index. Two more copies of the
		// same pointer run exist at RVA 0x00831B38 and 0x00839688, which is why a
		// search for one of these strings gives three hits.
		const DWORD ConfigKeyNames = 0x00825010;

		const DWORD ConfigLanguageCodes = 0x00825070;      // jp en fr de it es kr ch none
		const DWORD ConfigQualityPresetTable = 0x0077CBA8; // 24 rows x 4 cols, LOW..CUSTOM
		const DWORD ConfigPresetCompareMask = 0x0077CB90;  // which keys the preset owns
		const DWORD ConfigEnumValueNames = 0x00889F48;     // 24 x 8 char*, the SM_/VQ_/PAA_ spellings
		const DWORD DefaultKeyBindingsFfx = 0x0088A538;    // 81 dwords
		const DWORD DefaultKeyBindingsFfx2 = 0x0088A3F0;   // 81 dwords
		const DWORD DefaultKeyBindingsLm = 0x0088A2A8;     // 81 dwords
		const DWORD ConfigFilePathSuffix = 0x00740EB8;     // the wide literal appended to My Documents

		// The Special Feature page's row table. 5 entries of 24 bytes:
		// +0x00 key-binding action index (28, 33, 34, 29, 31), +0x04 label msg id,
		// +0x08 the $key$ hint template msg id (656 for all five), +0x0C description
		// msg id split on "@el@", +0x10 always 0, +0x14 a further msg id the builder
		// does not read. Field 0 is an ACTION index into the binding array at config
		// +0x60, not a settings index, which is why this page is a hotkey cheat sheet
		// rather than a page of editable values.
		const DWORD EscSpecialFeatureTable = 0x0083A2C0;

		// The Audio page's row table. 4 entries of 24 bytes:
		// +0x00 channel 0..3, +0x04 label msg id (559, 553, 555, 557), +0x08 bar max
		// (100), +0x0C config key index (5, 6, 7, 8), +0x10 AS3 callback name,
		// +0x14 the live-apply function. The only other real row table in the menu.
		const DWORD EscAudioRowTable = 0x00839628;

		// ---------------------------------------------------------------------------
		// The HD boosters, the simulation-affecting part of this area.
		//
		// These three are the ones addresses/MainLoop.h does NOT already have. The
		// fourth and most important, the fast forward speed index, is BoosterSpeedIndex
		// in addresses/MainLoop.h, because it multiplies dt and therefore changes how
		// many simulation steps run per presented frame.
		//
		// All four are written in exactly one place, BoosterPollAllHotkeys, from a
		// hotkey. None of them is persisted and none of them has a config key, so there
		// is nothing in the file to sync and nothing in the file to read them back
		// from. A co-op mod has to carry them itself.
		// ---------------------------------------------------------------------------
		const DWORD BoosterPollAllHotkeys = 0x00257140; // int __cdecl(void), once per frame

		const DWORD BoosterAutoBattle = 0x00F3D6E0;    // int, 31 reads in 22 battle AI functions
		const DWORD BoosterEncounterRate = 0x008421D8; // int, 0 off 1 normal 2 high
		const DWORD BoosterInvincible = 0x00EFB7CC;    // int, read by the damage path

		// The one gameplay reader of each, so a mod can confirm the write took effect.
		//
		// The encounter-rate reader moved out. It is 0x380D10 and it is not merely a
		// reader, it is FFX_Field_StepRandomEncounter, the whole random encounter check.
		// Encounter.h owns it now as Rva::FieldStepRandomEncounter. BoosterEncounterRate
		// stays here, because the settings menu owns the booster toggles.
		const DWORD DamagePathReader = 0x00392A90; // reads BoosterInvincible four times

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so a
		// typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* EscMenuRvaList(int* count)
		{
			static const DWORD list[] = {
				EscMenuCtor,
				EscMenuLoadMovie,
				EscMenuRebuildPages,
				BuildVideoPage,
				BuildVideoPageEntry,
				BuildAudioPage,
				BuildKeyboardPage,
				BuildControllerPage,
				BuildSpecialFeaturePage,
				BuildSpecialFeatureThunk,
				BuildChangeParaPage,
				SetAllPageLabels,
				IggyMenuCallAS3Function,
				IggyMenuGetPlayer,
				IggyRegisterAS3Callback,
				IggyMenuSetVisible,
				IggyValueSetNumber,
				IggyValueSetString,
				IggyValueSetBool,
				IggyValueSetArrayRef,
				IggyMakeStringArray,
				IggyMenuQueueDeferred1,
				IggyMenuQueueDeferred2,
				CallAS3_1Arg,
				CallAS3_2Args,
				CallAS3_3Args,
				CallAS3_4Args,
				CallAS3_5Args,
				CallAS3_6Args,
				TextGetLocalizedById,
				TextFetchMessage,
				IggyValueSetLocalizedText,
				EscMenuRegisterCallbacks,
				OnResume,
				OnQuit,
				OnConfirmKey,
				OnVideoSettingRefresh,
				OnAudioSettingRefresh,
				OnKeyboardSettingRefresh,
				OnControllerSettingRefresh,
				OnStartKeyEditing,
				OnStartControllerEditing,
				OnHelpTextRefresh,
				OnButtonClick,
				OnPopUpConfirmed,
				OnKeyboardSettingIncomplete,
				OnControllerSettingIncomplete,
				OnDefaultKey,
				PlayEscSound,
				OnMasterVolumeChanged,
				OnVoiceVolumeChanged,
				OnMusicVolumeChanged,
				OnSfxVolumeChanged,
				OnExitAudioMenu,
				ShowUnsavedChangesPopUp,
				ReadVideoRowValues,
				CommitVideoSettings,
				VideoRowBoundRead,
				VideoRowBoundCommit,
				VideoRowBoundDirty,
				VideoRowBufferConfirm,
				VideoRowBufferRefresh,
				SpecialFeatureLoopBound,
				ConfigObjectPtr,
				ConfigSetValue,
				ConfigLoadFile,
				ConfigSaveFile,
				ConfigParseFile,
				ConfigSerialize,
				ConfigSetDefaults,
				ConfigApplyKeyValue,
				ConfigConstruct,
				ConfigRecomputeQualityPreset,
				ConfigSerializeBindings,
				ConfigKeyNames,
				ConfigLanguageCodes,
				ConfigQualityPresetTable,
				ConfigPresetCompareMask,
				ConfigEnumValueNames,
				DefaultKeyBindingsFfx,
				DefaultKeyBindingsFfx2,
				DefaultKeyBindingsLm,
				ConfigFilePathSuffix,
				EscSpecialFeatureTable,
				EscAudioRowTable,
				BoosterPollAllHotkeys,
				BoosterAutoBattle,
				BoosterEncounterRate,
				BoosterInvincible,
				DamagePathReader,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
