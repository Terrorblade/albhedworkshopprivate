#include "workshop/SteamTransport.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workshop/HostModule.h" // Readable
#include "workshop/Log.h"

namespace workshop
{
	namespace
	{

		// How often to re-accept every known peer. See the header: calling
		// AcceptP2PSessionWithUser repeatedly is documented as safe, but it is an IPC
		// round trip, so once every two seconds at 30 Hz is plenty for a handshake.
		const unsigned long AcceptEveryFrames = 60;

		// One channel is enough. Keeping it at 0 everywhere means the channel argument
		// never becomes a thing to get wrong, and the session layer already multiplexes
		// by message kind.
		const int Channel = 0;

		ISteamNetworking005* Interface(void* raw)
		{
			return (ISteamNetworking005*)raw;
		}

	} // namespace

	SteamTransport::SteamTransport()
	    : networking(NULL),
	      running(false),
	      acceptUnknown(false),
	      frameCounter(0),
	      sent(0),
	      received(0),
	      dropped(0)
	{
		memset(peers, 0, sizeof(peers));
	}

	SteamTransport::~SteamTransport()
	{
		Stop();
	}

	bool SteamTransport::Start(bool acceptUnknownSenders)
	{
		if (running)
			return true;

		// The game imports steam_api.dll, so it is already loaded and we must not
		// load a second copy. GetModuleHandle, never LoadLibrary.
		HMODULE steamApi = GetModuleHandleA("steam_api.dll");
		if (!steamApi)
		{
			Log("steam: steam_api.dll is not loaded in this process, so there is no "
			    "Steam to talk to. Is the game running outside Steam?");
			return false;
		}

		FnSteamNetworking getNetworking =
		    (FnSteamNetworking)GetProcAddress(steamApi, "SteamNetworking");
		if (!getNetworking)
		{
			Log("steam: steam_api.dll has no SteamNetworking export, which should be "
			    "impossible for this build. Refusing to continue.");
			return false;
		}

		// The game owns SteamAPI_Init and calls it on its own schedule, so being
		// early gives a null here. That is a retry, not a failure.
		void* iface = getNetworking();
		if (!iface)
		{
			Log("steam: SteamNetworking() returned null, so the game has not "
			    "initialised Steam yet. Try again in a moment.");
			return false;
		}

		// A returned pointer still has to be a readable object with a readable
		// vtable before anything calls through it. Cheap, and the alternative is a
		// fault on the frame path.
		if (!Readable(iface, sizeof(void*)))
		{
			Log("steam: SteamNetworking() returned %p, which is not readable. "
			    "Refusing to call through it.",
			    iface);
			return false;
		}
		void* vtable = *(void**)iface;
		if (!Readable(vtable, sizeof(void*) * 8))
		{
			Log("steam: the interface at %p has an unreadable vtable at %p. "
			    "Refusing to call through it.",
			    iface, vtable);
			return false;
		}

		networking = iface;
		acceptUnknown = acceptUnknownSenders;
		running = true;
		frameCounter = 0;
		memset(peers, 0, sizeof(peers));

		// Relay is on by default, so this is not strictly needed. It is here because
		// relying on a default we did not set is how a later SDK change becomes a
		// mystery, and because for three friends behind home routers the relay is
		// the difference between working and not.
		Interface(networking)->AllowP2PPacketRelay(true);

		Log("steam: ISteamNetworking005 at %p, %s", iface,
		    acceptUnknown ? "accepting new SteamIDs as peers" : "only talking to peers we added");
		return true;
	}

