#pragma once

#include <windows.h>

// The Esc menu and the PC config file, typed.
//
// Three callers are designed for, in this order.
//
//   1. A mod that reads or writes the 24 PC options. ReadConfigValue and
//      WriteConfigValue, with ConfigKey naming every index. Writes go through
//      the game's own setter so nothing is bypassed.
//   2. A co-op mod that needs to know what has to match between machines.
//      ConfigKeyAffectsSimulation answers it for the file, and the three booster
//      accessors here plus BoosterSpeedIndex in ffx/MainLoop.h are the things
//      that actually do.
//   3. A mod that wants its own rows in the game's Esc menu. CallAS3 and the
//      IggyValue builders are the whole kit for that, and AddVideoPageRow and
//      AddSpecialFeaturePageRow are ready-made versions of the two calls worth
//      making.
//
// Every raw pointer read goes through workshop::Readable first, so a stale
// pointer returns false rather than taking the game down. The config object is
// reached through a pointer that is NULL until WinMain has run, and the menu
// object does not exist until the player opens the menu for the first time, so
// both are checked on every call rather than cached.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DUPLICATE. ffx/MainLoop.h owns the
// pause side of the Esc menu: EscMenuIsOpen() is the "is it on screen" test,
// and EscMenuLayout::OpenFlag and EscMenuLayout::OpenIndex are the two fields it
// reads. It also owns the fast forward booster, BoosterSpeedIndex and
// BoosterSpeedMultiplier, because that one changes how many simulation steps run
// per frame rather than anything about the menu. Use those, not copies.
//
// The derivation is in ..\..\..\reversing\SETTINGS_MENU.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// The 24 persisted options.
	//
	// They are one contiguous block of 24 dwords at the start of the config object,
	// and the index here is the same number the file parser, the serializer and
	// FFX_Config_SetValue all use. Read it as "config key 13 is TextureQuality" and
	// nothing else.
	// ---------------------------------------------------------------------------
	enum ConfigKey
	{
		kConfigLanguage = 0,
		kConfigResolution = 1, // (width << 16) | height, see ResolutionWidth
		kConfigScreenMode = 2, // SM_WINDOW, SM_FULLSCREEN, SM_BORDERLESS
		kConfigQuality = 3,    // VQ_LOW, VQ_MEDIUM, VQ_HIGH, VQ_CUSTOM
		kConfigMute = 4,
		kConfigMasterVolume = 5,
		kConfigVoiceVolume = 6,
		kConfigMusicVolume = 7,
		kConfigSfxVolume = 8,
		kConfigBrightness = 9, // 0..50, the one bar row on the Video page
		kConfigVSync = 10,
		kConfigAnisotropic = 11, // 1x 2x 4x 8x
		kConfigShadow = 12,
		kConfigTextureQuality = 13, // TQ_HIGH, TQ_MEDIUM, TQ_LOW
		kConfigMsaa = 14,           // 0, 2, 4, 8
		kConfigColorCorrection = 15,
		kConfigPostAa = 16, // PAA_OFF, PAA_FXAA, PAA_SMAA1..4
		kConfigUnSharpMask = 17,
		kConfigHdao = 18,
		kConfigInvincible = 19,    // persisted, and NOTHING IN FFX.exe READS IT
		kConfigEncounterRate = 20, // persisted, and NOTHING IN FFX.exe READS IT
		kConfigFfxKeyBinding = 21, // serialized from the binding array, not a value
		kConfigFfx2KeyBinding = 22,
		kConfigLmKeyBinding = 23,
		kConfigKeyCount = 24
	};

	// Key indices 19 and 20 are a trap. The file has Invincible and EncounterRate,
	// the serializer writes them, and no code in FFX.exe reads cfg[19] or cfg[20].
	// The live booster states are separate globals that only the hotkey handler
	// writes. The likely explanation is that the two keys belong to FFX-2.exe, which
	// shares the file. DO NOT TREAT THEM AS FREE SCRATCH SPACE for mod data, and do
	// not expect writing them to turn a booster on.

	// ---------------------------------------------------------------------------
	// Field offsets inside the config object. 0x63C bytes, one instance, created in
	// WinMain and never freed.
	// ---------------------------------------------------------------------------
	namespace ConfigLayout
	{
		const int Values = 0x000;         // int[24], indexed by ConfigKey
		const int FfxBindings = 0x060;    // int[81], what the Special Feature page indexes
		const int Ffx2Bindings = 0x1A4;   // int[81]
		const int LmBindings = 0x2E8;     // int[81]
		const int BindingCount = 0x42C;   // int, 81
		const int FilePath = 0x430;       // wchar_t[260], fed straight to _wfopen
		const int DefaultsFlag16 = 0x638; // WORD, 0x0101 when no file was read
		const int DefaultsFlag8 = 0x63A;  // BYTE, 1 when no file was read
		const int Size = 0x63C;

		const int BindingSlots = 81;
	}

	// ---------------------------------------------------------------------------
	// Field offsets inside the FFEscMenu object. Only the ones a mod needs, and
	// deliberately not the two ffx/MainLoop.h already owns: the visible byte at
	// 0x70 is EscMenuLayout::OpenFlag there and the open index at 0xE4 is
	// EscMenuLayout::OpenIndex.
	//
	// The first 0x64 bytes of the object are the deferred-call queue that the AS3
	// callbacks park work in, 5 slots of 20 bytes, which is why the Iggy player
	// pointer sits as far in as 0x6C.
	// ---------------------------------------------------------------------------
	namespace EscMenuFields
	{
		const int PlayerHolder = 0x6C;  // void **, the player handle is one more deref
		const int VideoRowCache = 0xA4; // int[12], EXACTLY 12, no slack
		const int ModeCount = 0xD4;     // int, display modes the adapter reported
		const int ModeIndex = 0xD8;     // int, selected mode, -1 before the page builds
		const int ModeList = 0xDC;      // entry stride 28, width at +0, height at +4

		// The row cache is 12 ints at 0xA4 and ModeCount is at 0xD4, which is
		// 0xA4 + 48, the byte immediately after it. So a 13th cached row value would
		// overwrite the display mode count. That is why the typed layer offers no
		// "widen the cache" helper: there is nowhere to widen it to, and a mod that
		// wants a 13th row has to hold that row's value itself.
		const int VideoRowCacheCount = 12;

		// One display mode. The Resolution row's choice list is built from these.
		const int ModeEntrySize = 28;
		const int ModeEntryWidth = 0x00;  // dword, only the low word is persisted
		const int ModeEntryHeight = 0x04; // dword, only the low word is persisted
	}

	// ---------------------------------------------------------------------------
	// An IggyValue. 16 bytes, PASSED BY VALUE, and the thing most likely to be got
	// wrong by a caller.
	//
	// HOW THIS WAS CONFIRMED, since a wrong struct here means a corrupt stack:
	//
	//   Writing side. The three builders at RVA 0x00628D30, 0x00628D70 and
	//   0x00628DD0 each write a tag dword at +0x00 and then one payload. The number
	//   builder is fild [arg] / fstp dword / fld dword / fstp qword ptr [obj+8], so
	//   the payload is a DOUBLE at +0x08 and the int went through 32-bit float on
	//   the way. The string builder writes the char* to +0x08 and strlen to +0x0C.
	//   The array builder writes the ref to +0x08. NONE of them writes +0x04.
	//
	//   Reading side, independently. The AS3 callback for the master volume slider
	//   does (int)*(double *)(args + 8) to get the new value out of the first
	//   argument. Same 16 bytes, same double at +0x08, arrived at from the opposite
	//   direction.
	//
	//   Size. All twelve addVideoPageItem call sites do add esp, 0x68 afterwards,
	//   and 0x68 is 8 bytes of (menu, name) plus 6 * 16.
	//
	// The number field being a double and not an int is the part that bites. Pass a
	// row index or a small enum and it is exact. Pass a pointer, a handle or a hash
	// and anything above 2^24 is silently rounded by the game's own builder, because
	// it narrows to float first. SetIggyNumber below does not, see the comment there.
	// ---------------------------------------------------------------------------
	struct IggyValue
	{
		DWORD tag;      // kIggyTag* below
		DWORD reserved; // the game never writes this, so neither do we
		union
		{
			double number; // tag 4
			DWORD boolean; // tag 3, a byte widened into the first dword
			struct
			{
				const char* text; // tag 5, NOT copied, must outlive the call
				DWORD length;
			} string;
			struct
			{
				DWORD ref; // tag 9
				DWORD unused;
			} array;
		} payload;
	};

	const DWORD kIggyTagBool = 3;
	const DWORD kIggyTagNumber = 4;
	const DWORD kIggyTagString = 5;
	const DWORD kIggyTagArray = 9;

	// Fill one. These do what the game's builders do, in our own code, so a caller
	// does not pay a call into the host for a struct store, and so the reserved
	// dword gets zeroed, which the game's builders do not bother to do.
	void SetIggyNumber(IggyValue* v, int value);
	void SetIggyBool(IggyValue* v, bool value);
	void SetIggyString(IggyValue* v, const char* text); // text must outlive the call
	void SetIggyArray(IggyValue* v, DWORD arrayRef);

	// ---------------------------------------------------------------------------
	// Typed pointers to the game functions this area needs.
	//
	// MSVC does allow __thiscall on a function pointer for x86 and the convention is
	// exactly "first argument in ecx, the rest on the stack", which is what these
	// are. workshop/SteamAbi.h relies on the same thing.
	// ---------------------------------------------------------------------------

	// The one AS3 call primitive worth using. resultPath is always 0 and reserved is
	// always 1 at every one of the game's own call sites. argCount and args are
	// ordinary parameters, so this makes a call of any arity without the variadic
	// casting the fixed-arity wrappers force on a caller. The callee pops the five
	// stack arguments.
	typedef int(__thiscall* IggyCallAS3Fn)(void* escMenu, void* resultPath,
	    const char* as3Name, int argCount,
	    const IggyValue* args, int reserved);

	// The iggy_player handle, which the string-array builder wants instead of the
	// menu. Two instructions in the host: *(void **)(*(void **)(menu + 0x6C)).
	typedef void*(__thiscall* IggyGetPlayerFn)(void* escMenu);

	// The choice array for a spinnerWithList row. This is a C++ constructor, so the
	// first parameter is the out struct and the return is the same pointer. Pull the
	// array ref out of IggyArrayRef::ref, which is the SECOND dword, and nothing
	// else. The strings are copied into the movie, so unlike a string IggyValue they
	// do not have to outlive the call.
	struct IggyArrayRef
	{
		void* player;
		DWORD ref;
	};
	typedef IggyArrayRef*(__thiscall* IggyMakeStringArrayFn)(IggyArrayRef* out, void* player,
	    const char** strings, int count);

	// Register a native handler the movie can call. Nothing ever unregisters, so
	// call it once per movie load and keep the handler alive for the process.
	typedef int(__thiscall* IggyRegisterCallbackFn)(void* escMenu, const char* as3Name,
	    void* handler);

	// What the movie calls. The second argument is the argument array, so
	// args[0].payload.number is the first AS3 argument when it is a number.
	typedef int(__cdecl* As3CallbackFn)(void* escMenu, const IggyValue* args);

	// cfg[keyIndex] = value, and that is the whole body. The single universal setter
	// for all 24 options, 19 callers, every one of them in the config loader or an
	// Esc menu commit handler. Hooking it hands you the key index and the new value
	// with no guessing.
	typedef int(__thiscall* ConfigSetValueFn)(void* cfg, int keyIndex, int value);

	// The page builders, for a detour that appends rows on return.
	typedef int(__thiscall* EscMenuBuildPageFn)(void* escMenu);

	// ---------------------------------------------------------------------------
	// Getting at the two objects.
	//
	// Both return NULL rather than a bad pointer, and both check the pointer they
	// followed, so a caller can use the result directly.
	// ---------------------------------------------------------------------------

	// The config object, or NULL before WinMain has built it. Validated for the full
	// 0x63C bytes, so a partially constructed object reads as absent.
	BYTE* ConfigObject();

	// The live FFEscMenu singleton, or NULL when the player has never opened the
	// menu. Non-NULL does NOT mean the menu is on screen, use ffx::EscMenuIsOpen()
	// from ffx/MainLoop.h for that.
	BYTE* EscMenuPtr();

	// The iggy_player handle behind the menu, or NULL. Needed only for
	// MakeChoiceArray, which is the one piece of the Iggy API that wants the player
	// rather than the menu.
	void* EscMenuIggyPlayer();

	// ---------------------------------------------------------------------------
	// Reading and writing the 24 options.
	// ---------------------------------------------------------------------------

	// Reads one option. Returns false for an out of range index or when the config
	// object is not there yet, and leaves out alone in both cases.
	bool ReadConfigValue(int keyIndex, int* out);

	// Writes one option THROUGH THE GAME'S OWN SETTER, so if a future patch ever
	// gives that function side effects we get them too. Returns false for a bad
	// index, a missing object, or a setter that does not look callable.
	//
	// This only touches memory. The file is written by the Esc menu, when it opens
	// and again when it closes, so a value written here reaches the file only if the
	// player opens the menu afterwards. Call SaveConfigFile to force it.
	bool WriteConfigValue(int keyIndex, int value);

	// The literal key name as it appears in GameSetting.ini, read out of the game's
	// own 24-entry pointer table rather than hardcoded here, so it cannot drift from
	// what the parser accepts. NULL for a bad index or an unreadable table.
	const char* ConfigKeyName(int keyIndex);

	// The path the game uses, out of the config object at +0x430. Written into out
	// as a NUL terminated wide string. The field holds 260 wide characters, so 260
	// is the most a caller ever needs. Returns false and leaves out alone when the
	// object is absent.
	bool ConfigFilePath(wchar_t* out, int count);

	// Resolution is packed, so these three spare every caller the same shift.
	inline int ResolutionWidth(int packed)
	{
		return (packed >> 16) & 0xFFFF;
	}
	inline int ResolutionHeight(int packed)
	{
		return packed & 0xFFFF;
	}
	inline int PackResolution(int width, int height)
	{
		return ((width & 0xFFFF) << 16) | (height & 0xFFFF);
	}

	// Writes the file now, rather than waiting for the Esc menu to do it. Use this
	// sparingly: the serializer runs twice per save (once to measure, once to fill)
	// and a bad write loses the player's graphics settings.
	bool SaveConfigFile();

	// ---------------------------------------------------------------------------
	// What has to match between machines.
	//
	// ALL 24 PERSISTED KEYS ARE PRESENTATION ONLY. Language, resolution, screen
	// mode, quality, the four volumes, mute, brightness, vsync, anisotropic,
	// shadows, texture quality, MSAA, colour correction, post AA, unsharp mask and
	// HDAO change what one player sees and hears and nothing else. The three
	// key-binding arrays are per-player by definition. Keys 19 and 20 look like
	// cheats but nothing reads them. So the config file never needs to be synced and
	// a co-op session must not try, because forcing a host's resolution onto a
	// client is a bug, not a feature.
	//
	// The four things in this area that DO change the simulation are the HD
	// boosters, and none of them is in the file.
	// ---------------------------------------------------------------------------

	// Always false, for all 24 indices, and that is the point: a caller can drive a
	// sync table off this rather than off a comment. Returns false for an out of
	// range index too, which is the safe answer.
	bool ConfigKeyAffectsSimulation(int keyIndex);

	// ---------------------------------------------------------------------------
	// The boosters that are not already in ffx/MainLoop.h.
	//
	// The fourth and most important one, the fast forward speed index, is
	// BoosterSpeedIndex and BoosterSpeedMultiplier in ffx/MainLoop.h, because it
	// multiplies the frame delta and so changes how many simulation steps run. The
	// three here change what the simulation DOES rather than how fast it runs.
	//
	// All three are plain ints written in exactly one place, the once-per-frame
	// hotkey poll, and none of them is persisted. Each reader returns false when the
	// global is not readable, which in practice means a bad module binding rather
	// than a real runtime condition, since all three are in static data.
	//
	// Every writer here is a bare store, which is fine precisely because the game
	// only ever writes them from the hotkey poll and never caches them. Hold the
	// value on both machines and write it on both, or make it host authoritative.
	// ---------------------------------------------------------------------------

	// Read by 31 instructions across 22 distinct battle functions, nearly all of
	// them "cmp g_boosterAutoBattle, 1" inside command selection. It decides who
	// issues battle commands, so a mismatch makes the two sides pick different
	// actions on the same turn.
	bool BoosterAutoBattle(int* out);
	bool SetBoosterAutoBattle(int value);

	// Read by exactly one gameplay site, the encounter roll. 0 off, 1 normal,
	// 2 high. A mismatch means one side walks into a battle and the other does not,
	// which is an immediate desync rather than a cosmetic difference.
	bool BoosterEncounterRate(int* out);
	bool SetBoosterEncounterRate(int value);

	// Read four times by the damage path. A mismatch means different HP on the two
	// sides from the first hit onward.
	bool BoosterInvincible(int* out);
	bool SetBoosterInvincible(int value);

	// ---------------------------------------------------------------------------
	// Adding our own rows to the game's menu. The headline feature.
	//
	// HOW IT WORKS. Native code builds each page by calling one AS3 function per
	// row, with the row index as the first argument. Nothing on the native side
	// holds a maximum and nothing on the native side ever asks the movie how many
	// rows a page has, which is not a guess: FFX.exe imports 41 iggy_w32 entry
	// points and not one of them returns an array length, and the only read-back
	// loop there is runs a hardcoded 0..11. So native is completely count-blind and
	// row indices are whatever we say they are.
	//
	// WHEN TO CALL. The constructor builds every page once, before the movie is
	// shown, and nothing rebuilds a page on navigation. So the place to add rows is
	// a detour on the tail of the constructor (Rva::EscMenuCtor) or on the return of
	// one page builder, and a mod gets exactly one chance per movie load.
	//
	// WHAT IS STILL UNPROVEN. Whether escmenu.swf's addVideoPageItem APPENDS a row
	// or fills one of a fixed number of pre-placed row instances. That answer is in
	// the ActionScript inside the Iggy asset and it cannot be read out of the exe.
	// The native side strongly suggests appending: the Video page emits 11 or 12
	// rows depending on whether the adapter reported a mode list, the Resolution
	// row's choice list is however long the adapter says, and the pages range from 3
	// to 23 rows and all scroll. But it is an inference. Test it before budgeting on
	// it, and AddVideoPageRow is written so that test is three lines from a detour.
	//
	// WHICH PAGE. Video for anything the user changes, Special Feature for anything
	// we only want to tell them. The reason is not cost but whether the value comes
	// back: there is no addSpecialFeature refresh callback in the registration list,
	// so the movie never reports a Special Feature row's state to native.
	//
	// THE CATCH ON VIDEO. The read-back loop, the commit cache copy and the
	// unsaved-changes compare all hardcode 12, and the cache in the menu object is
	// exactly 12 ints wide with the display mode count immediately after it. So an
	// extra Video row needs those three functions hooked, not the bounds patched.
	// The exact bytes are listed in addresses/EscMenu.h for verification.
	// ---------------------------------------------------------------------------

	// The first row index the game's own builders leave free. The Video builder
	// always passes indices up to 11, whether it emitted 11 rows or 12, so 12 is
	// free in both cases. The Special Feature builder walks a 5-entry table.
	const int kFirstFreeVideoRowIndex = 12;
	const int kFirstFreeSpecialFeatureRowIndex = 5;

	// The AS3 functions the game itself calls to make a row. Here as string
	// constants so a caller is not retyping them.
	extern const char* const kAs3AddVideoPageItem; // 6 args
	extern const char* const kAs3AddAudioPageItem; // spinnerWithBar rows
	extern const char* const kAs3AddKeyboardPageItem;
	extern const char* const kAs3AddControllerPageItem;
	extern const char* const kAs3AddSpecialFeaturePageItem; // 5 args, all strings
	extern const char* const kAs3AddChangeParaPageItem;     // 4 args, spinnerButton

	// The widget type strings the Video page passes in argument 2.
	extern const char* const kWidgetSpinner;         // a plain left/right choice list
	extern const char* const kWidgetSpinnerWithList; // choices come from the array argument
	extern const char* const kWidgetSpinnerWithBar;  // a 0..barMax slider
	extern const char* const kWidgetSpinnerButton;   // the Change Parameters page's row
	extern const char* const kWidgetKeyboardBinding;
	extern const char* const kWidgetControllerBinding;

	// Call any AS3 function in escmenu.swf. The general primitive: pass as many
	// IggyValues as the function takes. Returns false when the menu does not exist,
	// when the host function does not look callable, or when args and argCount
	// disagree.
	//
	// Safe to call only while the menu object exists, which for a row builder means
	// from a detour on a page builder or on the constructor's tail. Calling it from
	// an arbitrary frame is not known to be safe and was not tested. It can also
	// block briefly: the host turns the name into an Iggy fast name in a retry loop
	// that calls Sleep(5) until it succeeds.
	bool CallAS3(const char* as3Name, const IggyValue* args, int argCount);

	// Build a choice array for a spinnerWithList row. The ref it returns is what
	// SetIggyArray wants. Returns 0 on failure.
	//
	// NOTHING IN THE GAME FREES THESE. IggyValueRefFree is only ever called on the
	// read-back path, so the game already leaks one array per Video page build.
	// Build yours once and keep the ref rather than rebuilding it per row.
	DWORD MakeChoiceArray(const char** strings, int count);

	// One ready-made addVideoPageItem call. Start at kFirstFreeVideoRowIndex.
	//
	// choiceArrayRef may be 0 for a widget that does not need choices, and barMax
	// should be -1 for anything that is not kWidgetSpinnerWithBar, which is what the
	// game passes on its own non-bar rows. label and currentValueText are NOT copied
	// by the host, so they must stay alive until this returns, which for a literal
	// is always.
	bool AddVideoPageRow(int rowIndex, const char* widgetType, const char* label,
	    const char* currentValueText, DWORD choiceArrayRef, int barMax);

	// One ready-made addSpecialFeaturePageItem call. Five strings, no array, no
	// widget type, and the description really is two separate arguments because the
	// game splits its own description message on the literal "@el@" and passes the
	// halves. A row added here is text only, the movie will never report it back.
	bool AddSpecialFeaturePageRow(int rowIndex, const char* label, const char* hint,
	    const char* descLine1, const char* descLine2);

	// How many rows the game's own Video page builder emitted, 11 or 12, derived
	// from the display mode list pointer in the menu object rather than assumed.
	// Returns -1 when the menu does not exist. This is a row COUNT and not a free
	// index, because in the 11-row case the builder still used indices 1..11 and
	// skipped 0. For the index to pass, use kFirstFreeVideoRowIndex.
	int VideoRowsBuilt();

	// Register a native handler for an AS3 name of our own, so a new interactive row
	// can report back without hooking the game's own refresh callbacks. Returns
	// false when the menu does not exist.
	//
	// Call this at most once per name per movie load. Nothing unregisters and the
	// handler must outlive the process.
	bool RegisterAS3Callback(const char* as3Name, As3CallbackFn handler);

	// ---------------------------------------------------------------------------
	// Diagnostics.
	// ---------------------------------------------------------------------------

	// Logs the config object address, all 24 values with their key names, the file
	// path, the menu object state and the three booster globals. Worth calling once
	// at startup to confirm the bindings landed on something sensible, and again
	// when a player reports the menu behaving oddly.
	void LogEscMenuState();

} // namespace ffx
