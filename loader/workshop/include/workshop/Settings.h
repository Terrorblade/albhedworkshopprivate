#pragma once

#include <stddef.h>

// A described settings registry: the list of a mod's options, with enough
// metadata attached that something other than the code which declared them can
// read them, write them, draw them and compare them.
//
// Why a registry instead of a plain struct of globals. Four things need the same
// list and none of them should hardcode it:
//
//   the control panel      draws a row per setting
//   persistence            writes key=value and reads it back
//   the game's own menu     if its option list turns out to be extensible, it
//                          needs labels, ranges and a way to step a value
//   the network layer      has to know which settings must match on both peers
//                          and which are nobody else's business
//
// That last one is the reason this exists in this shape. The co-op design says
// game settings are deliberately not synced, except where one is game-changing.
// So every setting carries a scope, declared where the setting is declared, and
// the sync layer asks the registry rather than keeping its own list that drifts.
//
// The registry does not own any storage. Each record points at a global the mod
// already has, so registering a setting changes nothing about how the game
// thread reads it. That keeps the hot path exactly as cheap as it was.
//
// Thread notes. Registration happens once at startup before any other thread
// exists. After that, reads are safe from anywhere. Writes come from the UI
// thread and are single aligned stores, same as the direct writes they replace.

namespace workshop
{

	// What a setting means for a networked session. Declared per setting, because
	// "is this one game-changing" is a judgement call best made next to the setting
	// rather than in a list somewhere else.
	enum SettingScope
	{
		SettingLocal,  // never leaves this machine, for example which pad drives who
		SettingShared, // has to match on every peer, for example difficulty
	};

	enum SettingKind
	{
		SettingBool,  // storage is volatile long, 0 or 1
		SettingInt,   // storage is volatile long
		SettingFloat, // storage is volatile float
		SettingEnum,  // storage is volatile long, an index into choices
		// storage is a char buffer of textCapacity bytes, always NUL terminated.
		// Added for the one thing a number cannot hold: a 64-bit SteamID, which
		// does not fit in a float without losing digits. Keep text settings to
		// single-line values, since the settings file is key=value per line.
		SettingText,
	};

	struct Setting
	{
		const char* key;   // stable name used in the settings file. Never rename one
		                   // of these, a saved file keyed on it will stop loading.
		const char* label; // what a menu or panel shows
		SettingKind kind;
		SettingScope scope;
		void* storage; // volatile long * except for SettingFloat
		float minimum;
		float maximum;
		float step;                 // one nudge from a menu. Ignored for bool and enum.
		const char* const* choices; // SettingEnum only, NULL terminated
		int textCapacity;           // SettingText only, the size of the buffer
	};

	// Registration. The key, the label, the choices and the storage must all outlive
	// the process, so point them at string literals and globals. Returns false if
	// the table is full or the record is malformed, and logs why.
	bool RegisterSetting(const Setting& setting);

	int SettingCount();
	const Setting* SettingAt(int index);
	const Setting* FindSetting(const char* key);

	// Reading and writing without knowing which field you have. Everything goes
	// through float, which is exact for every range a menu would ever expose. An int
	// setting bigger than about 16 million would lose precision, so do not put one
	// in the registry.
	float ReadSetting(const Setting& setting);
	void WriteSetting(const Setting& setting, float value);

	// One nudge in either direction. Numbers clamp at the ends, enums and bools wrap,
	// which is what a menu row that cycles on a single button wants. Does nothing to
	// a text setting, since there is no sensible next value.
	void StepSetting(const Setting& setting, int direction);

	// Text settings. Reading one is always safe and never returns NULL, so it can go
	// straight into a printf. Reading a non-text setting as text gives an empty
	// string rather than garbage.
	const char* ReadSettingText(const Setting& setting);
	void WriteSettingText(const Setting& setting, const char* value);

	// The value as a menu would print it: "on", "off", an enum's choice text, or the
	// number. Returns out.
	const char* FormatSetting(const Setting& setting, char* out, size_t count);

	// Bumped by every WriteSetting and StepSetting. Poll it to decide whether a save
	// is due, which is cheaper and less fragile than saving on process shutdown.
	unsigned long SettingsRevision();

	// Persistence, as a plain key=value text file at <game dir>\AlBhedWorkshop\<fileName>.
	//
	// Load applies only keys it recognises and only values inside the declared range,
	// so an old or hand-edited file degrades to defaults for the parts it gets wrong
	// instead of refusing to load or writing a nonsense value. Unknown keys are
	// logged and skipped. Call it after the mod has applied its defaults, so
	// anything absent from the file keeps the default.
	bool LoadSettings(const wchar_t* fileName);
	bool SaveSettings(const wchar_t* fileName);

	// A hash over every SettingShared value, so two peers can tell in one comparison
	// whether their game-changing settings agree. Local settings are not included,
	// which is the whole point. The accumulation is order independent, so two builds
	// that registered in a different order still agree when the values do.
	unsigned long SharedSettingsHash();

} // namespace workshop
