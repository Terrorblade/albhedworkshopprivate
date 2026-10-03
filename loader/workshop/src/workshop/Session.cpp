#include "workshop/Session.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "workshop/Log.h"

// ---------------------------------------------------------------------------
// Topology: a star, with the host in the middle.
//
// Clients talk only to the host and never directly to each other. With three
// players a full mesh would need the host to introduce two clients to one
// another, which on Steam means exchanging SteamIDs and on UDP means punching a
// hole, and it buys nothing here. Under lockstep the host is already the thing
// that stamps a step onto every command, so a client to client message would go
// through the host anyway.
//
// The cost is one extra hop of latency on client to client traffic, which does
// not exist yet. If it ever does, the host relays it, and nothing above this
// layer has to change because everything above addresses peers by session peer
// id rather than by transport slot.
// ---------------------------------------------------------------------------

namespace workshop
{
	namespace
	{

		// Counted in frames, since Step is called once per frame and the simulation is
		// fixed step at 29.97 Hz. Roughly half a second between beats, ten seconds
		// before a silent peer is dropped. The timeout is deliberately generous: a peer
		// loading a map is not a peer that has gone away.
		const uint32_t HeartbeatEveryFrames = 15;
		const uint32_t PeerTimeoutFrames = 300;
		const uint32_t HelloRetryFrames = 30;

		// A flood, a loop or a hostile sender must not be able to hold the frame. Ten
		// times what a sane session produces in one frame.
		const int MaxMessagesPerFrame = 256;

		// The host is always session peer 0. Several things read better for saying so.

	} // namespace

	Session::Session()
	    : transport(NULL),
	      role(RoleNone),
	      state(SessionIdle),
	      localPeer(PeerUnassigned),
	      buildId(0),
	      sharedSettingsHash(0),
	      localStep(0),
	      messageSink(NULL),
	      frame(0),
	      nextSequence(1),
	      lastHeartbeatFrame(0),
	      messagesSent(0),
	      messagesReceived(0),
	      messagesIgnored(0)
	{
		memset(localName, 0, sizeof(localName));
		memset(peers, 0, sizeof(peers));
		memset(rejectText, 0, sizeof(rejectText));
		for (int i = 0; i < MaxPlayers; ++i)
			transportSlotForPeer[i] = InvalidPeer;
	}

	bool Session::Start(Transport* useTransport, SessionRole useRole, uint32_t useBuildId,
	    const char* playerName)
	{
		if (!useTransport || !useTransport->Running())
		{
			Log("session: refusing to start, the transport is not running");
			state = SessionFailed;
			return false;
		}
		if (useRole != RoleHost && useRole != RoleClient)
		{
			Log("session: refusing to start without a role");
			state = SessionFailed;
			return false;
		}

		transport = useTransport;
		role = useRole;
		buildId = useBuildId;
		frame = 0;
		nextSequence = 1;
		lastHeartbeatFrame = 0;
		messagesSent = messagesReceived = messagesIgnored = 0;
		rejectText[0] = 0;

		memset(peers, 0, sizeof(peers));
		for (int i = 0; i < MaxPlayers; ++i)
			transportSlotForPeer[i] = InvalidPeer;

		strncpy_s(localName, sizeof(localName), (playerName && *playerName) ? playerName : "player",
		    _TRUNCATE);

		if (role == RoleHost)
		{
			localPeer = HostPeer;
			peers[HostPeer].active = true;
			peers[HostPeer].isLocal = true;
			strncpy_s(peers[HostPeer].name, sizeof(peers[HostPeer].name), localName, _TRUNCATE);
			peers[HostPeer].sharedSettingsHash = sharedSettingsHash;
			state = SessionListening;
			Log("session: hosting over %s, up to %d players, build %08X",
			    transport->Name(), MaxPlayers, buildId);
		}
		else
		{
			// A client does not know its id until the host says so, and it must not
			// act on anything owner-specific before then.
			localPeer = PeerUnassigned;
			state = SessionJoining;
			Log("session: joining over %s, build %08X", transport->Name(), buildId);
			SendHello();
		}
		return true;
	}

