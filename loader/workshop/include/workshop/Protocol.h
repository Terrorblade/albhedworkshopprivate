#pragma once

#include <stdint.h>

// The wire format. One place, so both ends of a link agree by construction and
// a version mismatch is caught by the handshake instead of by a crash.
//
// Every message starts with MessageHeader and is followed by the payload for its
// kind. Messages are kept inside one datagram, see MaxPacketBytes in Transport.h.
//
// Two decisions worth knowing about before reading further.
//
// **Everything is little endian with explicit widths and no padding.** Both ends
// are the same 32-bit binary today, so this is belt and braces. It costs nothing
// and it means a future 64-bit build, or a build from a different compiler, does
// not silently reinterpret a struct.
//
// **Sequence numbers live in the header, on every message.** The development
// transport is plain UDP with no reliability layer at all, so a message marked
// reliable is not. Rather than pretend otherwise, the receiver watches for gaps
// in the per-sender sequence and says so out loud. A link that is quietly losing
// messages is the single worst failure mode for lockstep, because the symptom
// shows up minutes later as an unexplained divergence.

namespace workshop
{

	// 'A','B','W','P' for Al Bhed Workshop Protocol. Anything arriving without it is
	// not ours, which matters because a UDP port picks up whatever is sent to it.
	const uint32_t ProtocolMagic = 0x50574241u;

	// Bump on ANY change to a struct in this file or to the meaning of a field. The
	// handshake refuses a peer whose version differs, which is the behaviour you
	// want: a clear refusal beats two builds that almost agree.
	const uint16_t ProtocolVersion = 2;

	enum MessageKind
	{
		MessageHello = 1,     // joiner to host, asking in
		MessageWelcome = 2,   // host to joiner, you are peer N
		MessageReject = 3,    // host to joiner, and why
		MessageHeartbeat = 4, // both ways, keeps the link alive and carries the step
		MessageGoodbye = 5,   // both ways, a clean exit so the other end does not
		                      // sit through a timeout

		// The lockstep layer. See Lockstep.h for what each one is for and why input
		// is sent unreliably while a command is sent reliably.
		MessageInput = 6,      // both ways, a run of consecutive input frames
		MessageCommand = 7,    // host to all, an event that takes effect on a step
		MessageCommandAsk = 8, // client to host, please issue this command
		MessageChecksum = 9,   // both ways, a state hash for one step
	};

	// The sender's peer id before the host has given it one.
	const uint8_t PeerUnassigned = 0xFF;

	// Not a real peer. Passed to Session::Send to mean everyone but us.
	const uint8_t BroadcastPeer = 0xFE;

	// The host is always peer 0. Everything above the session addresses it by this
	// rather than by searching for whoever has the host role.
	const uint8_t HostPeer = 0;

#pragma pack(push, 1)

	struct MessageHeader
	{
		uint32_t magic;    // ProtocolMagic
		uint16_t version;  // ProtocolVersion
		uint8_t kind;      // one of MessageKind
		uint8_t sender;    // the sender's peer id, or PeerUnassigned
		uint32_t sequence; // per sender, from 1, never reused. Gaps mean loss.
		uint32_t step;     // the simulation step this belongs to, 0 when not stepped
	};

	struct HelloPayload
	{
		uint32_t buildId;            // so two different mod builds cannot pair up
		uint32_t sharedSettingsHash; // workshop::SharedSettingsHash
		char playerName[24];         // NUL terminated, cosmetic
	};

	struct WelcomePayload
	{
		uint8_t assignedPeer;        // the joiner's peer id from now on
		uint8_t playerCount;         // how many are in, including the host
		uint8_t maxPlayers;          // the session's cap
		uint8_t reserved;            // keeps the struct aligned and honest
		uint32_t hostStep;           // where the host's simulation is
		uint32_t sharedSettingsHash; // the host's, so the joiner can check too
	};

	// Why a joiner was turned away. Worth being specific: "connection failed" sends
	// people hunting their firewall when the real answer is that one of them has a
	// different build.
	enum RejectReason
	{
		RejectVersionMismatch = 1, // different ProtocolVersion
		RejectBuildMismatch = 2,   // same protocol, different mod build
		RejectSessionFull = 3,
		RejectSettingsMismatch = 4, // a shared, game-changing setting disagrees
		RejectNotHosting = 5,       // talking to someone who is not a host
	};

	struct RejectPayload
	{
		uint8_t reason; // one of RejectReason
		uint8_t reserved[3];
		uint32_t hostValue; // the host's version, build or hash, so the joiner can
		                    // say what it should have been instead of guessing
	};

	struct HeartbeatPayload
	{
		uint32_t step;               // the sender's simulation step
		uint32_t sharedSettingsHash; // rechecked every beat, because a setting can
		                             // be changed after the handshake
		uint8_t playerCount;
		uint8_t reserved[3];
	};