	int SteamTransport::AddPeer(uint64 steamId)
	{
		if (steamId == 0)
			return InvalidPeer;

		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].steamId == steamId)
				return i;

		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].steamId = steamId;
			peers[i].inUse = true;
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%llu", steamId);

			// Accept immediately as well as on the timer, so a handshake does not
			// wait two seconds for no reason.
			if (running)
				Interface(networking)->AcceptP2PSessionWithUser(steamId);

			Log("steam: peer %d is SteamID %llu", i, steamId);
			return i;
		}

		Log("steam: no room for another peer, %d is the capacity", TransportMaxPeers);
		return InvalidPeer;
	}

	int SteamTransport::AddPeerFromText(const char* steamIdText)
	{
		if (!steamIdText || !*steamIdText)
		{
			Log("steam: no SteamID given. It is the 17 digit number from the host's "
			    "Steam profile page.");
			return InvalidPeer;
		}

		// Deliberately strict. _strtoui64 would happily read "76561" out of
		// "76561abc" and connect to nobody, which is a worse failure than a clear
		// refusal.
		const char* p = steamIdText;
		while (*p == ' ' || *p == '\t')
			++p;
		int digits = 0;
		for (const char* q = p; *q; ++q)
		{
			if (*q >= '0' && *q <= '9')
			{
				++digits;
				continue;
			}
			if (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
				continue;
			Log("steam: '%s' is not a SteamID. Expected only digits, found '%c'.",
			    steamIdText, *q);
			return InvalidPeer;
		}
		if (digits < 10)
		{
			Log("steam: '%s' is too short to be a SteamID, it should be about 17 digits",
			    steamIdText);
			return InvalidPeer;
		}

		const uint64 id = (uint64)_strtoui64(p, NULL, 10);

		// An individual account on the public universe. Catches a lobby id or a
		// group id pasted by mistake, which otherwise connects to silence.
		if (SteamIdAccountType(id) != k_EAccountTypeIndividual_value ||
		    SteamIdUniverse(id) != k_EUniversePublic_value)
		{
			Log("steam: %llu is a valid number but not an individual public account. "
			    "That looks like a lobby or group id rather than a person.",
			    id);
			return InvalidPeer;
		}

		return AddPeer(id);
	}

	const char* SteamTransport::Name() const
	{
		return "steam";
	}
	bool SteamTransport::Running() const
	{
		return running;
	}

	void SteamTransport::Stop()
	{
		if (running && networking)
		{
			for (int i = 0; i < TransportMaxPeers; ++i)
				if (peers[i].inUse)
					Interface(networking)->CloseP2PSessionWithUser(peers[i].steamId);
			Log("steam: stopped, sent %lu received %lu dropped %lu", sent, received, dropped);
		}
		running = false;
		networking = NULL;
		memset(peers, 0, sizeof(peers));
	}

	void SteamTransport::Pump()
	{
		if (!running)
			return;
		++frameCounter;

		// Standing in for the P2PSessionRequest_t callback we deliberately do not
		// use. Re-accepting a peer we already have is documented as harmless, and it
		// is what lets a session recover when one side restarts.
		if ((frameCounter % AcceptEveryFrames) == 0)
		{
			for (int i = 0; i < TransportMaxPeers; ++i)
				if (peers[i].inUse)
					Interface(networking)->AcceptP2PSessionWithUser(peers[i].steamId);
		}
	}

	bool SteamTransport::Send(int peer, const void* data, int length, SendMode mode)
	{
		if (!running || !data)
			return false;
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return false;
		if (length <= 0)
			return false;

		// The ceiling is per mode, not global: 1200 bytes unreliable, 1 MB reliable
		// with Steam fragmenting and reassembling for us. Checked here rather than
		// trusted, because an oversized unreliable send fails silently and that is a
		// horrible thing to debug.
		const int ceiling = (mode == SendReliable)
		                        ? (int)k_cbMaxP2PPacketReliable
		                        : (int)k_cbMaxP2PPacketUnreliable;
		if (length > ceiling)
		{
			Log("steam: refusing a %d byte %s message, the limit for that mode is %d",
			    length, (mode == SendReliable) ? "reliable" : "unreliable", ceiling);
			return false;
		}

		const EP2PSend how = (mode == SendReliable) ? k_EP2PSendReliable
		                                            : k_EP2PSendUnreliableNoDelay;

		const bool ok = Interface(networking)->SendP2PPacket(peers[peer].steamId, data, (uint32)length, how, Channel);
		if (!ok)
		{
			++dropped;
			return false;
		}
		++sent;
		return true;
	}

	int SteamTransport::SendToAll(const void* data, int length, SendMode mode)
	{
		int reached = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && Send(i, data, length, mode))
				++reached;
		return reached;
	}

	bool SteamTransport::Receive(Packet& out)
	{
		if (!running)
			return false;

		// Loops rather than giving up on the first packet from a stranger, so one
		// stray message does not hide the real traffic queued behind it.
		for (;;)
		{
			uint32 waiting = 0;
			if (!Interface(networking)->IsP2PPacketAvailable(&waiting, Channel))
				return false;

			// ReadP2PPacket TRUNCATES silently if the buffer is too small, so a
			// message bigger than Packet can hold has to be read and discarded
			// rather than half delivered. Nothing we send is this big, which is
			// exactly why hitting it means something is wrong.
			if (waiting > (uint32)MaxPacketBytes)
			{
				unsigned char scratch[MaxPacketBytes];
				uint32 read = 0;
				SteamId from;
				from.m_unAll64Bits = 0;
				Interface(networking)->ReadP2PPacket(scratch, sizeof(scratch), &read, &from, Channel);
				++dropped;
				Log("steam: dropped a %lu byte message from %llu, which is over the %d "
				    "byte limit this transport carries",
				    (unsigned long)waiting, from.m_unAll64Bits, MaxPacketBytes);
				continue;
			}

			uint32 read = 0;
			SteamId from;
			from.m_unAll64Bits = 0;
			if (!Interface(networking)->ReadP2PPacket(out.data, (uint32)MaxPacketBytes, &read, &from, Channel))
				return false;
			if (read == 0)
				return false;

			const int peer = PeerForSteamId(from.m_unAll64Bits);
			if (peer == InvalidPeer)
			{
				++dropped;
				continue;
			}

			out.peer = peer;
			out.length = (int)read;
			++received;
			return true;
		}
	}

	int SteamTransport::PeerForSteamId(uint64 steamId)
	{
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].steamId == steamId)
				return i;

		if (!acceptUnknown || steamId == 0)
			return InvalidPeer;

		// The host learning a joiner's SteamID from its first message. This is what
		// replaces the P2PSessionRequest_t callback.
		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].steamId = steamId;
			peers[i].inUse = true;
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%llu", steamId);
			Interface(networking)->AcceptP2PSessionWithUser(steamId);
			Log("steam: accepted peer %d, SteamID %llu", i, steamId);
			return i;
		}
		return InvalidPeer;
	}

	int SteamTransport::PeerCount() const
	{
		int count = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse)
				++count;
		return count;
	}

	bool SteamTransport::PeerConnected(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers)
			return false;
		return peers[peer].inUse;
	}

	const char* SteamTransport::PeerLabel(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return "none";
		return peers[peer].label;
	}

	unsigned long SteamTransport::PacketsSent() const
	{
		return sent;
	}
	unsigned long SteamTransport::PacketsReceived() const
	{
		return received;
	}
	unsigned long SteamTransport::PacketsDropped() const
	{
		return dropped;
	}

	bool SteamTransport::DescribeLink(int peer, char* out, int count) const
	{
		if (!out || count <= 0)
			return false;
		out[0] = 0;
		if (!running || peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return false;

		P2PSessionState_t state;
		memset(&state, 0, sizeof(state));
		if (!Interface(networking)->GetP2PSessionState(peers[peer].steamId, &state))
		{
			strncpy_s(out, (size_t)count, "no session", _TRUNCATE);
			return false;
		}

		_snprintf_s(out, (size_t)count, _TRUNCATE, "%s%s, %s, %d bytes queued",
		    state.m_bConnectionActive ? "active" : "not active",
		    state.m_bConnecting ? " (connecting)" : "",
		    state.m_bUsingRelay ? "via Steam relay" : "direct",
		    (int)state.m_nBytesQueuedForSend);
		return true;
	}

} // namespace workshop
