#pragma once

#include <windows.h>

namespace ffx
{

	// An FFX CHR. Opaque on purpose: every field goes through the accessors in
	// Character.h, so the layout lives only in GameLayout.h and a wrong offset is a
	// single edit rather than a scattered cast.
	struct Character;

	// ---------------------------------------------------------------------------
	// The FFX entry points this mod calls. All __cdecl, all verified in the IDB.
	// ---------------------------------------------------------------------------
	typedef Character*(__cdecl* ChAllocateFn)(int chrId);
	typedef void(__cdecl* ChDisposeIfLiveFn)(Character* chr);
	typedef int(__cdecl* ChIsLiveFn)(Character* chr); // 0 means live
	typedef int(__cdecl* ChCountLiveFn)(void);
	typedef int(__cdecl* ChRomReadFn)(int chrId);
	typedef int(__cdecl* ChDataReadSyncFn)(int chrId);
	typedef int(__cdecl* ChMotionSetReadStartFn)(int chrId, int set);
	typedef int(__cdecl* ChMotionSetReadSyncFn)(int chrId, int set);
	typedef void(__cdecl* ChLoadMotionSetSyncFn)(int chrId, int set);
	typedef int(__cdecl* ChSetByte184Fn)(Character* chr, char value);
	typedef Character*(__cdecl* ChSetPartyIndexFn)(Character* chr, int index);
	typedef int(__cdecl* ChSetPosFn)(Character* chr, float x, float y, float z);
	typedef Character*(__cdecl* ChGetPosFn)(Character* chr, float* x, float* y, float* z);
	typedef int(__cdecl* ChSetRotFn)(Character* chr, float radians);
	typedef Character*(__cdecl* ChSetMoveSpeedFn)(Character* chr, float speed);
	typedef Character*(__cdecl* ChSetMoveDirFn)(Character* chr, float radians);
	typedef bool(__cdecl* ChSetFlags1Bit400Fn)(Character* chr, short on);
	typedef Character*(__cdecl* ChGetPlayerChrFn)(void);
	typedef void(__cdecl* ChWalkmeshMoveFn)(Character* chr);
	typedef void(__cdecl* ChSetGroundModeFn)(Character* chr, int mode);

	// Everything resolved from Rva, in one place. Populated by BindApi, which
	// must run after VerifyLayout has passed.
	struct Api
	{
		ChAllocateFn Allocate;
		ChDisposeIfLiveFn DisposeIfLive;
		ChIsLiveFn IsLive;
		ChCountLiveFn CountLive;
		ChRomReadFn RomRead;
		ChDataReadSyncFn DataReadSync;
		ChMotionSetReadStartFn MotionSetReadStart;
		ChMotionSetReadSyncFn MotionSetReadSync;
		ChLoadMotionSetSyncFn LoadMotionSetSync;
		ChSetByte184Fn SetByte184;
		ChSetPartyIndexFn SetPartyIndex;
		ChSetPosFn SetPos;
		ChGetPosFn GetPos;
		ChSetRotFn SetRot;
		ChSetMoveSpeedFn SetMoveSpeed;
		ChSetMoveDirFn SetMoveDir;
		ChSetFlags1Bit400Fn SetFlags1Bit400;
		ChGetPlayerChrFn GetPlayerChr;
		ChWalkmeshMoveFn WalkmeshMove;
		ChSetGroundModeFn SetGroundMode;

		Character** chrArray;
		int* chrCount;
		Character** tidusChr;      // a one-slot cache. See CloneSpawner for the hazard.
		Character** controlledChr; // player 1's binding
		BYTE** cameActiveSlot;
		BYTE* cameSlots;
		BYTE** application;
		BYTE** gfxContext;
		void** walkmeshTris;
		int* walkmeshTriCount;
		float* walkmeshScale;
		BYTE* motionKillSwitch;
	};

	extern Api Game;

	void BindApi();

	// Logs the pool, camera and player globals. Useful once at startup to confirm
	// the bindings landed on something sensible.
	void LogApiState();

} // namespace ffx
