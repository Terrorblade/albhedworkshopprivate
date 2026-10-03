#include "hooks/FrameHook.h"

#include <string.h>
#include <windows.h>

#include "ModState.h"
#include "clones/CloneDriver.h"
#include "clones/CloneRoster.h"
#include "clones/CloneSpawner.h"
#include "diag/CharacterDump.h"
#include "diag/CullOverride.h"
#include "diag/HashProbe.h"
#include "diag/InteractProbe.h"
#include "diag/SpawnWatch.h"
#include "ffx/AnimateHook.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"
#include "ffx/MainLoop.h"
#include "ffx/RenderProbe.h"
#include "ffx/Walkmesh.h"
#include "hooks/Hotkeys.h"
#include "menu/EscMenuRows.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Log.h"
#include "world/TriggerPass.h"
#include "battle/BattleSync.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
#include "world/WorldSync.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX knowledge
	// comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// Copies the active clone's state into telemetry for the control panel. Pass null
		// when there is no clone, which blanks the fields that would otherwise look stale.
		void PublishTelemetry(Character* chr)
		{
			telemetry.poolLive = Game.CountLive();
			telemetry.poolTotal = *Game.chrCount;

			if (!chr)
			{
				telemetry.instanceAttached = 0;
				return;
			}

			float x = 0.0f, y = 0.0f, z = 0.0f;
			Game.GetPos(chr, &x, &y, &z);
			telemetry.posX = x;
			telemetry.posY = y;
			telemetry.posZ = z;

			telemetry.speed = FloatAt(chr, Chr::Speed);
			telemetry.moveDirection = FloatAt(chr, Chr::MoveDirection);
			telemetry.facing = FloatAt(chr, Chr::Facing);
			telemetry.walkmeshTriangle = (LONG)ShortAt(chr, Chr::WalkmeshTri);

			telemetry.instanceAttached = DwordAt(chr, Chr::Instance) != 0;
			telemetry.secondInstanceAttached = DwordAt(chr, Chr::SecondInstance) != 0;
			telemetry.hideFlags = ByteAt(chr, Chr::HideFlags);
			telemetry.flags1 = (LONG)DwordAt(chr, Chr::Flags1);
			telemetry.flags2 = (LONG)DwordAt(chr, Chr::Flags2);
			telemetry.partyIndex = (LONG)DwordAt(chr, Chr::PartyIndex);
			telemetry.shadeAlpha = FloatAt(chr, Chr::ShadeAlpha);
			telemetry.cameLength = FloatAt(chr, Chr::CameLength);
			telemetry.clipZ = FloatAt(chr, Chr::ClipZ);

			telemetry.subMeshCount = SubMeshCount(chr, NULL);
			Character* player = LivePlayerCharacter();
			telemetry.playerSubMeshCount = player ? SubMeshCount(player, NULL) : -1;

			LONG shownByte = -1;
			telemetry.subMeshesLinked = LinkedSubMeshCount(chr, &shownByte);
			telemetry.instanceShownByte = shownByte;

			strcpy_s(telemetry.drawGate, sizeof(telemetry.drawGate), FirstFailingDrawGate(chr));
		}

		// Anything that touches a character has to run on the game thread, so the UI and
		// the hotkeys only raise flags and this drains them.
		void ServiceRequests()
		{
			if (InterlockedExchange(&requests.cycleClone, 0))
				CycleActiveClone();
			if (InterlockedExchange(&requests.cycleSpawnIdForward, 0))
				CycleSpawnId(1);
			if (InterlockedExchange(&requests.cycleSpawnIdBack, 0))
				CycleSpawnId(-1);
			if (InterlockedExchange(&requests.despawnAll, 0))
				DespawnAllClones();
			if (InterlockedExchange(&requests.despawn, 0))
				DespawnActiveClone();
			if (InterlockedExchange(&requests.spawn, 0))
			{
				if (SpawnClone())
					BeginSpawnWatch(counters.frames);
			}

			// Networking. Stop is drained before the two starts, so pressing start while
			// already connected tears down first instead of being refused.
			// The fixed timestep experiment. Nothing caches the value, so a write takes
			// effect on the next frame, and IsPacingFromWallClock reports whether it
			// actually took. Watch audio sync and any FMV, which is where a wrong answer
			// would show first.
			if (InterlockedExchange(&requests.toggleFixedTimeStep, 0))
			{
				const bool wasFixed = FixedTimeStep() != 0.0f;
				if (SetFixedTimeStep(wasFixed ? 0.0f : nominalFixedTimeStep))
					Log("fixed timestep %s, now %.9f. Pacing from the wall clock: %s",
					    wasFixed ? "OFF, delta back to the clock" : "ON",
					    FixedTimeStep(), IsPacingFromWallClock() ? "YES" : "no");
				else
					Log("could not write the fixed timestep, the application pointer was "
					    "not usable");
			}

			// Which save block regions sit still, measured instead of assumed. Reads
			// only, so the one risk is reading a block that is being torn down, and the
			// probe checks for that itself.
			if (InterlockedExchange(&requests.startHashProbe, 0))
				BeginHashProbe(counters.frames, 300);

			if (InterlockedExchange(&requests.toggleLockstepEnforce, 0))
				SetLockstepEnforced(!LockstepEnforced());

			if (InterlockedExchange(&requests.lookAtInteractables, 0))
				LookAtInteractables();
			if (InterlockedExchange(&requests.fireNearestExamine, 0))
				FireNearestExamine();
			if (InterlockedExchange(&requests.armEscMenuRow, 0))
				ArmEscMenuRowTest();

			if (InterlockedExchange(&requests.toggleTriggerFollow, 0))
				SetTriggerPassFollowClone(!TriggerPassFollowClone());
			if (InterlockedExchange(&requests.toggleTriggerArmed, 0))
				SetTriggerPassArmed(!TriggerPassArmed());
			if (InterlockedExchange(&requests.logTriggerPass, 0))
				LogTriggerPass();

			if (InterlockedExchange(&requests.logWorldSync, 0))
			{
				LogWorldSync();
				LogBoosterSync();
				LogBattleSync();
				LogDialogueSync();
			}
			if (InterlockedExchange(&requests.requestWorldResync, 0))
				RequestWorldFromHost(workshop::kWorldRequestManual);

			if (InterlockedExchange(&requests.stopNetworking, 0))
				StopNetworking();
			if (InterlockedExchange(&requests.startHosting, 0))
				StartHosting();
			if (InterlockedExchange(&requests.startJoining, 0))
				StartJoining();
		}

		// Every clone gets its walkmesh kept, and every clone that is not holding the
		// input focus is explicitly stopped. Stopping matters: DriveClone is what writes
		// m_speed, so a clone that lost focus mid-stride would otherwise keep walking
		// forever on its last speed.
		void ServiceAllClones()
		{
			Character* live[MaxClones];
			const LONG liveCount = PruneDeadClones(live);
			Character* active = ActiveClone();

			for (LONG i = 0; i < liveCount; ++i)
			{
				Character* chr = live[i];

				// While a character is hidden the engine's own motion pass skips it, so
				// this is the only thing that would relocate it, and without a binding its
				// Y is forced to m_groundHeight by ground mode 1. See ffx/Walkmesh.h.
				if (settings.autoBindWalkmesh && ShortAt(chr, Chr::WalkmeshTri) == -1)
					BindToWalkmesh(chr, "auto");

				if (chr != active)
					HoldCloneStill(chr);
			}
		}

		int __fastcall AnimateHook(void* self, void* unusedEdx)
		{
			// Run the game first, so we are writing into characters the engine has already
			// stepped this frame, and so a fault in our code cannot stop the game from
			// having run. The result is returned unchanged, which is not optional.
			const int result = OriginalAnimate()(self, unusedEdx);

			const LONG frame = InterlockedIncrement(&counters.frames);

			if (GameHasFocus())
				PollHotkeys();
			ApplyCullOverride();
			ServiceRequests();

			// Before the clone work, so a session that has something to say about this
			// frame has already said it by the time anything acts on the frame.
			PublishLocalIdentity();
			StepNetworking(frame);

			// After the session has pumped, so a hold decision is made with this frame's
			// arrivals already counted. self is the FFXApplication, which is where the
			// stall byte lives.
			ServiceLockstep(self);

			// The Esc menu's pages are built once when its singleton is constructed, so
			// the row test has to watch for that rather than being able to act when the
			// key is pressed.
			ServiceEscMenuRowTest();

			// Before the clone work, so a reading taken while clones are idle is not
			// picking up anything the mod itself did this frame.
			StepHashProbe(frame);

			ServiceAllClones();

			Character* active = ActiveClone();
			if (IsLive(active))
			{
				DriveClone(active);
				PublishTelemetry(active);
				StepSpawnWatch(frame, active);
			}
			else
			{
				PublishTelemetry(NULL);
			}

			// Serviced after the drive, so a diff sees a fully stepped frame.
			if (InterlockedExchange(&requests.dumpDiff, 0))
				DumpCharacterDiff(ActiveClone(), LivePlayerCharacter());

			if (frame <= 3 || frame == 300 || (frame % 3600) == 0)
				Log("animate #%ld  %ld clones live, driving #%ld  pool %ld/%ld",
				    frame, LiveCloneCount(), ActiveEntry() + 1,
				    telemetry.poolLive, telemetry.poolTotal);

			return result;
		}

	} // namespace

	bool InstallFrameHook()
	{
		return HookAnimate(&AnimateHook);
	}

} // namespace pilgrimage
