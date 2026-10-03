#pragma once

// The transport interface: move bytes between a small fixed number of peers.
//
// Nothing above this layer is allowed to know which backend is running. That is
// the whole reason it exists. The project wants Steam P2P in the end, but the
// Steam interface in the shipped steam_api.dll has to be called through a vtable
// whose exact layout is still being verified, and none of the session, lockstep
// or command-channel work depends on the answer. So the session layer is built
// and tested over loopback UDP now, and the Steam backend slots in underneath it
// later with nothing above having to change.
//
// Deliberately small. No connection negotiation, no reliability layer, no
// ordering guarantees beyond what the backend already gives. Those belong in the
// session layer where they can be tested without a network.
//
// Thread rules. Everything here is called from the game thread, once per frame,
// from inside the animate hook. No background threads, no locks, no allocation
// after Start. A UI thread must never touch a transport.

namespace workshop
{

	// Fixed capacity so nothing allocates and a peer index stays valid. Headroom
	// over what the session layer actually permits, which is its own constant and
	// its own reason.
	const int TransportMaxPeers = 4;

	const int InvalidPeer = -1;

	// Keep a single message inside one datagram. The reliable path on a real backend
	// will fragment for you, but the unreliable path will not, and a design that
	// quietly depends on fragmentation works on loopback and fails on a real link.
	const int MaxPacketBytes = 1200;

	enum SendMode
	{
		// Dropped rather than delayed. For anything that is replaced by the next
		// frame's version, which is most per-step traffic.
		SendUnreliable,
		// Retried and delivered in order. For anything whose loss changes the
		// outcome: handshakes, commands, checksums.
		SendReliable,
	};

	struct Packet
	{
		int peer;   // who sent it, an index into the transport's peers
		int length; // bytes valid in data
		unsigned char data[MaxPacketBytes];
	};

	// Why virtual rather than a function table or a compile-time switch: there are
	// at most three peers and a handful of messages a frame, so dispatch cost is
	// irrelevant, and being able to run the loopback backend and the Steam backend
	// side by side in one build is worth much more than the indirection.
	class Transport
	{
	public:
		virtual ~Transport() {}

		// Short name for the log and the control panel, for example "udp" or "steam".
		virtual const char* Name() const = 0;

		virtual bool Running() const = 0;
		virtual void Stop() = 0;

		// Call once per frame before reading. Backends that need to service their own
		// callbacks or retransmits do it here. Starting a backend is backend
		// specific, so each one declares its own Start rather than forcing a shape
		// that fits neither Steam nor a socket.
		virtual void Pump() = 0;

		virtual bool Send(int peer, const void* data, int length, SendMode mode) = 0;

		// Sends to every connected peer. Returns how many it reached.
		virtual int SendToAll(const void* data, int length, SendMode mode) = 0;

		// Pops one received message. Returns false when the queue is empty, so the
		// caller drains it with a while loop. Packets from an unknown source are
		// dropped by the backend rather than surfaced, so out.peer is always valid.
		virtual bool Receive(Packet& out) = 0;

		virtual int PeerCount() const = 0;
		virtual bool PeerConnected(int peer) const = 0;

		// Human readable, for logs. An address for a socket, a SteamID for Steam.
		// Never NULL, so it is safe to print without checking.
		virtual const char* PeerLabel(int peer) const = 0;

		// Counters, for the desync and link diagnostics. Cheap to read.
		virtual unsigned long PacketsSent() const = 0;
		virtual unsigned long PacketsReceived() const = 0;
		virtual unsigned long PacketsDropped() const = 0;
	};

} // namespace workshop
