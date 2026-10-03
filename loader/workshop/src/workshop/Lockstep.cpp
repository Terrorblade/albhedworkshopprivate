#include "workshop/Lockstep.h"

#include <math.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "workshop/Log.h"

namespace workshop
{

	namespace
	{

		// A step number of 0 is used as "this slot has never been written", so the ring
		// treats it as empty. The game's own step counter starts at 0 too, which means
		// the very first step of a session would look empty. Start the clock at 1 or
		// later and the problem goes away, which Start does by bumping a zero.
		const uint32_t NoStep = 0;

		int ClampInt(int value, int low, int high)
		{
			if (value < low)
				return low;
			if (value > high)
				return high;
			return value;
		}

	} // namespace

	InputFrame NeutralInput()
	{
		InputFrame frame;
		frame.buttons = 0;
		frame.moveAngle = 0;
		frame.moveMag = 0; // idle, which is what matters. The angle is then ignored
		frame.reserved = 0;

		return frame;
	}

	uint16_t QuantiseAngle(float radians)
	{
		// Wrap into one turn first. fmodf keeps the sign of its argument, so a negative
		// angle needs the extra turn added rather than being clamped: a caller handing in
		// -pi/2 means three quarters of a turn, not zero.
		const float turn = 6.28318530717959f;
		float wrapped = fmodf(radians, turn);
		if (wrapped < 0.0f)
			wrapped += turn;

		// Round rather than truncate, so the error is half a step either side instead of a
		// whole step in one direction. A systematic one-sided bias on a heading would show
		// up as a character drifting consistently off the direction that was pushed.
		const float scaled = wrapped * (65536.0f / turn);
		const long rounded = (long)(scaled + 0.5f);

		// The round can land exactly on 65536, which wraps to 0 rather than overflowing.
		return (uint16_t)(rounded & 0xFFFF);
	}

	float DequantiseAngle(uint16_t value)
	{
		return (float)value * (6.28318530717959f / 65536.0f);
	}

	uint8_t QuantiseMagnitude(float value)
	{
		if (value <= 0.0f)
			return 0;
		if (value >= 1.0f)
			return 255;

		return (uint8_t)(value * 255.0f + 0.5f);
	}

	float DequantiseMagnitude(uint8_t value)
	{
		return (float)value * (1.0f / 255.0f);
	}