	void Session::Stop()
	{
		if (state == SessionIdle)
			return;

		if (transport && transport->Running() &&
		    (state == SessionActive || state == SessionListening))
		{
			GoodbyePayload bye;
			memset(&bye, 0, sizeof(bye));
			for (int i = 0; i < MaxPlayers; ++i)
				if (transportSlotForPeer[i] != InvalidPeer)
					SendTo(transportSlotForPeer[i], MessageGoodbye, &bye, sizeof(bye), SendReliable);
		}

		Log("session: stopped after %lu frames, sent %lu received %lu ignored %lu gaps %lu",
		    frame, messagesSent, messagesReceived, messagesIgnored, TotalSequenceGaps());

		transport = NULL;
		role = RoleNone;
		state = SessionIdle;
		localPeer = PeerUnassigned;
		memset(peers, 0, sizeof(peers));
		for (int i = 0; i < MaxPlayers; ++i)
			transportSlotForPeer[i] = InvalidPeer;
	}

	void Session::Step()
	{
		if (!transport || state == SessionIdle || state == SessionFailed ||
		    state == SessionRejected)
			return;

		++frame;
		transport->Pump();

		Packet packet;
		int handled = 0;
		while (handled < MaxMessagesPerFrame && transport->Receive(packet))
		{
			++handled;
			HandleMessage(packet.peer, packet.data, packet.length);
		}
		if (handled >= MaxMessagesPerFrame)
			Log("session: hit the %d message cap in one frame, something is flooding",
			    MaxMessagesPerFrame);

		// A joiner keeps asking until it is answered. The host may not be up yet,
		// which on loopback is the usual case since one instance starts first.
		if (state == SessionJoining && (frame % HelloRetryFrames) == 0)
			SendHello();

		if ((state == SessionActive || state == SessionListening) &&
		    frame - lastHeartbeatFrame >= HeartbeatEveryFrames)
		{
			SendHeartbeat();
			lastHeartbeatFrame = frame;
		}

		DropQuietPeers();
	}

	// ---------------------------------------------------------------------------
	// Receiving
	// ---------------------------------------------------------------------------

	void Session::HandleMessage(int transportPeer, const unsigned char* data, int length)
	{
		if (length < (int)sizeof(MessageHeader))
		{
			++messagesIgnored;
			return;
		}

		MessageHeader header;
		memcpy(&header, data, sizeof(header));

		// A UDP port receives whatever is sent to it, so anything without our magic
		// is somebody else's traffic and is not worth a log line.
		if (header.magic != ProtocolMagic)
		{
			++messagesIgnored;
			return;
		}

		if (header.version != ProtocolVersion)
		{
			++messagesIgnored;
			// Only the host can do anything useful about this, and only in reply to a
			// hello. Anything else from a mismatched build gets dropped quietly
			// rather than filling the log once per frame.
			if (role == RoleHost && header.kind == MessageHello)
				SendReject(transportPeer, RejectVersionMismatch, ProtocolVersion);
			return;
		}

		++messagesReceived;

		const unsigned char* payload = data + sizeof(MessageHeader);
		const int payloadLength = length - (int)sizeof(MessageHeader);

		// Sequence tracking needs a known sender, so it happens after the handshake
		// has given one an id.
		if (header.sender != PeerUnassigned)
			TrackSequence(header.sender, header.sequence);

		switch (header.kind)
		{
		case MessageHello:
		{
			if (payloadLength < (int)sizeof(HelloPayload))
			{
				++messagesIgnored;
				return;
			}
			HelloPayload hello;
			memcpy(&hello, payload, sizeof(hello));
			HandleHello(transportPeer, header, hello);
			break;
		}
		case MessageWelcome:
		{
			if (payloadLength < (int)sizeof(WelcomePayload))
			{
				++messagesIgnored;
				return;
			}
			WelcomePayload welcome;
			memcpy(&welcome, payload, sizeof(welcome));
			// Remember how to reach the host before anything else, since every
			// later message goes back the same way.
			transportSlotForPeer[HostPeer] = transportPeer;
			HandleWelcome(welcome);
			break;
		}
		case MessageReject:
		{
			if (payloadLength < (int)sizeof(RejectPayload))
			{
				++messagesIgnored;
				return;
			}
			RejectPayload reject;
			memcpy(&reject, payload, sizeof(reject));
			HandleReject(reject);
			break;
		}
		case MessageHeartbeat:
		{
			if (payloadLength < (int)sizeof(HeartbeatPayload))
			{
				++messagesIgnored;
				return;
			}
			HeartbeatPayload beat;
			memcpy(&beat, payload, sizeof(beat));
			HandleHeartbeat(header, beat);
			break;
		}
		case MessageGoodbye:
			HandleGoodbye(header);
			break;

		default:
			// Not one of the session's own kinds, so offer it to whatever is
			// layered on top before writing it off.
			if (!messageSink ||
			    !messageSink->OnSessionMessage(header, payload, payloadLength))
				++messagesIgnored;
			break;
		}
	}

