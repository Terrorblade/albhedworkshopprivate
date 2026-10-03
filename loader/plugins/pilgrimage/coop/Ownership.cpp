#include "coop/Ownership.h"

#include <stdio.h>
#include <string.h>

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

		// The explicit bindings, one per peer, -1 for "use the default". Sized by
		// MaxPlayers rather than by the party size, because a peer id can be any of the
		// former even though only three characters are active at once.
		int g_explicitChar[MaxPlayers];

		char g_status[192] = "ownership: default, peer N drives party slot N";

		// How many characters a save block knows about.
		const int kCharacterCount = 8;

		bool CharIndexInRange(int charIndex)
		{
			return charIndex >= 0 && charIndex < kCharacterCount;
		}

	} // namespace

	int LocalPeerIndex()
	{
		Session* session = ActiveSession();
		if (!session)
			return -1;

		const uint8_t local = session->LocalPeer();
		return (local == PeerUnassigned) ? -1 : (int)local;
	}

	bool PeerInSession(int peer)
	{
		Session* session = ActiveSession();
		if (!session || peer < 0 || peer >= MaxPlayers)
			return false;

		// PeerReachable is false for ourselves by design, so add the local peer back.
		if ((uint8_t)peer == session->LocalPeer())
			return true;

		return session->PeerReachable((uint8_t)peer);
	}

	void ResetOwnership()
	{
		for (int i = 0; i < MaxPlayers; ++i)
			g_explicitChar[i] = -1;

		strcpy_s(g_status, sizeof(g_status),
		    "ownership: default, peer N drives party slot N");
	}

	bool SetCharacterOwner(int peer, int charIndex)
	{
		if (peer < 0 || peer >= MaxPlayers)
			return false;
		if (charIndex >= 0 && !CharIndexInRange(charIndex))
			return false;

		// Exclusive. Two peers bound to one character is the double-control failure, so
		// taking a character takes it off whoever had it rather than being refused.
		if (charIndex >= 0)
		{
			for (int other = 0; other < MaxPlayers; ++other)
			{
				if (other != peer && g_explicitChar[other] == charIndex)
					g_explicitChar[other] = -1;
			}
		}

		g_explicitChar[peer] = charIndex;
		Log("ownership: peer %d is now bound to %s", peer,
		    charIndex >= 0 ? CharacterName((BYTE)charIndex) : "nothing, back on the default");
		return true;
	}

	int OwnerCharacter(int peer)
	{
		if (peer < 0 || peer >= MaxPlayers)
			return -1;
		return g_explicitChar[peer];
	}

	int OwnerOfCharacter(int charIndex)
	{
		if (!ActiveSession())
			return kNoOwner;
		if (LocalPeerIndex() < 0)
			return kNoOwner;
		if (!CharIndexInRange(charIndex))
			return kNoOwner;

		// 1. An explicit binding wins, but only if that peer is still here. A binding to
		//    a peer who has gone away would hand the character to nobody, so it falls
		//    through to the default instead of stranding it.
		for (int peer = 0; peer < MaxPlayers; ++peer)
		{
			if (g_explicitChar[peer] == charIndex && PeerInSession(peer))
				return peer;
		}

		// 2. The default: the character's slot in the active party is the peer id.
		for (int slot = 0; slot < kActivePartySize; ++slot)
		{
			if ((int)ActivePartyMember(slot) != charIndex)
				continue;
			if (slot < MaxPlayers && PeerInSession(slot))
				return slot;
			break;
		}

		// 3. Everything left over goes to the host. Slot 2 in a two player game, a
		//    reserve member, an aeon. See the header for why that is the only answer
		//    both machines reach without another message.
		return (int)HostPeer;
	}

	bool LocalOwnsCharacter(int charIndex)
	{
		// Solo play has to behave exactly as the shipped game does.
		if (!ActiveSession())
			return true;

		const int owner = OwnerOfCharacter(charIndex);
		if (owner == kNoOwner)
		{
			// No peer id yet, which means the session is still handshaking. Behaving as
			// though everything is ours is right: nothing is replicated yet either, so
			// this is still effectively solo.
			return true;
		}

		return owner == LocalPeerIndex();
	}

	int LocalOwnedCharacter()
	{
		const int local = LocalPeerIndex();
		if (local < 0)
			return -1;

		if (g_explicitChar[local] >= 0)
			return g_explicitChar[local];

		if (local < kActivePartySize)
			return (int)ActivePartyMember(local);

		return -1;
	}

	const char* OwnershipStatus()
	{
		const int local = LocalPeerIndex();
		const int mine = LocalOwnedCharacter();

		if (local < 0)
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "ownership: no peer id yet, so everything is local");
		else if (mine >= 0)
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "ownership: peer %d drives %s%s", local, CharacterName((BYTE)mine),
			    g_explicitChar[local] >= 0 ? " (bound)" : " (party slot)");
		else
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "ownership: peer %d drives nothing, it is past the third party slot", local);

		return g_status;
	}

	void LogOwnership()
	{
		Log("=== ownership ===");
		Log("%s", OwnershipStatus());

		if (!ActiveSession())
		{
			Log("no session, so every character is local and nothing is gated");
			return;
		}

		Log("active party: slot 0 %s, slot 1 %s, slot 2 %s",
		    CharacterName(ActivePartyMember(0)), CharacterName(ActivePartyMember(1)),
		    CharacterName(ActivePartyMember(2)));

		for (int charIndex = 0; charIndex < kCharacterCount; ++charIndex)
		{
			const int owner = OwnerOfCharacter(charIndex);
			if (owner == kNoOwner)
				continue;

			Log("  %-10s -> peer %d%s", CharacterName((BYTE)charIndex), owner,
			    (owner == LocalPeerIndex()) ? "  (us)" : "");
		}

		for (int peer = 0; peer < MaxPlayers; ++peer)
		{
			if (g_explicitChar[peer] >= 0)
				Log("  explicit: peer %d is bound to %s%s", peer,
				    CharacterName((BYTE)g_explicitChar[peer]),
				    PeerInSession(peer) ? "" : " BUT THAT PEER IS GONE, so it falls through");
		}
	}

} // namespace pilgrimage
