#pragma once

#include <stdint.h>

#include "workshop/Protocol.h"
#include "workshop/Session.h"

// Delayed-input lockstep: the layer that decides whether this machine is allowed
// to run the next simulation step yet.
//
// FFX is a fixed step simulation at 29.97 Hz with one seeded RNG and a step
// counter the game keeps itself, so lockstep is the right shape for it. Both
// machines run the same code over the same inputs and arrive at the same state.
// Nothing about the world is sent over the wire, only what the players pressed.
//
// ## How delayed input works, in one paragraph
//
// Input cannot travel to another machine and back in less than a frame, so a
// naive lockstep would stall every single step waiting for the other player.
// Instead, everything a player presses is scheduled to take effect a few steps
// later. At step S the local player's input is stamped for step S + InputDelay
// and sent, which means it arrives well before step S + InputDelay comes up.
// The cost is that your own button press takes InputDelay steps to be felt,
// which at 29.97 Hz is about 33 ms per step of delay. The benefit is that
// neither machine normally waits at all.
//
// ## What this layer owns and what it does not
//
// It owns: the step clock, the input ring, the command queue and the checksum
// comparison. It does NOT own the transport, the handshake or the peer list,
// those are Session. It does not read the game's memory and it does not know
// what a button means. The mod feeds it input and asks it whether to step.
//
// ## Being honest about the one real failure mode
//
// If a peer's input for the step we want to run has not arrived, we must not
// run that step. StepGate returns GateWaiting and the mod has to hold the
// simulation, which on FFX means the stall byte at FFXApplication+0x3AD, the
// shipped pause menu's own mechanism. Holding is safe. Running the step anyway
// is not, because there is no rollback here and no way back once the two
// simulations differ.
//
// ## Thread rules
//
// Game thread only, from inside the per-frame hook, same as Session. No locks,
// no allocation, no clock reads.

namespace workshop
{

	// How many steps ahead input is scheduled. 2 is about 67 ms of added input lag
	// and tolerates a round trip of the same, which covers a decent connection.
	// Raising it trades felt responsiveness for tolerance of a worse link.
	const int DefaultInputDelay = 2;
	const int MinInputDelay = 1;
	const int MaxInputDelay = 10;

	// How many steps of history the ring keeps. At 29.97 Hz, 128 steps is a bit over
	// four seconds, which is far more than any sane input delay and leaves room to
	// hold a checksum long enough to compare it.
	const int InputRingSteps = 128;

	// How many past frames of our own input ride along in each message. One lost
	// packet then costs nothing at all, because the next packet carries the frame
	// again. This is cheaper and much simpler than asking for a retransmit.
	const int InputRedundancy = 6;

	typedef char InputRedundancyFitsMessage[(InputRedundancy <= MaxInputFramesPerMessage) ? 1 : -1];

	// How far ahead of the host's current step a newly issued command takes effect.
	// It has to exceed the one-way trip, or a client receives a command for a step
	// it has already run, which is unrecoverable. 6 steps is about 200 ms.
	const int DefaultCommandLead = 6;

	// How many issued commands are held. They are small and drain quickly, so this
	// only has to cover a burst.
	const int MaxQueuedCommands = 64;

	// How often a state hash is exchanged. Every step would be wasteful and every
	// few seconds would make a divergence hard to locate. 30 steps is about a
	// second, which is close enough to the cause to be useful.
	const int DefaultChecksumInterval = 30;

	// What StepGate says about running the next step.
	enum StepGateResult
	{
		GateOpen,     // everyone's input for this step is in, go
		GateWaiting,  // at least one peer has not sent it yet, hold the simulation
		GateSolo,     // not in a session, so nothing to wait for
		GateNotReady, // in a session but not active yet, hold
	};

	// A command as the mod sees it, once the host has fixed its place in time.
	struct Command
	{
		uint32_t step;  // the step it takes effect on
		uint32_t id;    // host assigned, so a duplicate can be spotted
		uint8_t issuer; // whose action it was
		uint8_t kind;   // mod defined
		uint8_t length;
		uint8_t data[MaxCommandBytes];
	};

	// A neutral input frame, which is what a peer is assumed to have pressed before
	// the session starts and what the ring is primed with.
	InputFrame NeutralInput();

	// The one place a stick float becomes a wire byte and back. Both machines must
	// simulate from the SAME number, so even the local player's own input goes
	// through this round trip before it is used. Getting this wrong is the classic
	// way to build a lockstep that drifts slowly and inexplicably.
	int8_t QuantiseStick(float value);
	float DequantiseStick(int8_t value);

	class Lockstep : public MessageSink
	{
	public:
		Lockstep();

		// Attaches to a session and starts the clock at startStep, which should be
		// the game's own step counter so the two never have to be translated.
		// Registers itself as the session's message sink.
		void Start(Session* session, uint32_t startStep);

		// Detaches from the session and forgets everything. Safe when not started.
		void Stop();

		bool Running() const
		{
			return session != NULL;
		}

		void SetInputDelay(int steps);
		int InputDelay() const
		{
			return inputDelay;
		}

		void SetCommandLead(int steps);
		void SetChecksumInterval(int steps);

		// ------------------------------------------------------------------
		// The per-frame cycle. Call these in this order, once per frame.
		// ------------------------------------------------------------------

