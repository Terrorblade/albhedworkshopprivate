#pragma once

#include <windows.h>

// Reading the ATEL world, and firing a world event as a chosen character.
//
// ATEL is FFX's event script machine. Chests, NPCs, line triggers, cutscene
// volumes and the field's own scripted logic all live here. A co-op build needs
// three things out of it, and this header is those three things:
//
//   1. See what is interactable, without relying on the engine's single-slot
//      nearest-target cache.
//   2. Fire a world event on behalf of a specific character, because that is how
//      "player 2 opened the chest" becomes a network command instead of an
//      input-simulation problem.
//   3. Know, BEFORE sending that command, whether it would do anything. That is
//      ActorCanFireEvent, and it is the most useful function in the file.
//
// The good news this header exists to encode: the engine's trigger steppers take
// the player actor as a PARAMETER, not from a global. Only the player's position
// is cached in a single slot, and that is six floats. Nothing has to be widened
// and nothing has to be patched.
//
// Nothing here allocates, throws, or uses the STL. Every raw pointer read goes
// through workshop::Readable first, because an ATEL context exists in .data
// before a map is loaded and is garbage until one is. AtelReady() answers the
// question "is there a world to look at" and every function below fails safe
// when the answer is no.
//
// The derivation is in ..\..\..\reversing\INTERACTION_PATH.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Event kinds.
	//
	// Exactly eight, 0 to 7. The proof is the table stride: the three channel tables
	// are 8 bytes apart and the four script-entry tables are 8 words apart. Every
	// name below came off a real call site rather than a guess.
	//
	// THE ENGINE DOES NOT BOUNDS CHECK THIS. FFX_Atel_GetEventChannel and
	// FFX_Atel_GetEventScriptEntry index an 8-entry table with the raw value, so a
	// kind of 8 or more reads off the end. Everything in this header clamps.
	// ---------------------------------------------------------------------------
	enum AtelEventKind
	{
		// The player pressed the confirm button while this actor was the nearest
		// examinable one. THIS IS THE CHEST, and it is also talking to an NPC and
		// reading a sign. There is no chest-specific event and no chest-specific
		// code anywhere in the binary.
		kAtelEventExamine = 0,

		// The same arbitration, reached with the other of the two examine buttons
		// (pad bit 0x80 rather than 0x20). Fires script entry 3 instead of 2.
		kAtelEventExamineAlt = 1,

		// Two CHRs physically bumped into each other. Fired by
		// FFX_Ch_ResolveCollisionsAll, and only actor kind 1 has a channel for it.
		kAtelEventCollide = 2,

		// The player is inside my radius this frame. DISTANCE ONLY, no button is
		// read anywhere on this path. This is what makes an NPC notice you.
		kAtelEventProximity = 3,

		// The player's swept movement segment crossed me. Line and box triggers.
		kAtelEventTouch = 4,

		// Outside to inside. Line, box and volume triggers.
		kAtelEventEnter = 5,

		// Inside to outside.
		kAtelEventLeave = 6,

		// Has a script entry in every table and a channel in none, so
		// FFX_Atel_FireActorEvent can never start it. Listed for completeness and so
		// the clamp has a real upper bound.
		kAtelEventUnreachable = 7,

		kAtelEventKindCount = 8
	};

	// ---------------------------------------------------------------------------
	// Actor kinds. The byte the actor's own header pointer points at.
	// ---------------------------------------------------------------------------
	enum AtelActorKind
	{
		kAtelActorCharacter = 1, // an NPC or a party member, CHR backed
		kAtelActorLineTrigger = 2,
		kAtelActorBoxTrigger = 3,
		kAtelActorPlain = 4, // no position sub-struct at all, never stepped
		kAtelActorPathTrigger = 5,
		kAtelActorVolumeTrigger = 6,
		// What FFX_Atel_GetActorKind returns for an out of range id. It has no
		// channel in any table, which is the engine's own safety net.
		kAtelActorInvalid = 7
	};

	// 0xFFFF means "nobody" in three different places: as a caller actor id, as the
	// per-actor target id at pos+40, and as the empty nearest-examinable slot.
	const int kAtelActorIdNone = 0xFFFF;
	const int kAtelNoCaller = 0xFFFF;
	const BYTE kAtelChannelNone = 255;
	const WORD kAtelScriptEntryNone = 0xFFFF;

	// Seven contexts of 568 bytes. Everything a mod cares about is context 0.
	const int kAtelContextCount = 7;

	// The two examine bits in the ATEL pad mask, and which event kind each commits.
	const WORD kAtelPadExamineBit = 0x0020;    // -> kAtelEventExamine
	const WORD kAtelPadExamineAltBit = 0x0080; // -> kAtelEventExamineAlt

	// ---------------------------------------------------------------------------
	// Context field offsets. Grouped in their own namespace so a wrong offset is one
	// edit rather than a scattered cast, same rule as ffx/Layout.h.
	// ---------------------------------------------------------------------------
	namespace AtelCtx
	{
		const DWORD Size = 568;

		const DWORD Flags = 0x00;             // byte. bits 0x02 and 0x20 mean a player is bound
		const DWORD ScriptFlags = 0x02;       // byte
		const DWORD StateByte = 0x08;         // byte
		const DWORD BoundPlayerId = 0x0A;     // word, 0xFFFF for none
		const DWORD ActorCount = 0x0C;        // word
		const DWORD PrevBoundPlayerId = 0x0E; // word, stashed by SetControlledActor
		const DWORD ActorCapacity = 0x10;     // word, the bound GetActor clamps to
		const DWORD Count1464 = 0x14;         // word, actors of stride 1464
		const DWORD Count1368 = 0x16;         // word, actors of stride 1368
		const DWORD Count744 = 0x18;          // word, actors of stride 744
		const DWORD Generation = 0x1A;        // word, part of the GetActor cache key
		const DWORD ActorPool = 0x1C;         // pointer

		// The examine arbitration, all one-at-a-time by construction. This is the
		// group a per-player pass has to save and restore.
		const DWORD ExamineArmed = 484;     // byte, 1 while a scan is live this frame
		const DWORD ExamineEventKind = 486; // byte, 0 or 1, what the commit will fire
		const DWORD NearestActorId = 488;   // word, the winner. 0xFFFF for none
		const DWORD NearestDistSq = 492;    // float, the winner's squared distance
		const DWORD PlayerRadius = 496;     // float, added to the actor's own radius

		const DWORD ScriptCounter = 500;        // dword
		const DWORD ThreadArgsFromCaller = 504; // dword
		const DWORD FieldFrameFlags = 528;      // dword, bit 2 gates StepFieldFrame

		// The single-slot player position cache. EVERY distance and sweep test in
		// the trigger set reads these six floats and nothing else. Swapping them is
		// the whole mechanism for running the trigger set as a second character.
		const DWORD PlayerPos = 536;     // float[3] current x y z
		const DWORD PlayerPrevPos = 552; // float[3] previous x y z
	}

	// ---------------------------------------------------------------------------
	// Actor field offsets.
	// ---------------------------------------------------------------------------
	namespace AtelActor
	{
		const DWORD KindPtr = 0x00; // pointer. The KIND IS THE FIRST BYTE OF THE TARGET
		const DWORD ScriptBase = 0x28;
		const DWORD Id = 0x2E;                  // word, the id every event call takes
		const DWORD StepFlags = 0x34;           // word. bit 0x20 player controlled, bit 0x80 step me
		const DWORD FrameFlags = 0x36;          // word
		const DWORD PartyCharacter = 0x40;      // int, 0..7 or 255 for not a party member
		const DWORD EventScriptOverride = 0x44; // u16 *, beats the per-kind default tables
		const DWORD ThreadList = 0x80;          // the running threads, for the channel-busy test
		const DWORD ThreadFreeList = 0x88;
		const DWORD Chr = 0x9C;       // Character *
		const DWORD SubKind = 0xAA;   // byte, 1 means CHR backed
		const DWORD EventMask = 0xAB; // byte, bit N enables event kind N

		// Where the position sub-struct lives depends on the kind. Kind 4 has none.
		const DWORD PosDefault = 1368; // kinds 1, 2, 3
		const DWORD PosPathVol = 644;  // kinds 5, 6
	}

	// A thread record on an actor's list, for the channel-busy test.
	namespace AtelThread
	{
		const DWORD Next = 0x00;           // the list is singly linked through +0
		const DWORD ChannelAndFlag = 0x0E; // byte, the channel is the low nibble
		const DWORD State = 0x0F;          // byte, 3 means finished
		const BYTE StateFinished = 3;
	}

	// ---------------------------------------------------------------------------
	// Position sub-struct offsets. Shared by every actor kind that has one.
	// ---------------------------------------------------------------------------
	namespace AtelPos
	{
		const DWORD PrevX = 0;
		const DWORD PrevY = 4;
		const DWORD PrevZ = 8;
		const DWORD X = 16;
		const DWORD Y = 20;
		const DWORD Z = 24;
		const DWORD YHalfHeight = 28; // 0 or less means do not gate on Y at all
		const DWORD TargetId = 40;    // word. 0xFFFF means use the bound player
		const DWORD XHalfExtent = 48;
		const DWORD RotationY = 52; // used NEGATED for the sin and cos
		const DWORD ZHalfExtent = 56;
		const DWORD ConeHalfAngle = 60; // the facing cone for examine arbitration
		const DWORD ExamineRadius = 64; // added to ctx+496 for the nearest-target contest
		const DWORD TriggerRadius = 72; // squared before the compare, for event 3
		const DWORD Size = 76;
	}

	// Opaque handle. Every field goes through an accessor below.
	struct AtelActorData;

	// ---------------------------------------------------------------------------
	// Is there a world to look at?
	// ---------------------------------------------------------------------------

	// The selected context, or NULL. Does the pointer indirection and the Readable
	// check, which is the part that is easy to get wrong: the RVA names a pointer
	// SLOT, not the struct.
	BYTE* AtelContext();

	// Context 0 specifically, which is what every public ATEL getter uses and what a
	// mod should walk. AtelContext() is whatever happened to be selected.
	BYTE* AtelContext0();

	// True when the context is readable, the actor pool pointer is a pointer and the
	// actor count is non-zero. Nothing else in this header works when this is false
	// and nothing else in this header will crash when it is.
	bool AtelReady();

	int ActorCount();

	// ---------------------------------------------------------------------------
	// The bound player
	// ---------------------------------------------------------------------------

	// The actor id the engine's own trigger pass uses as "the player", or
	// kAtelActorIdNone. Read through the game's getter, which maps 0xFFFF to -1, so
	// this normalises it back.
	int BoundPlayerActorId();

	// *** READ THIS BEFORE CALLING. ***
	//
	// Writes the bound player actor id directly. You almost certainly do not want
	// this, for two reasons.
	//
	// First, you do not need it. FFX_Atel_StepActor resolves the bound player once
	// and passes the ACTOR POINTER into the proximity test and the line and box
	// steppers. They are already callable for any character. What is actually
	// single-slot is the player POSITION cache, and SavePlayerPosCache /
	// SetPlayerPosCache handle that without touching this field.
	//
	// Second, flipping it perturbs three things: which actor's CHR position lands in
	// the position cache that frame, what FFX_Atel_GetPlayerActorId reports to any
	// ATEL script opcode that runs while it is flipped, and ctx+14 if you went via
	// the game's own setter. The second one matters: scripts run inside the actor
	// loop, so DO NOT hold a swap across the actor loop.
	//
	// This deliberately does NOT call FFX_Atel_SetPlayerActorId, because that
	// function also stomps the previous-position slot with the current position,
	// which loses any swept trigger crossing in flight on the swap frame.
	//
	// Returns false when there is no world or the id is out of range.
	bool SetBoundPlayerActorId(int actorId);

	// ---------------------------------------------------------------------------
	// Reading an actor
	// ---------------------------------------------------------------------------

	// The actor record for an id, or NULL. Bounds checked against the actor count
	// rather than trusting the engine's clamp, and validated with Readable.
	AtelActorData* ActorRecord(int actorId);

	// The kind, through the game's own FFX_Atel_GetActorKind so the answer matches
	// what the event tables will be indexed with. Returns kAtelActorInvalid for an
	// out of range id or no world.
	int ActorKind(int actorId);

	// Current world position into xyz[3]. False when the actor has no position
	// sub-struct, which is kind 4, or when there is no world.
	bool ActorPosition(int actorId, float* xyz);

	// Previous frame's position, the start of the swept segment the line and box
	// triggers test against.
	bool ActorPreviousPosition(int actorId, float* xyz);

	// The per-actor target id at pos+40. kAtelActorIdNone means "use the global
	// bound player", which is what kinds 1, 2 and 3 always do. Kinds 5 and 6 fill
	// this in, which is why they are already per-character capable.
	int ActorTargetId(int actorId);

	// The per-actor event enable mask at actor+171. Bit N enables event kind N, and
	// it is the third of the three gates on firing anything.
	BYTE ActorEventMask(int actorId);

	// Which party character this actor represents, 0..7, or -1 for none. This is the
	// link to ffx/GameState.h's character index, so it is how an ownership table
	// keyed on characters finds the actor to act as.
	int ActorPartyCharacter(int actorId);

	// The radius event kind 3 uses, at pos+72. The engine squares it before
	// comparing, this returns it unsquared.
	float ActorTriggerRadius(int actorId);

	// The radius the examine arbitration uses, at pos+64. The engine adds ctx+496 to
	// it, so AtelPlayerRadius() is the other half.
	float ActorExamineRadius(int actorId);

	float AtelPlayerRadius();

	// ---------------------------------------------------------------------------
	// Firing an event as a specific character.
	//
	// This is the co-op command executor. The engine already takes the actor id as a
	// parameter, so nothing has to be simulated.
	// ---------------------------------------------------------------------------

	// Fire eventKind on actorId with no caller. Equivalent to what every engine call
	// site does. Returns non-zero when a script thread actually started.
	//
	// Bounds checks the actor id and clamps the event kind, which the game function
	// does NOT do: it dereferences actor+68 and actor+171 before its own gates can
	// reject anything, through a pointer built from a clamped index.
	int FireActorEvent(int actorId, int eventKind);

	// Same, naming a caller actor so the started script can see who asked. Pass
	// kAtelNoCaller for the engine's own behaviour.
	int FireActorEventAs(int callerActorId, int actorId, int eventKind);

	// The deferred sibling. It starts the thread through a different starter whose
	// rule is "not already running this script entry" rather than "channel free".
	// The volume trigger uses this one. Prefer FireActorEvent unless you are
	// deliberately reproducing a volume trigger.
	int FireActorEventSyncAs(int callerActorId, int actorId, int eventKind);

	// ---------------------------------------------------------------------------
	// *** Would the command do anything? ***
	//
	// The single most useful function here for a co-op mod, because it lets a client
	// decide whether to show an interact prompt and whether to send a command at
	// all, with no round trip.
	//
	// It reproduces, exactly, the three gates inside FFX_Atel_FireActorEvent:
	//
	//   1. The actor's kind has a channel for this event kind. Read from the per-kind
	//      byte table, 255 meaning no channel. Actor kind 1 uses one table, kinds 2
	//      and 3 another, kinds 5 and 6 a third, and every other kind including the
	//      out-of-range kind 7 falls through to a hardcoded 255.
	//   2. A script entry exists. The actor's own override table at actor+68 wins
	//      when it is non-null, otherwise one of four per-kind default word tables
	//      is indexed. 0xFFFF means none.
	//   3. The per-actor enable bit is set: (1 << eventKind) & actor[171].
	//
	// All three are read through the game's own FFX_Atel_GetEventChannel and
	// FFX_Atel_GetEventScriptEntry rather than from a transcribed copy of the
	// tables, so this cannot drift from the engine.
	//
	// It deliberately does NOT consider whether a script is already running on that
	// channel, because that is a this-instant answer that will have changed by the
	// time a network command lands. Ask ActorEventChannelBusy separately if you want
	// it.
	// ---------------------------------------------------------------------------
	bool ActorCanFireEvent(int actorId, int eventKind);

	// The channel this (actorKind, eventKind) pair maps to, or kAtelChannelNone.
	int EventChannel(int actorKind, int eventKind);

	// The script entry this actor would run for this event kind, honouring the
	// actor's own override table. kAtelScriptEntryNone for none.
	int EventScriptEntry(int actorId, int eventKind);

	// Is a script already running on the channel this event would use? Reproduces
	// FFX_Atel_StartThreadIfChannelFree's walk of the actor's thread list: busy means
	// some thread on the list is both unfinished and on that channel.
	//
	// This is why a duplicated network command is harmless rather than a double
	// grant: the engine itself refuses the second one while the first script runs.
	bool ActorEventChannelBusy(int actorId, int eventKind);

	// Non-zero when a message window is up or busy and the engine would refuse to
	// arm an examine scan. 0 means clear. A command executor should check this
	// before firing an examine, because firing into an open dialogue is not
	// something the engine's own path can do.
	int InteractionBlockedKind();

	// ---------------------------------------------------------------------------
	// What is interactable near here?
	//
	// The client needs this because the engine's own answer lives in a single slot
	// (ctx+488) that is overwritten every frame for one player only.
	// ---------------------------------------------------------------------------

	// Fills outIds with up to maxIds actor ids whose current position is within
	// radius of xyz, nearest first. Returns how many were written. Actors with no
	// position sub-struct are skipped. Pass NULL outIds with maxIds 0 to just count.
	//
	// This is a plain sphere test, deliberately: it does NOT reproduce the engine's
	// facing cone or its per-kind radius, because a client wants "what could I
	// possibly interact with" rather than "what would win the arbitration". Pair it
	// with ActorCanFireEvent to filter down to things that would actually respond.
	int FindActorsNear(const float* xyz, float radius, int* outIds, int maxIds);

	// Same, but only actors that would respond to this event kind, which is the
	// query an interact prompt actually wants.
	int FindActorsNearForEvent(const float* xyz, float radius, int eventKind,
	    int* outIds, int maxIds);

	// The engine's current single-slot answer: the nearest examinable actor the
	// arbitration picked this frame, or kAtelActorIdNone. Read it to see what the
	// local player's prompt is pointing at, not as a general query.
	int NearestExaminableActorId();

	// ---------------------------------------------------------------------------
	// The link between an ATEL actor and a CHR.
	//
	// Found and confirmed, both directions. Three numbers name the same person and
	// keeping them apart is the whole game, exactly as in ffx/GameState.h:
	//
	//   ATEL actor id    what every event call takes. actor+46.
	//   CHR pool slot    where the drawable sits this frame. See ffx/Character.h.
	//   character index  the save block identity, 0..17. actor+64 holds it for 0..7.
	// ---------------------------------------------------------------------------

	// The CHR pointer this actor drives, or NULL. actor+156. Only meaningful when
	// the actor's sub-kind at +170 is 1, which this checks.
	void* ChrForActor(int actorId);

	// The CHR pool slot, or -1. Derived from ChrForActor and the pool stride rather
	// than from any engine call, so it works without BindApi having run.
	int ChrSlotForActor(int actorId);

	// The inverse: which actor drives the CHR in this pool slot, or -1. Goes through
	// the game's own FFX_Atel_FindActorIdByChr, with context 0 selected first,
	// because that function reads the ambient context and does not select one itself.
	int ActorIdForChrSlot(int slot);

	// Which actor represents this party character, or -1. The cheapest route from an
	// ownership table keyed on characters to the actor id an event command needs.
	int ActorIdForPartyCharacter(int charIndex);

	// ---------------------------------------------------------------------------
	// The player position cache, which is the one thing a per-player trigger pass
	// has to swap.
	//
	// Every distance and sweep test in the trigger set reads ctx+536 and ctx+552 and
	// nothing else for the player's position. The actor POINTER the steppers are
	// handed only ever supplies the facing. So the whole per-character pass is:
	//
	//     ffx::AtelPlayerPosCache saved;
	//     ffx::SavePlayerPosCache(&saved);
	//     ffx::SetPlayerPosCacheFromActor(clientActorId);
	//     ... run the pass ...
	//     ffx::RestorePlayerPosCache(&saved);
	//
	// Six floats. No patch, no widening.
	//
	// CORRECTION, and it matters. Those six floats are not the whole story. All
	// three steppers also keep a per-ACTOR "the player was inside me last frame"
	// bit, and read it back to decide enter against leave. That bit is one slot per
	// actor, not one per player, so a naive second pass makes a host standing inside
	// a trigger while the remote stands outside fire a spurious enter and leave pair
	// every single frame. See kAtelStepFlagsTriggerMask below and the pass helpers
	// that go with it. Positions alone are not enough.
	// ---------------------------------------------------------------------------
	struct AtelPlayerPosCache
	{
		float current[3];
		float previous[3];
		bool valid;
	};

	bool SavePlayerPosCache(AtelPlayerPosCache* out);
	bool RestorePlayerPosCache(const AtelPlayerPosCache* in);

	// Writes both halves. Pass NULL for previous to set it equal to current, which
	// is what the engine's own actor-swap path does and which means no swept
	// crossing can be detected on that pass.
	bool SetPlayerPosCache(const float* current, const float* previous);

	// Convenience: fill the cache from an actor's own current and previous position,
	// which preserves swept detection. This is the call a per-player pass wants.
	bool SetPlayerPosCacheFromActor(int actorId);

	// ---------------------------------------------------------------------------
	// Running the trigger set as a second character
	//
	// The three trigger steppers take the player actor as a parameter, so they are
	// already callable for a character that is not the bound player. What they are
	// not already able to do is remember a SEPARATE inside-or-outside state per
	// player, and without that every enter and leave event is wrong.
	//
	// Two words of per-actor state are the whole problem:
	//
	//   StepFlags  bit 0x040  the player was inside my radius last frame. Read at
	//                         the top of all three to pick event 5 against event 6.
	//              bit 0x100  the box trigger's "fully inside" flag, read as the
	//                         byte at +53 to gate the touch event.
	//   FrameFlags bit 0x001  a force-retest request the line trigger consumes.
	//              bits 0x006 the line trigger's crossing direction.
	//
	// Everything else in those two words belongs to the engine. StepFlags bit 0x80
	// is the "step me" gate, bit 0x20 is "player controlled", and FrameFlags bits
	// 0x180, 0x600 and 0x1000 are the script bookkeeping that FFX_Atel_StepFrame's
	// own second loop rewrites. So the save and restore is masked, never wholesale.
	// Get that wrong and you will stop actors stepping.
	//
	// The shape of one pass:
	//
	//     ffx::AtelPassState saved;
	//     ffx::SaveAtelPass(&saved);
	//     for (int i = 0; i < count; ++i) hostBits[i] = ffx::ActorTriggerState(i);
	//     // ... per player:
	//     ffx::SetAtelPassPlayer(pos, prevPos, facing);
	//     ffx::ArmAtelPassExamine(eventKind);          // or ClearAtelPassExamine
	//     for (int i = 0; i < count; ++i) {
	//         ffx::SetActorTriggerState(i, shadow[p][i]);
	//         ffx::StepActorTriggers(i, dtMs);
	//         shadow[p][i] = ffx::ActorTriggerState(i);
	//     }
	//     winner[p] = ffx::AtelPassExamineWinner();
	//     // ... then:
	//     for (int i = 0; i < count; ++i) ffx::SetActorTriggerState(i, hostBits[i]);
	//     ffx::RestoreAtelPass(&saved);
	//
	// Four bytes per actor per player. Nothing is patched and nothing is widened.
	// ---------------------------------------------------------------------------

	// The bits of each word the steppers own, and the only bits a pass may swap.
	const WORD kAtelStepFlagsTriggerMask = 0x0140;
	const WORD kAtelFrameFlagsTriggerMask = 0x0007;

	// Which stepper an actor needs.
	const int kAtelTriggerStepNone = 0;
	const int kAtelTriggerStepProximity = 1; // kind 1, a CHR backed actor
	const int kAtelTriggerStepLine = 2;      // kind 2
	const int kAtelTriggerStepBox = 3;       // kind 3

	// Everything one pass reads that is not per actor. The facing is the single
	// float the steppers take off the player actor, and SetAtelPassPlayer writes it
	// onto the bound player's own actor because that is the pointer the steppers get
	// handed. RestoreAtelPass puts it back.
	struct AtelPassState
	{
		float current[3];
		float previous[3];
		float facing;
		float playerRadius;
		BYTE examineArmed;
		BYTE examineEventKind;
		WORD nearestActorId;
		float nearestDistSq;
		bool valid;
	};

	bool SaveAtelPass(AtelPassState* out);
	bool RestoreAtelPass(const AtelPassState* in);

	// Point the pass at a character. Pass NULL for previous to set it equal to
	// current, which loses swept crossings for that pass.
	bool SetAtelPassPlayer(const float* current, const float* previous, float facing);

	// Arm or disarm the examine scan for this pass, and read who won it. Arming
	// clears the winner slot, which is what makes a second scan in the same frame
	// free rather than contaminated by the first.
	bool ArmAtelPassExamine(int eventKind);
	bool ClearAtelPassExamine(void);
	int AtelPassExamineWinner(void);

	// The two masked words packed into one value, low word StepFlags and high word
	// FrameFlags. Returns 0xFFFFFFFF when the actor cannot be read.
	DWORD ActorTriggerState(int actorId);
	bool SetActorTriggerState(int actorId, DWORD state);

	int ActorTriggerStepKind(int actorId);

	// Run one actor's trigger step against whatever the context cache currently says
	// the player is. Reproduces FFX_Atel_StepActor's own gate, which is StepFlags
	// bit 0x80, and its dispatch.
	//
	// For a kind 1 actor this calls ONLY the proximity test. It deliberately does
	// not run the move and rotate commands StepActor runs first, because those
	// integrate the actor's position and running them a second time in one frame
	// would move every NPC twice.
	bool StepActorTriggers(int actorId, int dtMs);

	// The context generation word. Changes when the actor pool is rebuilt, which is
	// the signal that every cached actor id and every shadow bit is now stale.
	int AtelGeneration(void);

	// The engine's own global permission to arm an examine scan at all, read by
	// FFX_Atel_StepFrame right beside the message-window check. A story state or a
	// script can clear it, and a per-player pass that ignores it would let a remote
	// player examine things during a sequence where nobody is supposed to.
	bool ExamineAllowed(void);

	// The once-per-save "you talked to this one" bonus FFX_Atel_StepFrame grants
	// just before it fires the examine event. A remote player's examine has to go
	// through this too, or the two players' interactions are not the same thing.
	bool GrantTalkBonusOnce(int actorId, int eventKind);

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// Logs the context pointer, the actor count, the bound player, the examine
	// arbitration state and the position cache. One call confirms the double
	// indirection landed on something sensible, which is the mistake this whole
	// header exists to prevent.
	void LogAtelWorld();

	// Logs every actor with its id, kind, party character, CHR slot and position,
	// plus which event kinds it would respond to. Bounded by maxActors so it cannot
	// flood the log on a busy map.
	void LogAtelActors(int maxActors);

	// Dumps the seven event lookup tables out of the live image, so a mod can see
	// the real values rather than trusting a transcription in a comment.
	void LogAtelEventTables();

} // namespace ffx
