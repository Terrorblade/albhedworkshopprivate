#pragma once

#include <stdint.h>

#include "workshop/Protocol.h"
#include "workshop/Transport.h"

// A session: who is connected, which of them is the host, and whether the link
// is healthy. Everything above this layer deals in peer ids and never touches a
// socket or a SteamID.
//
// Deliberately NOT in here: the step clock, the input exchange and the command
// channel. Those are the lockstep layer and they sit on top. Keeping membership
// separate from simulation means the handshake can be tested on its own, which
// is most of the value of having a session object at all.
//
// ## Time is counted in frames, not milliseconds
//
// Heartbeats and timeouts are measured in calls to Step rather than in wall clock
// time. Two reasons. The game's simulation is already fixed step at 29.97 Hz, so
// a frame is a perfectly good unit. And nothing in the session then reads the
// clock, which keeps the one genuinely non-deterministic input out of a layer
// that lockstep depends on.
//
// The practical consequence: if the game is paused or stalled, the session
// stalls with it and nobody times out. That is the behaviour you want, since a
// peer who is loading a map has not gone away.
//
// ## Thread rules
//
// Everything is called from the game thread, from inside the per-frame hook. No
// locks, no allocation after Start, no background threads. A UI thread may read
// the counters and the state, since a torn read of an int is cosmetic, but it
// must never call Start, Step or Stop.

namespace workshop
{

	// At most 3 players, one per FFX active battle slot. That cap is a design
	// decision rather than a technical limit: battle action ownership is per
	// character and there are three active characters, so a fourth player would have
	// nobody to be. The transport has headroom above this on purpose, so the cap is
	// enforced in one place where it can be read and argued with.
	const int MaxPlayers = 3;

	typedef char SessionCapFitsTransport[(MaxPlayers <= TransportMaxPeers) ? 1 : -1];

	// Anything the session does not handle itself goes here. The lockstep layer
	// registers one of these so input, commands and checksums ride on the same link
	// without a second socket and without the session knowing what a step is.
	//
	// Returning false means "not mine", and the session counts it as ignored exactly
	// as it would with no sink at all. That way an old peer sending a kind this build
	// has never heard of is a counter going up, not a crash.
	class MessageSink
	{
	public:
		virtual ~MessageSink() {}
		virtual bool OnSessionMessage(const MessageHeader& header,
		    const unsigned char* payload,
		    int payloadLength) = 0;
	};

	enum SessionRole
	{
		RoleNone,
		RoleHost,   // assigns peer ids, arbitrates, always peer 0
		RoleClient, // asks to join, accepts the id it is given
	};

	enum SessionState
	{
		SessionIdle,      // nothing started
		SessionListening, // host, up and waiting for joiners
		SessionJoining,   // client, hello sent, no welcome yet
		SessionActive,    // in, with at least the host present
		SessionRejected,  // the host said no, see RejectText
		SessionFailed,    // the transport would not start
	};

	struct PeerInfo
	{
		bool active;
		bool isLocal;
		uint32_t lastHeardFrame;     // Step count when we last had anything from them
		uint32_t step;               // their simulation step, from their last beat
		uint32_t sequenceSeen;       // highest sequence received from them
		uint32_t sequenceGaps;       // how many were missing. Should stay at zero.
		uint32_t sharedSettingsHash; // theirs, as of their last message
		char name[24];
	};

	class Session
	{
	public:
		Session();

		// Takes a transport that is already started. The session does not own it and
		// will not stop it, because the caller may want to keep a transport across
		// two sessions.
		//
		// buildId should be something that changes when the mod changes in a way that
		// breaks compatibility. A compile timestamp works.
		bool Start(Transport* transport, SessionRole role, uint32_t buildId,
		    const char* playerName);

		// Call once per frame. Pumps the transport, drains everything waiting,
		// answers handshakes, sends a heartbeat when one is due, and drops peers that
		// have gone quiet.
		void Step();

		// Sends a goodbye so the other end does not sit through a timeout, then goes
		// back to idle. Safe to call when nothing was started.
		void Stop();

		SessionState State() const
		{
			return state;
		}
		SessionRole Role() const
		{
			return role;
		}
		const char* StateText() const;

		// Our own peer id, or PeerUnassigned until the host has answered. A client
		// must not act on anything owner-specific before this is assigned.
		uint8_t LocalPeer() const
		{
			return localPeer;
		}

		bool IsHost() const
		{
			return role == RoleHost;
		}

		int PlayerCount() const;
		const PeerInfo* Peer(int index) const;

