#pragma once

#include <stdint.h>
#include <windows.h>

#include "workshop/Protocol.h"

// Held and carried object sync, and the research answer that decided how small it is.
//
// ===========================================================================
// THE ANSWER: IT IS A BONE PARENT. NOTHING STREAMS.
// ===========================================================================
//
// The question COOP_DESIGN.md has carried on its research list for several sessions was
// whether a carried object is attached to its carrier by a bone parent, by a script
// writing its position every frame, or by a Bullet rigid body with its own dynamics.
// It is the first one, and the proof is one function.
//
// FFX_Ch_BuildSkinMatrices 0x832760 is where the attachment is APPLIED. Its parented
// branch, taken when the carried CHR has a non-null m_parent, computes the carried
// object's world matrix like this and reads nothing else:
//
//     S          = identity scaled by (1 / carrier->m_data->m_f30) * chr[0x1B4]
//     jointLocal = carrier->m_joints[352 * chr->m_parentJoint + 136]
//     M          = jointLocal * S
//     M.trans    = (M * (chr->m_attachOffset * carrier->m_data->m_f30)) * 1000
//     chr->worldMatrix = carrier->worldMatrix * M
//
// The carried object's OWN position and rotation are not read on that path. So the
// transform is a pure function of the carrier, which both machines already hold
// identically because lockstep replicates it, plus three fields that a replicated
// script wrote. Both machines derive the same answer with no traffic at all.
//
// WHY IT IS NOT PHYSICS, stated separately because ruling Bullet out is what turns this
// from "probably cheap" into "cheap". Bullet IS linked into FFX.exe, but a sweep of every
// code reference into the 846 bullet-named functions found that every single referrer
// below the Bullet range sits in PhyreEngine's reflection and class-descriptor region
// (0x5D7000 to 0x603000), which is the PPhysics*Bullet asset wrapper classes registering
// themselves. No FFX game code touches Bullet, and the CHR family at 0x820000 to 0x840000
// touches it nowhere. Bullet is linked because Phyre links it.
//
// WHY IT IS NOT A SCRIPT POSITION WRITE either, though the script does set the carry up:
// three ATEL syscalls attach, FFX_AtelSys_Ch_044_resi 0xA79BC0 by joint index,
// FFX_AtelSys_Ch_045_resi 0xA79C50 by bone id and FFX_AtelSys_Ch_082_resi 0xA787E0 with
// an explicit offset. All three are one-shot: they write the four carry fields and
// return. After that the engine does the following every frame, with no script running.
//
// ===========================================================================
// WHAT FOLLOWS, AND THEREFORE WHAT THIS FILE IS
// ===========================================================================
//
// There is no transform stream here and there is not going to be one. What is left is
// three much smaller jobs.
//
// 1. PROVE IT AT RUNTIME. A claim that something replicates for free is worth exactly as
//    much as the detector that would catch it not doing. So this takes a census of every
//    carry in the CHR pool once per simulation step and hashes it. Two machines that
//    disagree about who is holding what will say so, at a step number, instead of
//    quietly coming apart. HeldObjectsHash is folded into the game state hash the
//    lockstep layer exchanges, as region 15, the last slot ChecksumPayload has.
//
// 2. GIVE A CARRY AN IDENTITY THAT MEANS THE SAME THING ON BOTH MACHINES. m_parent is a
//    raw CHR pointer and useless on a wire. The engine already solved this: the tail of
//    FFX_Ch_CopyState 0x828620 rebuilds m_parent from m_parentUid through
//    FFX_Atel_GetActorChrById, and m_parentUid is the carrier's m_objId, which for any
//    CHR an event script spawned IS ITS ATEL ACTOR ID. So the wire identity is an actor
//    id, chosen by the engine rather than invented here.
//
// 3. LET A CARRY CHANGE HANDS ON AN AGREED STEP. That is the one thing that genuinely
//    needs the command channel, and kCommandHeldObject carries it.
//
// ===========================================================================
// THE PROBLEM THAT IS LEFT, WHICH IS NOT A DESYNC
// ===========================================================================
//
// A field script that picks something up has to name the carrier, and the ones that mean
// "the player" ask FFX_AtelSys_Core_126_resi 0x85C580, a thunk for
// FFX_Atel_GetPlayerActorId, which reads the bound player actor id at ATEL context +10.
// That is ONE SLOT for the whole game.
//
// So in co-op, when the second player walks up to a Cloister pedestal and presses
// confirm, both machines fire the same event on the same step, both run the same script,
// and both attach the sphere to the BOUND PLAYER. The two machines agree perfectly. They
// are just both wrong about who should be holding it.
//
// That is what the handover command is for: re-point the object onto the character the
// peer who earned it is driving, on a step both machines apply. Not a correction of a
// divergence, a correction of the game's own single-slot assumption.
//
// ===========================================================================
// WHAT IS DELIBERATELY NOT HERE
// ===========================================================================
//
// NOTHING CALLS RequestHeldObjectHandover YET, and that is a decision rather than an
// omission. To ask for a handover you have to know which PEER earned the pickup, and
// that answer has to be identical on both machines or the re-point splits them, which
// is worse than the wrong character carrying a sphere. The replicated source for it
// exists but is not exposed: world/TriggerPass.cpp knows which trigger slot fired the
// examine, because the remote trigger pass runs identically on both machines. Exposing
// it is one accessor in a file this pass was not allowed to edit, so the seam is here
// and the policy is not. Deriving it from anything local instead, the way
// DialogueSync falls back to the local peer, would be exactly the split brain this
// layer exists to catch.
//
// No transform replication, no periodic corrective, no interpolation. If the research
// had come back "Bullet rigid body" all three would be here and this file would be ten
// times the size. It did not.
//
// ===========================================================================
// WIRED, AND WHAT IS STILL MISSING
// ===========================================================================
//
// ServiceHeldObjectStep runs from the lockstep gate in net/LockstepLink.cpp, next to
// PrepareMenuInput and ServiceCoopConfigStep. That is where the command half has to be:
// an ordered command can only be consumed on the exact step it was stamped for, because
// Lockstep::CommandsForStep matches step == currentStep and AdvanceStep retires anything
// at or behind it, so a consumer on the frame path drops one on every catch-up step.
// hooks/FrameHook.cpp also calls it, but only when the clock is NOT running, because the
// gate returns early then and the census is worth keeping alive for solo testing.
//
// HeldObjectsHash is folded into the lockstep checksum as region 15.
//
// WHAT IS STILL MISSING is the policy: nothing calls RequestHeldObjectHandover. The seam
// and the applier are built, the decision is not, and it is not a small decision.
//
// To ask for a handover you have to answer "which peer earned this pickup" identically on
// both machines, and the obvious route does not work. There is no census diff here, so
// "a new carry appeared" is not detected yet, and even with one the carrier it appears on
// is whatever FFX_Atel_GetPlayerActorId returned, which is the single bound-player slot.
// Whether that actor id is the same on both machines is the open question, and it depends
// on which character each machine's engine has bound, which is not settled yet. Guessing
// it wrong means re-pointing a carried object at a character on one machine and not the
// other, which is the exact unrecoverable split this file is built to avoid.
//
// world/DialogueSync.h's DialogueOwner() is the nearest thing to an answer that is known
// to agree across machines, and the reason it agrees is worth copying rather than its
// code: its local-peer fallback is symmetric, because whenever one machine falls back the
// other reaches the same peer through the remote pass. Any attribution here needs that
// property proved, not assumed.

