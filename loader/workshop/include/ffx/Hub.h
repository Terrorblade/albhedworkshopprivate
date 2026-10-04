#pragma once

#include "ffx/MainLoop.h"

// The FFX side of the event hub. Owns the engine's hook points so that no plugin
// has to, and raises workshop events from them.
//
//     ffx::StartHub();
//     workshop::Subscribe(workshop::EventFrame, &OnFrame);
//
// WHAT IT RAISES
//   EventFrame        every rendered frame, step = the engine's step counter,
//                     pointer = the FFXApplication the engine passed animate
//   EventStep         every simulation step the gates allowed
//   EventSubStep      every inner sub step, BEFORE the movement driver reads the
//                     latched pad, value = the running total. This is the ATEL
//                     script clock, so it is the one that scales script timers
//   EventMapChanged   value = the new live map id
//   EventBattleBegin  / EventBattleEnd
//   EventPaused       / EventResumed
//   EventMenuOpened   / EventMenuClosed, value = 1 for the Esc menu
//   EventFmvBegin     / EventFmvEnd, from FmvInProgress so it stays true while
//                     the decoder drains
//   EventShutdown     from FFXApplication::exitApplication, raised BEFORE the
//                     engine tears itself down so you can still read it
//
// workshop::Session raises the other four by itself, so you get them without
// owning the session:
//   EventSessionStarted / Stopped, value = the local peer id
//   EventPeerJoined     / EventPeerLeft, value = the peer index
//
// Anything nobody watches, detour it yourself and workshop::Publish it so other
// plugins can see it too.
//
// DO NOT CALL ffx::HookAnimate OR ffx::HookMainStep once the hub is running.
// Both are single slot, so a second caller unhooks the hub. Subscribe to
// EventFrame and add a step gate instead.

namespace ffx
{

	// Idempotent. Returns false only if a hook point refused, which is logged.
	bool StartHub();
	bool HubRunning();

	// A gate may hold the simulation by returning false, which is how lockstep
	// waits for a peer. Every registered gate is asked and any one of them can
	// hold. Returns false if the table is full.
	bool AddStepGate(StepGateFn gate);
	void RemoveStepGate(StepGateFn gate);
	int StepGateCount();

	// Total inner sub steps since the hub started. Not the same as a step count:
	// one step runs SubStepCount() of these, and that varies.
	unsigned long HubSubStepCount();

	const char* HubStatus();
	void LogHub();

} // namespace ffx