	bool Session::PeerReachable(uint8_t peer) const
	{
		if (peer >= (uint8_t)MaxPlayers)
			return false;
		if (!peers[peer].active || peers[peer].isLocal)
			return false;
		return transportSlotForPeer[peer] != InvalidPeer;
	}

	bool Session::Send(uint8_t peer, uint8_t kind, const void* payload,
	    int payloadLength, SendMode mode)
	{
		if (peer == BroadcastPeer)
			return Broadcast(kind, payload, payloadLength, mode);
		if (!PeerReachable(peer))
			return false;
		return SendTo(transportSlotForPeer[peer], kind, payload, payloadLength, mode);
	}

	bool Session::Broadcast(uint8_t kind, const void* payload, int payloadLength,
	    SendMode mode)
	{
		// Every reachable peer, and true only if they all took it. A partial send is
		// a failure from the caller's point of view, because a command that reached
		// one client and not the other is worse than one that reached nobody.
		bool all = true;
		int sent = 0;
		for (int i = 0; i < MaxPlayers; ++i)
		{
			if (!PeerReachable((uint8_t)i))
				continue;
			++sent;
			if (!SendTo(transportSlotForPeer[i], kind, payload, payloadLength, mode))
				all = false;
		}
		return sent > 0 && all;
	}

	void Session::HandleHello(int transportPeer, const MessageHeader&, const HelloPayload& hello)
	{
		if (role != RoleHost)
		{
			SendReject(transportPeer, RejectNotHosting, 0);
			return;
		}

		// Already admitted on this transport slot, so this is a retry whose welcome
		// went missing. Answer it again rather than taking a second slot.
		for (int i = 0; i < MaxPlayers; ++i)
		{
			if (peers[i].active && !peers[i].isLocal && transportSlotForPeer[i] == transportPeer)
			{
				WelcomePayload welcome;
				memset(&welcome, 0, sizeof(welcome));
				welcome.assignedPeer = (uint8_t)i;
				welcome.playerCount = (uint8_t)PlayerCount();
				welcome.maxPlayers = (uint8_t)MaxPlayers;
				welcome.hostStep = localStep;
				welcome.sharedSettingsHash = sharedSettingsHash;
				SendTo(transportPeer, MessageWelcome, &welcome, sizeof(welcome), SendReliable);
				peers[i].lastHeardFrame = frame;
				return;
			}
		}

		if (hello.buildId != buildId)
		{
			Log("session: refused a joiner, their build %08X is not ours %08X",
			    hello.buildId, buildId);
			SendReject(transportPeer, RejectBuildMismatch, buildId);
			return;
		}
		if (hello.sharedSettingsHash != sharedSettingsHash)
		{
			Log("session: refused a joiner, shared settings %08X do not match ours %08X",
			    hello.sharedSettingsHash, sharedSettingsHash);
			SendReject(transportPeer, RejectSettingsMismatch, sharedSettingsHash);
			return;
		}

		const int slot = ClaimPeerSlot(transportPeer);
		if (slot == InvalidPeer)
		{
			SendReject(transportPeer, RejectSessionFull, (uint32_t)MaxPlayers);
			return;
		}

		strncpy_s(peers[slot].name, sizeof(peers[slot].name), hello.playerName, _TRUNCATE);
		peers[slot].name[sizeof(peers[slot].name) - 1] = 0;
		peers[slot].sharedSettingsHash = hello.sharedSettingsHash;

		WelcomePayload welcome;
		memset(&welcome, 0, sizeof(welcome));
		welcome.assignedPeer = (uint8_t)slot;
		welcome.playerCount = (uint8_t)PlayerCount();
		welcome.maxPlayers = (uint8_t)MaxPlayers;
		welcome.hostStep = localStep;
		welcome.sharedSettingsHash = sharedSettingsHash;
		SendTo(transportPeer, MessageWelcome, &welcome, sizeof(welcome), SendReliable);

		state = SessionActive;
		Log("session: peer %d joined as '%s' from %s, %d of %d players",
		    slot, peers[slot].name, transport->PeerLabel(transportPeer),
		    PlayerCount(), MaxPlayers);
	}