namespace pilgrimage
{

	// ---------------------------------------------------------------------------
	// The wire payload.
	//
	// In this header rather than net/Commands.h because RequestCommand takes a void
	// pointer and a length, so nothing in that file needs the shape, and the FMV layer
	// does the same for the same reason.
	// ---------------------------------------------------------------------------

	// One carry changing hands, as an ABSOLUTE STATE rather than as a swap or an edge.
	//
	// Idempotent on purpose, the same reasoning as MenuOverrideCommand and PauseCommand:
	// the host orders these, so two crossing commands have to leave both machines in the
	// same place whichever way round they land, and only a state does that. A swap would
	// not.
	struct HeldObjectCommand
	{
		// Which map the two actor ids mean. An ATEL actor id is MAP SCOPED, so a command
		// issued just before a transition and stamped for a step after it would name a
		// completely different object on the new map. The engine would not refuse that,
		// it would resolve the id and attach the wrong thing. So the map id rides along
		// and a mismatch is a refusal with a log line.
		uint16_t mapId;

		// The carried object's own ATEL actor id. This is CHR m_objId, which for an
		// event-spawned CHR is the actor id, and -1 for a CHR with no actor behind it.
		// A -1 here is refused rather than searched for, because there is no way to
		// name such a CHR that both machines would agree on.
		int16_t objectActorId;

		// Who is to carry it, or -1 to put it down. Also an ATEL actor id.
		int16_t carrierActorId;

