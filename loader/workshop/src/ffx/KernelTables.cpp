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

		// A record's name offset is its first word, and its description offset is at +4.
		// Both are relative to the string blob.
		const int kOffRecordName = 0x00;

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

		// Adds every row of one kernel file to a list. Returns how many were added.
		//
		// EVERY DECLARED ROW GETS A ROW, including the ones whose name does not decode.
		// The row count comes from the file's own first and last id, so a record with a
		// blank name is still a real id the game can hold: an unused slot, a placeholder
		// the developers left in, or something a mod is free to fill. Dropping those was
		// hiding real ids, and an editor needs to see the gap it is filling.
		int AddFile(PickerList& list, const BYTE* file, int bytes, int idBase)
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
						++added;
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

				total += AddFile(g_lists[which], (const BYTE*)file, bytes,
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
				continue; // nothing above 0x89 is a character

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