		// Set this before Start, or whenever the value changes. The host compares it
		// against a joiner's and refuses a mismatch, and both ends recheck it on
		// every heartbeat, because a setting can be changed after the handshake.
		// Pass the result of workshop::SharedSettingsHash.
		void SetSharedSettingsHash(uint32_t hash)
		{
			sharedSettingsHash = hash;
		}

		// Our simulation step, carried on every heartbeat so each end knows how far
		// ahead or behind the other is. The lockstep layer owns the real clock, this
		// is only what gets reported.
		void SetLocalStep(uint32_t step)
		{
			localStep = step;
		}

		// Hands unhandled messages to the layers above. Sinks are offered a message in
		// registration order until one of them claims it by returning true, so an
		// unclaimed kind still gets counted as ignored exactly as before.
		//
		// There is more than one sink because there is more than one subsystem that owns
		// message kinds: the lockstep clock owns input, commands and checksums, and world
		// sync owns the snapshot transfer. Making them share one sink would have meant one
		// of them forwarding for the other, which puts a dependency between two things
		// that have no reason to know about each other.
		//
		// The session does not own a sink and will not delete it. Registering the same
		// sink twice is a no-op rather than a duplicate.
		bool AddMessageSink(MessageSink* sink);

		// Removes one sink. Safe to call for a sink that was never added.
		void RemoveMessageSink(MessageSink* sink);

		// Sends one message to a session peer id, or to everyone else when peer is
		// BroadcastPeer. This is how a layer above sends on the session's link
		// instead of reaching for the transport, which would mean duplicating the
		// header, the sequence counter and the peer-to-slot mapping.
		//
		// Returns false if the peer is not connected or the payload will not fit.
		bool Send(uint8_t peer, uint8_t kind, const void* payload, int payloadLength,
		    SendMode mode);
		bool Broadcast(uint8_t kind, const void* payload, int payloadLength, SendMode mode);

		// Whether a given session peer is somebody we can currently send to.
		bool PeerReachable(uint8_t peer) const;

		// Why the host refused us. Empty until State is SessionRejected.
		const char* RejectText() const
		{
			return rejectText;
		}

		// A one-line summary for a control panel or the log.
		const char* Summary(char* out, int count) const;

		uint32_t FramesRun() const
		{
			return frame;
		}
		uint32_t MessagesSent() const
		{
			return messagesSent;
		}
		uint32_t MessagesReceived() const
		{
			return messagesReceived;
		}
		uint32_t MessagesIgnored() const
		{
			return messagesIgnored;
		}
		uint32_t TotalSequenceGaps() const;

	private:
		void HandleMessage(int transportPeer, const unsigned char* data, int length);
		void HandleHello(int transportPeer, const MessageHeader& header,
		    const HelloPayload& hello);
		void HandleWelcome(const WelcomePayload& welcome);
		void HandleReject(const RejectPayload& reject);
		void HandleHeartbeat(const MessageHeader& header, const HeartbeatPayload& beat);
		void HandleGoodbye(const MessageHeader& header);

		bool SendTo(int transportPeer, uint8_t kind, const void* payload, int payloadLength,
		    SendMode mode);
		void SendHello();
		void SendHeartbeat();
		void SendReject(int transportPeer, uint8_t reason, uint32_t hostValue);

		void TrackSequence(uint8_t sender, uint32_t sequence);
		void DropQuietPeers();
		int ClaimPeerSlot(int transportPeer);

		Transport* transport;
		SessionRole role;
		SessionState state;

		uint8_t localPeer;
		uint32_t buildId;
		uint32_t sharedSettingsHash;
		uint32_t localStep;
		char localName[24];

		PeerInfo peers[MaxPlayers];

		// Maps a session peer id to the transport peer index it is reachable on.
		// They are not the same thing: the host is session peer 0 but, from a
		// client's point of view, it is whichever transport slot the client added it
		// to. Conflating those two is an easy and very confusing bug.
		int transportSlotForPeer[MaxPlayers];

		// Four is comfortably more than the subsystems that exist, and a fixed array
		// keeps the session free of allocation, which matters because it is pumped from
		// the frame path.
		static const int MaxMessageSinks = 4;
		MessageSink* messageSinks[MaxMessageSinks];
		int messageSinkCount;

		uint32_t frame;
		uint32_t nextSequence;
		uint32_t lastHeartbeatFrame;

		uint32_t messagesSent;
		uint32_t messagesReceived;
		uint32_t messagesIgnored;

		char rejectText[96];
	};

} // namespace workshop
