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

		// SaveData+0x3D58 is ONE 20-byte array of character record indices, 0xFF empty.
		// [0..2] are the three active field slots and [3..19] the 17 bench slots, which
		// is why SetCharInParty scans 20 and the party menu passes 3+bench as a slot.
		// Three active members is the array's own limit, not a soft rule.
		const DWORD FieldPartyActive = 0x00D307E8; // BYTE[3]
		const DWORD FieldPartyBench = 0x00D307EB;  // BYTE[17], contiguous with the above

		// int (int charIndex, int slot). THE party setter. Returns -1 applied and 0
		// REFUSED, and a refusal is otherwise silent. It refuses when either the
		// incoming character or the current occupant has the locked bit set, and it
		// refuses a character who is neither already in the order array nor in party.
		//
		// NO UPPER BOUND ON slot, only slot >= 0. A slot of 20 or more writes past the
		// array into the rest of the save block. Clamp to 0..19 yourself.
		const DWORD SaveDataSetPartyOrderSlot = 0x00384D00;

		// int (int s0, int s1, int s2). The engine's own three-at-once setter, which is
		// exactly SetPartyOrderSlot(s0,0), (s1,1), (s2,2). Prefer it over writing the
		// three bytes, because it keeps the bench a consistent partition.
		const DWORD SaveDataSetFieldParty3 = 0x00386950;

		// BYTE *(int *outCount). Returns FieldPartyBench and writes 17.
		const DWORD SaveDataGetPartyBenchArray = 0x003852D0;

		// The character record flags byte, record stride 148. Bit 0 in party, bit 1
		// permanent, bit 2 LOCKED INTO ITS SLOT, bit 4 selectable. Bit 2 is what makes a
		// party write silently do nothing, so a cheat that wants to move a story-locked
		// member has to clear it, and there is no setter for it.
		const DWORD CharRecordFlags = 0x00D32088; // BYTE, + 148 * charIndex
		const DWORD SaveDataGetCharFlagBit2 = 0x00385360;  // BOOL (unsigned char)
		const DWORD SaveDataIsCharPermanent = 0x003853C0;  // BOOL (unsigned char), bit 1
		const DWORD SaveDataIsCharSelectable = 0x003853A0; // BOOL (unsigned char), bit 4

		// char *(int unitIndex). The localised name, and the right label for a picker
		// because it covers all 18 including the aeons where DebugCharNames covers 8.
		//
		// ITS GUARD IS index <= 30, NOT 17. GetCharName(18) hands back the save block's
		// CRC field read as a string. Bound to 0..17 in the caller.
		const DWORD GetUnitDisplayName = 0x004AC850;

		// int (void). SaveData+0xD1. Non-zero means Rikku wears the alternate outfit,
		// which is why BtlResolveUnitChrId hands out c041 instead of c007 for her.
		const DWORD SaveDataGetRikkuAltOutfit = 0x0046A7E0;

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

		// int (unsigned char charIndex). THE authority on the record layout. Rebuilds
		// the derived stats, max HP and max MP from the base fields plus EquipStatBonus.
		const DWORD SaveDataRecomputeCharDerived = 0x003860F0;

		// char *(int charIndex). The menu's wrapper: recompute, then re-clamp current
		// HP and MP. SaveDataRecomputeAllCharDerived is declared in
		// addresses/MenuSystem.h.
		const DWORD MenuRecomputeCharAndClamp = 0x004C3070;

		// int (void). Refills all 18 name records from ply_save.bin, which is the
		// reset-name path. SaveDataReencodeCharNames is in addresses/WorldState.h.
		const DWORD SaveDataReloadCharNames = 0x00387100;

		// unsigned (int charIndex). AP needed for the next sphere level.
		const DWORD SaveDataApNeededForNextLevel = 0x00384E90;

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

		// int (void). Rebuilds the two menu display lists. Needed after a direct write
		// to the id or count arrays instead of SaveDataAddItem.
		const DWORD SaveDataRebuildItemLists = 0x003906A0;

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

		// short (unsigned charIndex, int abilityId). ORs a bit into the ability bitmap
		// at EquipStatBonus + 28*index + 0x10. Ignores any id that is not 0x3000-based.
		// SaveDataClearEquipStatBonus is declared in addresses/WorldState.h.
		const DWORD SaveDataSetEquipStatBonusAbility = 0x00398840;

		// int (EquipEntry *entry). An equipment name is a FUNCTION of its ability set:
		// 0x5000 to 0x5046 for a weapon, 0x504A to 0x509E for an armour.
		const DWORD EquipComputeNameId = 0x003A0CF0;

		// int (short nameId, int charIndex, int variant, WORD *outIcon) and
		// int (int slotId). Both return a char * out of w_name.bin. The second takes
		// an owned slot id, which is what a list UI has.
		const DWORD EquipGetNameString = 0x003A0C50;
		const DWORD SaveDataGetEquipNameString = 0x003ABDF0;

		// BOOL (EquipEntry *entry) and int (EquipEntry *entry, int force). The game's
		// own "has a free ability slot" and "can be customised" tests.
		const DWORD EquipHasFreeAbilitySlot = 0x004BF720;
		const DWORD MenuCustomizeIsEntryEligible = 0x004D5750;

		// char *(int *outCount). Row 0 of kaizou.bin and its count. Rows are 8 bytes:
		// WORD kind (1 weapon, 2 armour), WORD abilityId, WORD itemId, BYTE quantity.
		// THE enumerable auto-ability list, because nothing else says weapon or armour.
		const DWORD EquipGetCustomizeTable = 0x00390A10;
		const DWORD EquipCustomizeTable = 0x00D2A964; // short *, 125 x 8

		// EquipAddAbilityToEntry and EquipRefreshEntryNameId are declared in
		// addresses/MenuSystem.h. Call them in that order or the name goes stale.

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

		// The two tables behind a chest, both loaded from asset class 13 by
		// LoadTreasureTables. The equipment one holds 22-byte rows in the same shape as
		// an EquipEntry, and TreasureGiveReward case 5 is the recipe for turning one
		// into a new entry.
		const DWORD LoadTreasureTables = 0x00399020;     // int (void)
		const DWORD TreasureGiveReward = 0x00399420;     // void (int kind, int arg)
		const DWORD TreasureTable = 0x00D35FF4;          // BYTE *, 4-byte rows
		const DWORD TreasureTableSize = 0x00D35FF0;      // DWORD, byte count
		const DWORD EquipTemplateTable = 0x00D35FFC;     // BYTE *, 22-byte rows
		const DWORD EquipTemplateTableSize = 0x00D35FF8; // DWORD, byte count

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
		// The sphere grid. 4896 bytes at SaveData+0x21EC:
		//   +0x0000 1280 node slots of 2 bytes, [0] the panel kind (0xFF = empty),
		//           [1] a bitmask of which of characters 0..6 have activated it
		//   +0x0A00 1280 link bytes, a per-character trail mask, cosmetic only
		//   +0x0F00 7 WORDs, each character's current node index
		//   +0x0F18 BYTE grid id, +0x0F19 BYTE zoom level
		//
		// Setting an activation bit is the whole of "activated". Everything a node
		// gives is recomputed from those bits by SphereGridRecomputeDerived, declared
		// in addresses/WorldState.h, and that is the one call an edit must be followed
		// by. Note it only scans node slots 0..1023 of the 1280.
		// ---------------------------------------------------------------------------
		const DWORD SphereGridNodes = 0x00D2EC7C;            // SaveData+0x21EC
		const DWORD SaveDataGetSphereGridNodes = 0x00384F40; // char *(void)

		// char (short *nodes). New game only. Memsets 4896 bytes, then fills the kinds
		// and the start positions from the layout assets.
		const DWORD SphereGridInitNodes = 0x00653DE0;

		// int (void). Allocates and fills KernelTablePanel and KernelTableSphere, both
		// of which are null until the grid screen has run. Calling it twice leaks.
		const DWORD SphereGridLoadPanelTables = 0x00654810;

		// int (void). Parses the grid layout asset into MenuWork: 40-byte node records
		// at +0x808, 20-byte link records at +0xA808, counts at +2 and +4.
		const DWORD SphereGridLoadLayout = 0x00645570;

		// int (void) each. The save blob into the menu's working copy and back again.
		// The menu copy wins on the way out, so do not edit the blob with the grid up.
		const DWORD SphereGridSaveToMenu = 0x00649590;
		const DWORD SphereGridMenuToSave = 0x0065BB70;

		// DWORD at SaveData+0x3D0C. Bits 14..15 pick which grid the save uses, 0 being
		// Standard. Bit 3 picks the alternate name strings in the kernel tables.
		const DWORD SaveDataOptionFlags = 0x00D3079C;

		// WORD[7] start node per character, and WORD *[7] of 0xFFFF-terminated
		// pre-activated node lists, for the Standard grid. The other two grids are at
		// +0x10 and +0x20, and at +0x48 and +0x90.
		const DWORD SphereGridStartNodes = 0x00886C00;
		const DWORD SphereGridStartActivated = 0x00886C5C;

		// char *[70], the eiichi_abmap_data file names. 69 art, one data file.
		const DWORD SphereGridAssetNames = 0x00885EF0;

		// ---------------------------------------------------------------------------
		const DWORD CharRecords = 0x00D3205C; // SaveData+0x55CC, 18 x 148
		const DWORD CharAbility = 0x00D32AC4; // SaveData+0x6034, 18 x 44
		const DWORD CharNames = 0x00D32DDC;   // SaveData+0x634C, 18 x 20

		// char *(int index, short *table, int *outStringBase). The kernel .bin row
		// lookup every save-data and battle-data reader uses. Header is a DWORD count
		// at +0 then that many 12-byte descriptors from +8, each
		// { WORD lo, WORD hi, WORD stride, WORD stringOffset, DWORD dataOffset }.
		const DWORD KernelTableGetRow = 0x003AB870;

		// int (int table, int rangeIndex). hi - lo + 1 of that descriptor. Pass 0. Use
		// it instead of a hardcoded row count.
		const DWORD KernelTableRowCount = 0x003AB8F0;

		// unsigned (const char *name, void *dest) and int (int which). The file reader
		// and the six-case loader that makes the pointers below resident.
		const DWORD BtlReadKernelBin = 0x00382DF0;
		const DWORD BtlLoadKernelTables = 0x00381D40;

		// ---------------------------------------------------------------------------
		// THE resident table pointers, all short * for KernelTableGetRow. Every
		// shipped table has one range starting at 0, so row i is at *ptr + 20 + i*
		// stride and a name is stringBase + *(WORD *)(row + 0).
		//
		// command.bin (0x3xxx) is BtlPlayerAbilityTable and a_ability.bin (0x8xxx) is
		// BtlAbilityEffectTable, both declared in addresses/Battle.h.
		// ---------------------------------------------------------------------------
		const DWORD KernelTableItem = 0x00D2A940;      // item.bin, 112 x 96, 0x2xxx
		const DWORD KernelTableWName = 0x00D363B4;     // w_name.bin, 170 x 72, 0x5xxx
		const DWORD KernelTableImportant = 0x00D334C0; // important.bin, 64 x 20, 0xAxxx
		const DWORD KernelTablePlyRom = 0x00D2A938;    // ply_rom.bin, 20 x 44
		const DWORD KernelTableMonMagic1 = 0x00D2A930; // 300 x 92, 0x4xxx
		const DWORD KernelTableMonMagic2 = 0x00D2A934; // 247 x 92, 0x6xxx
		const DWORD KernelTablePanel = 0x016860E0;     // panel.bin, 127 x 24
		const DWORD KernelTableSphere = 0x016860E4;    // sphere.bin, 50 x 16

		// int (int id), returning a char *. THE one name function: it handles a 0x2xxx
		// item, a 0x3xxx command, a 0x4xxx or 0x6xxx monster ability and an 0x8xxx
		// auto-ability. The second is int (short id) for 0xAxxx key items.
		// MenuGetAbilityName and MenuGetAbilityHelp are in addresses/Battle.h.
		const DWORD BtlGetAbilityNameString = 0x004B8D70;
		const DWORD KeyItemGetNameString = 0x00390860;

		// important.bin declares 64 rows and about 52 of them are named, so a walk of
		// the whole range turns up the unnamed slots as well. The name comes back in the
		// battle kernel's own text encoding, NOT as plain ASCII, so it needs
		// ffx::DecodeKernelText the same as any other kernel string.
		const int KeyItemIdBase = 0xA000;
		const int KeyItemCount = 64;

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
				FieldPartyActive,
				FieldPartyBench,
				SaveDataSetPartyOrderSlot,
				SaveDataSetFieldParty3,
				SaveDataGetPartyBenchArray,
				CharRecordFlags,
				SaveDataGetCharFlagBit2,
				SaveDataIsCharPermanent,
				SaveDataIsCharSelectable,
				GetUnitDisplayName,
				SaveDataGetRikkuAltOutfit,
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
				SaveDataRecomputeCharDerived,
				MenuRecomputeCharAndClamp,
				SaveDataReloadCharNames,
				SaveDataApNeededForNextLevel,
				SaveDataRebuildItemLists,
				SaveDataSetEquipStatBonusAbility,
				EquipComputeNameId,
				EquipGetNameString,
				SaveDataGetEquipNameString,
				EquipHasFreeAbilitySlot,
				MenuCustomizeIsEntryEligible,
				EquipGetCustomizeTable,
				EquipCustomizeTable,
				LoadTreasureTables,
				TreasureGiveReward,
				TreasureTable,
				TreasureTableSize,
				EquipTemplateTable,
				EquipTemplateTableSize,
				SphereGridNodes,
				SaveDataGetSphereGridNodes,
				SphereGridInitNodes,
				SphereGridLoadPanelTables,
				SphereGridLoadLayout,
				SphereGridSaveToMenu,
				SphereGridMenuToSave,
				SaveDataOptionFlags,
				SphereGridStartNodes,
				SphereGridStartActivated,
				SphereGridAssetNames,
				KernelTableRowCount,
				BtlReadKernelBin,
				BtlLoadKernelTables,
				KernelTableItem,
				KernelTableWName,
				KernelTableImportant,
				KernelTablePlyRom,
				KernelTableMonMagic1,
				KernelTableMonMagic2,
				KernelTablePanel,
				KernelTableSphere,
				BtlGetAbilityNameString,
				KeyItemGetNameString,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
