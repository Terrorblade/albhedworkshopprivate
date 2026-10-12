#include "world/PlayerDrive.h"

#include <stdio.h>
#include <string.h>

#include "clones/CloneRoster.h"
#include "coop/Ownership.h"
#include "ffx/Api.h"
#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "ffx/Cutscene.h"
#include "ffx/GameState.h"
#include "ffx/Input.h"
#include "ffx/Walkmesh.h"
#include "ffx/addresses/Input.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "workshop/Session.h"
#include "world/RemotePlayers.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// Three, the same cap as everything else, and the same reason: one active battle
		// slot per player.
		const int MaxPeers = MaxPlayers;

		// The engine's own signature. The return value is a CHR pointer and the one call
		// site DISCARDS it: the instruction after the call at RVA 0x421010 is another
		// call. So the hook below returns void, and eax being whatever it happens to be
		// is exactly what the shipped game leaves there too.
		typedef void*(__cdecl* StepControlFn)(void);

		CallSitePatch g_site;
		bool g_installed = false;
		bool g_active = false;

		// One set of ramps per peer. This IS the movement smoothing, see
		// ffx::PlayerControlState, so it has to survive from step to step and it has to
		// be per character rather than per engine.
		PlayerControlState g_state[MaxPeers];

		// Which character each peer's ramps belong to. When this changes the ramps are
		// thrown away, because they describe the acceleration of a body that is gone.
		//
		// This is how a map transition, a rebind and an ownership change all get handled
		// without anybody having to remember to call us: the CHR pool is freed and
		// reallocated per map, so the pointer changes on its own.
		Character* g_chrForPeer[MaxPeers];

		// The last frame actually received per peer, plus how many steps have been run on
		// it since. Same idea as the coast in RemotePlayers.cpp and the same limit, but it
		// lives here now because this is the thing that drives.
		InputFrame g_lastFrame[MaxPeers];
		int g_coastSteps[MaxPeers];
		const int MaxCoastSteps = 4;
		LONG g_coastedTotal = 0;

		// Which step the bookkeeping was last brought up to date on.
		//
		// NOT the same as "how many times this hook ran". The engine calls the driver once
		// per SUBSTEP and a step can be several substeps, which is deliberate and is what
		// makes the HD fast forward accelerate movement rather than just the clock. The
		// driving repeats per substep, exactly as the shipped game does it, but the coast
		// counter must not, or four substeps of one step would burn the whole coast budget.
		bool g_haveStep = false;
		uint32_t g_lastStep = 0;

		int g_driven = 0;
		int g_skipped = 0;
		LONG g_passes = 0;
		char g_status[192] = "player drive: not installed";

		// ---------------------------------------------------------------------------
		// THE SCRIPT-WRITTEN ANCHOR.
		//
		// Two ATEL opcodes re-anchor the player's movement frame from outside the driver:
		// library 0 function 63 through FFX_Player__syncCamYaw, and function 64 through
		// FFX_Player__setHeadingOffsetDeg. Both write the LIVE state block, and under the
		// per-character swap the live block is scratch, so the write lands nowhere and the
		// script silently does nothing.
		//
		// So watch the two fields it writes. If they changed between one step and the
		// next, something outside wrote them, and the right thing is to copy the new
		// values into EVERY player character. The script said "the player", there are now
		// up to three of them, and doing it to all three is the only reading that is the
		// same on both machines. Doing it to the locally bound one would apply it to a
		// different character on each machine, which is the divergence this whole file
		// exists to remove.
		//
		// Compared as DWORDs and not as floats. This is a "did anybody write here" check,
		// so it wants a bit comparison, and a float compare would answer no forever once
		// a NaN got in.
		// ---------------------------------------------------------------------------
		const DWORD kBlockHeadingOffset = Rva::PlayerHeadingOffset - Rva::PlayerStateBlock;
		const DWORD kBlockCamYaw = Rva::PlayerCamYaw - Rva::PlayerStateBlock;

		DWORD g_seenHeadingOffset = 0;
		DWORD g_seenCamYaw = 0;
		bool g_haveSeenAnchor = false;
		LONG g_anchorWrites = 0;

		void PropagateScriptAnchor()
		{
			BYTE* live = (BYTE*)ModuleAddress(Rva::PlayerStateBlock);
			if (!Readable(live, Rva::PlayerStateBlockBytes))
				return;

			DWORD offset = 0;
			DWORD yaw = 0;
			memcpy(&offset, live + kBlockHeadingOffset, sizeof(offset));
			memcpy(&yaw, live + kBlockCamYaw, sizeof(yaw));

			if (!g_haveSeenAnchor)
			{
				g_seenHeadingOffset = offset;
				g_seenCamYaw = yaw;
				g_haveSeenAnchor = true;
				return;
			}

			if (offset == g_seenHeadingOffset && yaw == g_seenCamYaw)
				return;

			g_seenHeadingOffset = offset;
			g_seenCamYaw = yaw;
			InterlockedIncrement(&g_anchorWrites);

			for (int peer = 0; peer < MaxPeers; ++peer)
			{
				if (!g_chrForPeer[peer])
					continue;

				memcpy(g_state[peer].bytes + kBlockHeadingOffset, &offset, sizeof(offset));
				memcpy(g_state[peer].bytes + kBlockCamYaw, &yaw, sizeof(yaw));
			}
		}

		void* CallOriginal()
		{
			// g_site.previous, NOT the function by address.
			//
			// It works at all because the CALL SITE was patched and the function was
			// not. A detour on the function would have made this line infinite
			// recursion, and that asymmetry is most of why PatchCallSite exists.
			//
			// But it has to be `previous` rather than ModuleAddress, because the
			// workshop's own hub patches this same site for its sub step event and
			// whichever plugin loads first gets there first. Reaching for the real
			// function by address would silently cut the other plugin's hook out of the
			// chain, and that is exactly the failure that cost an afternoon: the hub in
			// one DLL had the site and the player drive in another refused it, so the
			// whole co-op simulation layer was registered on a chain nobody called.
			StepControlFn fn = (StepControlFn)g_site.previous;
			if (!fn || !Readable((void*)fn, 1))
				return NULL;

			return fn();
		}

		// How often the engine's player binding had to be moved, and how often moving it
		// was refused. Both belong on the status line: a refusal every pass means the
		// owned character has no body in this map.
		volatile LONG g_bindingMoves = 0;
		volatile LONG g_bindingRefusals = 0;

		// This machine's own body, as the ENGINE currently has it bound.
		//
		// Only used to decide whether the binding needs moving, and for the solo path.
		// It is deliberately NOT what decides which character this machine's player
		// drives. See CharacterForPeer.
		Character* EngineBoundCharacter()
		{
			if (!Game.controlledChr || !Readable(Game.controlledChr, sizeof(void*)))
				return NULL;

			Character* chr = *Game.controlledChr;
			if (!chr || !Readable(chr, 4))
				return NULL;

			return chr;
		}

		// ---------------------------------------------------------------------------
		// WHICH CHARACTER DOES THIS PEER DRIVE.
		//
		// THE RULE THAT MATTERS: both machines have to answer this the same way for the
		// same peer, or the same input moves two different characters and the worlds
		// come apart. The only thing both machines can compute identically is the
		// ownership table, which is a pure function of the peer id and the active party
		// order, and both of those are already in step. So every peer goes through it,
		// INCLUDING THE LOCAL ONE.
		//
		// This file used to read the local peer's body out of the engine's own
		// g_ffxControlledChr instead, with a comment arguing that whatever the engine
		// thinks it is driving is what the rest of the game thinks the player is. The
		// reasoning was sound and the result was not, because nothing ever moved that
		// binding. Both machines loaded the same save and the engine bound the party
		// LEADER on both, so "ask the engine" was the same as hard coding Tidus: each
		// machine drove Tidus from its own player's input and neither had a body for
		// the other. That is the whole of the "my friend was controlling their own
		// Tidus and never appeared for me" report.
		//
		// The engine's binding is still the right thing for the camera, noclip and the
		// player-only walkmesh codes to follow, which is why it gets MOVED to the
		// locally owned body rather than ignored. See SyncEngineBinding.
		//
		// Only the ACTIVE PARTY is searched, because only an active member has an actor
		// in this map and therefore a body to drive.
		// ---------------------------------------------------------------------------
		Character* CharacterForPeer(int peer, int local)
		{
			(void)local;

			if (peer < 0)
				return NULL;

			// ---------------------------------------------------------------------------
			// A SPAWNED BODY WINS OVER THE OWNERSHIP TABLE, and the order is the
			// interesting part of this function now.
			//
			// A peer who has been given a body of their own is playing THAT, not a party
			// member, so it has to come first. If the table were asked first, rule 3 of
			// OwnerOfCharacter would hand every peer the host's leader as a fallback and
			// a joining player would be driving Tidus again, which is the bug this whole
			// path exists to fix.
			//
			// Both machines answer this identically for the same reason the table does:
			// the binding comes from an ordered command both of them ran, so peer N's
			// body is peer N's body on both ends even though the CHR pool slot behind it
			// is a local detail that need not match.
			//
			// The ownership table stays underneath as the fallback, and it is not dead
			// code. The HOST never gets a spawned body, so the host always lands there,
			// and so does anybody playing a party member by explicit binding.
			// ---------------------------------------------------------------------------
			Character* body = PeerBodyCharacter(peer);
			if (body)
				return body;

			for (int slot = 0; slot < 3; ++slot)
			{
				const int charIndex = (int)ffx::ActivePartyMember(slot);
				if (charIndex < 0 || charIndex > 7)
					continue;
				if (OwnerOfCharacter(charIndex) != peer)
					continue;

				return (Character*)ffx::ChrForPartyCharacter(charIndex);
			}

			return NULL;
		}

		// ---------------------------------------------------------------------------
		// IS THIS CHARACTER ANYBODY'S PLAYER BODY. Asked by the engine's collision
		// pass, through the kit's player-only surface patch.
		//
		// Two walkmesh surface codes are "a wall for the player" and "a wall for
		// everyone but the player", and the engine answers both with the single global
		// this file deliberately points at a different character on each machine. So
		// without this the two machines disagree about 6087 shipped walls, and a
		// position is in no checksum region, so nothing catches it.
		//
		// THE SET IS EXACTLY THE BODIES THIS PASS DRIVES, which is the point. A body
		// that receives replicated input is a player for passability, and nothing else
		// is. One definition means the two can never drift apart.
		//
		// That also keeps the AI followers out of it. With two players in a three slot
		// party, slot 2 reports the host as its owner under Ownership's rule 3, but
		// CharacterForPeer returns the FIRST slot a peer owns, so slot 2 is nobody's
		// body and keeps the engine's non-player answer. Which is right: it is a
		// follower, not a player.
		//
		// EVERY UNCERTAINTY ANSWERS Unknown, and Unknown means the engine decides and
		// nothing changes. No clock, no local peer, a map load that has not rebuilt the
		// CHR pool yet, the first pass of a session: all Unknown. Saying No in those
		// states would be worse than useless, because the kit reads a disagreement with
		// the engine as a reason to flip the answer, and it would swing every
		// player-only wall open for a step or two.
		//
		// Runs on the step thread inside FFX_Ch_WalkmeshMove, several times per
		// character per frame, so it reads the table DrivePass already filled in and
		// calls nothing.
		// ---------------------------------------------------------------------------
		ffx::PlayerBodyAnswer __cdecl PlayerBodyOwnership(Character* chr)
		{
			if (!chr || !g_active)
				return ffx::PlayerBodyUnknown;

			bool resolved = false;
			for (int peer = 0; peer < MaxPeers; ++peer)
			{
				if (!g_chrForPeer[peer])
					continue;
				resolved = true;
				if (g_chrForPeer[peer] == chr)
					return ffx::PlayerBodyYes;
			}

			// Active but holding no bodies at all. The party is mid-rebuild, so we have
			// nothing to say yet.
			if (!resolved)
				return ffx::PlayerBodyUnknown;

			return ffx::PlayerBodyNo;
		}

		// ---------------------------------------------------------------------------
		// Point the engine's single player binding at the body this machine's player is
		// driving.
		//
		// One global, one writer, derived rather than messaged, and idempotent. Both
		// machines run this and reach a DIFFERENT answer on purpose: that is the point.
		// The camera, noclip, the player-bump trigger and FFX_Map_IsTriBlockedForChr all
		// resolve "the player" through this, so without it player two's machine watches
		// player one's character.
		//
		// Re-checked every pass rather than set once, because a map transition
		// re-authors the player actor from the script and drops it back to the leader.
		// ---------------------------------------------------------------------------
		void SyncEngineBinding(Character* wanted)
		{
			if (!wanted)
				return;
			if (EngineBoundCharacter() == wanted)
				return;

			if (ffx::SetControlledCharacter(wanted))
			{
				InterlockedIncrement(&g_bindingMoves);
				Log("player drive: moved the engine's player binding to the character "
				    "this machine owns. The camera, noclip and the player-only walkmesh "
				    "codes follow it from here.");
			}
			else
			{
				InterlockedIncrement(&g_bindingRefusals);
			}
		}

		// The map script keeps steering followers. While an actor has a move command
		// pending, FFX_Atel_ApplyMoveToChr re-asserts its speed every frame and walks it
		// back toward the script's target, so a driven follower would fight us and win.
		//
		// Cheap and self-clearing: it only does anything when there is something to
		// cancel, so running it per pass costs a word read once the actor is idle. Both
		// machines cancel the same actors on the same steps, so it is not a divergence.
		void ReleaseScriptHold(int peer)
		{
			if (peer < 0)
				return;

			for (int slot = 0; slot < 3; ++slot)
			{
				const int charIndex = (int)ffx::ActivePartyMember(slot);
				if (charIndex < 0 || charIndex > 7)
					continue;
				if (OwnerOfCharacter(charIndex) != peer)
					continue;

				const int actor = ffx::ActorIdForPartyCharacter(charIndex);
				if (actor >= 0)
					ffx::CancelActorMoveCommand(actor);
				return;
			}
		}

		void ForgetPeer(int peer)
		{
			ResetPlayerControlState(&g_state[peer]);
			g_lastFrame[peer] = NeutralInput();
			g_coastSteps[peer] = 0;
		}

		// Bring one peer's input up to date for a step we have not seen before. Only ever
		// called once per step per peer, see g_lastStep.
		void AdvancePeerInput(Lockstep* clock, int peer)
		{
			if (clock->HasInputForStep((uint8_t)peer))
			{
				g_lastFrame[peer] = *clock->InputForStep((uint8_t)peer);
				g_coastSteps[peer] = 0;
				return;
			}

			// Nothing arrived. Keep going on the last thing they really sent for a few
			// steps rather than stopping dead, because somebody walking in a straight
			// line is very likely to still be walking in a straight line 33 ms later.
			//
			// The BUTTONS are coasted here, unlike in RemotePlayers.cpp. That file does
			// not coast them because it fires examines off the press edge and a stale
			// held button would misreport one. Nothing here reads an edge: the only
			// buttons this path uses are the four dpad bits and cross for walk, and for
			// those "they are probably still holding it" is the better guess.
			if (g_coastSteps[peer] < MaxCoastSteps)
			{
				++g_coastSteps[peer];
				InterlockedIncrement(&g_coastedTotal);
				return;
			}

			// Out of patience. Neutral rather than a hard stop, so the engine's own ramps
			// decay this character to a halt over the next few steps the way releasing
			// the stick does. A snap to zero would be visible, and it would also be a
			// motion the other machine never produced.
			g_lastFrame[peer] = NeutralInput();
		}

		void DrivePass()
		{
			InterlockedIncrement(&g_passes);

			Lockstep* clock = ActiveLockstep();
			Session* session = ActiveSession();
			const int local = session ? (int)session->LocalPeer() : -1;

			if (!clock || !clock->Running() || local < 0 || local >= MaxPeers)
			{
				// Solo, or a session that has not got a clock yet. Do exactly what the
				// shipped game does and nothing else.
				if (g_active)
				{
					g_active = false;
					for (int peer = 0; peer < MaxPeers; ++peer)
					{
						ForgetPeer(peer);
						g_chrForPeer[peer] = NULL;
					}

					g_driven = 0;
					g_skipped = 0;
					strcpy_s(g_status, sizeof(g_status),
					    "player drive: no clock, the engine is driving");
				}

				CallOriginal();
				return;
			}

			if (!g_active)
			{
				g_active = true;
				Log("player drive: taking over the engine's player driver, local peer %d",
				    local);
			}

			const uint32_t step = clock->CurrentStep();
			const bool newStep = (!g_haveStep || step != g_lastStep);
			g_haveStep = true;
			g_lastStep = step;

			// Before anything is driven, because a script that re-anchored the movement
			// frame last step meant it to apply from this step on.
			if (newStep)
				PropagateScriptAnchor();

			int driven = 0;
			int skipped = 0;

			// The engine's binding follows the body this machine's player owns, and the
			// map script is told to stop steering every body a player is driving. Both
			// before anything is stepped, so the first step of a new map already has the
			// right player bound and no script fighting it.
			SyncEngineBinding(CharacterForPeer(local, local));

			// NOT DURING A CUTSCENE. While a scene is running the script is supposed to
			// be steering these actors, including the player, and cancelling its move
			// command every pass would pull the choreography apart. Nobody is driving
			// from input then either, so there is nothing to release the body for.
			//
			// Both machines run the same script from the same step and the dialogue and
			// FMV layers keep them agreeing about when a scene is up, so this gate does
			// not split the two.
			if (!ffx::GameplayInputShouldBeIgnored())
			{
				for (int peer = 0; peer < MaxPeers; ++peer)
					ReleaseScriptHold(peer);
			}

			for (int peer = 0; peer < MaxPeers; ++peer)
			{
				Character* chr = CharacterForPeer(peer, local);

				if (chr != g_chrForPeer[peer])
				{
					// A different body, so the ramps that described the old one are
					// meaningless. Both machines reach this the same way, because the
					// thing that changed the binding was replicated.
					ForgetPeer(peer);
					g_chrForPeer[peer] = chr;
				}

				if (!chr)
					continue;

				// Two peers must never end up pointing at one character. If they do it is
				// a binding bug upstream, and driving that character twice in one substep
				// would double its ramps, so skip the second one and say so.
				bool duplicate = false;
				for (int earlier = 0; earlier < peer; ++earlier)
				{
					if (g_chrForPeer[earlier] == chr)
					{
						duplicate = true;
						break;
					}
				}
				if (duplicate)
				{
					++skipped;
					continue;
				}

				if (newStep)
					AdvancePeerInput(clock, peer);

				PlayerControlInput in;
				in.analogLX = g_lastFrame[peer].analogLX;
				in.analogLY = g_lastFrame[peer].analogLY;
				in.buttons = (WORD)(g_lastFrame[peer].buttons & 0xFFFFu);

				// Their camera, not ours. The driver resolves the stick against a yaw and
				// the camera is local to each machine, so using ours would send their
				// character somewhere they never pointed it. See the InputFrame comment in
				// Protocol.h.
				in.cameraYaw = DequantiseAngle(g_lastFrame[peer].cameraYaw);

				if (StepPlayerControlFor(chr, &g_state[peer], in))
					++driven;
				else
					++skipped;
			}

			// Nobody to drive, which in practice means the engine has no bound player
			// right now: a map load, or a cutscene that took the body away. The engine's
			// own driver does almost nothing in that state, it writes the two deadzoned
			// stick globals and returns on the null check, but it DOES do that much, so
			// run it rather than quietly skipping a call the shipped game makes.
			if (driven == 0)
				CallOriginal();

			g_driven = driven;
			g_skipped = skipped;

			int coasting = 0;
			for (int peer = 0; peer < MaxPeers; ++peer)
				if (g_coastSteps[peer] > 0)
					++coasting;

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "player drive: %d driven on step %u, %d skipped, %d coasting, %ld coasted, "
			    "camera %s, %ld script anchors",
			    driven, (unsigned)step, skipped, coasting, g_coastedTotal,
			    PlayerCameraOverrideInstalled() ? "anchored" : "FIXED YAW", g_anchorWrites);
		}

		void __cdecl PlayerDriveHook()
		{
			DrivePass();
		}

	} // namespace

	bool InstallPlayerDrive()
	{
		if (g_installed)
			return true;

		ResetPlayerDrive();

		if (!PlayerControlDriverAvailable())
		{
			Log("player drive: the engine's player globals are not reachable, so the one "
			    "character this machine owns would still be driven a different way from "
			    "how the other machine drives it. Not patching.");
			strcpy_s(g_status, sizeof(g_status), "player drive: globals unreachable");
			return false;
		}

		// FIRST, because it is the difference between replicating the movement and
		// replicating a different game's movement. Without it the driver bypasses the
		// engine's camera anchor, so holding a direction while the camera swings curves
		// the walk instead of keeping it straight. Not fatal, and StepPlayerControlFor
		// falls back to the fixed-yaw route, which still agrees across machines.
		if (!InstallPlayerCameraOverride())
			Log("player drive: the driver's camera reads were not patched, so movement "
			    "will be continuously camera-relative rather than anchored. It will still "
			    "agree between machines, it just will not feel like FFX.");

		// 0x421010 is the ONLY caller of FFX_Player__stepControl in the whole binary,
		// checked rather than assumed, which is what makes patching the site equivalent
		// to detouring the function while still leaving the function callable.
		if (!PatchCallSite(g_site, Rva::PlayerStepControlCallSite, Rva::PlayerStepControl,
		        (void*)&PlayerDriveHook, "player drive"))
		{
			strcpy_s(g_status, sizeof(g_status), "player drive: call site refused");
			return false;
		}

		// The other half of moving the player binding per machine. SyncEngineBinding
		// makes the camera and the interactions follow the right body, and it also makes
		// the player-only walkmesh codes follow it, which is a divergence rather than a
		// feature. This hands those two codes to the ownership table so both machines
		// answer them the same way. A refusal is survivable and says so in the log.
		if (!ffx::InstallPlayerBodyWalkmesh(&PlayerBodyOwnership))
			Log("player drive: the player-only walkmesh surfaces were NOT patched, so "
			    "the two machines will disagree about the 6087 player-only walls. One "
			    "player will walk through a wall the other is stopped by and no checksum "
			    "region will notice, because positions are in none of them.");

		g_installed = true;
		strcpy_s(g_status, sizeof(g_status), "player drive: installed, no clock yet");
		Log("player drive: every player character will now go through the engine's own "
		    "driver with its own ramps, including this machine's, and the input comes out "
		    "of the lockstep ring rather than the pad");
		return true;
	}

	bool PlayerDriveInstalled()
	{
		return g_installed;
	}

	Character* PlayerBodyForPeer(int peer)
	{
		// The local argument is unused by the resolver, see CharacterForPeer. Passing
		// the peer itself keeps the call honest about having nothing to say.
		return CharacterForPeer(peer, peer);
	}

	bool PlayerDriveActive()
	{
		return g_installed && g_active;
	}

	void ResetPlayerDrive()
	{
		for (int peer = 0; peer < MaxPeers; ++peer)
		{
			ForgetPeer(peer);
			g_chrForPeer[peer] = NULL;
		}

		g_haveStep = false;
		g_lastStep = 0;
		g_driven = 0;
		g_skipped = 0;

		// Re-read the anchor rather than assuming it is unchanged. A session that starts
		// after a script already set an offset should start from that offset, not from a
		// stale idea of it, and the next pass will seed it.
		g_haveSeenAnchor = false;
	}

	int PlayerDriveCount()
	{
		return g_driven;
	}

	int PlayerDriveSkipped()
	{
		return g_skipped;
	}

	const char* PlayerDriveStatus()
	{
		return g_status;
	}

	void LogPlayerDrive()
	{
		Log("=== player drive ===");
		Log("%s", g_status);
		Log("installed %s, active %s, %ld substep passes", g_installed ? "yes" : "no",
		    g_active ? "yes" : "no", g_passes);
		Log("player-only surfaces: %s, %ld passages opened, %ld walls enforced",
		    ffx::PlayerBodyWalkmeshInstalled() ? "ours" : "THE ENGINE'S, single player",
		    ffx::WalkmeshPassagesOpened(), ffx::WalkmeshWallsEnforced());

		Session* session = ActiveSession();
		const int local = session ? (int)session->LocalPeer() : -1;

		for (int peer = 0; peer < MaxPeers; ++peer)
		{
			Character* chr = g_chrForPeer[peer];
			if (!chr)
			{
				Log("  peer %d%s: no body", peer, peer == local ? " (US)" : "");
				continue;
			}

			const InputFrame& f = g_lastFrame[peer];
			Log("  peer %d%s: stick %02X %02X buttons %04X camera %.2f, coasting %d, "
			    "facing %.2f",
			    peer, peer == local ? " (US)" : "", (unsigned)f.analogLX,
			    (unsigned)f.analogLY, (unsigned)(f.buttons & 0xFFFFu),
			    DequantiseAngle(f.cameraYaw), g_coastSteps[peer], CharacterFacing(chr));
		}
	}

} // namespace pilgrimage