	void Session::HandleWelcome(const WelcomePayload& welcome)
	{
		if (role != RoleClient)
			return;

		if (welcome.assignedPeer >= MaxPlayers)
		{
			Log("session: the host assigned peer %u, which is outside 0 to %d. Ignoring it.",
			    welcome.assignedPeer, MaxPlayers - 1);
			++messagesIgnored;
			return;
		}

		// A second welcome for the same id is a retry, not news.
		const bool firstTime = (localPeer != welcome.assignedPeer);

		localPeer = welcome.assignedPeer;

		peers[localPeer].active = true;
		peers[localPeer].isLocal = true;
		peers[localPeer].lastHeardFrame = frame;
		peers[localPeer].sharedSettingsHash = sharedSettingsHash;
		strncpy_s(peers[localPeer].name, sizeof(peers[localPeer].name), localName, _TRUNCATE);

		peers[HostPeer].active = true;
		peers[HostPeer].isLocal = false;
		peers[HostPeer].lastHeardFrame = frame;
		peers[HostPeer].step = welcome.hostStep;
		peers[HostPeer].sharedSettingsHash = welcome.sharedSettingsHash;
		if (!peers[HostPeer].name[0])
			strncpy_s(peers[HostPeer].name, sizeof(peers[HostPeer].name), "host", _TRUNCATE);

		state = SessionActive;

		if (firstTime)
			Log("session: joined as peer %u, host is at step %lu, %u of %u players",
			    localPeer, welcome.hostStep, welcome.playerCount, welcome.maxPlayers);
	}

	void Session::HandleReject(const RejectPayload& reject)
	{
		state = SessionRejected;
		_snprintf_s(rejectText, sizeof(rejectText), _TRUNCATE, "%s",
		    RejectReasonText(reject.reason));
		Log("session: the host refused us. %s (its value was %08X)",
		    RejectReasonText(reject.reason), reject.hostValue);
	}

	void Session::HandleHeartbeat(const MessageHeader& header, const HeartbeatPayload& beat)
	{
		if (header.sender >= MaxPlayers)
		{
			++messagesIgnored;
			return;
		}
		PeerInfo& peer = peers[header.sender];

		// A heartbeat from someone we have not admitted is either a stale peer from
		// a previous session or a mid-handshake race. Either way it is not a reason
		// to create a peer entry, since only the handshake may do that.
		if (!peer.active)
		{
			++messagesIgnored;
			return;
		}

		peer.lastHeardFrame = frame;
		peer.step = beat.step;

		// Rechecked every beat rather than only at the handshake, because a setting
		// can be changed after joining and a divergence then starts silently.
		if (peer.sharedSettingsHash != beat.sharedSettingsHash)
		{
			peer.sharedSettingsHash = beat.sharedSettingsHash;
			if (beat.sharedSettingsHash != sharedSettingsHash)
				Log("session: WARNING peer %u's shared settings are %08X and ours are %08X. "
				    "A setting that has to match was changed after joining.",
				    header.sender, beat.sharedSettingsHash, sharedSettingsHash);
		}
	}

