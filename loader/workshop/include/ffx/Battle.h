#pragma once

#include <windows.h>

// The battle command path: reading it, committing to it, and taking the player's
// choice over before the engine acts on it.
//
// FFX's battle is already data driven in the one way that matters for co-op. Every
// action any unit ever performs is a 72 byte record handed to one function,
// FFX_Btl_CommitCommand, and the player menu and the monster AI both go through it.
// So a second player's action does not have to be simulated, it has to be carried.
//
// WHAT THIS HEADER IS FOR, in the order the three jobs come up:
//
//   1. Read the command path. The queues, the turn queue, the phase, the per unit
//      state the executor will test. Enough to say in a log what a unit is about to
//      do and why a command was refused.
//   2. Commit a record safely. CommitBattleCommand bounds checks the things the
//      engine does not, and refuses outside a battle rather than writing into work
//      RAM that has not been allocated.
//   3. Intercept the player's own commit, so a co-op layer can ship the record
//      instead of acting on it locally, and keep the menu from opening at all on a
//      machine that does not own the acting unit. That is HookBattleMenuCommit and
//      HookBattleMenuOpen.
//
// THE ONE DESIGN FACT EVERYTHING HERE RESTS ON. The menu's staging record is a
// single global, rebuilt from the page stack every frame, and so are the page stack,
// the page depth, the menu owner and the target cursor. Two cursors on that is not a
// race, it is one player's command being attributed to the other player's unit. So
// the shape is one menu and replicated records: whoever owns the unit drives the
// shipped menu, the 72 bytes go on the wire at confirm, and every machine commits
// them. Nothing here helps you run two menus, on purpose.
//
// GATES AT COMMIT, which is why replay works at all: the queue's 62 slot capacity,
// and variant <= 1. That is the whole list. No CTB readiness test, no MP test, no
// status test, no "this unit already acted" flag. A record replayed a few steps
// later is accepted. The legality checks live earlier in the menu and again in the
// executor, and the executor's are MP, the overdrive gauge and whether any target
// survived, all three of which lockstep already keeps in step.
//
// Nothing here allocates, throws or uses the STL. Every raw read goes through
// workshop::Readable, because the battle actor arrays live behind a pointer that is
// null until a battle starts and the engine's own accessor does not check it.
// BattleRunning() answers "is there a battle to look at" and everything below fails
// safe when the answer is no.
//
// The derivation is in ..\..\..\reversing\BATTLE_COMMAND.md, and the co-op design
// note that came out of building against it is in reversing\BATTLE_SYNC.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// The command record.
	//
	// 72 bytes, and the same 72 bytes everywhere: the menu stages one, the AI stages
	// one, the queue holds 62 of them and each unit keeps a copy of its last one.
	//
	// A WARNING BEFORE YOU FILL ONE BY HAND. FFX_Btl_CmdQueue_Push overwrites +0,
	// +1, +2, +4, +5, +6 and +7 from its own arguments, and CommitCommand passes
	// postKind and prio as 0. Only +3, the entry count, and the four action entries
	// survive from the record you hand it. +0 still matters, because CommitCommand
	// reads the acting unit out of it before passing it down.
	// ---------------------------------------------------------------------------

	const int kBattleCommandBytes = 72;
	const int kBattleActionEntries = 4;

	// Unit indices 0..30 are the "ally" array, which is a misleading name the engine
	// chose: FFX_Btl_IsAllyUnit is literally "index <= 30" and the eight enemies live
	// at 20..27 inside the same array. 0..6 are the seven playable characters and the
	// unit index IS the save block character index for them, 8..17 are the aeons.
	const int kBattleMaxUnitIndex = 30;
	const int kBattleActiveSlots = 3;

