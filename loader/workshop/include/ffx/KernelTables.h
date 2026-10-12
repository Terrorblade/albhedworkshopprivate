#pragma once

// The battle kernel tables, which are the game's item, ability, weapon and monster name
// databases. This is where the labels for a data-driven picker come from.
//
//     ffx::LoadKernelTables();                       // once, after boot
//     const workshop::PickerList& items = ffx::KernelList(ffx::KernelItems);
//     workshop::Picker("item", items.Items(), items.Count(), &state, &itemId);
//
// Read out of the shipped archive through ffx/DataFile.h, so this needs the game to have
// booted and it blocks on disk. Load once and cache, which LoadKernelTables does.
//
// NO ONE LANGUAGE SET HAS EVERYTHING. The International build has every numeric table but
// no monster names, and the remaster's retranslation has the monster names but drops the
// numeric tables. Each table below already names the set it has to come from, so a caller
// never has to care.
//
// Ids are the file's own, which is what the game's own setters take. Note that the save
// block tags them: an inventory item id is 0x2000 plus the kernel id, and an equipment
// auto-ability id is 0x8000 plus it. KernelIdFromTagged strips that.
//
// c_ability.bin is deliberately NOT here. It shares the header format but its records
// are 20 bytes and no word in one points at a readable name, so whatever its labels are
// they are not where every other table keeps them. Command abilities may simply reuse
// command.bin's names, which would explain it, but that is a guess and a wrong picker
// is worse than a missing one.
//
// Read reversing\DATA_FILES.md for the file format and the text encoding.

#include "workshop/PickerCache.h"

namespace ffx
{

	enum KernelTable
	{
		KernelItems = 0,     // item.bin, 0..111, consumables
		KernelCommands,      // command.bin, 0..319, battle commands and spells
		KernelAutoAbilities, // a_ability.bin, 0..133, what goes on equipment
		KernelWeaponNames,   // w_name.bin, 0..169, SEVEN names per row, see below
		KernelMonsters,      // monster1/2/3.bin merged, 0..365
		KernelTableCount
	};

	// Reads and decodes every table. Safe to call repeatedly, the second call is free.
	// Returns the number that loaded, so 0 means the archive was not reachable and
	// KernelTableCount means everything worked.
//
	// BLOCKS ON DISK, and it needs the game booted, so do not call it from DllMain. A
	// hub step event or the first time a panel opens are both fine.
	int LoadKernelTables();

	// How many tables have loaded so far.
	int KernelTablesLoaded();

	// The list for one table. Empty with Live() false until it has loaded.
	const workshop::PickerList& KernelList(KernelTable which);

	// All five name tables for the list cache. They are kernel files in the
	// archive, so they are fixed by it. See ffx/ListExport.h.
	int CacheableKernelLists(workshop::CacheableList* out, int max);

	// The name for one id, or null when the table has not loaded or the id is not in it.
	// Points into the list's own storage, which lives as long as the process.
	const char* KernelName(KernelTable which, int id);

	// ---------------------------------------------------------------------------
	// The weapon table is a grid, not a list
	// ---------------------------------------------------------------------------

	// A w_name.bin ROW IS SEVEN NAMES, one per playable character, and the row on its
	// own does not name anything. Row 0 is Caladbolg, Nirvana, Masamune, Spirit Lance,
	// World Champion, Onion Knight and Godhand, which is the same weapon slot seen by
	// each of the seven. Reading only the first column, which this used to do, labelled
	// all 170 rows with Tidus gear and lost every other character's.
	//
	// The column order is the kit's own character index, so column 0 is Tidus, 1 Yuna,
	// 2 Auron, 3 Kimahri, 4 Wakka, 5 Lulu, 6 Rikku. Those seven and no more: a row has
	// nothing for Seymour or an aeon because neither carries equipment.
	//
	// The picker list for KernelWeaponNames therefore labels each row with all of its
	// distinct names, so typing "Nirvana" finds row 0 even though the row is not called
	// that. Use the two calls below when you know which character you mean.

	const int kWeaponNameColumns = 7;

	// The one name a row holds for one character, or null for a row or column that is
	// not there. forCharacter is a character index, 0..6.
	const char* KernelWeaponNameFor(int row, int forCharacter);

	// The weapon or armour MODEL that row and column uses, as a full chr id in the 'w'
	// category, so 0x4007 is w007. Zero when there is none.
	//
	// Many named weapons share one model: w004 covers 19 different Tidus swords. Only
	// the seven Celestial Weapons have a model to themselves. What this is good for is
	// the other direction, telling you a model belongs to Rikku.
	int KernelWeaponChrId(int row, int forCharacter);

	// Whether that name came out of the game's data or was made up here.
	//
	// A record with a blank name still gets a row, labelled "unnamed 163", because the
	// id is real and a picker has to be able to select it. That label is useful in a
	// list and useless as a name, so anything borrowing a name from these tables for
	// another purpose has to be able to tell the two apart. The models list is the
	// caller that needs it: "m163  unnamed 163 (mon)" says the same thing twice.
	//
	// False for an id the table does not have at all.
	bool KernelNameIsReal(KernelTable which, int id);

	// Strips the save block's id space tag, so 0x2005 becomes 5. Leaves an untagged id
	// alone, which makes it safe to call on either.
	int KernelIdFromTagged(int taggedId);

	// What a table is called, for a panel. Never null.
	const char* KernelTableName(KernelTable which);

	void LogKernelTables();

	// ---------------------------------------------------------------------------
	// The text encoding
	// ---------------------------------------------------------------------------

	// The kernel blob is not ASCII. It is ASCII 0x20..0x7A with the digits pulled to the
	// front, so a raw name reads as mojibake until it goes through this.
//
	// Everything below 0x30 is a control code: 0 ends the string, 0x0A is followed by a
	// one byte colour id, and 0x13 takes an argument and inserts a value at runtime.
	// Control codes and their arguments are dropped rather than rendered.
//
	// Returns the number of characters written, not counting the terminator.
	int DecodeKernelText(const unsigned char* src, int srcBytes, char* out, int outBytes);

} // namespace ffx
