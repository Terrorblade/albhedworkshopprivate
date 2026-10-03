#pragma once

#include <windows.h>

// ATEL: the event script machine, the trigger actors and world interaction.
//
// ATEL is the layer that decides a chest opened, an NPC started talking or the
// player crossed a line. It is a separate world model from the CHR pool: an ATEL
// ACTOR is a script-visible entity with a position, a radius and a script, and a
// CHR is the thing that gets drawn and walks the walkmesh. A character exists as
// both, linked through actor+156. See ffx/Atel.h for that link.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000. Prototypes were read
// back out of the IDB at the call sites, not from the Hex-Rays guesses.
//
// The derivation, every table dump and the co-op analysis are in
// reversing\INTERACTION_PATH.md. The two things to carry in your head:
//
//   1. The trigger steppers take the player actor as a PARAMETER. They are not
//      hard-wired to the bound player. Only the player's POSITION is cached in
//      the context, and that is six floats a mod can swap.
//   2. Firing an event is one call that already takes the actor id, which makes
//      "player 2 opened this chest" a network command rather than an input
//      simulation problem.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The context, and why it is two indirections.
		//
		// AtelContextPtr is a POINTER SLOT, not the struct. It holds a pointer into
		// AtelContextArray, which is seven 568-byte contexts. Every piece of Hex-Rays
		// output that reads "*(u16 *)(g_ffxAtelCtx + 10)" means "load the pointer at
		// AtelContextPtr, then read offset 10 of what it points at".
		//
		// Earlier notes in the repo called 0x1326B28 "g_ffxAtelCtx" as if it were the
		// struct base. It is not, and reading offsets off it directly reads the wrong
		// memory. ffx::AtelContext() does the right thing.
		//
		// Almost every public ATEL entry point brackets its own body with
		// AtelSelectContext(0) / AtelRestoreContext(saved), which is why a mod calling a
		// public getter does not have to care which context was selected. Code that
		// walks the pool itself DOES have to care, because the actor lookup cache below
		// is keyed without the context pointer.
		// ---------------------------------------------------------------------------
		const DWORD AtelContextPtr = 0x00F26B28;     // BYTE **, the selected context
		const DWORD AtelContextArray = 0x00F25BA0;   // 7 x 568 bytes
		const DWORD AtelSelectContext = 0x00462D30;  // int __cdecl (int index), returns the old pointer
		const DWORD AtelRestoreContext = 0x0046F730; // int __cdecl (int saved), returns the old pointer
		const DWORD AtelGetContext = 0x0046AFF0;     // int __cdecl (void), just reads the pointer

		// ---------------------------------------------------------------------------
		// The frame. This is the order world interaction actually happens in.
		//
		//   FFX_Atel_StepFieldFrame        gates on a player being bound and on its own
		//     -> FFX_Atel_StepFrame        field-state flags at ctx+528 and ctx+500
		//          arm the examine scan from the pad, then
		//          for every actor: FFX_Atel_RunScript
		//            -> FFX_Atel_StepActor     switches on the actor kind
		//               -> the five steppers below
		//          then COMMIT: fire the armed event on the winner in ctx+488
		//
		// The commit is the chest. Nothing else in the binary opens one.
		// ---------------------------------------------------------------------------
		const DWORD AtelInit = 0x0046D6D0; // registers the 11 syscall libraries
		const DWORD AtelStepFieldFrame = 0x00471BC0;
		const DWORD AtelStepFrame = 0x00467950; // int __cdecl (int state, int fullStep, int a3)
		const DWORD AtelRunScript = 0x004641E0; // the 123-case opcode switch
		const DWORD AtelStepActor = 0x004666E0; // void __cdecl (int actor, int dt)

		// The five per-kind steppers. The first three take the PLAYER ACTOR POINTER as
		// their last argument, which is the whole reason a second character is cheap.
		// The last two take no player at all, they carry a per-actor target id at
		// pos+40 and are already per-character capable.
		const DWORD AtelTestPlayerProximity = 0x00466980; // int __cdecl (actor, dt, playerActor)
		const DWORD AtelStepLineTrigger = 0x004684B0;     // int __cdecl (actor, dt, playerActor)
		const DWORD AtelStepBoxTrigger = 0x00466CC0;      // int __cdecl (actor, dt, playerActor)
		const DWORD AtelStepPathTrigger = 0x00468020;     // int __cdecl (actor, dt)
		const DWORD AtelStepPathTriggerLink = 0x004680A0; // the per-link half of the above
		const DWORD AtelStepVolumeTrigger = 0x00467C10;   // int __cdecl (actor)

		// The facing test the examine arbitration uses, so an actor behind you does not
		// win the slot. (playerRotY, coneHalfAngle, playerX, playerZ, actorX, actorZ).
		const DWORD AtelTestFacingCone = 0x0048B780;

		// ---------------------------------------------------------------------------
		// Actors: finding them, and the three identities that name the same thing.
		//
		//   ATEL ACTOR ID    0 .. ctx+12. What every event call takes. Stable within a
		//                    map, not across one.
		//   CHR pointer      actor+156. The live drawable. See ffx/Character.h.
		//   character index  actor+64, 0..7, or 255 for "not a party member". The save
		//                    block identity, see ffx/GameState.h.
		//
		// AtelGetActor keeps a ONE-SLOT result cache whose key is (ctx+26, actorId) and
		// which DOES NOT include the context pointer, so two contexts with the same
		// value at +26 can hand each other's actor back. Select context 0 first.
		// ---------------------------------------------------------------------------
		const DWORD AtelGetActor = 0x0046A830;     // int __cdecl (int actorId), clamps out of range
		const DWORD AtelGetActorKind = 0x0046A7B0; // int __cdecl (int actorId), 7 when out of range
		const DWORD AtelGetActorPos = 0x0046B1E0;  // float *__cdecl (int actorId, float *x, *y, *z)

		const DWORD AtelGetActorChrById = 0x0046B3A0;      // int __cdecl (int actorId) -> CHR *
		const DWORD AtelFindActorIdByChr = 0x00476440;     // int __cdecl (int chr) -> actor id or -1
		const DWORD AtelFindActorByPartyChar = 0x0046A470; // int __cdecl (int charIndex) -> actor id or -1

		// The actor lookup cache. Exposed so a diagnostic can see it, not so a mod can
		// write it.
		const DWORD AtelActorCachePtr = 0x00F270E4;   // void *, the cached actor
		const DWORD AtelActorCacheId = 0x00F270E0;    // WORD, which id it is
		const DWORD AtelActorCacheGen = 0x00F270DC;   // WORD, the ctx+26 it was valid for
		const DWORD AtelActorClampCount = 0x00F270D8; // DWORD, bumped on an out of range id
		// Which actor the ATEL frame loop is on. FFX_Atel_GetActorPos uses it as the
		// FALLBACK for an out of range actor id, so a bad id does not fault, it quietly
		// returns some other actor's position. Bounds check before you ask.
		const DWORD AtelCurrentActorIndex = 0x00F26B2C; // DWORD

		// ---------------------------------------------------------------------------
		// The bound player, which turned out NOT to be the obstacle.
		//
		// AtelSetPlayerActorId is the ONLY writer of the bound player actor id at
		// ctx+10 in the whole binary, confirmed by decompiling all 99 functions that
		// touch the context. It has seven readers and all seven are listed in
		// INTERACTION_PATH.md section 4.
		//
		// Do not reach for it. It also overwrites the player position cache with the
		// new actor's position in BOTH the current and the previous slot, which destroys
		// any swept trigger crossing in flight. The steppers take the player as an
		// argument, so swapping six floats is both cheaper and less invasive.
		// ---------------------------------------------------------------------------
		const DWORD AtelGetPlayerActorId = 0x0046C1A0;   // int __cdecl (void), -1 for none
		const DWORD AtelSetPlayerActorId = 0x0046F6A0;   // the only ctx+10 writer
		const DWORD AtelSetControlledActor = 0x0046F580; // saves the outgoing id into ctx+14
		const DWORD AtelBindPlayerChr = 0x00471AB0;
		const DWORD AtelUnbindPlayerChr = 0x004719E0;

		// Position plumbing. Pull copies the bound actor's CHR position into the context
		// cache once a frame, Push writes the other way. Both SetActorPos entries
		// refresh the cache, but only for the actor that equals ctx+10.
		const DWORD AtelPullActorPosFromChr = 0x00469E40;
		const DWORD AtelPushActorPosToChr = 0x00466800;
		const DWORD AtelSetActorPos = 0x00470B20;
		const DWORD AtelSetActorPosXZ = 0x00470970;

		// ---------------------------------------------------------------------------
		// Firing an event. THE co-op command executor.
		//
		// int __cdecl (int callerActorId, int targetActorId, int eventKind)
		//
		// Pass 0xFFFF as the caller for "nobody in particular", which is what every
		// engine call site does. Three gates decide whether anything happens, and
		// ffx::ActorCanFireEvent reproduces all three so a client can test a command
		// before it sends it.
		//
		// The Sync sibling differs only in which thread starter it uses. The volume
		// trigger uses Sync, everything else uses the plain one.
		//
		// HAZARD: both dereference actor+68 and actor+171 BEFORE the gates can reject
		// anything, through a pointer AtelGetActor built from a CLAMPED index. Bounds
		// check the actor id yourself. ffx::FireActorEvent does.
		// ---------------------------------------------------------------------------
		const DWORD AtelFireActorEvent = 0x004764F0;
		const DWORD AtelFireActorEventSync = 0x00476590;
		const DWORD AtelFireEventOnChrActor = 0x004764A0; // int __cdecl (int chr, int eventKind)

		const DWORD AtelGetEventChannel = 0x00476860;     // int __cdecl (actorKind, eventKind)
		const DWORD AtelGetEventScriptEntry = 0x004763E0; // int __cdecl (actorKind, eventKind, u16 *override)

		// NEITHER of those two bounds-checks the event kind. Both index an 8-entry table
		// with it. Clamp to 0..7 before calling.
		const int AtelEventKindTableSize = 8;

		// The lookup tables themselves, so a mod can dump them rather than trust a
		// transcription. Channels are bytes, 255 meaning "this actor kind has no channel
		// for that event". Script entries are words, 0xFFFF meaning none, and an actor's
		// own override table at actor+68 beats all four of them.
		const DWORD AtelEventChannelKind1 = 0x00852AC0;      // BYTE[8], actor kind 1
		const DWORD AtelEventChannelKind2Kind3 = 0x00852AC8; // BYTE[8], actor kinds 2 and 3
		const DWORD AtelEventChannelKind5Kind6 = 0x00852AD0; // BYTE[8], actor kinds 5 and 6
		const DWORD AtelEventScriptDefault = 0x00852AD8;     // WORD[8], actor kind 1 and anything unlisted
		const DWORD AtelEventScriptKind2Kind3 = 0x00852AE8;  // WORD[8]
		const DWORD AtelEventScriptKind5 = 0x00852AF8;       // WORD[8]
		const DWORD AtelEventScriptKind6 = 0x00852B08;       // WORD[8]

		// ---------------------------------------------------------------------------
		// Script threads.
		//
		// AtelStartThreadIfChannelFree is free protection for a networked command: it
		// walks the actor's own thread list at actor+128 and refuses when a thread on
		// the requested channel is still unfinished. So a spammed "open this chest"
		// cannot stack scripts, the second one is a quiet 0.
		//
		// AtelCreateThread takes a record off the actor's free list at actor+136 and
		// returns 0 when it is empty, so the concurrency ceiling is per actor rather
		// than global and a failure is never a fault.
		// ---------------------------------------------------------------------------
		const DWORD AtelStartThreadByChannel = 0x0046EBA0;
		const DWORD AtelStartThreadIfChannelFree = 0x0046EBE0;
		const DWORD AtelStartThreadDeferred = 0x0046EC40;
		const DWORD AtelCreateThread = 0x0046EA00;

		// ---------------------------------------------------------------------------
		// The examine press, which is the cheap half of the problem.
		//
		// FFX_Atel_StepFrame reads the confirm button exactly once per frame, from
		// AtelPadPressed, and that is a PORT 0 edge-pressed mask. If bit 0x20 or 0x80 is
		// set it arms a scan, and the bit decides which event kind the commit fires
		// (0x20 -> event 0, 0x80 -> event 1).
		//
		// AtelPadPort0Buttons and AtelPadPort1Buttons are HELD masks that
		// FFX_Atel_StepFrame remaps and then throws the results away. The ATEL layer
		// genuinely samples port 1 and discards it, which is a leftover PS2 two-port
		// API. Do not mistake them for a working second controller, see the warning in
		// addresses\Character.h about the FFX scePad port layer.
		//
		// None of this has to be touched to make a client open a chest. The command is
		// AtelFireActorEvent with an actor id. The pad only matters if you want the
		// engine's own facing-cone arbitration to run a second time.
		// ---------------------------------------------------------------------------
		const DWORD AtelSamplePadsBothPorts = 0x00471D70;
		const DWORD AtelPadPressed = 0x00F270D0;      // WORD, port 0 edge pressed. THE one that is used
		const DWORD AtelPadReleased = 0x00F270D4;     // WORD, port 0 edge released
		const DWORD AtelPadPort0Buttons = 0x00F270C0; // WORD, port 0 held. Sampled, discarded
		const DWORD AtelPadPort1Buttons = 0x00F270C4; // WORD, port 1 held. Sampled, discarded

		// Which context FFX_Atel_StepContextRange is currently stepping. It doubles as
		// the pad gate: FFX_Atel_StepFrame skips the pad read entirely when this is 2 or
		// more, so of the six field contexts only 0 and 1 ever arm an examine scan from
		// the controller. Both read the same port 0 mask, so this is not a free second
		// player, but a second ATEL context IS already wired to scan and commit.
		const DWORD AtelSteppingContextIndex = 0x00F26B30; // DWORD

		// Non-zero is required before the examine scan will arm at all. Written once
		// during field setup.
		const DWORD AtelExamineAllowed = 0x008526D4; // DWORD

		// Returns 0 when examining is allowed, 1 or 2 when a message window is up or
		// busy. A co-op command executor should consult it rather than firing an event
		// into an open dialogue.
		const DWORD AtelMesWinBlockingKind = 0x0046C8B0; // int __cdecl (void)

		// Called with the examine winner just before the commit fires. Named for what it
		// does, which is award the one-time bonus some NPCs give for being talked to.
		const DWORD AtelGrantTalkBonusOnce = 0x00465650; // (int actorId, int eventKind)

		// ---------------------------------------------------------------------------
		// The treasure syscalls, for the record.
		//
		// These are the two things that actually hand out chest contents, and they are
		// SCRIPT ONLY. Their only cross references in the whole binary are the ATEL
		// syscall dispatch tables in .data. No C code calls them. So a chest's reward
		// lives in the chest's script, which is why firing the event on two machines
		// double-grants and exactly one machine must do it.
		// ---------------------------------------------------------------------------
		const DWORD AtelSysGiveTreasure = 0x0045A8A0;       // syscall 347
		const DWORD AtelSysGiveTreasureSilent = 0x00457B70; // syscall 423
		const DWORD AtelTreasureStaging = 0x01F10EA0;       // where both decode into
		const DWORD AtelSysFuncLibs = 0x00F28558;           // the 11 syscall libraries AtelInit fills

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* AtelRvaList(int* count)
		{
			static const DWORD list[] = {
				AtelContextPtr,
				AtelContextArray,
				AtelSelectContext,
				AtelRestoreContext,
				AtelGetContext,
				AtelInit,
				AtelStepFieldFrame,
				AtelStepFrame,
				AtelRunScript,
				AtelStepActor,
				AtelTestPlayerProximity,
				AtelStepLineTrigger,
				AtelStepBoxTrigger,
				AtelStepPathTrigger,
				AtelStepPathTriggerLink,
				AtelStepVolumeTrigger,
				AtelTestFacingCone,
				AtelGetActor,
				AtelGetActorKind,
				AtelGetActorPos,
				AtelGetActorChrById,
				AtelFindActorIdByChr,
				AtelFindActorByPartyChar,
				AtelActorCachePtr,
				AtelActorCacheId,
				AtelActorCacheGen,
				AtelActorClampCount,
				AtelCurrentActorIndex,
				AtelGetPlayerActorId,
				AtelSetPlayerActorId,
				AtelSetControlledActor,
				AtelBindPlayerChr,
				AtelUnbindPlayerChr,
				AtelPullActorPosFromChr,
				AtelPushActorPosToChr,
				AtelSetActorPos,
				AtelSetActorPosXZ,
				AtelFireActorEvent,
				AtelFireActorEventSync,
				AtelFireEventOnChrActor,
				AtelGetEventChannel,
				AtelGetEventScriptEntry,
				AtelEventChannelKind1,
				AtelEventChannelKind2Kind3,
				AtelEventChannelKind5Kind6,
				AtelEventScriptDefault,
				AtelEventScriptKind2Kind3,
				AtelEventScriptKind5,
				AtelEventScriptKind6,
				AtelStartThreadByChannel,
				AtelStartThreadIfChannelFree,
				AtelStartThreadDeferred,
				AtelCreateThread,
				AtelSamplePadsBothPorts,
				AtelPadPressed,
				AtelPadReleased,
				AtelPadPort0Buttons,
				AtelPadPort1Buttons,
				AtelSteppingContextIndex,
				AtelExamineAllowed,
				AtelMesWinBlockingKind,
				AtelGrantTalkBonusOnce,
				AtelSysGiveTreasure,
				AtelSysGiveTreasureSilent,
				AtelTreasureStaging,
				AtelSysFuncLibs,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