	int8_t QuantiseStick(float value)
	{
		// The engine's sticks arrive as floats in roughly -1..1 with a 0.2 deadzone
		// already applied, so clamping at the ends loses nothing real.
		float scaled = value * 127.0f;
		if (scaled > 127.0f)
			scaled = 127.0f;
		if (scaled < -127.0f)
			scaled = -127.0f;

		// Round away from zero so a small push does not quantise to nothing, which
		// would make the stick feel dead near the edge of the deadzone.
		return (int8_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
	}

	float DequantiseStick(int8_t value)
	{
		return (float)value / 127.0f;
	}

	Lockstep::Lockstep()
	    : session(NULL),
	      inputDelay(DefaultInputDelay),
	      commandLead(DefaultCommandLead),
	      checksumInterval(DefaultChecksumInterval),
	      currentStep(0),
	      localInputThrough(0),
	      commandCount(0),
	      nextCommandId(1),
	      lastChecksumStep(0),
	      lastChecksumCombined(0),
	      lastChecksumPartCount(0),
	      desyncStep(0),
	      stepsRun(0),
	      // OFF by default, deliberately. Changing the delay changes how your own
	      // character feels, and that is not something to do to somebody without being
	      // asked. The mechanism is here because it is the right answer for a bad link,
	      // and SetAdaptiveInputDelay turns it on.
	      adaptiveDelay(false),
	      delayFloor(MinInputDelay),
	      quietSteps(0),
	      stallRuns(0),
	      delayRaises(0),
	      delayDrops(0),
	      stepsStalled(0),
	      longestStall(0),
	      currentStall(0),
	      inputSent(0),
	      framesReceived(0),
	      framesFilled(0),
	      commandsIssued(0),
	      lateCommands(0),
	      checksumsCompared(0)
	{
		memset(ring, 0, sizeof(ring));
		memset(commands, 0, sizeof(commands));
		memset(lastChecksumParts, 0, sizeof(lastChecksumParts));
		memset(peerInputThrough, 0, sizeof(peerInputThrough));
		desyncText[0] = 0;
	}

	void Lockstep::Start(Session* attachTo, uint32_t startStep)
	{
		Stop();
		if (!attachTo)
			return;

		session = attachTo;
		currentStep = (startStep == NoStep) ? 1 : startStep;

		memset(ring, 0, sizeof(ring));
		commandCount = 0;
		nextCommandId = 1;
		desyncStep = 0;
		desyncText[0] = 0;
		memset(peerInputThrough, 0, sizeof(peerInputThrough));

		// Prime the first InputDelay steps with neutral input for everybody,
		// including peers who have not said anything yet. Without this the gate
		// would never open and the game would never take its first step, because
		// input for step S is only sent at step S - InputDelay and that step never
		// happened.
		const InputFrame neutral = NeutralInput();
		for (int i = 0; i < inputDelay; ++i)
		{
			const uint32_t step = currentStep + (uint32_t)i;
			for (int peer = 0; peer < MaxPlayers; ++peer)
				RecordInput((uint8_t)peer, step, neutral);
		}
		localInputThrough = currentStep + (uint32_t)inputDelay - 1;

		session->AddMessageSink(this);

		Log("lockstep: started at step %lu, input delay %d steps, command lead %d steps",
		    (unsigned long)currentStep, inputDelay, commandLead);
	}

	void Lockstep::Stop()
	{
		if (session)
		{
			session->RemoveMessageSink(this);
			Log("lockstep: stopped at step %lu after %lu steps, %lu stalls, longest %lu",
			    (unsigned long)currentStep, (unsigned long)stepsRun,
			    (unsigned long)stepsStalled, (unsigned long)longestStall);
		}
		session = NULL;
		currentStep = 0;
		localInputThrough = 0;
		commandCount = 0;
		currentStall = 0;
	}

	void Lockstep::SetInputDelay(int steps)
	{
		const int wanted = ClampInt(steps, MinInputDelay, MaxInputDelay);
		if (wanted == inputDelay)
			return;

		// THIS USED TO REFUSE MID-SESSION, on the grounds that the two machines would have
		// to agree on which step the change happened. That reasoning was wrong, and the
		// correction is worth keeping because it is what makes adaptive delay possible.
		//
		// The delay only decides how far AHEAD a peer stamps its own input. It is not an
		// input to the simulation. StepGate asks one question, "is every peer's input for
		// currentStep in the ring", and InputForStep reads that same step. Neither looks at
		// the delay. So two peers stamping 1 and 3 steps ahead still read identical values
		// out of the ring for every step, and the simulations stay in lockstep.
		//
		// What the delay actually buys is slack: a peer that stamps further ahead gives
		// everybody more time for its packets to arrive before anyone needs them. That is a
		// property of one peer's link, which is exactly why each peer should be free to
		// pick its own and change it when its link changes.
		//
		// The one genuine problem was the hole left in the ring on a raise, and
		// SubmitLocalInput fills that now.
		const int was = inputDelay;
		inputDelay = wanted;

		if (session)
			Log("lockstep: input delay %d -> %d steps, about %d ms at 29.97 Hz. Only our own "
			    "stamping changed, the other end needs no telling.",
			    was, wanted, (int)(wanted * 1000 / 30));
	}

	void Lockstep::SetAdaptiveInputDelay(bool wanted)
	{
		if (adaptiveDelay == wanted)
			return;

		adaptiveDelay = wanted;
		quietSteps = 0;
		stallRuns = 0;

		Log("lockstep: adaptive input delay %s, currently %d steps",
		    wanted ? "ON, the clock will hunt for the smallest delay that does not stall"
		           : "off, the delay stays where it is",
		    inputDelay);
	}

	void Lockstep::SetInputDelayFloor(int steps)
	{
		delayFloor = ClampInt(steps, MinInputDelay, MaxInputDelay);
		if (inputDelay < delayFloor)
			SetInputDelay(delayFloor);
	}

	// Called once per successful step, with whether the step we just ran had been waited
	// for. Kept separate from AdvanceStep so the rule is readable on its own.
	void Lockstep::AdaptDelay(bool steppedAfterStall)
	{
		if (!adaptiveDelay)
			return;

		if (steppedAfterStall)
		{
			// A stall RUN just ended. Counting runs rather than stalled steps matters: one
			// late packet that costs four steps is one problem, not four, and counting
			// steps would make a single hiccup look like a collapsing link.
			++stallRuns;
			quietSteps = 0;

			// Two runs is the trigger rather than one, because a single stall happens on
			// any link and reacting to it would mean the delay only ever climbs.
			const uint32_t runsBeforeRaise = 2;
			if (stallRuns >= runsBeforeRaise && inputDelay < MaxInputDelay)
			{
				stallRuns = 0;
				++delayRaises;
				SetInputDelay(inputDelay + 1);
			}

			return;
		}

		++quietSteps;

		// About 30 seconds at 29.97 Hz. Long on purpose. Coming back down is a gamble that
		// the link got better, and losing that gamble costs a visible freeze, so it is only
		// worth taking when the evidence is substantial.
		const uint32_t quietBeforeDrop = 900;
		if (quietSteps >= quietBeforeDrop && inputDelay > delayFloor)
		{
			quietSteps = 0;
			stallRuns = 0;
			++delayDrops;
			SetInputDelay(inputDelay - 1);
		}
	}

	void Lockstep::SetCommandLead(int steps)
	{
		commandLead = ClampInt(steps, 1, InputRingSteps / 2);
	}

	void Lockstep::SetChecksumInterval(int steps)
	{
		checksumInterval = ClampInt(steps, 1, 600);
	}

	// ---------------------------------------------------------------------------
	// The ring
	// ---------------------------------------------------------------------------

	Lockstep::RingSlot* Lockstep::SlotFor(uint32_t step)
	{
		if (step == NoStep)
			return NULL;
		RingSlot* slot = &ring[step % InputRingSteps];

		// A slot still carrying an older step is stale, so claim it. Anything newer
		// means the caller is asking about a step that has already been overwritten,
		// which is a bug in the caller rather than something to paper over.
		if (slot->step != step)
		{
			if (slot->step > step)
				return NULL;
			slot->step = step;
			slot->have = 0;
			for (int i = 0; i < MaxPlayers; ++i)
				slot->frames[i] = NeutralInput();
		}
		return slot;
	}

	const Lockstep::RingSlot* Lockstep::SlotFor(uint32_t step) const
	{
		if (step == NoStep)
			return NULL;
		const RingSlot* slot = &ring[step % InputRingSteps];
		return (slot->step == step) ? slot : NULL;
	}

	void Lockstep::RecordInput(uint8_t peer, uint32_t step, const InputFrame& frame)
	{
		if (peer >= (uint8_t)MaxPlayers)
			return;

		// A frame for a step already run is simply late. The redundancy means we see
		// plenty of those and they are not a problem, so count nothing and move on.
		if (step < currentStep)
			return;

		// Beyond the ring's horizon means a peer is running far ahead of us, which
		// the gate makes impossible in a healthy session. Dropping it is right,
		// because the alternative is overwriting a step we still need.
		if (step >= currentStep + (uint32_t)InputRingSteps)
			return;

		RingSlot* slot = SlotFor(step);
		if (!slot)
			return;

		const uint8_t bit = (uint8_t)(1u << peer);
		if (slot->have & bit)
			return; // already had it, the redundant copy agrees

		slot->frames[peer] = frame;
		slot->have = (uint8_t)(slot->have | bit);
		++framesFilled;

		if (step > peerInputThrough[peer])
			peerInputThrough[peer] = step;
	}

	uint8_t Lockstep::ExpectedPeerMask() const
	{
		// Only peers the session says are present. A player who has not joined is
		// not something to wait for, and a player who disconnects must not wedge the
		// gate shut forever.
		uint8_t mask = 0;
		if (!session)
			return mask;
		for (int i = 0; i < MaxPlayers; ++i)
		{
			const PeerInfo* info = session->Peer(i);
			if (info && info->active)
				mask = (uint8_t)(mask | (1u << i));
		}
		return mask;
	}

	bool Lockstep::HaveAllInputFor(uint32_t step) const
	{
		const uint8_t wanted = ExpectedPeerMask();
		if (wanted == 0)
			return true;

		const RingSlot* slot = SlotFor(step);
		if (!slot)
			return false;
		return (slot->have & wanted) == wanted;
	}

	// ---------------------------------------------------------------------------
	// The per-frame cycle
	// ---------------------------------------------------------------------------

	void Lockstep::SubmitLocalInput(const InputFrame& frame)
	{
		if (!session)
			return;

		const uint8_t local = session->LocalPeer();
		if (local == PeerUnassigned)
			return;

		// Our input is for a step in the future, which is the whole trick. Only
		// stamp a step once, so calling this twice in a frame is harmless.
		const uint32_t target = currentStep + (uint32_t)inputDelay;
		if (target <= localInputThrough)
			return;

		// A RANGE, not a single step, and that one detail is what makes the delay safe to
		// change while a session is running.
		//
		// In the steady state this stamps exactly one step, because currentStep goes up by
		// one per AdvanceStep and so does target. The interesting case is a delay that just
		// went up: target jumps by two, and the step in between would otherwise never be
		// stamped at all. That hole is a guaranteed stall a second or so later, with no
		// obvious cause by the time it shows up.
		//
		// Filling it with the current frame is right rather than merely expedient. The hole
		// is one step of a button state that is about to be sent anyway, and duplicating
		// 33 ms of input is not something a player can perceive. Lowering the delay needs no
		// handling at all: target stops advancing for a step, the early return above catches
		// it, and the already-stamped step stands.
		for (uint32_t step = localInputThrough + 1; step <= target; ++step)
			RecordInput(local, step, frame);

		localInputThrough = target;

		SendLocalInput();
	}

	void Lockstep::SendLocalInput()
	{
		if (!session || session->State() != SessionActive)
			return;

		const uint8_t local = session->LocalPeer();
		if (local == PeerUnassigned)
			return;

		// Carry the last few frames as well as the newest one. A single lost packet
		// then costs nothing, because the next one repeats the frame.
		uint32_t first = localInputThrough;
		int count = 1;
		while (count < InputRedundancy && first > currentStep)
		{
			const RingSlot* slot = SlotFor(first - 1);
			if (!slot || !(slot->have & (1u << local)))
				break;
			--first;
			++count;
		}

		InputPayload payload;
		memset(&payload, 0, sizeof(payload));
		payload.firstStep = first;
		payload.frameCount = (uint8_t)count;
		for (int i = 0; i < count; ++i)
		{
			const RingSlot* slot = SlotFor(first + (uint32_t)i);
			payload.frames[i] = slot ? slot->frames[local] : NeutralInput();
		}

		// Unreliable on purpose. A reliable ordered channel would hold every later
		// frame behind a lost one, turning a single dropped packet into a visible
		// stall. The redundancy above covers the loss instead.
		if (session->Broadcast(MessageInput, &payload, (int)sizeof(payload), SendUnreliable))
			++inputSent;
	}

	StepGateResult Lockstep::StepGate()
	{
		if (!session)
			return GateSolo;
		if (session->State() != SessionActive)
			return GateNotReady;

		// Alone in a session we started is not a reason to wait. The mask is just us.
		const uint8_t wanted = ExpectedPeerMask();
		const uint8_t localBit =
		    (session->LocalPeer() == PeerUnassigned)
		        ? 0
		        : (uint8_t)(1u << session->LocalPeer());
		if (wanted == 0 || wanted == localBit)
			return GateSolo;

		if (HaveAllInputFor(currentStep))
			return GateOpen;

		++stepsStalled;
		++currentStall;
		if (currentStall > longestStall)
			longestStall = currentStall;
		return GateWaiting;
	}

	const InputFrame* Lockstep::InputForStep(uint8_t peer) const
	{
		static const InputFrame neutral = { 0, 0, 0, 0 };
		if (peer >= (uint8_t)MaxPlayers)
			return &neutral;

		const RingSlot* slot = SlotFor(currentStep);
		if (!slot)
			return &neutral;
		return &slot->frames[peer];
	}

	bool Lockstep::HasInputForStep(uint8_t peer) const
	{
		if (peer >= (uint8_t)MaxPlayers)
			return false;

		const RingSlot* slot = SlotFor(currentStep);
		if (!slot)
			return false;

		return (slot->have & (uint8_t)(1u << peer)) != 0;
	}

	int Lockstep::CommandsForStep(const Command** out, int maxCommands) const
	{
		if (!out || maxCommands <= 0)
			return 0;

		int found = 0;
		for (int i = 0; i < commandCount && found < maxCommands; ++i)
			if (commands[i].step == currentStep)
				out[found++] = &commands[i];
		return found;
	}

	void Lockstep::AdvanceStep()
	{
		if (!session)
			return;

		// Retire the commands that belonged to the step just run, and anything that
		// somehow got left behind it. Order is preserved for the rest.
		int kept = 0;
		for (int i = 0; i < commandCount; ++i)
		{
			if (commands[i].step > currentStep)
			{
				if (kept != i)
					commands[kept] = commands[i];
				++kept;
			}
		}
		commandCount = kept;

		++currentStep;
		++stepsRun;

		// Feed the adaptation BEFORE the stall counter is cleared, because a non-zero
		// currentStall here is exactly the signal "the step we just ran had been waited
		// for", and that is the only moment it can be observed.
		AdaptDelay(currentStall != 0);

		currentStall = 0;
	}

	// ---------------------------------------------------------------------------
	// Commands
	// ---------------------------------------------------------------------------

	bool Lockstep::RequestCommand(uint8_t kind, const void* data, int length)
	{
		if (!session || session->State() != SessionActive)
			return false;
		if (length < 0 || length > MaxCommandBytes)
			return false;

		const uint8_t local = session->LocalPeer();
		if (local == PeerUnassigned)
			return false;

		if (session->IsHost())
		{
			IssueCommand(local, kind, data, length);
			return true;
		}

		// A client does not get to decide when its own command happens, because two
		// clients deciding at once would give the two machines different orders.
		// Ask the host and wait to see it come back.
		CommandPayload ask;
		memset(&ask, 0, sizeof(ask));
		ask.effectiveStep = 0; // the host fills this in, ours would be a guess
		ask.issuer = local;
		ask.kind = kind;
		ask.length = (uint8_t)length;
		ask.id = 0;
		if (data && length > 0)
			memcpy(ask.data, data, (size_t)length);

		return session->Send(HostPeer, MessageCommandAsk, &ask,
		    (int)sizeof(ask), SendReliable);
	}

	void Lockstep::IssueCommand(uint8_t issuer, uint8_t kind, const void* data, int length)
	{
		if (!session || !session->IsHost())
			return;
		if (length < 0 || length > MaxCommandBytes)
			return;

		CommandPayload issued;
		memset(&issued, 0, sizeof(issued));
		issued.effectiveStep = currentStep + (uint32_t)commandLead;
		issued.issuer = issuer;
		issued.kind = kind;
		issued.length = (uint8_t)length;
		issued.id = nextCommandId++;
		if (data && length > 0)
			memcpy(issued.data, data, (size_t)length);

		// Reliable, because a lost command is a divergence and no amount of
		// redundancy makes an unreliable channel safe for one.
		session->Broadcast(MessageCommand, &issued, (int)sizeof(issued), SendReliable);

		// The host queues its own copy rather than acting now, so the host runs the
		// command on exactly the same step the clients do.
		AcceptCommand(issued);
	}

	void Lockstep::AcceptCommand(const CommandPayload& payload)
	{
		if (payload.length > MaxCommandBytes)
			return;

		// Already have it. The reliable channel should not duplicate, but an id check
		// costs one compare and saves a double-applied chest.
		for (int i = 0; i < commandCount; ++i)
			if (commands[i].id == payload.id && payload.id != 0)
				return;

		if (payload.effectiveStep <= currentStep)
		{
			// The step it was meant for has already been run here. There is no way to
			// apply it correctly now, so count it and be loud, because this is the
			// beginning of a divergence rather than a cosmetic miss.
			++lateCommands;
			Log("lockstep: command kind %u from peer %u arrived for step %lu but we are "
			    "already at %lu. Raise the command lead above %d steps.",
			    (unsigned)payload.kind, (unsigned)payload.issuer,
			    (unsigned long)payload.effectiveStep, (unsigned long)currentStep,
			    commandLead);
			return;
		}

		if (commandCount >= MaxQueuedCommands)
		{
			++lateCommands;
			Log("lockstep: the command queue is full at %d, dropping kind %u from peer %u",
			    MaxQueuedCommands, (unsigned)payload.kind, (unsigned)payload.issuer);
			return;
		}

		Command& slot = commands[commandCount++];
		slot.step = payload.effectiveStep;
		slot.id = payload.id;
		slot.issuer = payload.issuer;
		slot.kind = payload.kind;
		slot.length = payload.length;
		memcpy(slot.data, payload.data, sizeof(slot.data));

		// Keep the queue ordered by id, so every machine hands commands to the mod
		// in the same order even when two land on the same step. The queue is tiny
		// and nearly sorted already, so an insertion pass is the right tool.
		for (int i = commandCount - 1; i > 0; --i)
		{
			if (commands[i - 1].id <= commands[i].id)
				break;
			const Command swap = commands[i - 1];
			commands[i - 1] = commands[i];
			commands[i] = swap;
		}

		++commandsIssued;
	}

	// ---------------------------------------------------------------------------
	// Checksums
	// ---------------------------------------------------------------------------

	void Lockstep::SubmitChecksum(uint32_t combined, const uint32_t* parts, int partCount)
	{
		if (!session || session->State() != SessionActive)
			return;
		if (currentStep % (uint32_t)checksumInterval != 0)
			return;
		if (currentStep == lastChecksumStep)
			return;

		lastChecksumStep = currentStep;
		lastChecksumCombined = combined;

		const int keep = (partCount < 0) ? 0 : ((partCount > 16) ? 16 : partCount);
		lastChecksumPartCount = (uint32_t)keep;
		memset(lastChecksumParts, 0, sizeof(lastChecksumParts));
		if (parts && keep > 0)
			memcpy(lastChecksumParts, parts, sizeof(uint32_t) * (size_t)keep);

		ChecksumPayload payload;
		memset(&payload, 0, sizeof(payload));
		payload.step = currentStep;
		payload.combined = combined;
		payload.partCount = (uint32_t)keep;
		if (parts && keep > 0)
			memcpy(payload.parts, parts, sizeof(uint32_t) * (size_t)keep);

		session->Broadcast(MessageChecksum, &payload, (int)sizeof(payload), SendReliable);
	}

	void Lockstep::HandleChecksum(uint8_t sender, const ChecksumPayload& theirs)
	{
		// Only compare like for like. A checksum for a step we never hashed says
		// nothing, and reporting it would be a false alarm every time the interval
		// slips.
		if (theirs.step != lastChecksumStep)
			return;

		++checksumsCompared;
		if (theirs.combined == lastChecksumCombined)
			return;
		if (desyncStep != 0)
			return; // already reported, do not spam

		desyncStep = theirs.step;

		// Name the first region that differs. That is the whole point of sending the
		// parts, because "the state differs" is not actionable and "the inventory
		// differs" is.
		int differing = -1;
		const uint32_t shared =
		    (theirs.partCount < lastChecksumPartCount) ? theirs.partCount
		                                               : lastChecksumPartCount;
		for (uint32_t i = 0; i < shared && i < 16; ++i)
		{
			if (theirs.parts[i] != lastChecksumParts[i])
			{
				differing = (int)i;
				break;
			}
		}

		if (differing >= 0)
			_snprintf_s(desyncText, sizeof(desyncText), _TRUNCATE,
			    "desync at step %lu against peer %u, first difference in region %d "
			    "(ours %08lX, theirs %08lX)",
			    (unsigned long)theirs.step, (unsigned)sender, differing,
			    (unsigned long)lastChecksumParts[differing],
			    (unsigned long)theirs.parts[differing]);
		else
			_snprintf_s(desyncText, sizeof(desyncText), _TRUNCATE,
			    "desync at step %lu against peer %u, combined ours %08lX theirs "
			    "%08lX, no per-region detail to narrow it down",
			    (unsigned long)theirs.step, (unsigned)sender,
			    (unsigned long)lastChecksumCombined,
			    (unsigned long)theirs.combined);

		Log("lockstep: %s", desyncText);
	}

	void Lockstep::ClearDesync()
	{
		desyncStep = 0;
		desyncText[0] = 0;
	}

	// ---------------------------------------------------------------------------
	// Receiving
	// ---------------------------------------------------------------------------

	bool Lockstep::OnSessionMessage(const MessageHeader& header,
	    const unsigned char* payload, int payloadLength)
	{
		if (!session)
			return false;

		switch (header.kind)
		{
		case MessageInput:
		{
			if (payloadLength < (int)sizeof(InputPayload))
				return true;
			InputPayload input;
			memcpy(&input, payload, sizeof(input));
			if (input.frameCount > MaxInputFramesPerMessage)
				return true;
			if (header.sender >= (uint8_t)MaxPlayers)
				return true;

			for (int i = 0; i < (int)input.frameCount; ++i)
			{
				RecordInput(header.sender, input.firstStep + (uint32_t)i,
				    input.frames[i]);
				++framesReceived;
			}
			return true;
		}

		case MessageCommandAsk:
		{
			// Only the host answers these, and only the host gets to pick the
			// step. A client receiving one is either confused or malicious, and
			// either way the right answer is to ignore it.
			if (!session->IsHost())
				return true;
			if (payloadLength < (int)sizeof(CommandPayload))
				return true;
			CommandPayload ask;
			memcpy(&ask, payload, sizeof(ask));
			if (ask.length > MaxCommandBytes)
				return true;

			// Trust the sender's identity from the header, not from the payload,
			// so a client cannot issue commands as somebody else.
			IssueCommand(header.sender, ask.kind, ask.data, (int)ask.length);
			return true;
		}

		case MessageCommand:
		{
			// A host does not take commands from a client, it issues them. This
			// message only means anything to a client.
			if (session->IsHost())
				return true;
			if (payloadLength < (int)sizeof(CommandPayload))
				return true;
			CommandPayload issued;
			memcpy(&issued, payload, sizeof(issued));
			AcceptCommand(issued);
			return true;
		}

		case MessageChecksum:
		{
			if (payloadLength < (int)sizeof(ChecksumPayload))
				return true;
			ChecksumPayload theirs;
			memcpy(&theirs, payload, sizeof(theirs));
			HandleChecksum(header.sender, theirs);
			return true;
		}

		default:
			return false;
		}
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	int Lockstep::PeerInputLead(uint8_t peer) const
	{
		if (peer >= (uint8_t)MaxPlayers)
			return 0;
		return (int)((long)peerInputThrough[peer] - (long)currentStep);
	}

	const char* Lockstep::Summary(char* out, int count) const
	{
		if (!out || count <= 0)
			return "";

		if (!session)
		{
			_snprintf_s(out, (size_t)count, _TRUNCATE, "lockstep: not running");
			return out;
		}

		if (desyncStep != 0)
		{
			_snprintf_s(out, (size_t)count, _TRUNCATE, "%s", desyncText);
			return out;
		}

		_snprintf_s(out, (size_t)count, _TRUNCATE,
		    "step %lu, delay %d, %lu run, %lu stalls (longest %lu), peer leads "
		    "%+d %+d %+d, %lu cmds, %lu late",
		    (unsigned long)currentStep, inputDelay, (unsigned long)stepsRun,
		    (unsigned long)stepsStalled, (unsigned long)longestStall,
		    PeerInputLead(0), PeerInputLead(1), PeerInputLead(2),
		    (unsigned long)commandsIssued, (unsigned long)lateCommands);
		return out;
	}

} // namespace workshop
