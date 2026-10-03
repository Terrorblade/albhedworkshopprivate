#pragma once

#include <windows.h>

// The game state: party, per-character stats, inventory, gil, equipment and
// progression. This is the data the menus edit and the data two co-op machines
// have to agree on.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000. Names, argument
// counts and calling conventions were read back out of the IDB rather than
// assumed. Everything in the FFX_SaveData family is __cdecl.
//
// The derivation and the evidence are in ..\..\..\..\reversing\GAME_STATE.md.
// Field offsets for these blocks live in ffx\GameState.h next to the accessors
// that use them, not here, because here is only for addresses.
//
// Owned by the game-state work. New subsystems get their own file in this
// folder instead of being added here.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// THE save block. One contiguous 0x68C0 = 26,816 byte run that holds every
		// piece of persistent game state: party, inventory, gil, equipment, character
		// records, abilities, names and progression. That is why a desync hash over
		// this area can be a handful of ranges rather than a hundred reads.
		//
		// FFX_InitNewSaveData memsets exactly 26816 bytes at this address, which is
		// where the size comes from.
		// ---------------------------------------------------------------------------
		const DWORD SaveData = 0x00D2CA90;     // the block base
		const DWORD SaveDataSize = 0x000068C0; // NOT an address, the byte count

		// __int16 *(void), returns &SaveData. 142 call sites, so this is the canonical
		// way the game itself reaches the block.
		const DWORD GetSaveData = 0x00385240;

		// void (void). memsets the whole block and refills it from the battle kernel
		// tables. Running at an arbitrary moment would wipe the player's game, so this
		// is listed to be recognised, not to be called.
		const DWORD InitNewSaveData = 0x00386B00;

		// ---------------------------------------------------------------------------
		// The party.
		//
		// The three active members are three bytes at SaveData+0x3D58, each a character
		// record index 0..17 with 0xFF meaning the slot is empty. There is no separate
		// roster array: membership is bit 0 of each character record's flags byte.
		// ---------------------------------------------------------------------------

		// int (int *slot0, int *slot1, int *slot2). Each pointer optional.
		const DWORD SaveDataGetFieldParty = 0x003852F0;

		// __int16 *(int *outCount). Returns the array and writes 3 to outCount.
		const DWORD SaveDataGetFieldPartyArray = 0x00385270;

		// int (unsigned char charIndex). record[0x2C] & 1.
		const DWORD SaveDataIsCharInParty = 0x00385380;

		// int (unsigned char charIndex, int on). Also drives the equipment lock bit and
		// the aeon order array, so prefer it over poking the flags byte by hand.
		const DWORD SaveDataSetCharInParty = 0x003869B0;

		// BYTE[7] of character record indices, 0xFF padded. [0..2] are the three ACTIVE
		// battle members and [3..6] the reserves. NOT inside the save block, this is
		// battle work RAM. Built from the field party at battle start by
		// FFX_Btl_SetupUnitRoster and written back by FFX_Btl_CommitPartyToField.
		//
		// This is the ownership table's backing store for the co-op model. The engine's
		// own per-actor slot number for the same thing is battle actor + 1278.
		const DWORD BattlePartyOrder = 0x00D2C895;

		// void (void). Copies BattlePartyOrder[0..2] back into SaveData+0x3D58.
		//
		// DO NOT put this one in VerifyLayout's calledFunctions list. Its first bytes
		// are 66 A1 ... (mov ax, [imm32] with the operand size prefix), which
		// LooksLikePrologue does not accept, so it would fail the check even though the
		// address is right. Nothing in the library calls it through a pointer, it is
		// here to be recognised. Everything else in this file that IS called through a
		// pointer was checked against LooksLikePrologue and passes.
		const DWORD BtlCommitPartyToField = 0x00386930;

		// void (void). Battle unit initialiser. The loop that assigns active battle
		// slots 0..2 from the field party lives here.
		const DWORD BtlSetupUnitRoster = 0x0039C110;

		// ---------------------------------------------------------------------------
		// Which character is which. DWORD[18] mapping a character record index to the
		// chr id FFX_Ch_Allocate wants:
		//
		//   0 Tidus 1,  1 Yuna 2,  2 Auron 3,  3 Kimahri 4,
		//   4 Wakka 5,  5 Lulu 6,  6 Rikku 7,  7 Seymour 8
		//   8..17 the ten aeons, chr ids 0x3001 0x3002 0x3003 0x3004 0x3006 0x3007
		//         0x3008 0x3009 0x300A 0x300B
		//         (Valefor Ifrit Ixion Shiva Bahamut Anima Yojimbo Cindy Sandy Mindy)
		//
		// Rikku is chr id 41 instead of 7 when the byte at SaveData+0xD1 is non-zero,
		// which is her alternate outfit.
		//
		// Only reader is FFX_Btl_ResolveUnitChrId 0x79A440. Read the table rather than
		// hardcoding the ids, because reading it costs nothing and proves the build.
		// ---------------------------------------------------------------------------
		const DWORD CharIndexToChrId = 0x008423A0;

		// char *[8], Shift-JIS debug names for character indices 0..7. Only useful as a
		// cross-check that the index order has not moved.
		const DWORD CharNamesJp = 0x00853728;

		// char *[8], ASCII debug names "Tidus(0)".."Seymour(7)". Same purpose.
		const DWORD DebugCharNames = 0x0083432C;

		// ---------------------------------------------------------------------------
		// Per-character stats. 18 records of 0x94 = 148 bytes at SaveData+0x55CC.
		// Records 0..7 are the playable characters, 8..17 the ten aeons.
		// ---------------------------------------------------------------------------

		// char *(unsigned char index). base + 148*index for index <= 0x11, else NULL.
		const DWORD SaveDataGetCharRecord = 0x00385330;

		// char *(unsigned index, int *out). Fills 10 dwords: out[0..7] the eight stats,
		// out[8] max HP, out[9] max MP, with the equipped-gear bonus folded in.
		const DWORD SaveDataGetCharBaseStats = 0x00385B60;

		// int (int index, void *out). Fills 20 bytes: out[0] sphere levels, out+4
		// current HP, out+8 current MP, out+12..19 the eight effective stats.
		//
		// CO-OP HAZARD. It reads the battle actor instead of the save record whenever
		// FFX_Battle_IsActive(), which is one process-wide flag. With one player in
		// battle and another in the field there is no per-player answer.
		const DWORD SaveDataGetCharCurrentStats = 0x00387230;

		// int (int index, void *in). The write-side twin of the above. Only does
		// anything while a battle is active.
		const DWORD SaveDataSetCharCurrentStats = 0x003873D0;

		// int (unsigned char charIndex). record[0x3B], available sphere levels.
		const DWORD SaveDataGetSphereLevels = 0x003853E0;

		// int (unsigned char charIndex, int n). Moves n from record[0x3B] to
		// record[0x3C]. Returns -1 when there are not enough.
		const DWORD SaveDataSpendSphereLevels = 0x00386EF0; // THE sphere grid commit,
		                                                    // and it has exactly one
		                                                    // caller in the binary

		// int (unsigned char charIndex, int ap). Adds AP and converts it to sphere
		// levels, exactly as the game does at the end of a battle.
		const DWORD SaveDataAddAp = 0x00384610;

		// char *(int index). &CharNames[20*index], or NULL for a non-ally index.
		const DWORD SaveDataGetCharName = 0x00384FB0;

		// int (int charIndex, int abilityId). Tests the wide per-character ability
		// bitmap at SaveData+0x6034. Ability ids are 0x3000-based.
		const DWORD SaveDataTestCharAbility = 0x00385140;
		const DWORD SaveDataSetCharAbility = 0x00385E00; // (charIndex, abilityId, on)

		// ---------------------------------------------------------------------------
		// Inventory and gil.
		// ---------------------------------------------------------------------------

		// int (void) / int (int amount) / int (int cost). Gil lives at SaveData+0x3D48
		// and is clamped to 0..999999999. SpendGil returns 0 and deducts when the
		// player can afford it, -1 and changes nothing otherwise, and a NEGATIVE cost
		// adds gil, which is how chests pay out.
		const DWORD SaveDataGetGil = 0x00384E80;
		const DWORD SaveDataSetGil = 0x00385C20;
		const DWORD SaveDataSpendGil = 0x003859A0; // shops. Negative amount adds.

		// *** THE inventory commit point ***
		//
		// int (int itemId, int delta). The single add and remove funnel for consumable
		// items. Requires (itemId & 0xFFFFF000) == 0x2000 and delta != 0, else -1.
		// Finds or allocates a slot, clamps the count to 0..99, frees the slot when the
		// count reaches zero, sets the gained or lost mask bit and rebuilds the two
		// menu display lists. Returns 0 on success.
		//
		// Nothing else in the binary writes the id and count arrays except the new-game
		// init and three debug helpers, so a chest in co-op must reach exactly this
		// function exactly once per machine.
		const DWORD SaveDataAddItem = 0x00390550;

		// int (int itemId). Held count, minus one when the id equals the staged item.
		const DWORD SaveDataGetItemCount = 0x003904B0;

		// __int16 *(int *outCount) / char *(int *outCount). Both write 112.
		const DWORD SaveDataGetItemIdArray = 0x00390530;
		const DWORD SaveDataGetItemCountArray = 0x00390510;

		// WORD[256] of 0x2000-based item ids (255 = empty slot) and the parallel
		// BYTE[256] of counts 0..99. Physically 256 entries each, but every gameplay
		// loop in the binary bounds at 112, so treat 112 as the capacity.
		const DWORD ItemIds = 0x00D3095C;    // SaveData+0x3ECC
		const DWORD ItemCounts = 0x00D30B5C; // SaveData+0x40CC

		// 256-bit masks of what went up and down since the menu last looked. UI state,
		// not game state, and single-global, so do not hash them with the rest.
		const DWORD ItemGainedMask = 0x00D30C5C; // SaveData+0x41CC
		const DWORD ItemLostMask = 0x00D30C7C;   // SaveData+0x41EC

		// CO-OP HAZARD. One global staged item id for the whole process, which
		// SaveDataGetItemCount subtracts for with no player index.
		const DWORD ItemInUse = 0x00D2C948;

		// 128-bit flag array of key items held, no counts. Indexed by (id & 0xFFF)
		// which must be below 0x80. The observed id space is 0xA000-based.
		const DWORD KeyItemFlags = 0x00D30F1C; // SaveData+0x448C

		const DWORD SaveDataTestKeyItemFlag = 0x00390800; // BOOL (short id)
		const DWORD SaveDataSetKeyItemFlag = 0x00390930;  // (short id)

		// ---------------------------------------------------------------------------
		// Equipment. 200 entries of 22 bytes at SaveData+0x449C. A character record
		// names a piece by a 16-bit slot id, not by pointer.
		// ---------------------------------------------------------------------------
		const DWORD EquipmentArray = 0x00D30F2C; // SaveData+0x449C

		// char *(int slotId, int *outNameStringBase). Decodes a slot id: 0xFF gives
		// NULL, high nibble 7 and 11 select two 8-entry scratch arrays, anything else
		// selects the main 200-entry array at index (slotId & 0xFFF).
		const DWORD SaveDataGetEquipEntry = 0x003ABBD0;

		// int (unsigned char charIndex, int which, short slotId). which 0 = weapon
		// (record+0x2D), non-zero = armour (record+0x2E). Fixes up the old and new
		// entries' owner byte, so use this rather than writing the record field.
		const DWORD SaveDataSetCharEquip = 0x003AB970; // THE equipment commit

		// int (EquipEntry *src). Copies 22 bytes into the first free slot, marks it in
		// use and unowned, and returns the new slot id 0x5000 + index. 0 when full.
		// The equipment counterpart of SaveDataAddItem, and the same once-per-machine
		// rule applies.
		const DWORD SaveDataAddEquipEntry = 0x003AB910; // a shop buy

		// int (int slotId). Clears entry+2 and unequips it from anyone holding it.
		const DWORD SaveDataRemoveEquipEntry = 0x003ABCA0; // a shop sell

		// int (int *outFree). Returns how many in-use entries are not locked by being
		// equipped, and writes 200 minus the in-use count to outFree.
		const DWORD SaveDataCountEquipEntries = 0x003ABC60;

		// void (void). Rebuilds every entry+6 owner byte from the character records.
		const DWORD SaveDataRebuildEquipOwners = 0x003ABE30;

		// int (EquipEntry *entry, short abilityId). Linear search of the four
		// auto-ability words at entry+0x0E. Ability ids are 0x8000-based.
		const DWORD EquipHasAbility = 0x003A0C20;

		// char *(unsigned index). The 8 x 28 byte equipped-gear stat bonus cache at
		// EquipStatBonus + 28*index, or NULL for index > 7. OUTSIDE the save block, so
		// it is derived state and must not be hashed.
		const DWORD SaveDataGetEquipStatBonus = 0x003987F0;
		const DWORD EquipStatBonus = 0x00D35E00;

		// ---------------------------------------------------------------------------
		// Treasure, the chest side of the inventory.
		//
		// CO-OP HAZARD. TreasureStaging is ONE global decode buffer for the whole
		// process. Both ATEL give-treasure commands call TreasureDecodeToStaging and
		// then read the reward out of it, so two chests resolving in the same frame
		// clobber each other.
		//
		// Reward fields, as offsets into TreasureStaging:
		//   +0xCC dword  gil, passed to SpendGil NEGATED so it adds
		//   +0xD0 byte   non-zero when there is a consumable
		//   +0xD1 byte   non-zero when there is a key item
		//   +0xD2 byte   how many equipment pieces
		//   +0xD4 word   item id
		//   +0xE4 byte   item quantity
		//   +0xEC word   key item id
		//   +0xEE word[] equipment slot ids
		// ---------------------------------------------------------------------------
		const DWORD TreasureStaging = 0x01F10EA0;

		// char *(int treasureId). Decodes one treasure row into TreasureStaging.
		const DWORD TreasureDecodeToStaging = 0x00398FD0;

		// The two script commands that commit a treasure. 347 is the normal chest with
		// the "You got X" window, 423 is the silent one.
		const DWORD AtelGiveTreasureWithMessage = 0x0045A8A0;
		const DWORD AtelGiveTreasureSilent = 0x00457B70;

		// ---------------------------------------------------------------------------
		// Monster Arena and the bestiary. The three blocks between the item masks and
		// the key item flags.
		//
		// MonsterCaptureCounts is BYTE[512] indexed by (monsterId & 0xFFF) and clamped
		// to 0..10, which is the Monster Arena's per-species requirement. The two masks
		// are 512 bits each with the same bit math; which one means "seen" and which
		// "defeated" is not established.
		// ---------------------------------------------------------------------------
		const DWORD MonsterCaptureCounts = 0x00D30C9C; // SaveData+0x420C
		const DWORD MonsterSeenMask = 0x00D30E9C;      // SaveData+0x440C
		const DWORD MonsterMask2 = 0x00D30EDC;         // SaveData+0x444C

		const DWORD SaveDataGetCaptureCount = 0x00390AF0; // int (short monsterId)
		const DWORD SaveDataAddCaptureCount = 0x00390B90; // int (short id, int delta)

		// ---------------------------------------------------------------------------
		// Progression and the shared kernel-table primitive.
		// ---------------------------------------------------------------------------
		const DWORD CharRecords = 0x00D3205C; // SaveData+0x55CC, 18 x 148
		const DWORD CharAbility = 0x00D32AC4; // SaveData+0x6034, 18 x 44
		const DWORD CharNames = 0x00D32DDC;   // SaveData+0x634C, 18 x 20

		// char *(int index, short *table, int *outStringBase). The kernel .bin row
		// lookup every save-data and battle-data reader uses. Header is a DWORD count
		// at +0 then that many 12-byte descriptors from +8, each
		// { WORD lo, WORD hi, WORD stride, WORD stringOffset, DWORD dataOffset }.
		const DWORD KernelTableGetRow = 0x003AB870;

		// int (void). Non-zero while a battle is running. One flag for the whole
		// process, which is what makes SaveDataGetCharCurrentStats ambiguous in co-op.
		const DWORD BattleIsActive = 0x00395970;

		// void *(int unitIndex). The battle actor for ally unit 0..30.
		const DWORD BattleGetActor = 0x00394020;

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// SaveDataSize is deliberately NOT in the list, because it is a byte count
		// rather than an RVA and the check would reject it.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* GameStateRvaList(int* count)
		{
			static const DWORD list[] = {
				SaveData,
				GetSaveData,
				InitNewSaveData,
				SaveDataGetFieldParty,
				SaveDataGetFieldPartyArray,
				SaveDataIsCharInParty,
				SaveDataSetCharInParty,
				BattlePartyOrder,
				BtlCommitPartyToField,
				BtlSetupUnitRoster,
				CharIndexToChrId,
				CharNamesJp,
				DebugCharNames,
				SaveDataGetCharRecord,
				SaveDataGetCharBaseStats,
				SaveDataGetCharCurrentStats,
				SaveDataSetCharCurrentStats,
				SaveDataGetSphereLevels,
				SaveDataSpendSphereLevels,
				SaveDataAddAp,
				SaveDataGetCharName,
				SaveDataTestCharAbility,
				SaveDataSetCharAbility,
				SaveDataGetGil,
				SaveDataSetGil,
				SaveDataSpendGil,
				SaveDataAddItem,
				SaveDataGetItemCount,
				SaveDataGetItemIdArray,
				SaveDataGetItemCountArray,
				ItemIds,
				ItemCounts,
				ItemGainedMask,
				ItemLostMask,
				ItemInUse,
				KeyItemFlags,
				SaveDataTestKeyItemFlag,
				SaveDataSetKeyItemFlag,
				EquipmentArray,
				SaveDataGetEquipEntry,
				SaveDataSetCharEquip,
				SaveDataAddEquipEntry,
				SaveDataRemoveEquipEntry,
				SaveDataCountEquipEntries,
				SaveDataRebuildEquipOwners,
				EquipHasAbility,
				SaveDataGetEquipStatBonus,
				EquipStatBonus,
				TreasureStaging,
				TreasureDecodeToStaging,
				AtelGiveTreasureWithMessage,
				AtelGiveTreasureSilent,
				MonsterCaptureCounts,
				MonsterSeenMask,
				MonsterMask2,
				SaveDataGetCaptureCount,
				SaveDataAddCaptureCount,
				CharRecords,
				CharAbility,
				CharNames,
				KernelTableGetRow,
				BattleIsActive,
				BattleGetActor,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
