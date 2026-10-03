#include "ModState.h"

#include "clones/CloneSpawner.h"
#include "workshop/Log.h"
#include "workshop/Settings.h"

namespace pilgrimage
{

	// The mod plumbing: logging and the status line.
	using namespace workshop;

	Settings settings;
	Requests requests;
	Telemetry telemetry;
	Counters counters;

	void ApplyDefaultSettings()
	{
		// m_runThreshold is 18.0, and FFX_Ch_AutoLocomotionAnim plays walk at or
		// below it and run above it, so walkSpeed and runSpeed pick the gait as well
		// as the speed. There is nothing else to drive.
		settings.walkSpeed = 12.0f;
		settings.runSpeed = 30.0f;
		settings.yawBiasDegrees = 0.0f;

		settings.inputEnabled = 1;
		settings.requireForeground = 1;
		settings.cameraRelative = 1;
		settings.inputSource = InputSource::Auto;
		settings.padSlot = 1; // slot 0 is player 1's, so slot 1 is ours

		settings.spawnIdIndex = 0;      // SpawnableChrIds[0] is 1, Tidus
		settings.spawnPartyIndex = 255; // not a party member
		settings.spawnOffset = 3.0f;    // was 60.0, which could put it out of view
		settings.autoBindWalkmesh = 1;

		settings.forceVisible = 0;
		settings.disableCull = 0;

		// Above the ephemeral range Windows hands out, and not a port anything else
		// is likely to want.
		settings.hostPort = 27960;

		// Loopback by default, because that is the one that works with no setup at
		// all. Steam is the shipping path but it needs a SteamID typed in first.
		settings.useSteam = 0;
		settings.hostSteamId[0] = 0;

		telemetry.spawnedChrId = 0;
		SetStatus("not spawned");
	}

	// ---------------------------------------------------------------------------
	// The settings table.
	//
	// One row per field of Settings. The scope column is the interesting one: it
	// records whether a setting changes the simulation, because in a lockstep
	// session anything that does has to match on both machines or the two games
	// drift apart. Anything that only changes what this machine shows or which
	// device this machine listens to is local and deliberately not synced, which is
	// what the co-op design asks for.
	//
	// Keys are what the file is written with, so they are stable forever. Renaming
	// one silently drops a player's saved value.
	// ---------------------------------------------------------------------------

	namespace
	{

		void Register(const char* key, const char* label, SettingKind kind, SettingScope scope,
		    void* storage, float minimum, float maximum, float step,
		    const char* const* choices, int textCapacity)
		{
			Setting record;
			record.key = key;
			record.label = label;
			record.kind = kind;
			record.scope = scope;
			record.storage = storage;
			record.minimum = minimum;
			record.maximum = maximum;
			record.step = step;
			record.choices = choices;
			record.textCapacity = textCapacity;
			RegisterSetting(record);
		}

// These keep the table below readable as a table. Bool and enum ranges come from
// the registry rather than from here, so there is nothing to get wrong.
#define SETTING_BOOL(field, key, label, scope) \
	Register(key, label, SettingBool, scope, (void*)&settings.field, 0, 0, 0, NULL, 0)

#define SETTING_ENUM(field, key, label, scope, choices) \
	Register(key, label, SettingEnum, scope, (void*)&settings.field, 0, 0, 0, choices, 0)

#define SETTING_INT(field, key, label, scope, lo, hi, step)         \
	Register(key, label, SettingInt, scope, (void*)&settings.field, \
	    (float)(lo), (float)(hi), (float)(step), NULL, 0)

#define SETTING_FLOAT(field, key, label, scope, lo, hi, step)         \
	Register(key, label, SettingFloat, scope, (void*)&settings.field, \
	    (float)(lo), (float)(hi), (float)(step), NULL, 0)

// Text carries its own buffer size, taken from the array itself so the two can
// never disagree.
#define SETTING_TEXT(field, key, label, scope)                                     \
	Register(key, label, SettingText, scope, (void*)settings.field, 0, 0, 0, NULL, \
	    (int)sizeof(settings.field))

		const char* const InputSourceChoices[] = { "auto", "keyboard", "pad", NULL };

	} // namespace

