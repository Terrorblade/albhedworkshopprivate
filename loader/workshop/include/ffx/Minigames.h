#pragma once

// Starting a minigame, and editing the one that is running.
//
//     ffx::Minigame now = ffx::CurrentMinigame();
//     const ffx::MinigameLaunch* l = ffx::MinigameLaunchAt(i);
//     ffx::LaunchMinigame(l);          // warps, then fires the script when it lands
//     ffx::PumpMinigameLaunch();       // once per step, from the hub's step event
//
//     double v = 0;
//     const ffx::MinigameField* f = ffx::MinigameFieldAt(now, 0);
//     ffx::ReadMinigameField(f, &v);
//     ffx::WriteMinigameField(f, 7);
//
// THERE IS NO PER MINIGAME ENTRY POINT. Every field map, cutscene and field
// minigame in FFX is one ATEL event package named by a single integer, and the
// same integer is the map id, the event id and the row index of eventid.bin. So
// launching is a warp, plus firing the actor script entry the NPC dialogue would
// normally have fired.
//
// WHICH MINIGAME IS RUNNING comes from the package name the engine keeps in a
// char[256], matched on the tail after the slash. Two traps: nothing clears that
// buffer on map unload so it goes stale rather than empty, and a battle does not
// reload the package, so during an encounter it still names the field map.
// BattleRunning() is the companion test, and the overdrive minigames are a
// separate set of globals in ffx/Battle.h rather than script variables.
//
// WHERE MINIGAME STATE LIVES. Script variables have seven storage classes. Class 0
// is an 8 KB block inside the save file, so an edit there is persistent and
// follows the player home. Classes 2 to 6 live inside the loaded package image and
// the actor pool, both of which are freed and reallocated on EVERY map change, so
// nothing here caches a resolved pointer across a load and neither should a
// caller. Every live clock, gauge and score in the game is class 3 or class 6.
//
// Research is in reversing/CHEAT_MODELS.md sections 5 to 7 and
// reversing/MINIGAMES_TIMED.md.

