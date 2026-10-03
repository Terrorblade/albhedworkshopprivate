#pragma once

#include "workshop/SteamAbi.h"
#include "workshop/Transport.h"

// The shipping transport: Steam's peer to peer networking, through
// ISteamNetworking005.
//
// Why this one and not the modern ISteamNetworkingSockets: the game ships an SDK
// 1.31 era steam_api.dll whose only networking interface is SteamNetworking005.
// That is the deprecated P2P API, and deprecated is not removed. It still gives
// NAT traversal and Valve relay fallback, which is the whole reason to use Steam
// rather than raw sockets. For three players it is comfortably adequate.
//
// The ABI is pinned in workshop/SteamAbi.h, verified against Valve's own
// vendored SDK headers and against the compiler that builds this. Read the
// provenance comments there before changing anything in this file.
//
// ## No callbacks, deliberately
//
// Steam would normally tell us about an incoming session with a
// P2PSessionRequest_t callback, which means handing Steam a hand-rolled
// CCallbackBase for it to call into. That object's layout is verified but its
// dispatch is the one thing never actually exercised, and it is the only place we
// give Steam something to call rather than calling Steam ourselves.
//
// So we skip it. Two facts from the SDK make that work: AcceptP2PSessionWithUser
// is safe to call repeatedly, and SendP2PPacket implicitly accepts a peer's
// pending request. So each end accepts the other on a timer and sends anyway.
// At three players that is a handful of extra calls and it retires the only
// unverified item on the list.
//
// ## Who dials whom
//
// A client needs the host's SteamID up front, since it has to send the first
// packet. The host needs nothing: ReadP2PPacket reports the sender, so the host
// learns a client's SteamID from its first message, exactly like the UDP
// backend's accept-unknown-senders mode.
//
// ## The thing that will bite you
//
// The GAME owns Steam's lifetime. It calls SteamAPI_Init on its own schedule, so
// the SteamNetworking export returns something that must not be dereferenced
// until that has happened. Start can therefore fail simply for being early, and
// is safe to retry. We never call SteamAPI_Init, SteamAPI_Shutdown or
// SteamAPI_RunCallbacks, because the game is already doing it and doing it again
// would fight with it.
//
// Everything here runs on the game thread, once per frame, from the frame hook.

namespace workshop
{

	class SteamTransport : public Transport
	{
	public:
		SteamTransport();
		virtual ~SteamTransport();

		// Resolves the interface out of the already-loaded steam_api.dll. Returns
		// false and logs why if Steam is not up yet, which is a retry rather than an
		// error. acceptUnknownSenders is the host's mode, same meaning as the UDP
		// backend: a new SteamID that sends us something becomes a peer.
		bool Start(bool acceptUnknownSenders);

		// Adds a peer by SteamID, which is what a client does with the host's. The
		// text form is the 17 digit number from a Steam profile. Returns the peer
		// index, or InvalidPeer if the text will not parse or the table is full.
		int AddPeer(uint64 steamId);
		int AddPeerFromText(const char* steamIdText);

		virtual const char* Name() const;
		virtual bool Running() const;
		virtual void Stop();
		virtual void Pump();
		virtual bool Send(int peer, const void* data, int length, SendMode mode);
		virtual int SendToAll(const void* data, int length, SendMode mode);
		virtual bool Receive(Packet& out);
		virtual int PeerCount() const;
		virtual bool PeerConnected(int peer) const;
		virtual const char* PeerLabel(int peer) const;
		virtual unsigned long PacketsSent() const;
		virtual unsigned long PacketsReceived() const;
		virtual unsigned long PacketsDropped() const;

		// Diagnostics straight out of GetP2PSessionState. The SDK calls that
		// debug-only, so treat the result as something to show a player, never as
		// control flow. Returns false when there is no session with that peer.
		bool DescribeLink(int peer, char* out, int count) const;

	private:
		struct PeerEntry
		{
			uint64 steamId;
			bool inUse;
			char label[24];
		};

		int PeerForSteamId(uint64 steamId);

		void* networking; // ISteamNetworking005 *, untyped so the header above
		                  // is the only place that knows the shape
		bool running;
		bool acceptUnknown;

		PeerEntry peers[TransportMaxPeers];

		// Accepting on a timer rather than every frame. Calling it repeatedly is
		// documented as safe but it is still an IPC round trip to the Steam client,
		// and once every couple of seconds is plenty for a handshake.
		unsigned long frameCounter;

		unsigned long sent;
		unsigned long received;
		unsigned long dropped;
	};

} // namespace workshop
