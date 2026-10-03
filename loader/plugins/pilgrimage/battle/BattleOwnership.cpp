#include "battle/BattleOwnership.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Battle.h"
#include "ffx/GameState.h"
#include "net/NetLink.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// The explicit bindings, one per peer, -1 for "use the default". Indexed by
		// peer id and sized by MaxPlayers rather than by kBattleOwnerSlots, because a
		// peer id can be any of the former even though only three can own a unit.
		int explicitChar[MaxPlayers];

		char g_status[160] = "battle: not syncing";

		// Is this peer actually in the session.
		//
		// PeerReachable is false for ourselves by design, so the local peer has to be
		// added back. Both machines compute this from their own peer table, and those
		// tables agree except during a join or a drop. That window is the one place the
		// two machines can briefly disagree about who owns slot 2, and the cost is one
		// battle turn where either both machines suppress the menu or neither does. It
		// is called out in reversing\BATTLE_SYNC.md rather than papered over, because
		// the fix is an ordered ownership command and not a cleverer predicate.
		bool PeerPresent(Session* session, int peer)
		{
			if (!session || peer < 0 || peer >= MaxPlayers)
				return false;
			if ((uint8_t)peer == session->LocalPeer())
				return true;
			return session->PeerReachable((uint8_t)peer);
		}

	} // namespace

	void ResetBattleOwnership()
	{
		for (int i = 0; i < MaxPlayers; ++i)
			explicitChar[i] = -1;
		strcpy_s(g_status, sizeof(g_status), "battle: ownership on the default, peer N drives slot N");
	}

	bool SetBattleOwner(int peer, int charIndex)
	{
		if (peer < 0 || peer >= MaxPlayers)
			return false;
		if (charIndex >= 0 && !IsAllyUnit(charIndex))
			return false;

		// Two peers bound to the same character would be the double-commit failure, so
		// the binding is exclusive: whoever takes a character takes it off whoever had
		// it. Done here rather than refused, because the caller asking for a swap is
		// the normal case and a refusal would leave the table half changed.
		if (charIndex >= 0)
		{
			for (int other = 0; other < MaxPlayers; ++other)
			{
				if (other != peer && explicitChar[other] == charIndex)
					explicitChar[other] = -1;
			}
		}

		explicitChar[peer] = charIndex;
		Log("battle: peer %d is now bound to %s", peer,
		    charIndex >= 0 ? CharacterName((BYTE)charIndex) : "nothing, back on the default");
		return true;
	}

	int BattleOwnerCharacter(int peer)
	{
		if (peer < 0 || peer >= MaxPlayers)
			return -1;
		return explicitChar[peer];
	}

	int BattleOwnerOfUnit(int unitIndex)
	{
		Session* session = ActiveSession();
		if (!session)
			return kNoBattleOwner;
		if (session->LocalPeer() == PeerUnassigned)
			return kNoBattleOwner;
		if (!IsAllyUnit(unitIndex))
			return kNoBattleOwner;

		// 1. An explicit binding wins, if that peer is actually here. A binding to a
		//    peer who has gone away would hand the unit to nobody and hang the battle,
		//    so it falls through to the default instead.
		for (int peer = 0; peer < MaxPlayers; ++peer)
		{
			if (explicitChar[peer] == unitIndex && PeerPresent(session, peer))
				return peer;
		}

		// 2. The default: the unit's active battle slot is the peer id. Read from the
		//    roster rather than from unit+0x4FE, because that field holds the aeon index
		//    for an aeon and the enemy index for an enemy, and both would look like a
		//    perfectly good slot number.
		const int slot = BattleSlotOfUnit(unitIndex);
		if (slot >= 0 && slot < kBattleOwnerSlots && PeerPresent(session, slot))
			return slot;

		// 3. Anything left over goes to the host. Slot 2 in a two player game, an aeon,
		//    a reserve member, an enemy that the Mon Input debug flag handed a menu to.
		//    Not a judgement about who should drive those, just the only answer both
		//    machines reach without another message.
		return (int)HostPeer;
	}

	bool LocalOwnsUnit(int unitIndex)
	{
		Session* session = ActiveSession();

		// No session means a solo game, and a solo game must behave exactly as the
		// shipped game does. Every hook that calls this treats true as "carry on as
		// normal", so this is the fail-safe direction.
		if (!session)
			return true;

		const int owner = BattleOwnerOfUnit(unitIndex);
		if (owner == kNoBattleOwner)
		{
			// We are in a session but cannot answer yet, which means the host has not
			// assigned us a peer id. Saying no would suppress a menu on both machines
			// and hang a battle. Saying yes risks a double commit for one turn. The
			// clock is not running in this window either, so the battle sync is not
			// started and this value is not actually acted on. Yes is still the safer
			// of the two, because it degrades to the shipped behaviour.
			return true;
		}
		return owner == (int)session->LocalPeer();
	}

	int LocalOwnedUnit()
	{
		Session* session = ActiveSession();
		if (!session)
			return -1;

		for (int slot = 0; slot < kBattleOwnerSlots; ++slot)
		{
			const int unit = UnitForBattleSlot(slot);
			if (unit >= 0 && BattleOwnerOfUnit(unit) == (int)session->LocalPeer())
				return unit;
		}
		return -1;
	}

	const char* BattleOwnershipStatus()
	{
		Session* session = ActiveSession();
		if (!session)
		{
			strcpy_s(g_status, sizeof(g_status), "no session, so every unit is yours");
			return g_status;
		}

		const int mine = LocalOwnedUnit();
		if (!BattleRunning())
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "peer %u, no battle running", (unsigned)session->LocalPeer());
			return g_status;
		}

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "peer %u drives %s", (unsigned)session->LocalPeer(),
		    mine >= 0 ? CharacterName((BYTE)mine) : "nobody in this party");
		return g_status;
	}

	void LogBattleOwnership()
	{
		Log("--- battle ownership ---");
		Log("%s", BattleOwnershipStatus());

		for (int slot = 0; slot < kBattleOwnerSlots; ++slot)
		{
			const int unit = UnitForBattleSlot(slot);
			if (unit < 0)
			{
				Log("slot %d: empty", slot);
				continue;
			}
			Log("slot %d: unit %d %s -> peer %d%s", slot, unit,
			    CharacterName((BYTE)unit), BattleOwnerOfUnit(unit),
			    LocalOwnsUnit(unit) ? "  (this machine)" : "");
		}

		for (int peer = 0; peer < MaxPlayers; ++peer)
		{
			if (explicitChar[peer] >= 0)
			{
				Log("peer %d is explicitly bound to %s, which beats the slot default",
				    peer, CharacterName((BYTE)explicitChar[peer]));
			}
		}
	}

} // namespace pilgrimage