#pragma pack(push, 1)

	struct BattleActionEntry
	{
		WORD abilityId; // top nibble is the class: 2 item, 3 player ability, 4 monster
		WORD subId;     // 0xFF means none

		// Never seen written by any path that was read. Zeroed by every reset, so
		// reliably zero in practice, which is not the same as known to be unused.
		DWORD reserved1;

		// THE TARGET. A 32 bit bitmask, bit n = unit index n. Single target is one
		// bit and multi target is several, there is no second representation and no
		// party slot form anywhere on this path.
		DWORD targetMask;

		DWORD reserved2; // same story as reserved1
	};

	struct BattleCommandRecord
	{
		BYTE unit;        // +0x00 the acting unit, 0..30
		BYTE variant;     // +0x01 0 a normal CTB turn, 1 a counter or extra turn
		BYTE entryCursor; // +0x02 which entry is executing. Zeroed at commit
		BYTE entryCount;  // +0x03 1..4. Zero means "no choice yet, go ask the AI"
		BYTE execState;   // +0x04 0 fresh, 1 finished, 2..6 waiting on an effect
		BYTE zero;        // +0x05
		BYTE postKind;    // +0x06 copied to unit+0xDEA. Every commit site passes 0
		BYTE priority;    // +0x07 priority class, high nibble only

		BattleActionEntry entries[kBattleActionEntries];
	};

	// One CTB turn queue entry. A different thing from a command: this is "whose turn
	// is it", 8 bytes, 62 of them.
	struct BattleTurnEntry
	{
		BYTE unit;     // +0 the unit whose turn it is
		BYTE variant;  // +1 becomes the command record's +1
		BYTE realTurn; // +2
		BYTE claimed;  // +3 FFX_Btl_SendMenu sets this to 1 when it hands the turn out
		BYTE priority; // +4 priority class, high nibble
		BYTE spare[3]; // the rest of the 8 byte stride
	};

