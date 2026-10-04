#pragma once

// The engine's asset path table, and the two shipped ways to make it name something
// it does not name today. This is the editor-facing half of the asset layer: reading
// what a path resolves to, redirecting an existing one, and filling an empty slot.
//
//     ffx::LogAssetPaths();                                  // what is where
//     ffx::RegisterAssetSubstitution("bl/bltz0000/bltz0000.ebp",
//                                    "bl/bltz0000/mine.ebp");
//     ffx::AddEventPackagePath(25, "host0:/ffx/master/jppc/event/obj/te/test22/test22.ebp");
//
// ALL OF IT IS GAME THREAD ONLY. The resolver writes one process-global 255-byte
// static, and the table edit moves bytes the engine reads.
//
// NONE OF IT HAS BEEN RUN IN THE GAME. Every claim behind it was read out of the
// disassembly and cross-checked against the shipped archive, and reversing/CHEAT_WARP.md
// section 12 is the evidence. Treat the first run as an experiment.
//
// THREE ROUTES, in increasing order of how much they touch:
//
//   1. A LOOSE FILE, which needs nothing from this header at all.
//      FFX_fios_openFile searches the archive first and falls through to CreateFileW,
//      so a path the archive does not contain is read off disk. Any id that already has
//      a path can be pointed at a new file this way with no code. The catch is where:
//      the engine prepends "../../../ffx_ps2/" and the working directory is the one
//      holding FFX.exe, so the default spot is three directories ABOVE the install.
//      See CHEAT_WARP.md 12.2 for how to spend that prefix to land inside the game
//      folder instead.
//
//   2. A SUBSTITUTION, which is a shipped hook the game itself uses one slot of.
//      Redirects an id that already has a path. No table work. 15 slots free for the
//      life of the process. Start here.
//
//   3. FILLING AN EMPTY SLOT, which is the only way to reach a package no id names.
//      54 of the 402 event ids have no path, against 67 orphan packages in the archive,
//      so there is not quite room for all of them and the id space cannot be grown
//      without rebuilding the whole table.

#include <windows.h>

