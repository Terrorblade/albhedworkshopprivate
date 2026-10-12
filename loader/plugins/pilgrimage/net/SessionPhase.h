#pragma once

// HOW FAR ALONG A SESSION IS, which is not the same question as whether the
// transport is connected.
//
// ## Why this exists
//
// The transport's SessionState answers "can these two machines exchange packets".
// Everything above it needs a different answer: "are these two machines simulating
// the same world yet". Those came apart the moment people were allowed to connect
// from the TITLE SCREEN.
//
// Before this, a session turning active was treated as the signal to start
// everything. Connect at the main menu and the consequences were silent and all
// wrong: the host captured a save block that was still all zeroes and sent it, the
// client transplanted those zeroes over its own, and the lockstep clock started
// counting steps of a world that did not exist. Nothing logged a complaint, because
// nothing was checking.
//
// ## The phases
//
// Connected is not playing. The host picks New Game or Load Game in its own time,
// and until it has, there is nothing to sync and nothing to hold in step.
//
//   Offline   no session.
//   Lobby     connected, and nobody has a game yet. The host is deciding. A client
//             is waiting to be told. NOTHING simulation-facing runs here.
//   Loading   the host has a game and the world is on its way, or it has arrived and
//             the map is still coming up. The one transient phase.
//   Playing   both ends have the same world and it is playable. This is the only
//             phase in which the clock runs and bodies are spawned.
//
// ## Derived, not stored
//
// There is deliberately no state machine here. Every phase is computed from things
// that are already true or already replicated, so there is no transition to miss, no
// message to lose, and no way for the two machines to hold different ideas of the
// phase for reasons of their own. A state machine would be a fifth thing to keep in
// sync, and keeping things in sync is the entire difficulty of this mod.
//
// THE PHASES ARE LOCAL ANSWERS AND NEED NOT MATCH. The host reaches Playing before
// the client does, necessarily, because the host is the one that had the world first.
// That is correct. What matters is that each machine refuses to do simulation-facing
// work until its OWN answer is Playing.

namespace pilgrimage
{

	enum SessionPhase
	{
		PhaseOffline = 0,
		PhaseLobby,
		PhaseLoading,
		PhasePlaying,
	};

	// This machine's phase, computed fresh. Cheap: a handful of global reads and one
	// save block test, no syscalls.
	SessionPhase CurrentSessionPhase();

	// The common question, spelled so call sites read as intent rather than as a
	// comparison. True only in PhasePlaying.
	bool SessionIsPlaying();

	// "offline", "lobby", "loading", "playing". Never NULL.
	const char* SessionPhaseName(SessionPhase phase);

	// One sentence on why the phase is what it is, for the status line and the log.
	// Never NULL. This is the line that turns "my friend joined and nothing happened"
	// into an answer, so it names the thing being waited for rather than the state.
	const char* SessionPhaseReason();

	// Call once per frame while a session is active. Logs a phase change, and only a
	// change, so the log carries the handful of lines that matter instead of one per
	// frame.
	void ServiceSessionPhase();

	// Forget the last logged phase, so the next change logs. Call when a session
	// starts or stops.
	void ResetSessionPhase();

} // namespace pilgrimage
