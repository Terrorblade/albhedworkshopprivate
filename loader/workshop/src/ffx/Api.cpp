#include "ffx/Api.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/Addresses.h"

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;

	Api Game;

	void BindApi()
	{
		Game.Allocate = (ChAllocateFn)ModuleAddress(Rva::ChAllocate);
		Game.DisposeIfLive = (ChDisposeIfLiveFn)ModuleAddress(Rva::ChDisposeIfLive);
		Game.IsLive = (ChIsLiveFn)ModuleAddress(Rva::ChIsLive);
		Game.CountLive = (ChCountLiveFn)ModuleAddress(Rva::ChCountLive);
		Game.RomRead = (ChRomReadFn)ModuleAddress(Rva::ChRomRead);
		Game.DataReadSync = (ChDataReadSyncFn)ModuleAddress(Rva::ChDataReadSync);
		Game.MotionSetReadStart = (ChMotionSetReadStartFn)ModuleAddress(Rva::ChMotionSetReadStart);
		Game.MotionSetReadSync = (ChMotionSetReadSyncFn)ModuleAddress(Rva::ChMotionSetReadSync);
		Game.LoadMotionSetSync = (ChLoadMotionSetSyncFn)ModuleAddress(Rva::ChLoadMotionSetSync);
		Game.SetByte184 = (ChSetByte184Fn)ModuleAddress(Rva::ChSetByte184);
		Game.SetPartyIndex = (ChSetPartyIndexFn)ModuleAddress(Rva::ChSetPartyIndex);
		Game.SetPos = (ChSetPosFn)ModuleAddress(Rva::ChSetPos);
		Game.GetPos = (ChGetPosFn)ModuleAddress(Rva::ChGetPos);
		Game.SetRot = (ChSetRotFn)ModuleAddress(Rva::ChSetRot);
		Game.SetMoveSpeed = (ChSetMoveSpeedFn)ModuleAddress(Rva::ChSetMoveSpeed);
		Game.SetMoveDir = (ChSetMoveDirFn)ModuleAddress(Rva::ChSetMoveDir);
		Game.SetFlags1Bit400 = (ChSetFlags1Bit400Fn)ModuleAddress(Rva::ChSetFlags1Bit400);
		Game.GetPlayerChr = (ChGetPlayerChrFn)ModuleAddress(Rva::ChGetPlayerChr);
		Game.WalkmeshMove = (ChWalkmeshMoveFn)ModuleAddress(Rva::ChWalkmeshMove);
		Game.SetGroundMode = (ChSetGroundModeFn)ModuleAddress(Rva::ChSetGroundMode);

		Game.chrArray = (Character**)ModuleAddress(Rva::ChrArray);
		Game.chrCount = (int*)ModuleAddress(Rva::ChrCount);
		Game.tidusChr = (Character**)ModuleAddress(Rva::TidusChr);
		Game.controlledChr = (Character**)ModuleAddress(Rva::ControlledChr);
		Game.cameActiveSlot = (BYTE**)ModuleAddress(Rva::CameActiveSlot);
		Game.cameSlots = (BYTE*)ModuleAddress(Rva::CameSlots);
		Game.application = (BYTE**)ModuleAddress(Rva::Application);
		Game.gfxContext = (BYTE**)ModuleAddress(Rva::GfxContext);
		Game.walkmeshTris = (void**)ModuleAddress(Rva::WalkmeshTris);
		Game.walkmeshTriCount = (int*)ModuleAddress(Rva::WalkmeshTriCount);
		Game.walkmeshScale = (float*)ModuleAddress(Rva::WalkmeshScale);
		Game.motionKillSwitch = (BYTE*)ModuleAddress(Rva::MotionKillSwitch);
	}

	void LogApiState()
	{
		Log("pool: chrArray at 0x%08X holds 0x%08X, chrCount=%d",
		    (unsigned)(UINT_PTR)Game.chrArray, (unsigned)(UINT_PTR)*Game.chrArray, *Game.chrCount);
		Log("camera: activeSlot=0x%08X -> 0x%08X, slots base 0x%08X",
		    (unsigned)(UINT_PTR)Game.cameActiveSlot, (unsigned)(UINT_PTR)*Game.cameActiveSlot,
		    (unsigned)(UINT_PTR)Game.cameSlots);
		Log("player globals: tidusChr=0x%08X controlledChr=0x%08X",
		    (unsigned)(UINT_PTR)*Game.tidusChr, (unsigned)(UINT_PTR)*Game.controlledChr);
	}

} // namespace ffx