#pragma pack(pop)

	// Phrased as array sizes rather than static_assert to match workshop/Protocol.h,
	// because this project targets a compiler era where static_assert is not
	// dependable. If either of these ever fails the packing is not what the engine
	// uses and every read through these structs is wrong.
	typedef char BattleCommandRecordSizeCheck[(sizeof(BattleCommandRecord) == 72) ? 1 : -1];
	typedef char BattleActionEntrySizeCheck[(sizeof(BattleActionEntry) == 16) ? 1 : -1];
	typedef char BattleTurnEntrySizeCheck[(sizeof(BattleTurnEntry) == 8) ? 1 : -1];

	// ---------------------------------------------------------------------------
	// Is there a battle to look at
	// ---------------------------------------------------------------------------

	// Non-zero while the battle actor arrays are allocated. This is the gate every
	// other function here checks first, because FFX_Battle_GetActor computes
	// base + 3984 * index off a pointer global without testing it, so calling it
	// outside a battle hands back an address near zero.
	bool BattleRunning();

	// The phase byte. 1 is a running battle, 13 and 14 turn up during data load.
	// FFX_BtlMenu_Open refuses unless this is 1.
	int BattlePhase();

	// True when a battle is running or one has been queued. This is the test the
	// field step uses to skip the encounter roll, so it is the right question for
	// "am I about to be in a battle" rather than "am I in one".
	bool BattlePendingOrActive();

	// BattleRunning() and phase 1 together, which is the state in which a command can
	// be committed and the menu can be opened. The one to check before replaying a
	// network command.
	bool BattleCommandPhase();

	// ---------------------------------------------------------------------------
	// Units
	//
	// Three numbers name the same person and keeping them apart is the whole game:
	//
	//   unit index       what every battle function takes. 0..30 for the big records.
	//   character index  the save block identity. For units 0..17 these are the SAME
	//                    number, which was read off FFX_Btl_SetupUnitRoster calling
	//                    FFX_Battle_GetActor(characterIndex) directly.
	//   battle slot      0..2, which of the three active party places this unit holds.
	//                    Lives at unit+0x4FE and in the battle party roster.
	// ---------------------------------------------------------------------------

	// Literally "index <= 30". Not "on my team". Keep saying this out loud, because
	// the name invites exactly one wrong assumption and the enemies are inside it.
	bool IsAllyUnit(int unitIndex);

	// The 3984 byte actor record for a unit 0..30, or NULL. Validated with Readable,
	// bounds checked, and refused outside a battle. Monster indices 31..92 use a
	// different 912 byte array and are deliberately NOT returned here, because every
	// offset in this header belongs to the big record.
	BYTE* BattleUnit(int unitIndex);

	// The AI controlled flag at unit+0xDF3, which FFX_Btl_SetupUnitRoster sets on
	// units 20..27. This is the single bit that decides whether FFX_Btl_SendMenu
	// opens the player menu or runs a script, so it is the bit a co-op layer must not
	// casually flip: setting it does suppress the menu, and it also hands the unit to
	// the AI, which then picks and commits an action of its own.
	//
	// Read as a field rather than through FFX_Btl_IsActorAiControlled on purpose. The
	// engine function has a Yojimbo special case that rolls a random number, so
	// calling it from a mod would consume battle RNG and desync a lockstep session.
	bool UnitIsAiControlled(int unitIndex);

	// unit+0xDC8. The presence bit FFX_Btl_CtbTick requires before a unit can be
	// given a turn. Named "present" here because that is how the CTB uses it, though
	// FFX_Btl_SetupUnitRoster sets it alongside a front row flag, so do not read more
	// into it than "the CTB will consider this unit".
	bool UnitPresent(int unitIndex);

	bool UnitKo(int unitIndex);      // unit+0xDCC
	bool UnitEscaped(int unitIndex); // unit+0xDCE
	bool UnitReady(int unitIndex);   // unit+0xDD6, ready to take a turn

	// unit+0x4FE, the active battle slot 0..2 for a playable character. Returns -1
	// when the unit is not in the active party. For an aeon this field holds the aeon
	// index instead and for an enemy the enemy index, so it is only meaningful next
	// to a unit you already know is a party member. BattleSlotOfUnit is the safer
	// question.
	int UnitBattleSlotField(int unitIndex);

	// The two halves of the party mapping, both read from the battle party roster
	// rather than from unit+0x4FE, so neither can be confused by an aeon.
	//
	// UnitForBattleSlot: which unit is in active slot 0..2, or -1.
	// BattleSlotOfUnit:  which active slot holds this unit, or -1 for none.
	int UnitForBattleSlot(int slot);
	int BattleSlotOfUnit(int unitIndex);

	// The two things the executor can still refuse a committed command for, plus the
	// CTB counter. Worth logging side by side on both machines when an action fires
	// on one and quietly does nothing on the other, because these are the only state
	// that can cause that.
	int UnitCurrentMp(int unitIndex);    // unit+0x5D4, a dword
	int UnitOverdrive(int unitIndex);    // unit+0x5BC
	int UnitOverdriveMax(int unitIndex); // unit+0x5BD
	int UnitCtbCounter(int unitIndex);   // unit+0x65C

	// unit+0xDF0, the action state machine: 0 idle, 1 finishing, 2 acting, 3 done,
	// 4 and 5 waiting.
	int UnitActionState(int unitIndex);

	// ---------------------------------------------------------------------------
	// The command queue
	//
	// One global FIFO shared by every unit. 62 slots of 72 bytes, strict FIFO, and
	// the executor only ever looks at slot 0.
	// ---------------------------------------------------------------------------

	int BattleCommandQueueCount();
	int BattleCommandQueueCapacity();

	// Slot index 0 is the one about to execute. False for an out of range index, or
	// when the queue is shorter than that.
	bool ReadBattleCommandSlot(int slot, BattleCommandRecord* out);

	// This unit's live queue slot, through unit+0xDE5, or false when it has none.
	bool ReadBattleCommandForUnit(int unitIndex, BattleCommandRecord* out);

	// The copy of the last command this unit actually executed, at unit+0x72C. Still
	// readable after the queue slot is gone, which makes it the thing to log when
	// asking "what did that unit just do".
	bool ReadBattleLastCommandForUnit(int unitIndex, BattleCommandRecord* out);

	// ---------------------------------------------------------------------------
	// The turn queue, which is the CTB order and not the command list.
	//
	// It is also what stops the clock, and this is worth knowing because it is the
	// thing that makes a networked command safe. FFX_Btl_CtbTick returns immediately
	// while the TURN queue count is non zero, so the whole span from "the CTB decided
	// whose turn it is" to "that unit's action finished" is one frozen clock. A player
	// thinking in a menu for ten seconds advances nothing, and neither does a command
	// spending six steps on the wire.
	//
	// (reversing\BATTLE_COMMAND.md says the gate is the COMMAND queue count. It is
	// not, it is the turn queue count, which is stronger. Checked in the disassembly.)
	// ---------------------------------------------------------------------------

	int BattleTurnQueueCount();
	bool ReadBattleTurnEntry(int index, BattleTurnEntry* out);

	// The front of the queue, which is the turn being handed out right now.
	bool ReadBattleTurnHead(BattleTurnEntry* out);

	// Clears the claimed byte on this unit's front turn entry, so FFX_Btl_SendMenu
	// offers the turn again next frame.
	//
	// *** THIS IS A RECOVERY TOOL, NOT A MECHANISM. *** FFX_Btl_SendMenu claims the
	// entry BEFORE it opens the menu and returns early forever after on a claimed
	// head, so a turn that was claimed and then never committed hangs the battle with
	// no way back. That happens to a co-op layer in exactly one situation: it took
	// the player's commit over, failed to send it, and now nothing will ever commit
	// it. Calling this puts the turn back and the player picks again.
	//
	// It writes one byte of battle state that the other machine is not writing, so
	// only use it on a path where the other machine never heard about the turn in the
	// first place. Returns false when the unit has no claimed entry.
	bool ReleaseBattleTurnClaim(int unitIndex);

	// ---------------------------------------------------------------------------
	// The menu, read only
	//
	// Every one of these is a single global. They are exposed so a co-op layer can
	// SEE what the local menu is doing and log it, not so it can drive it.
	// ---------------------------------------------------------------------------

	// The 72 bytes the menu has staged, which is rebuilt from the page stack every
	// single frame rather than only on confirm, and whose +0 is stamped each frame
	// from page 0's owner.
	bool ReadStagedBattleCommand(BattleCommandRecord* out);

	// The gil cost the menu has staged, for Bribe and for paying Yojimbo. The commit
	// copies it into the pending slot and the executor spends it.
	int StagedBattleGil();

	// The unit the menu belongs to, or -1 when it is 255 meaning none.
	int BattleMenuOwnerUnit();

	// True when the menu is open at all. The underlying global is a real bitmask
	// (|= 1 << unit) but every reader in the game tests it as a boolean and Close
	// zeroes the whole thing, so it is reported as a boolean here.
	bool BattleMenuOpen();

	// ---------------------------------------------------------------------------
	// Committing
	// ---------------------------------------------------------------------------

	// FFX_Btl_CommitCommand, with the checks the engine skips.
	//
	// Refuses, without calling the engine, when: there is no battle, the record is
	// null, the acting unit is above 30, the variant is above 1, the entry count is
	// not 1..4, or the queue is already full. The engine checks only the last two of
	// those, and an out of range unit index would have it index work RAM off the end
	// of the array.
	//
	// Returns 1 when the command was pushed, 0 when this function refused it, and -1
	// when the engine refused it.
	//
	// THREE SIDE EFFECTS WORTH KNOWING BEFORE CALLING. First, pushing an item command
	// is what decrements the inventory, inside FFX_Btl_Cmd_ConsumeItems, so under
	// lockstep both machines must push or their inventories part company. Second, the
	// gil cost is spent later by the executor, from a single global, so two commits
	// between a commit and its execution would pay the wrong amount. Third, the push
	// does NOT retire the unit's turn queue entry: see the note on
	// ReleaseBattleTurnClaim and the correction in reversing\BATTLE_SYNC.md.
	int CommitBattleCommand(const BattleCommandRecord* record, int gilCost, unsigned variant);

	// ---------------------------------------------------------------------------
	// Ability and item lookups, so a command can be described in a log
	//
	// The ids are 16 bit with the class in the top nibble and the row index in the
	// low 12 bits. The rows live in kernel.bin, NOT in the exe, so the names are not
	// available here and a log can report the class and the row but not "Fire".
	// ---------------------------------------------------------------------------

	enum BattleAbilityClass
	{
		kBattleAbilityCharacter = 0x0, // character or party scope id
		kBattleAbilityItem = 0x2,      // low 12 bits are an item id
		kBattleAbilityPlayer = 0x3,    // a player ability or command
		kBattleAbilityMonster = 0x4,   // a monster ability
		kBattleAbilityOther = 0x6
	};

	int BattleAbilityClassOf(int abilityId); // the top nibble
	int BattleAbilityRowOf(int abilityId);   // the low 12 bits
	bool BattleAbilityIsItem(int abilityId);

	// The kernel.bin row for an id, or NULL.
	//
	// *** ONLY CALL THIS INSIDE A BATTLE. *** FFX_Btl_GetAbilityRecord dispatches on
	// the class to a per class table pointer global and dereferences that pointer
	// without testing it, so before kernel.bin is loaded the fault happens inside the
	// engine where no guard of ours can catch it. BattleRunning() is checked here
	// first for that reason and for no other.
	//
	// outStringBase, when not null, gets the secondary value the engine writes, which
	// for the table classes is the row's string base. It is passed down to the engine
	// as a non-null pointer regardless, because three of the eight class branches do
	// not check it.
	BYTE* BattleAbilityRecord(int abilityId, DWORD* outStringBase);

	// One line describing a record: the unit, the variant, the entry count and each
	// entry's id, class and target mask. Returns out, so it can be used inline in a
	// log call. Never fails: an unreadable record comes back as a sentence saying so,
	// and a buffer too short to hold the whole thing is truncated rather than
	// overflowed. A full four entry record wants about 300 bytes, so 320 is the size
	// to give it if you want the whole line.
	const char* DescribeBattleCommand(const BattleCommandRecord* record, char* out, int bytes);

	// ---------------------------------------------------------------------------
	// TAKING THE PLAYER'S COMMIT OVER
	//
	// This is the hook a co-op layer needs and the reason this file exists.
	//
	// HookBattleMenuCommit arms a callback on the FIVE places the player's own choice
	// reaches FFX_Btl_CommitCommand: the menu confirm, and the escape shortcut in
	// each of the four menu page procs. Returning true from the callback means "I
	// have taken this over", and the engine's commit does not run. Returning false
	// lets it run exactly as shipped, which is what a solo game and a game with no
	// session must get.
	//
	// WHY THOSE FIVE AND NOT FFX_Btl_CommitCommand ITSELF. CommitCommand has ten
	// callers and five of the other five are the engine deciding for itself: the AI
	// script runner, the three forced action paths for confuse, berserk and provoke,
	// and FFX_Btl_SendMenu's empty placeholder. Those run identically on every
	// machine from state that is already in step, so intercepting them would replicate
	// a decision both machines had already made and commit it twice.
	//
	// HOW IT IS DONE, because it is not an inline detour and the difference matters.
	// Each of the five sites is a plain 5 byte call, and the patch rewrites the call's
	// rel32 to point at this library instead. Nothing is relocated, the engine's own
	// functions are left byte for byte alone, and the other five callers are
	// untouched by construction. An inline detour was not an option anyway:
	// FFX_BtlMenu_ConfirmCommand's FIRST instruction is a rel32 call, so there are no
	// five position independent bytes at its entry to steal.
	//
	// The callback gets a COPY of the record, so it cannot accidentally mutate the
	// menu's staging buffer, and it runs on the game thread inside the battle step.
	// ---------------------------------------------------------------------------

	typedef bool(__cdecl* BattleMenuCommitFn)(const BattleCommandRecord* record,
	    int gilCost,
	    unsigned variant);

	// Arms the callback and patches the five sites if they are not patched yet. Safe
	// to call again to change the callback, and safe to call with NULL to disarm,
	// which leaves the patch in place and sends every commit straight through.
	bool HookBattleMenuCommit(BattleMenuCommitFn callback);
	bool BattleMenuCommitHookInstalled();

	// How many of the five sites were actually patched. Five is the whole player
	// side. Anything less and some commits will still reach the engine directly,
	// which for a co-op layer means one machine acting without the other, so this is
	// worth logging rather than ignoring.
	int BattleMenuCommitHookSites();

	// ---------------------------------------------------------------------------
	// KEEPING THE MENU SHUT
	//
	// HookBattleMenuOpen arms a callback on the one call to FFX_BtlMenu_Open in the
	// whole binary. Returning false means the menu does not open for that unit.
	//
	// WHY IT IS SAFE TO JUST NOT OPEN IT. FFX_Btl_SendMenu claims the turn queue
	// entry before it decides between the menu and the AI, and the top of its loop
	// returns the moment the head is already claimed. So a suppressed open leaves the
	// turn sitting there claimed and the engine's own early return is what makes the
	// battle wait.
	//
	// And nothing simulated can tell the two machines apart while that happens. The
	// CTB is frozen on both of them, because the gate is the turn queue count and
	// both have the same entry sitting in it. Not one function in the menu module
	// writes either queue count or any of the eight flags FFX_Btl_IsCtbTickAllowed
	// reads, which was checked by listing the writers rather than assumed. So "a menu
	// is open on this machine" is invisible to the simulation, which is exactly the
	// property this hook needs.
	//
	// WHAT IS NOT SAFE is leaving a turn claimed with nothing ever coming to commit
	// it. See ReleaseBattleTurnClaim.
	//
	// THE SAME CALL SITE TRICK, for the same reason: FFX_BtlMenu_Open's prologue is
	// push ebp, mov ebp esp, call FFX_Btl_GetPhase, so any five byte steal lands
	// inside a rel32 call. The site is found by scanning the small gap between two
	// addresses this library already knows for the single call whose target is
	// FFX_BtlMenu_Open, and the bytes are verified before anything is written.
	// ---------------------------------------------------------------------------

	typedef bool(__cdecl* BattleMenuOpenFn)(int unitIndex);

	bool HookBattleMenuOpen(BattleMenuOpenFn callback);
	bool BattleMenuOpenHookInstalled();

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// How many commits the capture hook has seen, and how many of those a callback
	// took over. The gap between them is every commit that went through to the engine
	// locally, which in a session is the number that should be zero.
	long BattleMenuCommitsSeen();
	long BattleMenuCommitsTaken();
	long BattleMenuOpensSuppressed();

	// The phase, the queue depths, the turn queue head, the menu owner and the three
	// active units with the state the executor will test. One call to answer "why is
	// this battle not doing what the other machine's is".
	void LogBattleState();

	// Every queued command and every queued turn, described. Bounded, so it cannot
	// flood the log off a corrupt count.
	void LogBattleQueues();

} // namespace ffx
