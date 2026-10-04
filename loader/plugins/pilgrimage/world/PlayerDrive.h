#pragma once

// Driving every player character through the engine's own movement driver.
//
// THE HOLE THIS CLOSES. Before this existed, the bound player was the one character on
// the map not driven from the replicated input. The engine drove it from the live local
// pad through FFX_Player__stepControl, with four accumulating direction ramps and its own
// speed table, while the OTHER machine drove that same character as a clone from a
// quantised heading and the mod's own speed constants. Two code paths and two precisions
// for one character, so the two machines disagreed about where it was by construction
// rather than by bad luck, and every trigger and encounter check downstream inherited the
// disagreement.
//
// WHAT REPLACES IT. FFX_Player__stepControl is called from exactly one site in the whole
// binary, inside FFX_MainStep, once per substep. That one call is patched to the loop in
// this file, so every player character gets the engine's own driver, at the point in the
// step the engine wanted it, on the engine's own substep cadence, each with its own copy
// of the ramp state. Identical inputs into identical code. See ffx::StepPlayerControlFor
// for how the state swap works and why reimplementing the driver was the worse option.
//
// WHERE THE LOCAL PLAYER'S INPUT COMES FROM, which is the actual fix. Not the pad. It
// comes out of the lockstep ring for the step being run, the same place a remote player's
// does, so the local machine simulates its own character from the bytes it put on the
// wire rather than from the full-precision pad those bytes were made from. The pad reaches
// the ring in the gate callback and nowhere else.
//
// SOLO IS UNTOUCHED. With no session the hook calls the original and returns, so a single
// player game runs the shipped code path exactly.

namespace pilgrimage
{

	// Patches the one call site. Once at startup, not per session: the hook decides per
	// step whether there is anything to replicate, so leaving it in with no session costs
	// one branch.
	//
	// Returns false when the site was not what we expect, and says so in the log. That is
	// a missing feature rather than a hazard: without it the bound player goes back to
	// being driven from the local pad, which is playable and wrong rather than broken.
	bool InstallPlayerDrive();
	bool PlayerDriveInstalled();

	// Is the loop actually driving, as opposed to passing through to the original. False
	// with no session, no clock, or no local peer id.
	bool PlayerDriveActive();

	// Forget every character's ramp state. Call when a session starts, and when ownership
	// changes, so a character does not inherit the ramps of whoever drove it last and
	// lurch on its first step.
	void ResetPlayerDrive();

	// How many characters the last pass drove, and how many it skipped for want of an
	// input frame. Both are per substep, not per frame.
	int PlayerDriveCount();
	int PlayerDriveSkipped();

	const char* PlayerDriveStatus();
	void LogPlayerDrive();

} // namespace pilgrimage