	void Session::HandleGoodbye(const MessageHeader& header)
	{
		if (header.sender >= MaxPlayers)
		{
			++messagesIgnored;
			return;
		}
		if (!peers[header.sender].active)
			return;

		Log("session: peer %u ('%s') left", header.sender, peers[header.sender].name);
		memset(&peers[header.sender], 0, sizeof(peers[header.sender]));
		transportSlotForPeer[header.sender] = InvalidPeer;

		// A client whose host left has no session any more. A host whose client left
		// goes back to waiting.
		if (role == RoleClient && header.sender == HostPeer)
		{
			state = SessionJoining;
			localPeer = PeerUnassigned;
		}
		else if (role == RoleHost && PlayerCount() <= 1)
		{
			state = SessionListening;
		}
	}

	// ---------------------------------------------------------------------------
	// Sending
	// ---------------------------------------------------------------------------

	bool Session::SendTo(int transportPeer, uint8_t kind, const void* payload,
	    int payloadLength, SendMode mode)
	{
		if (!transport || transportPeer == InvalidPeer)
			return false;
		if (payloadLength < 0 || payloadLength > MaxPacketBytes - (int)sizeof(MessageHeader))
			return false;

		unsigned char buffer[MaxPacketBytes];
		MessageHeader header;
		header.magic = ProtocolMagic;
		header.version = ProtocolVersion;
		header.kind = kind;
		header.sender = localPeer;
		header.sequence = nextSequence++;
		header.step = localStep;

		memcpy(buffer, &header, sizeof(header));
		if (payload && payloadLength > 0)
			memcpy(buffer + sizeof(header), payload, payloadLength);

		const bool ok = transport->Send(transportPeer, buffer,
		    (int)sizeof(header) + payloadLength, mode);
		if (ok)
			++messagesSent;
		return ok;
	}

	void Session::SendHello()
	{
		HelloPayload hello;
		memset(&hello, 0, sizeof(hello));
		hello.buildId = buildId;
		hello.sharedSettingsHash = sharedSettingsHash;
		strncpy_s(hello.playerName, sizeof(hello.playerName), localName, _TRUNCATE);

		// Before a welcome arrives there is no host peer id to address, so the hello
		// goes to every transport peer the caller set up. In practice a client was
		// given exactly one, the host's address.
		if (transportSlotForPeer[HostPeer] != InvalidPeer)
		{
			SendTo(transportSlotForPeer[HostPeer], MessageHello, &hello, sizeof(hello),
			    SendReliable);
			return;
		}
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (transport->PeerConnected(i))
				SendTo(i, MessageHello, &hello, sizeof(hello), SendReliable);
	}

	void Session::SendHeartbeat()
	{
		HeartbeatPayload beat;
		memset(&beat, 0, sizeof(beat));
		beat.step = localStep;
		beat.sharedSettingsHash = sharedSettingsHash;
		beat.playerCount = (uint8_t)PlayerCount();

		for (int i = 0; i < MaxPlayers; ++i)
		{
			if (i == localPeer)
				continue;
			if (!peers[i].active)
				continue;
			// Unreliable on purpose. A lost beat is replaced half a second later, and
			// the timeout is twenty beats wide, so retrying one buys nothing.
			SendTo(transportSlotForPeer[i], MessageHeartbeat, &beat, sizeof(beat),
			    SendUnreliable);
		}
	}

	void Session::SendReject(int transportPeer, uint8_t reason, uint32_t hostValue)
	{
		RejectPayload reject;
		memset(&reject, 0, sizeof(reject));
		reject.reason = reason;
		reject.hostValue = hostValue;
		SendTo(transportPeer, MessageReject, &reject, sizeof(reject), SendReliable);
	}

	// ---------------------------------------------------------------------------
	// Bookkeeping
	// ---------------------------------------------------------------------------

