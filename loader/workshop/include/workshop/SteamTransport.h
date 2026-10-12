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
// ## One callback, and it is not optional
//
// P2PSessionRequest_t is registered. An earlier version of this file skipped it
// on the theory that we always know every peer's SteamID up front and so could
// just accept everyone on a timer. That is true of a CLIENT, which is handed the
// host's SteamID by the player. IT IS FALSE OF A HOST, which is given nothing and
// has to learn a joiner's SteamID from the joiner.
//
// Skipping it deadlocks a real join, and this is what that looked like:
//
//   client   steam: peer 0 is SteamID 765...079
//   client   net: joining SteamID 765...079
//   host     (nothing, for two solid minutes)
//
// ISteamNetworking will not hand you a packet from a peer you have no session
// with. It holds the packet, posts P2PSessionRequest_t, and drops the packet if
// you never accept. So the host's "learn the sender from ReadP2PPacket" path sits
// downstream of the very thing that is blocking it:
//
//   accept needs the SteamID -> the SteamID comes from a packet -> the packet
//   needs the session accepted
//
// The callback is the only thing that breaks that circle, because it is the only
// place Steam volunteers the SteamID of someone you have never talked to.
//
// P2PSessionConnectFail_t is registered too, so a join that cannot get through
// says so instead of looking like a hang. Error 4 is a timeout, which in practice
// means the far end never accepted.
//
// ## The dispatch, which we checked rather than assumed
//
// FFX.exe calls SteamAPI_RunCallbacks in exactly one place, FFXApplication
// ::animate at RVA 0x0042F52C, gated on a byte set when SteamAPI_Init succeeds.
// So the game pumps callbacks once per frame and we must not pump them too.
//
// That call is INSIDE animate, and a mod's animate hook wraps animate, so a
// handler can re-enter us while we are mid-hook. Handlers therefore do nothing
// but push a SteamID onto a queue, and Pump drains it on our own tick.
//
// ## Who dials whom
//
// A client needs the host's SteamID up front, since it has to send the first
// packet. The host needs nothing in advance: it learns a joiner from
// P2PSessionRequest_t, accepts the session, and the joiner's next send arrives
// normally.
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

		// ---------------------------------------------------------------------------
		// CALLED BY STEAM, NOT BY YOU.
		//
		// These two are what the registered callbacks funnel into. They run inside
		// SteamAPI_RunCallbacks, which FFX calls from animate, so they can land in the
		// middle of a mod that hooks animate. They do the least possible: record and
		// return. Pump does the real work.
		// ---------------------------------------------------------------------------
		void NoteSessionRequest(uint64 steamId);
		void NoteConnectFail(uint64 steamId, int sessionError);

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

		// Session requests handed to us by Steam and not yet turned into peers. Written
		// only by NoteSessionRequest, drained only by Pump. Small on purpose: a request
		// we have no room for is a peer we could not have accepted anyway.
		uint64 incoming[TransportMaxPeers];
		volatile long incomingCount;

		unsigned long sent;
		unsigned long received;
		unsigned long dropped;
	};

} // namespace workshop
