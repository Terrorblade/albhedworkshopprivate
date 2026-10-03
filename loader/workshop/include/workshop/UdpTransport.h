#pragma once

#include "workshop/Transport.h"

// The development transport: plain UDP, usually over loopback.
//
// This is not the shipping backend. Steam P2P is, and this exists because of a
// practical problem with building against Steam first. Steam P2P needs two Steam
// accounts on two machines, so every iteration of the session layer would mean
// coordinating a second person. Over loopback, two copies of the game on one PC
// talk to each other, which is the difference between testing the lockstep layer
// a few times a day and testing it every build.
//
// It also works over a real LAN, and through any tunnel the user already has, so
// it is a usable fallback rather than purely a test fixture.
//
// **What it does not do: reliability.** SendReliable and SendUnreliable both come
// out as one sendto. On loopback that is honest, because loopback does not drop.
// On a real link it is not, so the protocol layer above numbers its messages and
// reports gaps rather than trusting the word reliable. See coopnet/Protocol.h.
// The Steam backend gives real reliability, so this is the weaker of the two and
// nothing above should be written to depend on the gap.
//
// Everything here runs on the game thread. The socket is non-blocking, so a frame
// never stalls on the network.

namespace workshop
{

	class UdpTransport : public Transport
	{
	public:
		UdpTransport();
		virtual ~UdpTransport();

		// Binds localPort and gets ready to talk. Returns false and logs why on
		// failure, which is almost always the port already being in use by the other
		// instance on this machine.
		//
		// acceptUnknownSenders is the host's mode: the first packet from a new
		// address adds that address as a peer, up to the capacity. A client leaves it
		// false and calls AddPeer for the host instead. That split mirrors Steam,
		// where the host accepts an incoming session request and the client already
		// knows who it is dialling.
		bool Start(unsigned short localPort, bool acceptUnknownSenders);

		// Adds a peer by address, for example "127.0.0.1". Returns its peer index, or
		// InvalidPeer if the table is full or the address will not parse.
		int AddPeer(const char* address, unsigned short port);

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

		unsigned short LocalPort() const;

	private:
		struct PeerEntry
		{
			unsigned long address; // network order, as sockaddr_in wants it
			unsigned short port;   // network order
			bool inUse;
			char label[32];
		};

		// Returns the peer index for a sender, adding it when the host is accepting
		// unknown senders. InvalidPeer means drop the packet.
		int PeerForSender(unsigned long address, unsigned short port);

		unsigned __int64 socketHandle; // SOCKET, kept untyped so this header needs
		                               // no winsock include
		bool winsockStarted;
		bool running;
		bool acceptUnknown;
		unsigned short localPort;

		PeerEntry peers[TransportMaxPeers];

		unsigned long sent;
		unsigned long received;
		unsigned long dropped;
	};

} // namespace workshop