	void Session::TrackSequence(uint8_t sender, uint32_t sequence)
	{
		if (sender >= MaxPlayers)
			return;
		PeerInfo& peer = peers[sender];

		if (peer.sequenceSeen == 0)
		{
			peer.sequenceSeen = sequence;
			return;
		}
		if (sequence <= peer.sequenceSeen)
			return; // a duplicate or a reorder

		// Any skip means messages went missing between those two. Worth counting
		// rather than ignoring, because the development transport has no reliability
		// layer and silent loss is the worst failure mode lockstep has.
		const uint32_t missing = sequence - peer.sequenceSeen - 1;
		if (missing > 0)
		{
			peer.sequenceGaps += missing;
			Log("session: %lu message%s from peer %u went missing (%lu to %lu)",
			    missing, missing == 1 ? "" : "s", sender, peer.sequenceSeen + 1, sequence - 1);
		}
		peer.sequenceSeen = sequence;
	}

	void Session::DropQuietPeers()
	{
		for (int i = 0; i < MaxPlayers; ++i)
		{
			if (!peers[i].active || peers[i].isLocal)
				continue;
			if (frame - peers[i].lastHeardFrame < PeerTimeoutFrames)
				continue;

			Log("session: peer %d ('%s') has been quiet for %lu frames, dropping it",
			    i, peers[i].name, PeerTimeoutFrames);
			const bool wasHost = (i == HostPeer);
			memset(&peers[i], 0, sizeof(peers[i]));
			transportSlotForPeer[i] = InvalidPeer;

			if (role == RoleClient && wasHost)
			{
				state = SessionJoining;
				localPeer = PeerUnassigned;
			}
			else if (role == RoleHost && PlayerCount() <= 1)
			{
				state = SessionListening;
			}
		}
	}

	int Session::ClaimPeerSlot(int transportPeer)
	{
		// Slot 0 belongs to the host, so a joiner always lands at 1 or above.
		for (int i = 1; i < MaxPlayers; ++i)
		{
			if (peers[i].active)
				continue;
			memset(&peers[i], 0, sizeof(peers[i]));
			peers[i].active = true;
			peers[i].isLocal = false;
			peers[i].lastHeardFrame = frame;
			transportSlotForPeer[i] = transportPeer;
			return i;
		}
		return InvalidPeer;
	}

	int Session::PlayerCount() const
	{
		int count = 0;
		for (int i = 0; i < MaxPlayers; ++i)
			if (peers[i].active)
				++count;
		return count;
	}

	const PeerInfo* Session::Peer(int index) const
	{
		if (index < 0 || index >= MaxPlayers)
			return NULL;
		return &peers[index];
	}

	uint32_t Session::TotalSequenceGaps() const
	{
		uint32_t total = 0;
		for (int i = 0; i < MaxPlayers; ++i)
			total += peers[i].sequenceGaps;
		return total;
	}

	const char* Session::StateText() const
	{
		switch (state)
		{
		case SessionIdle:
			return "idle";
		case SessionListening:
			return "hosting, waiting";
		case SessionJoining:
			return "joining";
		case SessionActive:
			return "connected";
		case SessionRejected:
			return "refused";
		case SessionFailed:
			return "failed to start";
		default:
			return "?";
		}
	}

	const char* Session::Summary(char* out, int count) const
	{
		if (!out || count <= 0)
			return out;

		if (state == SessionRejected)
		{
			_snprintf_s(out, count, _TRUNCATE, "refused: %s", rejectText);
			return out;
		}

		char peerStep[32] = "";
		for (int i = 0; i < MaxPlayers; ++i)
		{
			if (!peers[i].active || peers[i].isLocal)
				continue;
			const long drift = (long)peers[i].step - (long)localStep;
			_snprintf_s(peerStep, sizeof(peerStep), _TRUNCATE, " drift %+ld", drift);
			break;
		}

		_snprintf_s(out, count, _TRUNCATE, "%s%s, %d/%d players, step %lu%s, gaps %lu",
		    (role == RoleHost) ? "host" : (role == RoleClient ? "client" : "none"),
		    (localPeer == PeerUnassigned) ? " (no id yet)" : "",
		    PlayerCount(), MaxPlayers, localStep, peerStep, TotalSequenceGaps());
		return out;
	}

} // namespace workshop