#include "ffx/addresses/WorldState.h"

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Is the asset loader actually up
	// ---------------------------------------------------------------------------

	// THE PRECONDITION FOR EVERYTHING ELSE IN THIS HEADER, and for any call to the
	// engine's own kind selector. Check it or crash, there is no softer failure.
	//
	// IT IS NOT SLOT 8 YOU HAVE TO CHECK, and getting that wrong cost a boot crash.
	// FFX_Asset_SelectKind dereferences SLOT 11, the resident cdrom.fid, at
	// "movsx eax, word ptr [eax+ecx*2]" RVA 0x0036C53B. Slot 8 is what the selector
	// WRITES, and the only two writes to slot 8 in the whole binary are its own, at
	// 0x0036C53F and 0x0036C54E. So a slot 8 test says nothing about whether the call
	// is safe, and it cannot even detect "not loaded yet", for the reason below.
	//
	// THE SENTINEL THAT GUARD TESTED FOR DOES NOT EXIST AT RUNTIME. The whole asset
	// loader table sits past the end of initialized .data, which stops at RVA
	// 0x0088B5FF, so there are no bytes for it in the file and IDA shows 0xFF for
	// every one of them. That 0xFF is an artefact of asking about a byte the file does
	// not contain. The Windows loader zero fills the tail, so at process start every
	// slot reads 0, never 0xFFFFFFFF. A "== 0xFFFFFFFF means not ready" test is
	// therefore false on the one occasion it needed to be true, and the call went
	// through with slot 11 holding 0.
	//
	// The same applies to every kit global above RVA 0x0088B5FF, which is most of
	// them. Test for 0.
	//
	// Slot 12, the path table, is checked too, because the resolver dereferences it
	// after a null test, and the count it then reads has to be sane.
	bool AssetLoaderReady();

	// ---------------------------------------------------------------------------
	// Reading the table
	// ---------------------------------------------------------------------------

	// How many paths the table declares, 0 before it is loaded. 16,305 on the shipped
	// archive. This count is a HARD BOUND inside the engine's resolver, so an index at
	// or past it resolves to nothing.
	int AssetPathCount();

	// The path at an absolute table index, read straight out of the table with no
	// engine call and no substitution applied. False for an out of range index or an
	// empty slot, which is the table's own way of saying "no path".
	bool AssetPathAt(int index, char* out, int outBytes);

	// Where a kind's rows start, out of cdrom.fid, or -1 when the kind is unused.
	// Kind 12 is 562 and kind 14 is 7942 on the shipped archive. Read it rather than
	// baking it.
	int AssetKindBase(int kind);

	// A kind's sub-index resolved THROUGH THE ENGINE, so the three hardcoded package
	// swaps and every registered substitution apply, exactly as they would for a real
	// load. That makes this the honest answer to "what would the game open".
	//
	// outBytes wants at least 256, the resolver's own buffer being 255.
	bool ResolveAssetPath(int kind, int subIndex, char* out, int outBytes);

	// The .ebp path for an event id, which is kind 12 sub-index 0 of its 18 slots.
	bool EventPackagePath(int eventId, char* out, int outBytes);

	// ---------------------------------------------------------------------------
	// Substitutions, the shipped hook
	// ---------------------------------------------------------------------------
	//
	// FFX_Asset_ResolvePathWithDebugOverrides runs every resolved path through a 16
	// entry key/value table and replaces EVERY occurrence of the first matching key.
	// The game uses exactly one slot, for the localised event directory, so 15 are
	// free and a plugin that claims them early keeps them.
	//
	// IT APPLIES TO EVERY ASSET KIND, not just events. A key of ".ebp" would rewrite
	// paths across the whole game. Pick something distinctive.
	//
	// It CANNOT create a path for an empty slot: a zero length slot returns nothing
	// before the substitution loop is reached. Use AddEventPackagePath for that.

	const int kAssetSubstitutionSlots = 16;

	// How many of the 16 slots hold a key.
	int AssetSubstitutionCount();

	// One slot's key and value, both pointers into kit-owned storage or into the
	// engine's own strings. False for an empty or out of range slot.
	bool AssetSubstitutionAt(int slot, const char** outKey, const char** outValue);

	// Claims a slot and returns its index, or -1 when all 16 are taken.
	//
	// THE REFUSAL MATTERS. The engine's own register asserts on a full table and then
	// writes keys[16] anyway, and keys[16] IS values[0], so the 17th registration
	// silently destroys the first substitution's value. This refuses instead.
	//
	// Both strings are COPIED into kit storage, because the engine keeps the pointers
	// and nothing else would keep the bytes alive.
	int RegisterAssetSubstitution(const char* key, const char* value);

	// Releases a slot. The engine's own unregister, so the game's localised event
	// directory slot can be released too, though there is no reason to.
	bool UnregisterAssetSubstitution(int slot);

	// ---------------------------------------------------------------------------
	// Filling an empty path slot, which is how a new package becomes reachable
	// ---------------------------------------------------------------------------

	// Whether this event id has no .ebp path, so there is a slot to fill. 54 of the
	// 402 are like this on the shipped archive.
	bool EventPackagePathSlotEmpty(int eventId);

	// Gives an event id a path. Returns false and logs why on any refusal.
	//
	// WHAT THE PATH MUST LOOK LIKE, all three load bearing:
	//
	//   - it MUST contain the literal "/event/obj/" and at least one "/" after it.
	//     FFX_Ev_LoadEventPackage does strstr(name, "/event/obj/") + 11 and then
	//     *strrchr(that, '/') = 0 WITH NO NULL CHECK, so a path without it faults.
	//     The label the game shows is the text between the two, which is why the
	//     shipped shape is ".../event/obj/<group>/<name>/<name>.ebp".
	//   - under 128 bytes. FFX_RomDev_EnqueueRead copies it into a 128-byte-per-slot
	//     queue and the copy stops at the NUL, so a longer path is stored with no
	//     terminator and runs into the next slot.
	//   - the first 7 bytes are discarded blindly, not matched, so any 7 characters
	//     work. Keep "host0:/" for consistency with everything else.
	//
	// HOW IT WORKS, because it matters if it goes wrong. The table is one heap block
	// holding cdrom.fnd verbatim, nothing in the binary caches a pointer into it, no
	// length or checksum is kept, there is no second copy, and it is loaded exactly
	// once at boot. So the first call copies it into a kit-owned block with room to
	// spare and repoints the loader's slot 12 with one aligned dword store. Later
	// calls shift inside that block. THE ORIGINAL IS NEVER FREED, deliberately, so
	// there is no use after free even if something held a pointer after all.
	//
	// It refuses to overwrite a slot that already has a path. Redirecting one of those
	// is what RegisterAssetSubstitution is for.
	bool AddEventPackagePath(int eventId, const char* path);

	// ---------------------------------------------------------------------------
	// Deriving the orphan packages' paths, so nothing has to be typed
	// ---------------------------------------------------------------------------
	//
	// 67 .ebp files ship that no id names, and they are the developers' test and sample
	// packages. Their paths do not have to be typed in or baked, because all three
	// pieces are already in the game:
	//
	//   the NAME comes from eventid.bin, which has a name for 400 of the 402 ids
	//   the SHAPE is the shipped table's own convention, "<name[0..1]>/<name>/<name>.ebp"
	//   the EXISTENCE comes from asking the archive
	//
	// Measured against the shipped archive: 46 of the 54 empty ids derive to a package
	// that is really there. The 8 that do not are ids 40, 101, 111, 246, 251, 379, 400
	// and 401, two of which have no name at all.
	//
	// The 21 that remain unreachable afterwards are almost all PREFIXED VARIANTS, like
	// psv_bltz0000 beside bltz0000, which live in the base name's own directory. Those
	// need no new id, because the base id already has a path and a substitution can
	// point it at the variant. See EventPackageVariantAt.

	// The archive path for an event id's own name, derived as above. False when the id
	// has no name, when eventid.bin has not been read, or when the file is not in the
	// archive. outBytes wants at least 128.
	//
	// NEEDS ffx::LoadEventTable TO HAVE RUN, because the name is what the path is built
	// from and a stock boot never reads that table.
	bool DeriveEventPackagePath(int eventId, char* out, int outBytes);

	// One of the seven ids the package loader routes to a HARDCODED BOOT PATH before it
	// ever looks at the table, 393 through 399. Giving one of those a table entry would
	// be ignored on the load path while still making a picker believe it is loadable,
	// which is worse than leaving it alone.
	bool EventIdIsBootScene(int eventId);

	// Fills every empty event id slot whose package the archive actually holds, using
	// the derived path. Returns how many were filled.
	//
	// includeBootScenes should be false. It is a parameter rather than a hardcoded skip
	// so that the refusal is visible at the call site instead of being a surprise.
	int AddDerivedEventPackagePaths(bool includeBootScenes);

	// A VARIANT of an id's own package that the archive holds, which is the other half
	// of the orphan set: "cn_", "psv_", "psvcn_", "dbg_" and "full_" prefixed files
	// sitting in the base name's directory. The blitzball alternates are 15 of them.
	//
	// These need no new id. The base id already resolves, so pointing it at the variant
	// is a substitution, which is the cheaper and more reversible route.
	int EventPackageVariantCount(int eventId);
	bool EventPackageVariantAt(int eventId, int index, char* outPath, int outBytes);

	// The substring of the base id's own path that a substitution key should use, which
	// is "<group>/<name>/<name>.ebp" rather than anything shorter. A shorter key would
	// match other asset kinds too.
	bool EventPackageSubstitutionKey(int eventId, char* out, int outBytes);

	// How many paths have been added, and how many more will fit in the block that was
	// allocated for them. The arena is fixed at first use rather than grown, so that
	// nothing ever has to decide when the engine is done reading an old block.
	int AddedAssetPathCount();
	int AddedAssetPathBytesFree();

	// ---------------------------------------------------------------------------

	// The table state, the substitution slots, and how many event ids have no path.
	// For finding out what is actually there without adding a panel for it.
	void LogAssetPaths();

} // namespace ffx