	struct GoodbyePayload
	{
		uint8_t reason; // 0 for a normal quit, room for more later
		uint8_t reserved[3];
	};

	// ---------------------------------------------------------------------------
	// The lockstep payloads.
	//
	// These three are the ones sent every frame or near enough, so they are sized
	// with that in mind. An input message with the default redundancy is 64 bytes on
	// the wire including the header, which at 29.97 Hz is under 2 KB a second per
	// peer. That is nothing, and it buys loss recovery with no retransmit round trip.
	// ---------------------------------------------------------------------------

	// One player's input for one simulation step.
	//
	// The sticks are quantised to a signed byte ON PURPOSE, and the sender has to
	// use the quantised value for its own simulation too. If one machine steps with
	// the full float and the other with the byte that came off the wire, the two
	// simulations are being fed different numbers and they will drift apart. See
	// QuantiseStick in Lockstep.h, which is the only place that conversion lives.
	// IMPORTANT, and it is not what the field names suggest. leftX and leftY are a
	// WORLD SPACE direction, not the raw left stick.
	//
	// The engine's own control path computes m_moveDir = cameraYaw - atan2(right, forward),
	// so a raw stick value only means something next to the camera that read it. The camera
	// is per-player, driven by that player's right stick, and deliberately not replicated.
	// Put a raw stick on the wire and two machines turn it into two different headings, which
	// desyncs on the first step anybody walks.
	//
	// So the owning machine resolves its stick against its own camera first and sends the
	// result as a unit-ish vector in the XZ plane, scaled by stick deflection. The receiver
	// takes atan2(leftY, leftX) and needs no camera at all. Same eight bytes, and the camera
	// is out of the determinism surface entirely.
	//
	// rightX and rightY stay raw. They drive the camera, which is presentation and stays
	// local, so nothing simulated reads them.
	struct InputFrame
	{
		uint32_t buttons; // the FFX global button mask, as read from g_ffxInput
		int8_t leftX;     // -127..127, right and down positive
		int8_t leftY;
		int8_t rightX;
		int8_t rightY;
	};

	// The most frames one input message can carry. Raising it costs bytes on every
	// packet and buys tolerance of a longer loss burst.
	const int MaxInputFramesPerMessage = 8;

	struct InputPayload
	{
		uint32_t firstStep; // the step frames[0] belongs to
		uint8_t frameCount; // 1..MaxInputFramesPerMessage, consecutive from firstStep
		uint8_t reserved[3];
		InputFrame frames[MaxInputFramesPerMessage];
	};

	// A command is anything that is not per-step input: a chest opened, a menu
	// opened, a battle action chosen. The kind and the bytes are the mod's business,
	// the step is not. Commands are host-ordered, so a client ASKS and the host
	// ISSUES, which is why there are two message kinds for one struct.
	const int MaxCommandBytes = 64;

	struct CommandPayload
	{
		uint32_t effectiveStep; // the step this takes effect on. Host assigned.
		uint8_t issuer;         // the peer whose action this was
		uint8_t kind;           // mod defined
		uint8_t length;         // bytes of data actually used, 0..MaxCommandBytes
		uint8_t reserved;
		uint32_t id; // host assigned, from 1, so a duplicate is ignorable
		uint8_t data[MaxCommandBytes];
	};

	// A state hash for one step, so a divergence is caught near where it started
	// rather than minutes later when something visible goes wrong.
	struct ChecksumPayload
	{
		uint32_t step;
		uint32_t combined;  // the whole-state hash
		uint32_t partCount; // how many of parts[] are meaningful
		uint32_t parts[16]; // per region, so a mismatch says WHERE
	};

#pragma pack(pop)

	// Sanity checks that cost nothing at runtime. If one of these ever fires, the
	// struct packing is not what this file claims and the wire format is broken.
	// Phrased as array sizes rather than static_assert because the project targets a
	// compiler era where static_assert is not dependable.
	typedef char ProtocolHeaderSizeCheck[(sizeof(MessageHeader) == 16) ? 1 : -1];
	typedef char ProtocolHelloSizeCheck[(sizeof(HelloPayload) == 32) ? 1 : -1];
	typedef char ProtocolWelcomeSizeCheck[(sizeof(WelcomePayload) == 12) ? 1 : -1];
	typedef char ProtocolInputFrameSizeCheck[(sizeof(InputFrame) == 8) ? 1 : -1];
	typedef char ProtocolInputSizeCheck[(sizeof(InputPayload) == 72) ? 1 : -1];
	typedef char ProtocolCommandSizeCheck[(sizeof(CommandPayload) == 76) ? 1 : -1];
	typedef char ProtocolChecksumSizeCheck[(sizeof(ChecksumPayload) == 76) ? 1 : -1];

	const char* MessageKindName(uint8_t kind);
	const char* RejectReasonText(uint8_t reason);

} // namespace workshop
