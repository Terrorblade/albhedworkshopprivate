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
	// 6 added the raw stick bytes and the owner's camera yaw to InputFrame, so that
	// both machines can run the engine's own player driver rather than one of them
	// running it and the other approximating it.
	// 7 grew the checksum regions from 16 to 24 and added MessageRandomState. The
	// RNG state sits outside the save block, so the world transfer never carried it.
	const uint16_t ProtocolVersion = 7;

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

		// The world transfer. A joining client needs the host's entire save state before
		// it can simulate anything, and that is 26,816 bytes, so it is chunked.
		MessageWorldRequest = 10,  // client to host, send me the world and why
		MessageWorldSnapshot = 11, // host to client, one chunk of the save block
		MessageWorldApplied = 12,  // client to host, installed it, here is my hash
		MessageWorldAnchor = 13,   // host to client, where everybody is standing

		// The RNG state. Seeded from constants at boot, so two processes that start
		// together agree, but a joiner does not and the save block does not carry it.
		MessageRandomState = 14, // host to client
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
	// The movement is a WORLD SPACE HEADING plus a deflection, not a stick.
	//
	// Why not a stick: the engine's own control path computes
	// m_moveDir = cameraYaw - atan2(right, forward), so a raw stick value only means
	// something next to the camera that read it. The camera is per-player, driven by that
	// player's right stick, and deliberately not replicated. Put a raw stick on the wire and
	// two machines turn it into two different headings, which desyncs on the first step
	// anybody walks. So the owning machine resolves against its own camera before sending,
	// and the receiver needs no camera at all.
	//
	// Why an ANGLE and not the resolved vector, which is what this used to be: a pair of
	// signed bytes loses angular resolution exactly where it hurts. At 10 percent deflection
	// the vector has a radius of about 13 units, so there are roughly 8 distinct headings per
	// quadrant and a slow walk looks steppy. An angle is a flat 0.0055 degrees at every
	// deflection, because the precision is no longer coupled to the magnitude. It also saves
	// the receiver an atan2, and more importantly it means the atan2 happens ONCE, on the
	// machine that owns the stick, rather than being re-derived from lossy bytes.
	//
	// The right stick is not here at all. It used to be, and nothing ever read it: it drives
	// the camera, which is presentation and stays local. Those are the two bytes the angle is
	// paid for with, so the frame is still 8 bytes.
	//
	// THE QUANTISATION RULE, unchanged and still the thing that bites: the sender has to
	// simulate from the quantised value too. If one machine steps with the full float and the
	// other with what came off the wire, the two are being fed different numbers and they
	// drift apart slowly and inexplicably. QuantiseAngle and QuantiseMagnitude in Lockstep.h
	// are the only place those conversions live.
	// WHAT IS AUTHORITATIVE HERE, because there are now two ways to read this struct.
	//
	// analogLX, analogLY, buttons and cameraYaw are the INPUTS to the engine's own
	// player driver, FFX_Player__stepControl, and they are what actually moves a
	// character. Both machines feed them to that same function through
	// ffx::StepPlayerControlFor, which is what makes two machines agree: identical
	// inputs into identical code, including the four direction ramps that do the
	// smoothing.
	//
	// moveAngle and moveMag are DERIVED and are no longer what drives anybody. They
	// are kept because "is this peer pushing the stick, and roughly where" is a cheap
	// question a lot of diagnostics want, and recomputing it from the raw bytes at
	// every call site would be worse. Do not drive movement from them.
	//
	// WHY THE CAMERA YAW IS ON THE WIRE. The driver resolves the stick against the
	// camera, and the camera is local to each machine, so the owner's yaw has to
	// travel with the stick or the two machines send the same character in two
	// different directions. It goes through the engine's fixed-yaw override.
	struct InputFrame
	{
		uint32_t buttons;   // the FFX global button mask, as read from g_ffxInput
		uint16_t moveAngle; // DERIVED world heading, 0..65535 a full turn. 0 is +X, 16384 is +Z
		uint8_t moveMag;    // DERIVED stick deflection, 0..255. Exactly 0 means idle
		uint8_t analogLX;   // raw left stick X, 0x80 centred, exactly as the pad layer has it
		uint8_t analogLY;   // raw left stick Y, 0x80 centred, POSITIVE IS DOWN
		uint8_t reserved;   // keeps cameraYaw aligned and the frame a round 12
		uint16_t cameraYaw; // the OWNER'S camera yaw, quantised like moveAngle
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
	//
	// 96 because a battle command carries the engine's own 72 byte command record
	// verbatim plus the gil cost and the variant, and 64 did not fit it. Raising this
	// costs bytes on every command message, which is fine: commands are rare, unlike
	// input. Raising it IS a wire format change, so ProtocolVersion goes up with it.
	const int MaxCommandBytes = 96;

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
	// How many hash regions a checksum message carries. 13 save block buckets, 2
	// menu, 1 carried objects, 1 minigame, and spares. Raising this is a protocol
	// change, which is why it is one constant rather than a literal in five places.
	const int ChecksumRegionCount = 24;

	struct ChecksumPayload
	{
		uint32_t step;
		uint32_t combined;  // the whole-state hash
		uint32_t partCount; // how many of parts[] are meaningful
		uint32_t parts[ChecksumRegionCount]; // per region, so a mismatch says WHERE
	};

	// Why the stream is carried by value rather than chunked: 272 bytes fits one
	// datagram with room to spare, and a partial RNG state is worse than none.
	const int RandomStreamBytes = 272;

	enum RandomStateReason
	{
		RandomStateJoin = 1,   // a peer is joining and needs the host's state
		RandomStateReseed = 2, // the host saw a reseed and is re-publishing
	};

	struct RandomStatePayload
	{
		uint32_t step;        // the step the host read this on
		uint32_t reason;      // one of RandomStateReason
		uint32_t streamBytes; // 272 today. A different value is a refusal, not a guess
		uint32_t battle;      // the single LCG
		uint32_t effect;      // effect and particle RNG, float bits
		uint8_t stream[RandomStreamBytes];
	};

	// ---------------------------------------------------------------------------
	// The world transfer.
	//
	// Why the chunking is explicit rather than left to the transport: Steam's reliable
	// send does fragment and reassemble above 1200 bytes on its own, but the dev UDP
	// transport does not, and a transfer that only works on one of the two backends is a
	// transfer that cannot be debugged locally. See MaxPacketBytes in Transport.h.
	//
	// Chunks are sent RELIABLY and in order, so the receiver does not have to handle loss.
	// It still tracks which chunks arrived, because "the transfer stalled at chunk 19" is
	// a far more useful thing to log than "the world never arrived".
	// ---------------------------------------------------------------------------

	// 1024 keeps the whole message at 1064 bytes including the header, which leaves room
	// under the 1200 cap without being so small that the save block takes hundreds of
	// packets. 26,816 bytes is 27 chunks.
	const int WorldChunkBytes = 1024;

	// Why a client is asking for the world, which is worth carrying because the host's
	// answer is the same but the log entry is not.
	enum WorldRequestReason
	{
		kWorldRequestJoining = 0, // first time, nothing to simulate against yet
		kWorldRequestDesync = 1,  // the checksums diverged and we want a fresh start
		kWorldRequestManual = 2,  // somebody pressed the key, for testing
	};

	struct WorldRequestPayload
	{
		uint32_t reason; // one of WorldRequestReason
		uint32_t step;   // where the asker's clock is, for the host's log
	};

	struct WorldSnapshotPayload
	{
		// The id ties every chunk of one transfer together. A second attempt gets a new
		// id, so a late chunk from an abandoned transfer cannot be mistaken for part of
		// the current one. That is the whole reason this field exists.
		uint32_t snapshotId;

		uint32_t totalBytes; // the whole block, so the receiver can size and check
		uint32_t offset;     // where this chunk goes in the block
		uint32_t hostStep;   // the step the host snapshotted on

		uint16_t chunkBytes; // how many of data are real. The last chunk is short
		uint16_t chunkIndex;
		uint16_t chunkCount;
		uint16_t reserved;

		uint8_t data[WorldChunkBytes];
	};

	// One character's standing place. charIndex is a save block character index, 0..7,
	// and -1 marks an unused slot so the receiver does not have to trust slotCount
	// alone.
	struct WorldAnchorSlot
	{
		float x;
		float y; // +Y IS DOWN in FFX. This is a real height, not a placeholder zero
		float z;
		float facing;      // m_rotY in radians, zero along +Z
		int32_t charIndex; // -1 for unused
	};

	// Where the host's party is standing, which is the one thing the save block does
	// NOT carry.
	//
	// The block names a doorway, a (map id, entry point) pair, so a joiner that
	// installs it and loads lands at the last door the host walked through. That can
	// be most of a map away. This message closes the gap.
	//
	// It is sent once per transfer, before the chunks, because reliable delivery is in
	// order and the joiner wants it in hand by the time it has a map to place into.
	//
	// EVERY CHARACTER, not just the leader. The followers trail the leader by a few
	// metres and those offsets are part of the world state, so sending one position
	// and fanning out from it would put both machines in different places. Sending all
	// of them makes the match exact.
	struct WorldAnchorPayload
	{
		uint32_t snapshotId; // ties this to the transfer it belongs to
		uint32_t hostStep;   // the step the host read these positions on

		uint16_t mapId;      // which map these coordinates mean. A mismatch is a refusal
		uint16_t entryPoint; // for the log, so a wrong landing is attributable

		int32_t slotCount; // how many of slots are filled, from the front
		int32_t reserved;

		WorldAnchorSlot slots[8]; // 8 because a save block character index is 0..7
	};

	struct WorldAppliedPayload
	{
		uint32_t snapshotId;   // which transfer this is answering
		uint32_t combinedHash; // the client's hash after installing, for an instant check
		uint32_t step;         // the step the client installed on
		uint8_t ok;            // 0 means it refused, and the host should know
		uint8_t reserved[3];
	};

#pragma pack(pop)

	// Sanity checks that cost nothing at runtime. If one of these ever fires, the
	// struct packing is not what this file claims and the wire format is broken.
	// Phrased as array sizes rather than static_assert because the project targets a
	// compiler era where static_assert is not dependable.
	typedef char ProtocolHeaderSizeCheck[(sizeof(MessageHeader) == 16) ? 1 : -1];
	typedef char ProtocolHelloSizeCheck[(sizeof(HelloPayload) == 32) ? 1 : -1];
	typedef char ProtocolWelcomeSizeCheck[(sizeof(WelcomePayload) == 12) ? 1 : -1];
	typedef char ProtocolInputFrameSizeCheck[(sizeof(InputFrame) == 12) ? 1 : -1];
	// 104, up from 72 when InputFrame grew to carry the stick bytes and the camera
	// yaw: 8 bytes of header plus 8 frames of 12. At 29.97 Hz with redundancy that is
	// a few KB a second, which is nothing against what the world transfer does once.
	typedef char ProtocolInputSizeCheck[(sizeof(InputPayload) == 104) ? 1 : -1];
	typedef char ProtocolCommandSizeCheck[(sizeof(CommandPayload) == 108) ? 1 : -1];
	typedef char ProtocolChecksumSizeCheck
	    [(sizeof(ChecksumPayload) == 12 + 4 * ChecksumRegionCount) ? 1 : -1];
	typedef char ProtocolRandomStateSizeCheck
	    [(sizeof(RandomStatePayload) == 20 + RandomStreamBytes) ? 1 : -1];
	typedef char ProtocolRandomStateFitsCheck
	    [(sizeof(RandomStatePayload) + sizeof(MessageHeader) <= 1200) ? 1 : -1];
	typedef char ProtocolWorldRequestSizeCheck[(sizeof(WorldRequestPayload) == 8) ? 1 : -1];
	typedef char ProtocolWorldAppliedSizeCheck[(sizeof(WorldAppliedPayload) == 16) ? 1 : -1];
	typedef char ProtocolWorldAnchorSlotSizeCheck[(sizeof(WorldAnchorSlot) == 20) ? 1 : -1];
	typedef char ProtocolWorldAnchorSizeCheck[(sizeof(WorldAnchorPayload) == 180) ? 1 : -1];
	typedef char ProtocolWorldSnapshotSizeCheck[(sizeof(WorldSnapshotPayload) == 1048) ? 1 : -1];

	// The snapshot message plus its header has to fit one datagram. Checked here rather
	// than trusted, because WorldChunkBytes is the kind of constant somebody raises later
	// without thinking about the cap.
	typedef char ProtocolWorldSnapshotFitsCheck
	    [(sizeof(WorldSnapshotPayload) + sizeof(MessageHeader) <= 1200) ? 1 : -1];

	const char* MessageKindName(uint8_t kind);
	const char* RejectReasonText(uint8_t reason);

} // namespace workshop