		// 1. What the local player is pressing right now. Stamped for
		//    CurrentStep() + InputDelay and sent, with redundancy.
		void SubmitLocalInput(const InputFrame& frame);

		// 2. May the simulation advance? See StepGateResult. Does not change
		//    anything, so it can be called more than once in a frame.
		StepGateResult StepGate();

		// 3. The input every peer is to be simulated with for the current step. A
		//    peer with nothing recorded reads as neutral, which only happens when
		//    the gate was ignored or before the session is active.
		//
		//    The local player's input comes back from here too, not from the pad, so
		//    both machines are stepping from identical numbers.
		const InputFrame* InputForStep(uint8_t peer) const;

		// 4. The commands that take effect on the current step, in a stable order
		//    (by id, which the host assigns). Drained by AdvanceStep.
		int CommandsForStep(const Command** out, int maxCommands) const;

		// 5. Called after the game has actually run the step. Moves the clock on and
		//    retires anything that belonged to the step just run.
		void AdvanceStep();

		uint32_t CurrentStep() const
		{
			return currentStep;
		}

		// ------------------------------------------------------------------
		// Commands
		// ------------------------------------------------------------------

		// Asks for a command to happen. On the host this issues it immediately,
		// stamped CommandLead steps ahead and broadcast. On a client it is sent to
		// the host, which stamps and broadcasts it, so the local machine sees its own
		// command come back. Either way nobody acts on it until its step comes up,
		// which is what makes two machines agree on when it happened.
		//
		// Returns false if the session cannot carry it, in which case the caller
		// should not pretend it happened locally.
		bool RequestCommand(uint8_t kind, const void* data, int length);

		// ------------------------------------------------------------------
		// Desync detection
		// ------------------------------------------------------------------

		// Offers a state hash for the current step. Sent only on the checksum
		// interval, so calling it every frame is fine and cheap. parts is optional
		// per-region detail, so a mismatch can say which region moved.
		void SubmitChecksum(uint32_t combined, const uint32_t* parts, int partCount);

		bool DesyncDetected() const
		{
			return desyncStep != 0;
		}
		uint32_t DesyncStep() const
		{
			return desyncStep;
		}
		const char* DesyncText() const
		{
			return desyncText;
		}
		void ClearDesync();

		// ------------------------------------------------------------------
		// Diagnostics. A lockstep you cannot see inside is a lockstep you cannot
		// debug, and these are the numbers that matter when it misbehaves.
		// ------------------------------------------------------------------

		uint32_t StepsRun() const
		{
			return stepsRun;
		}
		uint32_t StepsStalled() const
		{
			return stepsStalled;
		}
		uint32_t LongestStall() const
		{
			return longestStall;
		}
		uint32_t CurrentStall() const
		{
			return currentStall;
		}
		uint32_t InputMessagesSent() const
		{
			return inputSent;
		}
		uint32_t InputFramesReceived() const
		{
			return framesReceived;
		}
		uint32_t InputFramesFilled() const
		{
			return framesFilled;
		}
		uint32_t CommandsIssued() const
		{
			return commandsIssued;
		}
		uint32_t LateCommands() const
		{
			return lateCommands;
		}
		uint32_t ChecksumsCompared() const
		{
			return checksumsCompared;
		}

		// How far ahead of us a peer's reported input is, in steps. Negative means
		// they are behind. This is the number that tells you which side is the
		// bottleneck.
		int PeerInputLead(uint8_t peer) const;

		const char* Summary(char* out, int count) const;

		// MessageSink
		virtual bool OnSessionMessage(const MessageHeader& header,
		    const unsigned char* payload,
		    int payloadLength);

	private:
		struct RingSlot
		{
			uint32_t step; // which step this slot holds
			uint8_t have;  // bit per peer
			InputFrame frames[MaxPlayers];
		};

		RingSlot* SlotFor(uint32_t step);
		const RingSlot* SlotFor(uint32_t step) const;
		void RecordInput(uint8_t peer, uint32_t step, const InputFrame& frame);
		bool HaveAllInputFor(uint32_t step) const;
		uint8_t ExpectedPeerMask() const;

		void SendLocalInput();
		void IssueCommand(uint8_t issuer, uint8_t kind, const void* data, int length);
		void AcceptCommand(const CommandPayload& command);
		void HandleChecksum(uint8_t sender, const ChecksumPayload& theirs);

		Session* session;

		int inputDelay;
		int commandLead;
		int checksumInterval;

		uint32_t currentStep;
		uint32_t localInputThrough; // the highest step we have stamped our input for

		RingSlot ring[InputRingSteps];

		Command commands[MaxQueuedCommands];
		int commandCount;
		uint32_t nextCommandId;

		// Our own last checksum, kept so a peer's can be compared against it
		// whichever order they arrive in.
		uint32_t lastChecksumStep;
		uint32_t lastChecksumCombined;
		uint32_t lastChecksumParts[16];
		uint32_t lastChecksumPartCount;

		uint32_t desyncStep;
		char desyncText[160];

		uint32_t peerInputThrough[MaxPlayers];

		uint32_t stepsRun;
		uint32_t stepsStalled;
		uint32_t longestStall;
		uint32_t currentStall;
		uint32_t inputSent;
		uint32_t framesReceived;
		uint32_t framesFilled;
		uint32_t commandsIssued;
		uint32_t lateCommands;
		uint32_t checksumsCompared;
	};

} // namespace workshop
