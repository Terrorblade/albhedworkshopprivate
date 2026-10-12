#include "ffx/KernelTables.h"

#include "ffx/DataFile.h"
#include "ffx/GameState.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// The header is 20 bytes and the records start right after it.
		const int kHeaderBytes = 20;
		const int kOffFirstId = 0x08;
		const int kOffLastId = 0x0A;
		const int kOffRecordBytes = 0x0C;
		const int kOffRecordStart = 0x10;

		// A record's name offset is its first word, relative to the string blob.
		const int kOffRecordName = 0x00;

		// ---------------------------------------------------------------------------
		// w_name.bin's record is a grid. Measured off the shipped file, stride 72.
		//
		//   +0x00  seven pairs of (name offset, small number), so the name for
		//          character c is the u16 at +4*c. The odd word of each pair is a small
		//          integer, not an offset, it lands mid string if you follow it.
		//   +0x1C  seven more string offsets, two bytes apart, every one of them "-".
		//          An abbreviated name the remaster never filled in.
		//   +0x38  seven u16 chr ids, the model each character uses for this row.
		//   +0x46  zero.
		//
		// Seven because that is how many characters carry equipment. The order is the
		// kit's character index, which row 0 proves: Caladbolg, Nirvana, Masamune,
		// Spirit Lance, World Champion, Onion Knight, Godhand.
		const int kWeaponColumnStride = 4;
		const int kOffWeaponChrIds = 0x38;

		// Enough for 170 rows of seven names. The whole string blob in the shipped file
		// is 16818 bytes including the descriptions, and the names alone measure 14438,
		// so this has room to spare for a longer localisation.
		const int kWeaponRows = 192;
		const int kWeaponArenaBytes = 24 * 1024;

		struct WeaponGrid
		{
			const char* name[kWeaponNameColumns];
			WORD chrId[kWeaponNameColumns];
		};

		WeaponGrid g_weaponGrid[kWeaponRows];
		char g_weaponArena[kWeaponArenaBytes];
		int g_weaponArenaUsed = 0;
		int g_weaponRowsRead = 0;

		// Copies one name into the grid's own arena. Returns null when it will not fit,
		// which leaves that cell empty rather than truncating a name.
		const char* KeepWeaponName(const char* text)
		{
			const int need = (int)strlen(text) + 1;
			if (g_weaponArenaUsed + need > kWeaponArenaBytes)
				return nullptr;

			char* at = g_weaponArena + g_weaponArenaUsed;
			memcpy(at, text, (size_t)need);
			g_weaponArenaUsed += need;
			return at;
		}

		struct Source
		{
			const char* path;
			int idBase; // added to the file's own id, for the three monster files
		};

		struct TableSpec
		{
			const char* name;
			Source sources[3]; // a null path ends the list
		};

		// Which file each table comes from, and from which language set. The sets are not
		// interchangeable: inpc has every numeric table and no monster names, new_uspc has
		// the monster names and drops the numeric tables.
		const TableSpec g_specs[KernelTableCount] = {
		    { "items",
		        { { "ffx_ps2/ffx/master/inpc/battle/kernel/item.bin", 0 }, { nullptr, 0 } } },
		    { "commands and spells",
		        { { "ffx_ps2/ffx/master/inpc/battle/kernel/command.bin", 0 },
		            { nullptr, 0 } } },
		    { "auto abilities",
		        { { "ffx_ps2/ffx/master/inpc/battle/kernel/a_ability.bin", 0 },
		            { nullptr, 0 } } },
		    { "weapon and armour names",
		        { { "ffx_ps2/ffx/master/inpc/battle/kernel/w_name.bin", 0 },
		            { nullptr, 0 } } },
		    // The three monster files are one id space split across three files, and each
		    // one's header already carries its own firstId, so no base is needed.
		    { "monsters",
		        { { "ffx_ps2/ffx/master/new_uspc/battle/kernel/monster1.bin", 0 },
		            { "ffx_ps2/ffx/master/new_uspc/battle/kernel/monster2.bin", 0 },
		            { "ffx_ps2/ffx/master/new_uspc/battle/kernel/monster3.bin", 0 } } },
		};

		PickerList g_lists[KernelTableCount];
		bool g_done[KernelTableCount] = { false };
		int g_loaded = 0;

		WORD Rd16(const BYTE* p)
		{
			return (WORD)(p[0] | ((WORD)p[1] << 8));
		}

		// ONE BIT PER ROW, SET ONLY WHERE THE GAME SUPPLIED A NAME.
		//
		// The alternative was for callers to test the label against the "unnamed "
		// prefix, which works right up until a real name in some language starts with
		// it. The loader is the only place that actually knows, so it is the only place
		// that should be deciding.
		//
		// Indexed by row, which is id minus the table's id base, so the key is the same
		// number the synthesized label prints. 366 rows is the largest table, monsters.
		const int kMaxRows = 384;
		unsigned char g_hasRealName[KernelTableCount][kMaxRows / 8];

		void MarkRealName(KernelTable which, int row)
		{
			if (row < 0 || row >= kMaxRows)
				return;
			g_hasRealName[which][row >> 3] |= (unsigned char)(1u << (row & 7));
		}

		bool HasRealName(KernelTable which, int row)
		{
			if (row < 0 || row >= kMaxRows)
				return false;
			return (g_hasRealName[which][row >> 3] & (unsigned char)(1u << (row & 7))) != 0;
		}

		// Reads one w_name.bin row into the grid and writes its label.
		//
		// The label is the distinct names in column order joined by " / ", so row 0 is
		// seven names and row 70 is seven again, while a row where several characters
		// share a name only says it once. Four rows can carry the identical seven names,
		// which is correct: rows 70 to 73 really are the same weapons with different
		// ability sets, and the row number is what tells them apart.
		bool ReadWeaponRow(const BYTE* rec, int stride, const BYTE* file, int bytes,
		    int blobStart, int row, char* label, int labelBytes)
		{
			label[0] = 0;
			if (stride < kOffWeaponChrIds + 2 * kWeaponNameColumns)
				return false;

			WeaponGrid* grid = (row >= 0 && row < kWeaponRows) ? &g_weaponGrid[row] : nullptr;
			if (grid && row >= g_weaponRowsRead)
				g_weaponRowsRead = row + 1;

			int used = 0;
			int found = 0;
			for (int c = 0; c < kWeaponNameColumns; ++c)
			{
				const int at = blobStart + (int)Rd16(rec + kWeaponColumnStride * c);

				char text[96];
				int written = 0;
				if (at < bytes)
					written = DecodeKernelText(file + at, bytes - at, text,
					    (int)sizeof(text));

				if (grid)
				{
					grid->chrId[c] = Rd16(rec + kOffWeaponChrIds + 2 * c);
					grid->name[c] = (written > 0) ? KeepWeaponName(text) : nullptr;
				}
				if (written <= 0)
					continue;
				++found;

				// Only the names not already in the label, compared against what has
				// been written rather than against the other columns, which keeps this
				// to one pass and handles three columns agreeing just as well as two.
				bool seen = false;
				for (int prev = 0; prev < c && !seen; ++prev)
				{
					const char* other = grid ? grid->name[prev] : nullptr;
					seen = other && strcmp(other, text) == 0;
				}
				if (seen)
					continue;

				const int need = written + (used > 0 ? 3 : 0);
				if (used + need + 1 > labelBytes)
					break;
				if (used > 0)
				{
					memcpy(label + used, " / ", 3);
					used += 3;
				}
				memcpy(label + used, text, (size_t)written);
				used += written;
				label[used] = 0;
			}
			return found > 0;
		}

		// Adds every row of one kernel file to a list. Returns how many were added.
		//
		// EVERY DECLARED ROW GETS A ROW, including the ones whose name does not decode.
		// The row count comes from the file's own first and last id, so a record with a
		// blank name is still a real id the game can hold: an unused slot, a placeholder
		// the developers left in, or something a mod is free to fill. Dropping those was
		// hiding real ids, and an editor needs to see the gap it is filling.
		int AddFile(KernelTable which, PickerList& list, const BYTE* file, int bytes, int idBase)
		{
			if (!file || bytes < kHeaderBytes)
				return 0;

			const int firstId = (int)Rd16(file + kOffFirstId);
			const int lastId = (int)Rd16(file + kOffLastId);
			const int stride = (int)Rd16(file + kOffRecordBytes);
			const int recordStart = (int)Rd16(file + kOffRecordStart);

			if (stride <= 0 || lastId < firstId)
				return 0;
			if (recordStart < kHeaderBytes || recordStart > bytes)
				return 0;

			const int rows = lastId - firstId + 1;

			// The byte count at +0x0E is a u16 and it overflows, item_get.bin needs 102480
			// and the field holds 36944, so the record block size is computed here instead.
			const int recordBytes = rows * stride;
			if (recordStart + recordBytes > bytes)
				return 0;

			// The blob starts straight after the records. A purely numeric table has no
			// blob, and then every name offset is 0 and points at nothing.
			const int blobStart = recordStart + recordBytes;
			if (blobStart >= bytes)
				return 0;

			int added = 0;
			for (int row = 0; row < rows; ++row)
			{
				const BYTE* rec = file + recordStart + (size_t)row * stride;
				if (stride < 2)
					break;

				// Seven names, and a label carrying all of the distinct ones so that
				// typing any character's weapon finds the row it lives on.
				if (which == KernelWeaponNames)
				{
					char label[256];
					const int id = idBase + firstId + row;
					if (ReadWeaponRow(rec, stride, file, bytes, blobStart, row, label,
					        (int)sizeof(label)))
					{
						if (list.Add(id, label))
						{
							MarkRealName(which, id - idBase);
							++added;
						}
					}
					else if (list.AddFormatted(id, "unnamed %d", id - idBase))
						++added;
					continue;
				}

				// Offset 0 is a VALID name, it is the first string in the blob, so it
				// must not be read as "no name". Dropping it cost every table its id 0,
				// which is Potion, Caladbolg, Sensor and Attack. The real emptiness test
				// is whether anything decodes, below.
				const int nameOffset = (int)Rd16(rec + kOffRecordName);
				const int at = blobStart + nameOffset;

				char text[96];
				int written = 0;
				if (at < bytes)
					written = DecodeKernelText(file + at, bytes - at, text,
					    (int)sizeof(text));

				const int id = idBase + firstId + row;

				if (written > 0)
				{
					if (list.Add(id, text))
					{
						MarkRealName(which, id - idBase);
						++added;
					}
				}
				// A blank name, or a name offset past the end of the file. Both mean the
				// record is there and nothing named it, so the id goes in with the only
				// label that is true about it. "unnamed" is at the front where a filter
				// will find it.
				else if (list.AddFormatted(id, "unnamed %d", id - idBase))
					++added;
			}
			return added;
		}

		bool LoadOne(KernelTable which)
		{
			if (which < 0 || which >= KernelTableCount)
				return false;
			if (g_done[which])
				return !g_lists[which].Empty();

			const TableSpec& spec = g_specs[which];
			g_lists[which].Reset(spec.name);

			int total = 0;
			for (int i = 0; i < 3 && spec.sources[i].path; ++i)
			{
				int bytes = 0;
				void* file = ReadDataFile(spec.sources[i].path, &bytes);
				if (!file)
				{
					Log("kernel: could not read %s", spec.sources[i].path);
					continue;
				}

				total += AddFile(which, g_lists[which], (const BYTE*)file, bytes,
				    spec.sources[i].idBase);
				FreeDataFile(file);
			}

			// Marked done either way, so a missing file is not retried on every panel
			// frame. LoadKernelTables is the way to retry.
			g_done[which] = true;
			g_lists[which].SetLive(total > 0);
			if (total > 0)
				++g_loaded;
			return total > 0;
		}

	} // namespace

	int DecodeKernelText(const unsigned char* src, int srcBytes, char* out, int outBytes)
	{
		if (!src || !out || outBytes < 1)
			return 0;

		int written = 0;
		for (int i = 0; i < srcBytes && written < outBytes - 1; ++i)
		{
			const unsigned char c = src[i];

			if (c == 0x00)
				break;

			// Control codes. 0x0A carries a one byte colour id and 0x13 carries an
			// argument, so both eat the next byte. Anything else below 0x30 is dropped.
			if (c < 0x30)
			{
				if (c == 0x0A || c == 0x13)
					++i;
				continue;
			}

			char decoded = 0;
			if (c <= 0x39)
				decoded = (char)c; // digits, already where ASCII has them
			else if (c <= 0x49)
				decoded = (char)(c - 0x1A); // space through /
			else if (c <= 0x4F)
				decoded = (char)(c - 0x10); // : through ?
			else if (c <= 0x89)
				decoded = (char)(c - 0x0F); // A through z
			else
				// 0x8A AND UP IS THE EXTENDED BLOCK, not padding to throw away.
				// name_txt.bin rows 36..46 enumerate 0xA3..0xCF as the accented
				// letters, and 0x9C is the arrow in "Overdrive -> AP". Dropping them
				// silently shortens a French or German name and turns that arrow into
				// nothing, which reads as a different name rather than an unreadable
				// one. A question mark is the honest answer until there is a real
				// mapping for them.
				decoded = '?';

			out[written++] = decoded;
		}

		// Several names are padded, "Armor  " for one, so the pad comes off here rather
		// than in every caller's label.
		while (written > 0 && out[written - 1] == ' ')
			--written;

		out[written] = 0;
		return written;
	}

	int LoadKernelTables()
	{
		if (!DataFileSystemReady())
		{
			Log("kernel: the file system is not up yet, so no name tables were read. "
			    "This is normal from DllMain, try again once the game has booted.");
			return 0;
		}

		for (int i = 0; i < KernelTableCount; ++i)
			LoadOne((KernelTable)i);

		Log("kernel: %d of %d name tables loaded", g_loaded, (int)KernelTableCount);
		return g_loaded;
	}

	int KernelTablesLoaded()
	{
		return g_loaded;
	}

	int CacheableKernelLists(workshop::CacheableList* out, int max)
	{
		// The names are the file names, so they stay stable even if the enum moves.
		static const char* const names[KernelTableCount] = {
			"kernel-items",
			"kernel-commands",
			"kernel-auto-abilities",
			"kernel-weapon-names",
			"kernel-monsters",
		};

		int wrote = 0;
		for (int i = 0; i < (int)KernelTableCount && wrote < max; ++i)
		{
			out[wrote].name = names[i];
			out[wrote].list = &g_lists[i];
			++wrote;
		}
		return wrote;
	}

	const PickerList& KernelList(KernelTable which)
	{
		if (which < 0 || which >= KernelTableCount)
			return g_lists[0];

		// Lazy, so a caller that never opens the monsters tab never pays for the read.
		if (!g_done[which] && DataFileSystemReady())
			LoadOne(which);

		return g_lists[which];
	}

	const char* KernelName(KernelTable which, int id)
	{
		const PickerList& list = KernelList(which);
		for (int i = 0; i < list.Count(); ++i)
			if (list.Items()[i].id == id)
				return list.Items()[i].label;
		return nullptr;
	}

	const char* KernelWeaponNameFor(int row, int forCharacter)
	{
		if (row < 0 || row >= g_weaponRowsRead)
			return nullptr;
		if (forCharacter < 0 || forCharacter >= kWeaponNameColumns)
			return nullptr;
		return g_weaponGrid[row].name[forCharacter];
	}

	int KernelWeaponChrId(int row, int forCharacter)
	{
		if (row < 0 || row >= g_weaponRowsRead)
			return 0;
		if (forCharacter < 0 || forCharacter >= kWeaponNameColumns)
			return 0;
		return (int)g_weaponGrid[row].chrId[forCharacter];
	}

	bool KernelNameIsReal(KernelTable which, int id)
	{
		if (which < 0 || which >= KernelTableCount)
			return false;
		return HasRealName(which, KernelIdFromTagged(id));
	}

	int KernelIdFromTagged(int taggedId)
	{
		// The save block tags an id with its space in the high nibbles: 0x2000 for an
		// inventory item, 0x8000 for an equipment auto-ability, 0x3000 for a character
		// ability. Anything with no tag is returned unchanged, which is what makes this
		// safe to call on either kind.
		if ((taggedId & 0xF000) != 0)
			return taggedId & 0x0FFF;
		return taggedId;
	}

	const char* KernelTableName(KernelTable which)
	{
		if (which < 0 || which >= KernelTableCount)
			return "?";
		return g_specs[which].name;
	}

	void LogKernelTables()
	{
		for (int i = 0; i < KernelTableCount; ++i)
			Log("kernel: %s", g_lists[i].Describe());
	}

} // namespace ffx