	const wchar_t* const ModSettingsFileName = L"pilgrimage_together.txt";

	void RegisterModSettings()
	{
		// Spawning. All of these decide what gets created and where, so all of them
		// move the simulation.
		SETTING_INT(spawnIdIndex, "spawn.chr_id_index", "character id",
		    SettingShared, 0, SpawnableChrIdCount - 1, 1);
		SETTING_INT(spawnPartyIndex, "spawn.party_index", "party slot, 255 for none",
		    SettingShared, 0, 255, 1);
		SETTING_FLOAT(spawnOffset, "spawn.offset", "spawn distance to the side",
		    SettingShared, 0.0f, 200.0f, 1.0f);
		SETTING_BOOL(autoBindWalkmesh, "spawn.auto_bind_walkmesh", "re-bind to the walkmesh",
		    SettingShared);

		// Movement. m_runThreshold is 18.0 and the gait follows the speed, so these
		// two pick the animation as well as the velocity. Both move the simulation.
		SETTING_FLOAT(walkSpeed, "move.walk_speed", "walk speed",
		    SettingShared, 0.0f, 18.0f, 1.0f);
		SETTING_FLOAT(runSpeed, "move.run_speed", "run speed",
		    SettingShared, 0.0f, 120.0f, 2.0f);
		SETTING_FLOAT(yawBiasDegrees, "move.yaw_bias", "camera yaw bias, degrees",
		    SettingShared, -180.0f, 180.0f, 15.0f);

		// Input. Which device this machine listens to, and whether it listens at all,
		// is this machine's business and nobody else's. This is the group the design
		// had in mind when it said settings are deliberately not synced.
		SETTING_BOOL(inputEnabled, "input.enabled", "input drives the clone", SettingLocal);
		SETTING_BOOL(requireForeground, "input.require_foreground", "only when the game has focus",
		    SettingLocal);
		SETTING_BOOL(cameraRelative, "input.camera_relative", "input is relative to the camera",
		    SettingLocal);
		SETTING_ENUM(inputSource, "input.source", "input source", SettingLocal, InputSourceChoices);
		SETTING_INT(padSlot, "input.pad_slot", "Phyre pad slot", SettingLocal, 0, 17, 1);

		// Rendering diagnostics. These change what this machine draws and nothing
		// else, so they never need to agree with the other machine.
		SETTING_BOOL(forceVisible, "render.force_visible", "force clones visible", SettingLocal);
		SETTING_BOOL(disableCull, "render.disable_cull", "hold the cull override on", SettingLocal);

		// Networking. The port is local plumbing, not a rule of the session, so it is
		// deliberately NOT shared. If it were, changing it would refuse the very
		// connection it was changed to make.
		SETTING_INT(hostPort, "net.host_port", "host UDP port", SettingLocal, 1024, 65535, 1);

		// The host's SteamID, the 17 digit number from their profile. Text because a
		// 64-bit id does not survive a float. Local rather than shared: each client
		// stores the host's id and the host stores nothing, so requiring the two to
		// match would refuse the very connection it was set up to make.
		SETTING_BOOL(useSteam, "net.use_steam", "use Steam P2P instead of loopback",
		    SettingLocal);
		SETTING_TEXT(hostSteamId, "net.host_steam_id", "host SteamID", SettingLocal);

		Log("settings: registered %d settings, shared hash %08lX",
		    SettingCount(), SharedSettingsHash());
	}

	void LoadModSettings()
	{
		LoadSettings(ModSettingsFileName);

		// The index is stored rather than the id, so a build that changes the
		// spawnable list could leave it pointing past the end. The registry already
		// range checks on load, but the list length is a build detail rather than a
		// declared constant, so say out loud which id we ended up on.
		Log("settings: spawn id index %ld, shared hash %08lX",
		    settings.spawnIdIndex, SharedSettingsHash());
	}

	void SaveModSettingsIfChanged()
	{
		static unsigned long lastSaved = 0;

		const unsigned long now = SettingsRevision();
		if (now == lastSaved)
			return;
		lastSaved = now;

		if (!SaveSettings(ModSettingsFileName))
			Log("settings: save failed, the next change will try again");
	}

} // namespace pilgrimage
