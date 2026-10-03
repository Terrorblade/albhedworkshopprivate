#include "workshop/UdpTransport.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "workshop/Log.h"

#pragma comment(lib, "ws2_32.lib")

namespace workshop
{

	UdpTransport::UdpTransport()
	    : socketHandle(INVALID_SOCKET),
	      winsockStarted(false),
	      running(false),
	      acceptUnknown(false),
	      localPort(0),
	      sent(0),
	      received(0),
	      dropped(0)
	{
		memset(peers, 0, sizeof(peers));
	}

	UdpTransport::~UdpTransport()
	{
		Stop();
	}

	bool UdpTransport::Start(unsigned short port, bool acceptUnknownSenders)
	{
		if (running)
		{
			Log("udp: already running on port %u", localPort);
			return true;
		}

		// Refcounted, so starting Winsock again is safe even though Steam has
		// certainly already started it in this process.
		WSADATA wsa;
		const int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (startup != 0)
		{
			Log("udp: WSAStartup failed, %d", startup);
			return false;
		}
		winsockStarted = true;

		SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (handle == INVALID_SOCKET)
		{
			Log("udp: could not create a socket, error %d", WSAGetLastError());
			Stop();
			return false;
		}

		sockaddr_in local;
		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = htons(port);
		if (bind(handle, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			Log("udp: could not bind port %u, error %d.%s", port, error,
			    error == WSAEADDRINUSE
			        ? " That port is already taken, which usually means the other"
			          " instance on this machine has it. Give this one a different port."
			        : "");
			closesocket(handle);
			Stop();
			return false;
		}

		// Non-blocking, because this is called from the frame path and a frame must
		// never wait on the network.
		u_long nonBlocking = 1;
		ioctlsocket(handle, FIONBIO, &nonBlocking);

		socketHandle = handle;
		localPort = port;
		acceptUnknown = acceptUnknownSenders;
		running = true;

		Log("udp: listening on port %u, %s", port,
		    acceptUnknown ? "accepting new senders as peers" : "only talking to peers we added");
		return true;
	}

	int UdpTransport::AddPeer(const char* address, unsigned short port)
	{
		if (!address || !*address)
			return InvalidPeer;

		unsigned long resolved = 0;
		if (inet_pton(AF_INET, address, &resolved) != 1)
		{
			Log("udp: '%s' is not an address I can parse", address);
			return InvalidPeer;
		}
		const unsigned short netPort = htons(port);

		// Already known is success, not a duplicate. Makes AddPeer safe to call every
		// frame from a connect button without building up entries.
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].address == resolved && peers[i].port == netPort)
				return i;

		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].address = resolved;
			peers[i].port = netPort;
			peers[i].inUse = true;
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%s:%u", address, port);
			Log("udp: peer %d is %s", i, peers[i].label);
			return i;
		}

		Log("udp: no room for another peer, %d is the capacity", TransportMaxPeers);
		return InvalidPeer;
	}

	int UdpTransport::PeerForSender(unsigned long address, unsigned short port)
	{
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].address == address && peers[i].port == port)
				return i;

		if (!acceptUnknown)
			return InvalidPeer;

		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].address = address;
			peers[i].port = port;
			peers[i].inUse = true;

			char text[INET_ADDRSTRLEN] = "?";
			inet_ntop(AF_INET, &address, text, sizeof(text));
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%s:%u",
			    text, (unsigned)ntohs(port));
			Log("udp: accepted peer %d from %s", i, peers[i].label);
			return i;
		}
		return InvalidPeer;
	}

	const char* UdpTransport::Name() const
	{
		return "udp";
	}
	bool UdpTransport::Running() const
	{
		return running;
	}
	unsigned short UdpTransport::LocalPort() const
	{
		return localPort;
	}

	void UdpTransport::Stop()
	{
		if (socketHandle != INVALID_SOCKET)
		{
			closesocket((SOCKET)socketHandle);
			socketHandle = INVALID_SOCKET;
		}
		if (winsockStarted)
		{
			WSACleanup();
			winsockStarted = false;
		}
		if (running)
			Log("udp: stopped, sent %lu received %lu dropped %lu", sent, received, dropped);
		running = false;
		memset(peers, 0, sizeof(peers));
	}

	// Nothing to service. UDP has no retransmits of ours to drive and no callbacks
	// to pump, so receiving happens entirely in Receive.
	void UdpTransport::Pump() {}

	bool UdpTransport::Send(int peer, const void* data, int length, SendMode)
	{
		// SendMode is ignored on purpose. See the header: this backend has no
		// reliability layer, and pretending otherwise would be worse than the gap.
		if (!running || !data)
			return false;
		if (length <= 0 || length > MaxPacketBytes)
		{
			Log("udp: refusing a %d byte message, the limit is %d", length, MaxPacketBytes);
			return false;
		}
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return false;

		sockaddr_in to;
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = peers[peer].address;
		to.sin_port = peers[peer].port;

		const int written = sendto((SOCKET)socketHandle, (const char*)data, length, 0,
		    (sockaddr*)&to, sizeof(to));
		if (written == SOCKET_ERROR)
		{
			++dropped;
			return false;
		}
		++sent;
		return true;
	}

	int UdpTransport::SendToAll(const void* data, int length, SendMode mode)
	{
		int reached = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && Send(i, data, length, mode))
				++reached;
		return reached;
	}

	bool UdpTransport::Receive(Packet& out)
	{
		if (!running)
			return false;

		// Loops rather than returning false on the first unknown sender, so one
		// stray packet does not hide the real traffic queued behind it.
		for (;;)
		{
			sockaddr_in from;
			int fromLength = sizeof(from);
			const int read = recvfrom((SOCKET)socketHandle, (char*)out.data, MaxPacketBytes, 0,
			    (sockaddr*)&from, &fromLength);
			if (read == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK)
					return false; // nothing queued, normal

				// On Windows a UDP socket reports WSAECONNRESET when an earlier send
				// drew an ICMP port-unreachable, which happens constantly while the
				// other instance has not started listening yet. It says nothing about
				// this socket's health, so count it and keep draining rather than
				// treating it as a dead link.
				++dropped;
				if (error == WSAECONNRESET || error == WSAEMSGSIZE)
					continue;
				return false;
			}
			if (read <= 0)
				return false;

			const int peer = PeerForSender(from.sin_addr.s_addr, from.sin_port);
			if (peer == InvalidPeer)
			{
				++dropped;
				continue;
			}

			out.peer = peer;
			out.length = read;
			++received;
			return true;
		}
	}

	int UdpTransport::PeerCount() const
	{
		int count = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse)
				++count;
		return count;
	}

	bool UdpTransport::PeerConnected(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers)
			return false;
		return peers[peer].inUse;
	}

	const char* UdpTransport::PeerLabel(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return "none";
		return peers[peer].label;
	}

	unsigned long UdpTransport::PacketsSent() const
	{
		return sent;
	}
	unsigned long UdpTransport::PacketsReceived() const
	{
		return received;
	}
	unsigned long UdpTransport::PacketsDropped() const
	{
		return dropped;
	}

} // namespace workshop
