#include "clones/CloneSpawner.h"

#include <stdio.h>

#include "ffx/GameState.h"

#include "ModState.h"
#include "clones/CloneRoster.h"
#include "workshop/Log.h"
#include "diag/CharacterDump.h"
#include "ffx/RenderProbe.h"
#include "ffx/Character.h"
#include "ffx/Api.h"
#include "ffx/Layout.h"
#include "ffx/Walkmesh.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	const int SpawnableChrIds[] = {
		1, 2, 3, 4, 5, 6, 7, 8,
		41, 45,
		101, 102, 103, 104, 105, 106, 107, 108,
		307, 901, 908
	};
	const int SpawnableChrIdCount = (int)(sizeof(SpawnableChrIds) / sizeof(SpawnableChrIds[0]));

	namespace
	{

		// Footstep sound selection. 15 is what field characters get.
		const char FieldSoundSelector = 15;

		// How long after a spawn to keep logging the render fields.
		const LONG DiagFrames = 150;

		// Pages in the character's data and its field motion set. Both calls are
		// idempotent and both are already resident whenever that character is on screen,
		// so in practice they return ready on the first try. They matter for the case
		// where the player is controlling somebody else.
		//
		// Returns false and sets the status when the caller should try again or give up.
		bool EnsureCharacterDataLoaded(int chrId)
		{
			Game.RomRead(chrId);

			int dataState = Game.DataReadSync(chrId);
			if (dataState == 1)
			{
				SetStatus("chr data still loading, press F9 again");
				return false;
			}
			if (dataState == 2)
			{
				// 2 means no cache entry, and the game pops an error box for it. Bail
				// rather than push on into FFX_Ch_Allocate's fallback path.
				SetStatus("id %d has no chr cache entry, try an id that is on this map", chrId);
				Log("spawn refused: DataReadSync(%d) returned 2, no cache entry. That id's "
				    "data is not resident on this map.",
				    chrId);
				return false;
			}

			Game.MotionSetReadStart(chrId, 0);
			if (Game.MotionSetReadSync(chrId, 0) == 1)
			{
				SetStatus("motion set still loading, press F9 again");
				return false;
			}
			return true;
		}

	} // namespace

	const char* ChrIdName(int chrId)
	{
		static char text[48];

		// The high-detail variants are the same eight characters offset by 100, so
		// resolve the base id and say which variant it was.
		int base = chrId;
		const char* detail = "";
		if (chrId >= 101 && chrId <= 108)
		{
			base = chrId - 100;
			detail = " (high detail)";
		}

		// 41 is Rikku's alternate outfit, which the game selects with its own flag
		// rather than by a separate character index.
		if (chrId == 41)
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE, "Rikku (alt outfit)");
			return text;
		}

		const BYTE charIndex = CharacterForChrId((DWORD)base);
		if (charIndex == 0xFF)
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE, "id %d, unidentified", chrId);
			return text;
		}

		// The name table is part of the save data, so before a game is loaded there
		// is nothing to read. Say so rather than printing an empty name.
		const char* name = CharacterName(charIndex);
		if (!name || !*name)
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE, "character %d%s (no game loaded)",
			    (int)charIndex, detail);
			return text;
		}

		_snprintf_s(text, sizeof(text), _TRUNCATE, "%s%s", name, detail);
		return text;
	}

	int SelectedChrId()
	{
		LONG index = settings.spawnIdIndex;
		if (index < 0 || index >= SpawnableChrIdCount)
			index = 0;
		return SpawnableChrIds[index];
	}

	void CycleSpawnId(int step)
	{
		LONG wrapped = (LONG)((step % SpawnableChrIdCount) + SpawnableChrIdCount);
		LONG next = (settings.spawnIdIndex + wrapped) % SpawnableChrIdCount;
		InterlockedExchange(&settings.spawnIdIndex, next);
		SetStatus("next spawn: chr id %d", SpawnableChrIds[next]);
		Log("spawn id is now %d (index %ld of %d)",
		    SpawnableChrIds[next], next, SpawnableChrIdCount);
	}

	bool SpawnClone()
	{
		InterlockedIncrement(&counters.spawnAttempts);
		const int chrId = SelectedChrId();

		if (!PoolIsReady())
		{
			SetStatus("pool not up yet, load a map first");
			return false;
		}
		LONG entry = FirstFreeEntry();
		if (entry < 0)
		{
			SetStatus("at the clone cap of %ld, despawn one first", MaxClones);
			Log("spawn refused: all %ld clone entries are in use", MaxClones);
			return false;
		}
		if (!PoolHasFreeSlot())
		{
			SetStatus("pool full (%d/%d), cannot allocate", Game.CountLive(), *Game.chrCount);
			Log("spawn refused: pool full, %d live of %d", Game.CountLive(), *Game.chrCount);
			return false;
		}
		if (!EnsureCharacterDataLoaded(chrId))
			return false;

		// Where to put it: beside whoever player 1 is driving.
		float playerX = 0.0f, playerY = 0.0f, playerZ = 0.0f, playerYaw = 0.0f;
		Character* player = LivePlayerCharacter();
		if (player)
		{
			Game.GetPos(player, &playerX, &playerY, &playerZ);
			playerYaw = FloatAt(player, Chr::Facing);
		}
		else
		{
			Log("spawn: no live player character, spawning at the origin");
		}

		// THE ONE SINGLE-INSTANCE ASSUMPTION IN THE WHOLE PATH.
		//
		// FFX_Ch_BindChrData, called from inside FFX_Ch_Allocate, writes the Tidus
		// cache unconditionally for any character whose CHRDATA name is "c001" or
		// "c101". It is a one-slot cache with no guard, so allocating a second Tidus
		// silently repoints it at our clone. That matters because FFX_Ch_Dispose does
		//
		//     if (GetPlayerChr() == chr || tidusChr == chr) {
		//         SetPlayerChr(nullptr);
		//         tidusChr = nullptr;
		//     }
		//
		// so disposing the clone later would also null player 1's pad binding and drop
		// their control. Save the real one across the allocation and put it back. One
		// dword, and it is the entire difference between this working and breaking
		// player 1 on despawn.
		//
		// Only ids 1 and 101 can repoint the cache, because FFX_Ch_BindChrData strcmps
		// for exactly those two names, but saving a dword costs nothing for the rest.
		Character* realTidus = *Game.tidusChr;

		Character* clone = Game.Allocate(chrId);

		if (*Game.tidusChr != realTidus)
		{
			Log("note: Allocate(%d) repointed the Tidus cache 0x%08X -> 0x%08X, putting it back",
			    chrId, (unsigned)(UINT_PTR)realTidus, (unsigned)(UINT_PTR)*Game.tidusChr);
		}
		*Game.tidusChr = realTidus;

		if (!clone)
		{
			SetStatus("Allocate(%d) returned null", chrId);
			Log("spawn failed: FFX_Ch_Allocate(%d) returned null", chrId);
			return false;
		}

		// Straight after the allocation, before anything else is touched. If the model
		// attach completed synchronously then the instance is already non-zero here.
		LogCharacterDiag("post-alloc", clone);

		Game.LoadMotionSetSync(chrId, 0);
		Game.SetByte184(clone, FieldSoundSelector);
		Game.SetPartyIndex(clone, (int)settings.spawnPartyIndex);

		// THE SPAWN OFFSET. This was +60.0 and that was an unforced error: 60 world
		// units to the side can land inside geometry or outside the view, which would
		// make a clone invisible for an entirely mundane reason. A few units beside the
		// player is close enough to see and far enough not to interpenetrate, and
		// successive clones fan out so number two does not land inside number one.
		const float offset = settings.spawnOffset * (float)(LiveCloneCount() + 1);
		Game.SetPos(clone, playerX + offset, playerY, playerZ);
		Game.SetRot(clone, playerYaw);

		// Bind the walkmesh now rather than waiting for a next-frame recovery that will
		// not come if the character is hidden. See Walkmesh.h for the deadlock.
		BindToWalkmesh(clone, "spawn");

		const LONG poolSlot = SlotOfCharacter(clone);
		ClaimEntry(entry, poolSlot);
		SetActiveEntry(entry); // drive the one just spawned
		InterlockedExchange(&telemetry.spawnedChrId, chrId);

		Log("spawned clone: id=%d character=0x%08X poolSlot=%ld at (%.1f %.1f %.1f) yaw=%.3f  "
		    "pool %d/%d  tidusCache=0x%08X  partyIndex=%ld  offset=%.1f  motionKill=%u",
		    chrId, (unsigned)(UINT_PTR)clone, poolSlot,
		    playerX + offset, playerY, playerZ, playerYaw,
		    Game.CountLive(), *Game.chrCount, (unsigned)(UINT_PTR)realTidus,
		    settings.spawnPartyIndex, (double)offset, *Game.motionKillSwitch);

		if (player)
			LogCharacterDiag("the player", player);
		LogCharacterDiag("post-setup", clone);
		LogInstanceGeometry(clone, "clone");
		if (player)
			LogInstanceGeometry(player, "player");

		SetStatus("clone %ld of %ld live, id %d", entry + 1, LiveCloneCount(), chrId);
		return true;
	}

	namespace
	{

		void DespawnEntry(LONG entry)
		{
			if (entry < 0 || entry >= MaxClones)
				return;

			LONG slot = SlotOfEntry(entry);
			Character* chr = CharacterFromSlot(slot);
			if (!IsLive(chr))
			{
				ReleaseEntry(entry);
				SetStatus("clone %ld was already gone", entry + 1);
				return;
			}

			// Stop it moving first, so the last thing the engine sees is a still character.
			Game.SetMoveSpeed(chr, 0.0f);

			// The same hazard as on spawn, from the other side: FFX_Ch_Dispose nulls the
			// Tidus cache and the player binding if either points at the character being
			// disposed. The cache was restored on spawn so it should point at the real
			// Tidus, but check rather than trust, because an intervening map load or a
			// second spawn could have changed it.
			if (*Game.tidusChr == chr)
				Log("despawn: the Tidus cache unexpectedly points at the clone, clearing it first");

			Game.DisposeIfLive(chr);
			ReleaseEntry(entry);

			Log("despawned clone %ld from pool slot %ld, pool now %d/%d",
			    entry + 1, slot, Game.CountLive(), *Game.chrCount);
			SetStatus("despawned clone %ld, %ld left", entry + 1, LiveCloneCount());

			// Move the input focus to something that still exists.
			if (ActiveEntry() == entry && LiveCloneCount() > 0)
				CycleActiveClone();
		}

	} // namespace

	void DespawnActiveClone()
	{
		if (LiveCloneCount() == 0)
		{
			SetStatus("no live clone to despawn");
			return;
		}
		DespawnEntry(ActiveEntry());
	}

	void DespawnAllClones()
	{
		LONG before = LiveCloneCount();
		if (!before)
		{
			SetStatus("no clones to despawn");
			return;
		}
		for (LONG entry = 0; entry < MaxClones; ++entry)
			if (SlotOfEntry(entry) >= 0)
				DespawnEntry(entry);

		Log("despawned all %ld clones", before);
		SetStatus("despawned all %ld", before);
	}

} // namespace pilgrimage
