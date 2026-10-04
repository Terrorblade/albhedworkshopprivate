#include "world/PlayerDrive.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Api.h"
#include "ffx/Character.h"
#include "ffx/Input.h"
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
			// By address, which works precisely because the CALL SITE was patched and the
			// function itself was not. A detour on the function would have made this line
			// infinite recursion, and that asymmetry is most of why PatchCallSite exists.
			StepControlFn fn = (StepControlFn)ModuleAddress(Rva::PlayerStepControl);
			if (!Readable((void*)fn, 1))
				return NULL;

			return fn();
		}

		// This machine's own body, the one the engine has bound.
		//
		// Read from the engine's own global rather than worked out from the ownership
		// table on purpose. Whatever the engine thinks it is driving is what the rest of
		// the game thinks the player is, so taking a second opinion from the roster would
		// only create a way for the two to disagree.
		Character* LocalCharacter()
		{
			if (!Game.controlledChr || !Readable(Game.controlledChr, sizeof(void*)))
				return NULL;

			Character* chr = *Game.controlledChr;
			if (!chr || !Readable(chr, 4))
				return NULL;

			return chr;
		}

		Character* CharacterForPeer(int peer, int local)
		{
			return (peer == local) ? LocalCharacter() : RemotePlayerCharacter(peer);
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