		// The LOGICAL BONE ID to attach at, or -1 for "keep whatever bone it is on".
		//
		// A bone id and not a joint index, and the difference is the trap in this whole
		// area. A joint index is an index into one skeleton's joint array and means
		// nothing on another character. A bone id is semantic, 0..21, resolved per
		// skeleton through the CHRDATA bone point table. Handing a carry from Tidus to
		// Wakka has to go through the bone id, or the object lands on whatever joint
		// happened to have that index.
		//
		// The -1 case does the translation here: it reads the object's current joint,
		// turns it back into a bone id against the OLD carrier, and uses that. See
		// ffx::BoneIdForJoint.
		int16_t boneId;

		uint8_t reserved[2];
	};

	// 10 bytes against MaxCommandBytes of 96, so there is room to grow this without a
	// protocol change.
	//
	// A static_assert rather than the array-size trick that workshop/Protocol.h uses.
	// That file says it avoids static_assert for compiler-era reasons, but
	// workshop/SteamAbi.h uses it freely and the toolchain this builds with is fine
	// with it, so the clearer error message wins here.
	static_assert(sizeof(HeldObjectCommand) <= (size_t)workshop::MaxCommandBytes,
	    "HeldObjectCommand does not fit in a command payload");

	// How many simultaneous carries the census tracks. A field map runs about 36 actors
	// and the CHR pool is sized to match, so almost all of these are headroom. The
	// census REFUSES rather than truncates if it ever overflows, because a truncated
	// census would produce a hash that silently stopped covering part of the world,
	// which is the one failure a desync detector must not have.
	const int kMaxTrackedCarries = 48;

	// ---------------------------------------------------------------------------
	// Lifecycle
	// ---------------------------------------------------------------------------

	// Checks the engine entry points the handover needs and arms the census. Call once at
	// startup, not per session: the census is read-only and is useful solo, which is how
	// the research above can be re-confirmed by anybody with a keyboard.
	//
	// Returns false when ffx::CarryApiReady says the attach family does not resolve.
	// Non-fatal. See PilgrimageMod.cpp for the consequence.
	bool InstallHeldObjects();
	bool HeldObjectsInstalled();

	// Arms and disarms the COMMAND half only. The census keeps running either way, so a
	// session ending does not take the readout away.
	void StartHeldObjects();
	void StopHeldObjects();

	// Call once per simulation step from the lockstep gate. See the wiring note in the
	// file header for why the frame path is not good enough for the command half.
	void ServiceHeldObjectStep();

	// ---------------------------------------------------------------------------
	// The seam
	// ---------------------------------------------------------------------------

	// Ask for a carry to change hands on a step the host picks. Both actor ids are ATEL
	// actor ids. Pass -1 as the carrier to put the object down, and -1 as the bone id to
	// keep it on the bone it is already on.
	//
	// DRIVE THIS FROM SOMETHING REPLICATED, never from a local keypress or a local
	// fallback. See "what is deliberately not here" in the file header. Returns false
	// when there is no session to carry it, in which case nothing happened locally
	// either, which is the right way round: a half applied handover is worse than none.
	bool RequestHeldObjectHandover(int objectActorId, int carrierActorId, int boneId);

	// ---------------------------------------------------------------------------
	// Reading the census
	// ---------------------------------------------------------------------------

	// How many carries are live right now, or -1 when the census could not be taken
	// (no pool, or more carries than kMaxTrackedCarries).
	int HeldObjectCount();

	// A hash over the whole census, for the desync detector. Covers the object actor
	// id, the carrier actor id and the carrier joint of every carry, IN POOL SLOT
	// ORDER.
	//
	// Slot order rather than sorted on purpose. Under lockstep both machines allocate
	// CHRs from the same script in the same order, so the slot order already agrees,
	// and FFX_Ch_MoveChrMemory does not relocate CHRs. If the two orders ever differ the
	// machines have already diverged somewhere upstream, and a hash that papered over
	// that by sorting would hide the bigger problem to report the smaller one.
	//
	// Zero means "no carries", which is also what an empty world reads as. That
	// collision is deliberate and harmless: both machines in an empty world agree.
	uint32_t HeldObjectsHash();

	// Who is carrying this object, as an ATEL actor id, or -1 for nobody and for an
	// object the census does not know about. Reads the census rather than the pool, so
	// it is consistent with the hash.
	int HeldObjectCarrier(int objectActorId);

	const char* HeldObjectsStatus();
	void LogHeldObjects();

} // namespace pilgrimage
