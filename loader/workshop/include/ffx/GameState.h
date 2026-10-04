#pragma once

#include <windows.h>

// Reading and writing the game state: party, per-character stats, inventory,
// gil, equipment and progression.
//
// Everything here goes through workshop::Readable, because the whole save block
// is a static buffer that exists before a game is loaded but is all zeroes until
// one is. So "the pointer is valid" and "there is a game" are two different
// questions, and GameLoaded() answers the second one.
//
// Nothing in this header calls a game function. The two write paths that MUST go
// through the game's own code, because they maintain caches and derived lists
// that nothing else rebuilds, are AddItem and AddEquipment, and those are
// wrapped in GameState.cpp over the resolved function pointers.
//
// The derivation is in ..\..\..\reversing\GAME_STATE.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Identities
	//
	// A CHARACTER INDEX is a slot in the 18-entry character record array. It is NOT
	// a chr id and it is NOT a CHR pool slot. Three different numbers name the same
	// person and keeping them apart is the whole game here:
	//
	//   character index  0..17, indexes g_ffxCharRecords. The stats, the equipment,
	//                    the name, the party flags. Persistent.
	//   chr id           the model/archive id FFX_Ch_Allocate takes. Derived from a
	//                    character index through ChrIdForCharacter below.
	//   CHR pool slot    where the live actor happens to sit this frame. See
	//                    ffx/Character.h. Not stable across a map transition.
	// ---------------------------------------------------------------------------
	enum CharacterIndex
	{
		kCharTidus = 0,
		kCharYuna = 1,
		kCharAuron = 2,
		kCharKimahri = 3,
		kCharWakka = 4,
		kCharLulu = 5,
		kCharRikku = 6,
		kCharSeymour = 7,
		// 8..17 are the ten aeons, in the order Valefor, Ifrit, Ixion, Shiva,
		// Bahamut, Anima, Yojimbo, Cindy, Sandy, Mindy.
		kCharFirstAeon = 8,
		kCharCount = 18,
		// The playable set for equipment and ability purposes is 0..6. Seymour is
		// index 7 and the game's own equipment and ability loops skip him.
		kCharPlayableCount = 7,
		kCharNone = 0xFF
	};

	// There are at most three active members, which is why the co-op model caps at
	// three players.
	const int kActivePartySize = 3;

	// The battle party roster, active members plus reserves.
	const int kBattlePartySize = 7;

	// SaveData+0x3D58 is one 20-byte array, the three active slots then 17 bench
	// slots. The engine's own setter bounds only on slot >= 0, so the upper bound is
	// the caller's job.
	const int kPartyOrderSlots = 20;
	const int kBenchPartySize = 17;

	// Inventory. The arrays are physically 256 entries but every gameplay loop in
	// the binary bounds at 112, so 112 is the capacity that matters.
	const int kItemSlots = 112;
	const int kItemSlotsPhysical = 256;
	const int kItemCountMax = 99;
	const int kItemIdNone = 0xFF;
	const DWORD kItemIdSpace = 0x2000; // (id & 0xFFFFF000) must equal this

	const int kEquipSlots = 200;
	const int kEquipEntrySize = 22;
	const int kEquipAbilitySlots = 4;
	const DWORD kAbilityIdSpace = 0x8000; // auto-ability ids on equipment
	const int kEquipSlotIdNone = 0xFF;

	const int kGilMax = 999999999;

	// ---------------------------------------------------------------------------
	// Field offsets. Grouped per structure, in the style of ffx/Layout.h, so a
	// wrong offset is one edit rather than a scattered cast.
	// ---------------------------------------------------------------------------

	// A character record. 0x94 = 148 bytes, 18 of them.
	namespace CharRecord
	{
		const DWORD Size = 0x94;

		const DWORD BaseMaxHp = 0x04;    // dword
		const DWORD BaseMaxMp = 0x08;    // dword
		const DWORD BaseStats = 0x0C;    // 8 bytes, see StatOrder below
		const DWORD TotalAp = 0x14;      // dword
		const DWORD ApToNextSLv = 0x18;  // dword
		const DWORD CurrentHp = 0x1C;    // dword
		const DWORD CurrentMp = 0x20;    // dword
		const DWORD MaxHp = 0x24;        // dword, bonuses included
		const DWORD MaxMp = 0x28;        // dword, bonuses included
		const DWORD Flags = 0x2C;        // byte, see the Flag constants
		const DWORD WeaponSlot = 0x2D;   // byte, equipment slot id, 0xFF = none
		const DWORD ArmourSlot = 0x2E;   // byte, equipment slot id, 0xFF = none
		const DWORD Stats = 0x2F;        // 8 bytes, effective, same order
		const DWORD SphereLevels = 0x3B; // byte, available S.Lv
		const DWORD SphereSpent = 0x3C;  // byte
		const DWORD ReviveCount = 0x3D;  // byte, carried out of battle
		const DWORD AbilityWords = 0x3E; // 6 words, ability ids 0x3000..0x305F

		// Party flags at +0x2C.
		const BYTE FlagInParty = 0x01;   // the only membership record there is
		const BYTE FlagPermanent = 0x02; // cannot be removed from the party

		// Locked into its party slot. SaveDataSetPartyOrderSlot refuses any swap where
		// either side has this, which is the story-forced member lock and the most
		// likely reason a party write appears to do nothing at all.
		const BYTE FlagSlotLocked = 0x04;

		const BYTE FlagSelectable = 0x10; // set and cleared with FlagInParty
	}

	// The eight stat bytes, in the order they sit in a record at both +0x0C and
	// +0x2F, and in the order FFX_SaveData__getCharBaseStats reports them.
	enum StatOrder
	{
		kStatStrength = 0,
		kStatDefence = 1,
		kStatMagic = 2,
		kStatMagicDefence = 3,
		kStatAgility = 4,
		kStatLuck = 5,
		kStatEvasion = 6,
		kStatAccuracy = 7,
		kStatCount = 8
	};

	// One piece of equipment. 22 bytes, 200 of them.
	namespace EquipEntry
	{
		const DWORD Size = 22;

		const DWORD NameId = 0x00;     // word, derived from the ability set
		const DWORD InUse = 0x02;      // byte, 0 means the slot is free
		const DWORD Flags = 0x03;      // byte
		const DWORD ForChar = 0x04;    // byte, which character the model is for
		const DWORD Kind = 0x05;       // byte, 0 = weapon, 1 = armour
		const DWORD OwnerChar = 0x06;  // byte, who has it equipped, 0xFF = nobody
		const DWORD ActorF1473 = 0x08; // byte, copied to battle actor+1473
		const DWORD ActorF1479 = 0x09; // byte, copied to battle actor+1479
		const DWORD ActorF1496 = 0x0A; // byte, added into battle actor+1496
		const DWORD ModelId = 0x0C;    // word, per-character model/name id
		const DWORD Abilities = 0x0E;  // 4 words of 0x8000-based ability ids

		const BYTE FlagEquipped = 0x02; // locked, somebody is wearing it
		const BYTE FlagSpecial = 0x08;

		const BYTE KindWeapon = 0;
		const BYTE KindArmour = 1;

		// An empty ability slot reads as either of these.
		const WORD AbilityNoneA = 0x0000;
		const WORD AbilityNoneB = 0x00FF;
	}

	// A character name entry. 20 bytes, 18 of them.
	namespace CharName
	{
		const DWORD Size = 20;
		const DWORD Text = 0;      // 18 bytes
		const DWORD Language = 18; // byte, the language it was written in
		const DWORD Renamed = 19;  // byte, the player changed it
	}

	// Offsets inside the save block, for anything that is not reached through a
	// typed accessor below.
	namespace SaveBlock
	{
		const DWORD Size = 0x68C0;

		const DWORD LoveParam = 0x002C;      // dword[8]
		const DWORD RikkuAltOutfit = 0x00D1; // byte, non-zero -> Rikku chr id 41
		// WAS NAMED CurrentMapId AND THAT WAS WRONG. This is the CHECKPOINT, the
		// save-point location, packed as entry point in the LOW word and map id in the
		// HIGH word. FFX_Map_RequestResumeFromCheckpoint 0x88DD60 unpacks it, copies the
		// halves to the live fields below, and then ZEROES this one, so it is not even
		// valid most of the time.
		//
		// The live location is LiveMapId and LiveEntryPoint. Anything asking "where is the
		// player right now" wants those, not this.
		const DWORD CheckpointMapAndEntry = 0x00B8; // word entry, word map. Consumed on use

		// Where the player actually is. Written by the map change path, and the two fields
		// FFX_Map_RequestResumeFromCheckpoint writes the checkpoint into.
		const DWORD LiveMapId = 0x0000;         // word
		const DWORD LivePreviousMapId = 0x0002; // word, from the +0xC0 history on a resume
		const DWORD LiveEntryPoint = 0x000C;    // byte
		const DWORD LiveEntrySubId = 0x000D;    // byte

		// The game's own "I was just transplanted" flag. Set on every save file and
		// consumed once by FFX_SaveData_SetSceneAndSub 0x88EAF0, which then takes the
		// previous-scene fields from the +0xC0 history instead of from the live fields.
		// A runtime block transplant should set this, because that is exactly the
		// situation it exists for.
		const DWORD JustLoadedFlag = 0x002A;  // byte
		const DWORD Playtime = 0x00BC;        // dword, ticks every frame
		const DWORD Scenario = 0x0BEC;        // word
		const DWORD PartyDataBlock = 0x3D0C;  // 0x1C, kernel "party_data"
		const DWORD KernelTable31 = 0x3D28;   // 0x20
		const DWORD ConfBlock = 0x3D48;       // 0x84, kernel "conf"
		const DWORD Gil = 0x3D48;             // dword, first field of conf
		// One 20-byte array: 3 active slots then 17 bench slots, character record
		// indices with 0xFF for empty. See kPartyOrderSlots.
		const DWORD FieldParty = 0x3D58;      // BYTE[3], THE ACTIVE PARTY
		const DWORD PartyBench = 0x3D5B;      // BYTE[17], contiguous with the above
		const DWORD AeonOrder = 0x3D5B;       // 17 bytes
		const DWORD SharedAbilities = 0x3D6C; // 32 bytes, ability ids 0x3060+
		const DWORD StartItemIds = 0x3D8C;    // word[8], new-game starting items
		const DWORD StartItemCounts = 0x3D9C; // byte[8]
		const DWORD ItemIds = 0x3ECC;         // word[256], 112 used
		const DWORD ItemCounts = 0x40CC;      // byte[256], 112 used
		const DWORD ItemGainedMask = 0x41CC;  // 32 bytes
		const DWORD ItemLostMask = 0x41EC;    // 32 bytes
		const DWORD MonsterCaptures = 0x420C; // byte[512], 0..10 each
		const DWORD MonsterSeenMask = 0x440C; // 64 bytes, 512 flags
		const DWORD MonsterMask2 = 0x444C;    // 64 bytes, 512 flags
		const DWORD KeyItemFlags = 0x448C;    // 16 bytes, 128 flags
		const DWORD Equipment = 0x449C;       // 200 x 22
		const DWORD CharRecords = 0x55CC;     // 18 x 148
		const DWORD CharAbilities = 0x6034;   // 18 x 44
		const DWORD CharNames = 0x634C;       // 18 x 20
		const DWORD Tail = 0x64B4;
	}

	// ---------------------------------------------------------------------------
	// Opaque handles. Every field goes through an accessor, so a wrong offset is
	// one edit.
	// ---------------------------------------------------------------------------
	struct CharRecordData;
	struct EquipEntryData;

	// ---------------------------------------------------------------------------
	// Is there a game to read?
	//
	// The save block is a static buffer, so a plain Readable check passes even at
	// the title screen. GameLoaded additionally checks that the block is not all
	// zero in the places a loaded game is never zero, which is the question a mod
	// actually wants answered.
	// ---------------------------------------------------------------------------
	bool SaveBlockReadable();
	bool GameLoaded();

	// The block base, or NULL when it is not readable. Use this rather than
	// resolving the RVA yourself, so the Readable check cannot be skipped.
	BYTE* SaveBlockBase();

	// ---------------------------------------------------------------------------
	// The party
	// ---------------------------------------------------------------------------

	// The character index in active slot 0..2, or kCharNone. Returns kCharNone for
	// an out of range slot or when there is no game.
	BYTE ActivePartyMember(int slot);

	// Which active slot holds this character, or -1. This is the lookup the
	// ownership table wants: given a character, which player owns it.
	int ActiveSlotOfCharacter(BYTE charIndex);

	// Writes all three at once. Each may be kCharNone. Returns false when the block
	// is not writable. This writes the save block directly, which is correct
	// outside battle; inside a battle the engine has already copied the party into
	// its own work RAM, so see BattlePartyMember.
	bool SetActiveParty(BYTE slot0, BYTE slot1, BYTE slot2);

	// The same three slots through the engine's own setter, which is the one to use
	// from a UI. It keeps the 17-slot bench a consistent partition of the 20-byte
	// order array, where writing the bytes raw can leave a character in both halves.
	//
	// It also REFUSES silently in two cases, and this reports that: a character who
	// is not in the party at all, and either side of the swap having the locked bit
	// set. outRefused, when given, gets a bit per slot that was refused.
	//
	// Does NOT make the change visible on a map that is already loaded. Follow it
	// with RefreshPartyVisibility.
	bool SetFieldParty(BYTE slot0, BYTE slot1, BYTE slot2, int* outRefused = nullptr);

	// One slot of the 20-byte order array, 0..2 active and 3..19 bench. False when
	// the engine refused it.
	bool SetPartyOrderSlot(BYTE charIndex, int slot);

	// The bench, the 17 slots after the three active ones.
	BYTE BenchPartyMember(int slot);

	// Adds or removes a character through the game's own setter, which also drives
	// the equipment lock bit and the aeon order array, so it beats poking the flags
	// byte. Clamped to 0..17 here, because the engine's own version has no bound at
	// all and an index of 255 writes about 37 KB into the save block.
	bool SetCharacterInParty(BYTE charIndex, bool inParty);

	// The other three flag bits. Locked is the one that matters: SetPartyOrderSlot
	// refuses any swap where either side has it, which is the story-forced member
	// lock, and it is the most likely reason a party write appears to do nothing.
	bool CharacterPermanent(BYTE charIndex);
	bool CharacterSelectable(BYTE charIndex);
	bool CharacterLockedInSlot(BYTE charIndex);

	// Clears or sets the locked bit. There is no engine setter for it, so this pokes
	// the flags byte, which is why it is a separate call from the rest.
	bool SetCharacterLockedInSlot(BYTE charIndex, bool locked);

	// Rebuilds the party visible mask and so makes a field party change show up on
	// the map that is already loaded. Writing the party bytes alone does not.
	// GAME THREAD ONLY, it walks the actor table.
	bool RefreshPartyVisibility();

	// The localised name for a character index, bounded to 0..17 here because the
	// engine's own guard is 30 and index 18 hands back the save block CRC read as a
	// string. NULL out of range. This is the right label for a picker, where
	// DebugCharNames only covers the first eight and is not localised.
	const char* UnitDisplayName(BYTE charIndex);

	// The battle-side roster, 7 entries, [0..2] active. Lives outside the save
	// block in battle work RAM, and is only meaningful while a battle is running.
	BYTE BattlePartyMember(int slot);

	// record[0x2C] & FlagInParty. Membership is a flag on the record, there is no
	// roster array.
	bool CharacterInParty(BYTE charIndex);

	// How many of the 18 records have FlagInParty set.
	int PartyMemberCount();

	// ---------------------------------------------------------------------------
	// Which character is which
	// ---------------------------------------------------------------------------

	// The chr id FFX_Ch_Allocate wants for this character index, read out of the
	// game's own 18-entry table rather than hardcoded. 0 when unavailable.
	//
	// Pass wantAlternate to get Rikku's chr id 41 instead of 7. Passing false
	// returns the table entry unchanged, which is 7 for Rikku. RikkuAltOutfit()
	// reports what the game itself would pick.
	DWORD ChrIdForCharacter(BYTE charIndex, bool wantAlternate);

	// Non-zero byte at SaveBlock::RikkuAltOutfit, which is what makes the game give
	// Rikku chr id 41.
	bool RikkuAltOutfit();

	// The inverse, by scanning the table. kCharNone when the chr id is not a
	// character. Accepts the 101..108 high-detail variants by subtracting 100 and
	// accepts 41 as Rikku.
	BYTE CharacterForChrId(DWORD chrId);

	// A stable English name for a character index. Static storage, never NULL, and
	// it does NOT read the game, so it works before a game is loaded. For the
	// player-facing name, which may have been edited in the rename screen, use
	// CharacterDisplayName.
	const char* CharacterName(BYTE charIndex);

	// The player-facing name out of the save block, written into out as a NUL
	// terminated string. outBytes should be at least 19. Returns false and leaves
	// out alone when there is no game. The game's own field is 18 bytes and is not
	// guaranteed to be terminated, which is why this copies rather than returning a
	// pointer.
	bool CharacterDisplayName(BYTE charIndex, char* out, int outBytes);

	// ---------------------------------------------------------------------------
	// Per-character stats
	// ---------------------------------------------------------------------------

	// base + 148*charIndex, or NULL for an out of range index or no game.
	CharRecordData* CharacterRecord(BYTE charIndex);

	// Field readers. All take the record rather than an index, so a caller that
	// wants five fields pays for one bounds check. A NULL record reads as 0.
	DWORD RecordCurrentHp(const CharRecordData* rec);
	DWORD RecordMaxHp(const CharRecordData* rec);
	DWORD RecordCurrentMp(const CharRecordData* rec);
	DWORD RecordMaxMp(const CharRecordData* rec);
	DWORD RecordBaseMaxHp(const CharRecordData* rec);
	DWORD RecordBaseMaxMp(const CharRecordData* rec);
	DWORD RecordTotalAp(const CharRecordData* rec);
	DWORD RecordApToNext(const CharRecordData* rec);

	// stat is a StatOrder value. Stat() is the effective value at +0x2F and
	// BaseStat() the pre-bonus value at +0x0C.
	BYTE RecordStat(const CharRecordData* rec, int stat);
	BYTE RecordBaseStat(const CharRecordData* rec, int stat);

	BYTE RecordFlags(const CharRecordData* rec);
	BYTE RecordSphereLevels(const CharRecordData* rec);
	BYTE RecordSphereSpent(const CharRecordData* rec);
	BYTE RecordWeaponSlot(const CharRecordData* rec);
	BYTE RecordArmourSlot(const CharRecordData* rec);

	// Writers. These touch only the save block and maintain nothing else, which is
	// right for HP and MP but not for equipment, so there is deliberately no
	// equip writer here. Use EquipItemOn below.
	bool SetRecordCurrentHp(CharRecordData* rec, DWORD hp);
	bool SetRecordCurrentMp(CharRecordData* rec, DWORD mp);

	// The game's own composite readers, which is what the menus use. Prefer these
	// when you want "what the game thinks", because they fold in the equipped-gear
	// bonus and, for CurrentStats, the in-battle copy.
	struct BaseStats
	{
		int stats[kStatCount]; // bonus included
		int maxHp;
		int maxMp;
	};
	bool ReadBaseStats(BYTE charIndex, BaseStats* out);

	struct CurrentStats
	{
		BYTE sphereLevels;
		DWORD currentHp;
		DWORD currentMp;
		BYTE stats[kStatCount];
	};
	// WARNING, and this is a co-op hazard, not a caveat: the game function this
	// wraps reads the battle actor instead of the save record whenever a battle is
	// active, and "a battle is active" is one flag for the whole process. With one
	// player in battle and another in the field this does not have a per-player
	// answer. For a deterministic read of the stored value, use the Record*
	// accessors instead.
	bool ReadCurrentStats(BYTE charIndex, CurrentStats* out);

	// ---------------------------------------------------------------------------
	// Inventory and gil
	// ---------------------------------------------------------------------------

	DWORD Gil();
	bool SetGil(DWORD amount); // clamped to kGilMax by the game
	bool SpendGil(int cost);   // true when it was affordable
	bool AddGil(int amount);   // SpendGil with the sign flipped

	// Slot readers. slot is 0..kItemSlots-1.
	WORD ItemIdAt(int slot); // kItemIdNone when empty
	BYTE ItemCountAt(int slot);

	// Held count of one item id. This is the game's own reader, so it subtracts one
	// for the globally staged item, which is a co-op hazard in itself. For a
	// deterministic count use CountItemInSlots.
	int ItemCount(WORD itemId);

	// Sums the slots holding itemId without consulting any global. There is only
	// ever one slot per id in practice, but summing is free and cannot be wrong.
	int CountItemInSlots(WORD itemId);

	// How many of the 112 slots are free.
	int FreeItemSlots();

	// *** The commit point. ***
	//
	// Calls FFX_SaveData_AddItem, which is the single add and remove funnel in the
	// binary. Pass a negative delta to remove. Returns true when the game returned
	// 0, meaning the change landed.
	//
	// Everything a co-op build does to the inventory should go through here,
	// because the game function also maintains the gained and lost masks and
	// rebuilds the two menu display lists, and nothing else does. Writing the id
	// and count arrays by hand leaves the menus showing stale rows.
	//
	// It is not idempotent. Calling it twice adds twice. That is exactly why a
	// chest in co-op has to reach it once per machine and no more.
	bool AddItem(WORD itemId, int delta);

	// Key items are flags, not counts.
	bool HasKeyItem(WORD keyItemId);
	bool SetKeyItem(WORD keyItemId, bool held);

	// ---------------------------------------------------------------------------
	// Monster Arena and the bestiary
	// ---------------------------------------------------------------------------

	// How many of this species have been captured, 0..10. The Monster Arena wants
	// ten, which is where the cap comes from.
	int MonsterCaptureCount(WORD monsterId);

	// Adds to the capture count, clamped to 0..10 by the game, and returns the new
	// value. Pass a negative delta to take some back.
	int AddMonsterCaptures(WORD monsterId, int delta);

	// Has this species been encountered? The bestiary "seen" list.
	bool MonsterSeen(WORD monsterId);

	// ---------------------------------------------------------------------------
	// Equipment
	// ---------------------------------------------------------------------------

	// Decodes a 16-bit equipment slot id. Handles the two scratch arrays (high
	// nibbles 7 and 11) and kEquipSlotIdNone. NULL when there is nothing there.
	EquipEntryData* EquipEntryFromSlotId(WORD slotId);

	// Direct index into the 200-entry save-block array, for walking it.
	EquipEntryData* EquipEntryAt(int index);

	bool EquipEntryInUse(const EquipEntryData* e);
	BYTE EquipEntryKind(const EquipEntryData* e);    // 0 weapon, 1 armour
	BYTE EquipEntryForChar(const EquipEntryData* e); // whose model it is
	BYTE EquipEntryOwner(const EquipEntryData* e);   // who has it on, 0xFF none
	WORD EquipEntryNameId(const EquipEntryData* e);
	WORD EquipEntryModelId(const EquipEntryData* e);
	BYTE EquipEntryFlags(const EquipEntryData* e);

	// The four auto-ability slots. index is 0..3. Returns 0 for an empty slot,
	// normalising the game's two different empty markers to 0.
	WORD EquipEntryAbility(const EquipEntryData* e, int index);

	// How many of the four are filled.
	int EquipEntryAbilityCount(const EquipEntryData* e);

	// The slot id for an array index, which is what every setter below wants. The
	// game's own adder returns 0x5000 plus the index and its decoder takes the index
	// back out with slotId & 0xFFF, so high nibble 5 names the main 200 entry array.
	WORD EquipSlotId(int index);

	// The game's own name for a piece, out of w_name.bin. An FFX equipment name is a
	// FUNCTION of the ability set rather than a stored string, and the row holds one
	// name per character, so this is the only correct source for a label. NULL when
	// there is no game or the slot id is not a real one. Points into an engine buffer
	// that the next call overwrites, so copy it.
	const char* EquipName(WORD slotId);

	// Writes one ability word directly. This is the only way to clear or replace a
	// slot, because no engine function removes an auto-ability. Pass 0 to clear.
	// Refreshes the name afterwards, which is not optional.
	bool SetEquipEntryAbility(EquipEntryData* e, int index, WORD abilityId);

	// Appends through the game's own adder, which honours the piece's own slot count
	// at +0x0B and recomputes the derived stats on the way out. False when the piece
	// is full. Prefer this to the setter when appending, because it is the path the
	// Customise screen takes.
	bool AddEquipEntryAbility(EquipEntryData* e, WORD abilityId);

	// Recomputes the name id and the icon word from the ability set. Both writers
	// above already call it. Exposed for a caller that pokes the words itself.
	bool RefreshEquipEntryName(EquipEntryData* e);

	// The game's own tests, rather than a reimplementation of the terminator rules.
	bool EquipEntryHasAbility(const EquipEntryData* e, WORD abilityId);
	bool EquipEntryHasFreeSlot(const EquipEntryData* e);

	// Whether the Customise screen would accept this piece. force skips the
	// "somebody is wearing it" part of the rule.
	bool EquipEntryCustomisable(const EquipEntryData* e, bool force = false);

	// One row of kaizou.bin, the customise recipe table. THE source for which
	// auto-abilities are legal on a weapon versus an armour, because nothing else in
	// the game says. 125 rows.
	struct EquipRecipe
	{
		int kind;      // 1 weapon, 2 armour
		int abilityId; // 0x8000 based
		int itemId;    // what the Customise screen charges
		int quantity;
	};

	// How many recipes there are, and one row. Zero and false before the table has
	// been filled, which happens during boot.
	int EquipRecipeCount();
	bool EquipRecipeAt(int index, EquipRecipe* out);

	// What this character has on. kEquipSlotIdNone when nothing.
	WORD EquippedWeaponSlotId(BYTE charIndex);
	WORD EquippedArmourSlotId(BYTE charIndex);
	EquipEntryData* EquippedWeapon(BYTE charIndex);
	EquipEntryData* EquippedArmour(BYTE charIndex);

	// Equips a slot id on a character through the game's own setter, which fixes up
	// the old and the new entry's owner byte. Pass kEquipSlotIdNone to unequip.
	// armour selects which of the two record fields. Returns false when there is no
	// game.
	bool EquipItemOn(BYTE charIndex, bool armour, WORD slotId);

	// Copies a 22-byte entry into the first free slot through the game's own adder,
	// and reports the new slot id. The equipment counterpart of AddItem, and it has
	// the same once-per-machine rule. False when the array is full.
	bool AddEquipment(const void* entry22Bytes, WORD* outSlotId);

	// In-use and free counts, from the game's own counter.
	bool CountEquipment(int* outUnlocked, int* outFree);

	// ---------------------------------------------------------------------------
	// The desync hash
	//
	// The whole of the persistent game state is one contiguous 26,816 byte run, so
	// a divergence check is a handful of FNV-1a passes rather than a hundred reads.
	// It is split into named buckets because "the hash changed" is far less
	// actionable than "the equipment array changed".
	//
	// Playtime is deliberately excluded: it advances every frame, and including it
	// would report a divergence constantly. The two item change masks are in their
	// own bucket because they are menu state rather than game state and diverge
	// harmlessly whenever one machine has a menu open.
	//
	// Nothing outside the save block is hashed. The battle party roster, the battle
	// actors and the equipped-gear bonus cache are all reconstructible from it, so
	// hashing them would report divergences that fix themselves.
	// ---------------------------------------------------------------------------
	enum HashBucket
	{
		// +0x0000 .. +0x00B8. This is the bucket that holds the LIVE location, the word
		// at +0x00 and the byte at +0x0C, so "we are on different maps" does get caught
		// here. It was not obvious, because the bucket below used to be called MapId.
		kBucketHeader = 0,

		// +0x00B8 .. +0x00BC, the CHECKPOINT, not the current map. Formerly kBucketMapId,
		// which was a misreading of +0xB8. Kept as its own bucket and kept out of the
		// combined hash because a save point is a local act: one machine saving should not
		// be reported as a divergence.
		kBucketCheckpoint,
		kBucketProgress,   // +0x00C0 .. +0x3D0C
		// Named Config for historical reasons. It is mostly NOT config: the kernel
		// "conf" block is 0x84 bytes of the 448, the rest is gil, the party arrays and
		// 256 bytes of world event flags. Reported as "PartyGilFlags".
		kBucketConfig,     // +0x3D0C .. +0x3ECC
		kBucketInventory,  // +0x3ECC .. +0x41CC
		kBucketItemMasks,  // +0x41CC .. +0x420C, menu state
		kBucketMonsters,   // +0x420C .. +0x448C, capture counts and the bestiary
		kBucketKeyItems,   // +0x448C .. +0x449C
		kBucketEquipment,  // +0x449C .. +0x55CC
		kBucketCharacters, // +0x55CC .. +0x6034
		kBucketAbilities,  // +0x6034 .. +0x634C
		kBucketNames,      // +0x634C .. +0x64B4
		kBucketTail,       // +0x64B4 .. +0x68C0
		kBucketCount
	};

	struct GameStateHash
	{
		DWORD combined; // all buckets except ItemMasks and Checkpoint
		DWORD bucket[kBucketCount];
		bool valid; // false when there was nothing to read
	};

	// Hashes the save block. Cheap enough to call every frame: 13 FNV-1a passes
	// over 26 KB total.
	bool HashGameState(GameStateHash* out);

	// A name for a bucket, for logging a divergence. Never NULL.
	const char* HashBucketName(int bucket);

	// Which bucket covers this save-block offset, or -1 for the excluded playtime
	// field. Handy when a divergence needs to be turned back into "what changed".
	int HashBucketForOffset(DWORD offset);

	// The byte range a bucket covers, as save-block offsets. False for a bad index.
	bool HashBucketRange(int bucket, DWORD* outBegin, DWORD* outEnd);

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// Logs the party, gil, item slot usage, equipment usage and the three active
	// members with their names and chr ids. One call at startup is the fastest way
	// to confirm the bindings landed on something sensible.
	void LogGameState();

} // namespace ffx