#include <windows.h>

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Which minigame is on screen
	// ---------------------------------------------------------------------------

	enum Minigame
	{
		MinigameNone = 0,
		MinigameBlitzball,      // bl/bltz0000, a live match
		MinigameBlitzballHub,   // bl/bltz0200, the team and roster screens
		MinigameChocoboCalm,    // na/nagi0000, the Calm Lands race
		MinigameChocoboRemiem,  // lm/lmyt0000, Remiem Temple
		MinigameLightningDodge, // ka/kami0000, the Thunder Plains
		MinigameButterflies,    // mc/mcfr0100, Macalania
		MinigameJechtShot,      // sw/swin0000
		MinigameCactuarHunt,    // bi/bika0000 and friends
		MinigameViaPurifico,    // bv/bvyt0500
		MinigameMonsterArena,   // na/nagi0700
		MinigameAirshipSearch,  // hi/hiku2100
		MinigameCount
	};

	// What the engine's own package-name buffer holds, such as "bl/bltz0000". The
	// group prefix comes from the archive layout rather than the exe, so match on the
	// tail. False when there is no readable name.
	bool CurrentPackageName(char* out, int outBytes);

	// The package name matched against the table above, or MinigameNone. STALE RATHER
	// THAN EMPTY after a map change out of a minigame, because the engine never clears
	// the buffer, so pair it with PackageLoaded().
	Minigame CurrentMinigame();

	const char* MinigameName(Minigame which);

	// Is there an ATEL package image right now. This is the test that makes
	// CurrentMinigame trustworthy.
	bool PackageLoaded();

	// The LIVE map id, out of SaveData+0x00. Not GameState's CurrentMapId, which is
	// the checkpoint: high word map, low word entry point.
	int LiveMapId();

	// ---------------------------------------------------------------------------
	// Launching
	// ---------------------------------------------------------------------------

	// One row per thing worth starting. eventId is the map to warp to, and when
	// scriptEntry is 0 or more the launcher also fires that entry on that actor once
	// the package has loaded, because for several of these the warp alone just drops
	// you on the map next to the NPC.
	struct MinigameLaunch
	{
		const char* label;
		const char* note;
		Minigame kind;
		int eventId;
		int entryPoint;
		int actorId;     // -1 when there is nothing to fire
		int scriptEntry; // -1 when there is nothing to fire
	};

	int MinigameLaunchCount();
	const MinigameLaunch* MinigameLaunchAt(int index);

	// Requests the map change and, when the row names a script entry, arms the fire so
	// it happens once the package is loaded. Refuses an event id that ships no package,
	// because loading one of those spins the simulation thread in while(1) forever.
	//
	// GAME THREAD. The warp writes live save data.
	bool LaunchMinigame(const MinigameLaunch* launch);

	// The armed fire, which is "run actor A entry E as soon as event N is loaded".
	// Useful on its own, not just for the table above.
	bool ArmScriptFire(int eventId, int actorId, int scriptEntry);
	bool ScriptFireArmed(int* outEventId, int* outActorId, int* outScriptEntry);
	void CancelScriptFire();

	// CALL ONCE PER SIMULATION STEP from the hub's step event. Does nothing until the
	// armed event id is the loaded one, then fires once and disarms. Returns true on
	// the step it fired.
	bool PumpMinigameLaunch();

	// ---------------------------------------------------------------------------
	// Running a script entry directly
	// ---------------------------------------------------------------------------

	// How many script entry points this actor has, out of its definition record, or
	// -1 when the package or the actor is not readable. The engine does NOT bounds
	// check the entry index anywhere, so this is the bound to respect.
	int ActorScriptEntryCount(int actorId);

	// Runs one script entry on one actor, with no dedupe of any kind. Returns 0 when
	// the actor id is 0xFFFF or its thread free list is empty, which is a quiet
	// failure rather than a fault. Caller 0xFFFF means nobody.
	//
	// GAME THREAD, and really the step: it starts a script that the next ATEL step
	// will run.
	int StartActorScript(int callerActorId, int actorId, int channel, int scriptEntry);

	// The nicer shipped route. Keeps the engine's channel test and the actor+171
	// event-mask test but lets you name the entry, and an out of range entry falls
	// back to entry record 0 rather than reading off the end. Pass -1 as the entry to
	// let the engine pick it from the per-kind table, which is then exactly
	// ffx::FireActorEventAs.
	int FireActorEventWithEntry(int callerActorId, int actorId, int eventKind,
	    int scriptEntry);

	// ---------------------------------------------------------------------------
	// Scripted battles, which is how the Monster Arena fights start
	// ---------------------------------------------------------------------------

	// Both gates the engine tests, so a caller can grey out a button instead of
	// calling into a no-op.
	bool ScriptedBattleAllowed();

	// battleId is (mapId << 16) | encounterId. Deferred: it sets pending kind 2 and
	// FFX_Btl_MainStep picks it up.
	//
	// The engine's function ALWAYS returns -1, success or not, so this reads the
	// pending-kind byte afterwards and reports whether it actually became 2.
	bool RequestScriptedBattle(int mapId, int encounterId);

	// The Monster Arena's fights, baked because the ids live in nagi0700's bytecode
	// and parsing bytecode at runtime is not a thing a plugin should do.
	int ArenaFightCount();
	bool ArenaFightAt(int index, int* outMapId, int* outEncounterId,
	    const char** outLabel);

	// ---------------------------------------------------------------------------
	// Script variables, the generic typed browser
	// ---------------------------------------------------------------------------

	enum ScriptVarType
	{
		ScriptVarU8 = 0,
		ScriptVarS8,
		ScriptVarU16,
		ScriptVarS16,
		ScriptVarU32,
		ScriptVarS32,
		ScriptVarF32,
		ScriptVarTypeCount
	};

	// Storage classes, as the engine's resolver numbers them.
	const int kScriptClassSave = 0;    // the 8 KB block inside the save file
	const int kScriptClassUnused = 1;  // never pointed anywhere, do not offer it
	const int kScriptClassActor2 = 2;  // per actor, in the package image
	const int kScriptClassActor3 = 3;  // per actor, and the one with a resolver hook
	const int kScriptClassActor4 = 4;  // per actor, usually the resource list
	const int kScriptClassRegs = 5;    // the live actor's own register block
	const int kScriptClassPackage = 6; // one base for the whole package. The busiest
	const int kScriptClassCount = 7;

	struct ScriptVar
	{
		int index;
		int type;         // a ScriptVarType
		int storageClass; // 0..6
		DWORD offset;     // within that class
		int elements;     // array length, 1 for a scalar
		bool flagBit;     // descriptor bit 24, whose meaning was never settled
	};

	// Is there a package with a descriptor table to read. Everything below fails safe
	// when this is false.
	bool ScriptVarsReady();

	// How many descriptors the package declares. Derived from the gap between the
	// descriptor table and the int pool, which is the only way to count them in
	// process. -1 when there is no package.
	int ScriptVarCount();

	bool ScriptVarAt(int index, ScriptVar* out);

	// Which actor the per-actor classes resolve against. Classes 2, 3, 4 and 5 are
	// PER ACTOR, so the same descriptor names a different byte for a different actor,
	// and this is the one knob that decides which. Defaults to 0.
	int ScriptVarActor();
	bool SetScriptVarActor(int actorId);

	// How many actors the loaded package declares, or -1. SetScriptVarActor bounds
	// against this.
	int PackageActorCount();

	// The actual byte. Classes 2 to 6 go through the engine's own resolver, which is
	// what makes the class 3 hook behave the same way it does for a script, and they
	// need a live actor and a loaded package. CLASS 0 IS RESOLVED HERE INSTEAD, with
	// the resolver's own one-line arithmetic, so the persistent halves stay readable
	// with no package loaded. Otherwise the chocobo best times and the lightning dodge
	// counters could only be edited while standing on that map, and they live in the
	// save block. NULL when anything on the chain is not readable.
	//
	// Pass -1 as actorId to use ScriptVarActor.
	//
	// NEVER STORE THE RESULT. Classes 2 to 6 point inside the package image, which is
	// freed on the next map change.
	void* ScriptVarAddress(int storageClass, DWORD offset, int actorId);

	// Read and write as a double, so one pair of calls covers all seven types. A write
	// saturates at the type's range rather than wrapping.
	bool ReadScriptVar(const ScriptVar* var, int element, double* out);
	bool WriteScriptVar(const ScriptVar* var, int element, double value);

	const char* ScriptVarTypeName(int type);
	const char* ScriptVarClassName(int storageClass);
	int ScriptVarTypeBytes(int type);

	// Class 0 is in the save file. A panel wants to say so before offering the row,
	// because a wrong write there follows the player into their save.
	bool ScriptVarClassPersistent(int storageClass);

	// ---------------------------------------------------------------------------
	// The named live fields, which is the friendly half of the same thing
	// ---------------------------------------------------------------------------

	// A field whose meaning is known. actorId is -1 for a class that is not per
	// actor, and when it is set it OVERRIDES ScriptVarActor, because the butterfly
	// clock is on actor 37 and the butterfly gauge is on actor 38 and neither is a
	// choice.
	struct MinigameField
	{
		const char* name;
		const char* note;
		int storageClass;
		int actorId;
		DWORD offset;
		int type; // a ScriptVarType
		double lo;
		double hi;
	};

	int MinigameFieldCount(Minigame which);
	const MinigameField* MinigameFieldAt(Minigame which, int index);

	bool ReadMinigameField(const MinigameField* field, double* out);
	bool WriteMinigameField(const MinigameField* field, double value);

	// ---------------------------------------------------------------------------
	// The cheap levers, which beat poking the package image
	// ---------------------------------------------------------------------------

	// With this on, the Blitzball match script itself polls the pad: L1 plus Up is
	// home +1, Down is away +1, Left resets the match clock, Right ends the half.
	// PREFER IT to writing the class 6 clock, because it is an ordinary script write
	// and cannot leave the interpreter inconsistent.
	bool BlitzCheatEnabled();
	bool SetBlitzCheatEnabled(bool on);

	// The game's own Blitzball unlock. Fills the 2560-byte save block with every tech
	// for all 60 players. It does NOT start a match.
	bool BlitzUnlockEverything();

	// Three package swaps, all inside one asset-path override. Each one makes the same
	// map id load DIFFERENT bytecode, so set it before the warp, not after.
	bool ChocoboDebugPackage(); // nagi0000 -> dbg_nagi0000
	bool SetChocoboDebugPackage(bool on);
	bool ThunderPlainTreasure(); // kami0400 -> 200thunder_kami0400
	bool SetThunderPlainTreasure(bool on);

	// The Monster Arena fully unlocked. The menu label calls it "Full Arena
	// Localization", which is not what it does.
	bool FullMonsterArena();
	bool SetFullMonsterArena(bool on);

	// Keeps every overdrive gauge full, which is the cheapest route into the four
	// battle overdrive minigames.
	bool OverdriveAlwaysFull();
	bool SetOverdriveAlwaysFull(bool on);

	// 0 idle, 1 armed, 2 running, -1 unreadable. The overdrive input minigames are
	// native globals rather than script variables, so this is the discriminator for
	// them and the package name is no use during a battle. The whole live set, the
	// time budget, the Tidus bar zone, the Auron sequence and the Lulu gauge, is
	// declared in addresses/Battle.h, which owns it. ONE GLOBAL SET, so two characters
	// cannot be inside an overdrive minigame at the same time.
	int OverdriveMinigamePhase();

	void LogMinigames();

} // namespace ffx
