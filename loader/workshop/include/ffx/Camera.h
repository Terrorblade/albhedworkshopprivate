#pragma once

#include <windows.h>

namespace ffx
{

	struct Character;

	// The active camera's yaw, in the same convention as m_moveDir. Returns false
	// when there is no usable camera, in which case the caller should fall back to
	// world-relative input rather than guessing.
	bool ActiveCameraYaw(float* outYaw);

	// ---------------------------------------------------------------------------
	// WHO OWNS THE VIEW RIGHT NOW.
	//
	// FFX has two cameras and the difference decides whether moving the player
	// binding moves the view.
	//
	//   * The FREE-FOLLOW camera. FFX_PlayerCam_Step reads g_ffxControlledChr every
	//     step through FFX_Player__getPos and has no follow target of its own, so it
	//     goes wherever the player binding goes, for free, with no camera code.
	//
	//   * A SCRIPT camera. A map or event handle with a real class, which outranks
	//     the follow camera and decides the view itself. While one of these holds the
	//     screen, moving the binding changes what you control and not what you see.
	//
	// FollowCameraIsDriving is the question to ask. A co-op mod pointing the binding
	// at a second body needs to know which of the two it is up against, and the
	// honest answer changes several times a minute as scripts open and close cameras.
	// ---------------------------------------------------------------------------

	// Is the free-follow camera the one reaching the screen on this screen index?
	// True means the player binding controls the view. Screen 0 unless you know
	// otherwise.
	bool FollowCameraIsDriving(int screen);

	// The packed handle winning on a screen, or 0 when none is. See
	// Rva::CameGetActiveHandle for the bit layout.
	DWORD ActiveCameraHandle(int screen);

	// The class nibble of a handle: 1 map, 3 event, 5 battle, 7 viewer, 0 none.
	int CameraHandleClass(DWORD handle);

	// "map", "event", "battle", "viewer", "none", or "class N" for one the engine
	// does not name. Never NULL, so it is safe to print.
	const char* CameraHandleClassName(DWORD handle);

	// The engine's own name for the mode the winning camera is running, for example
	// "pos", "act", "pflw", "hyp1". Never NULL. Returns "unknown" when the tables
	// are not readable yet, which is the normal answer before a map is loaded.
	const char* ActiveCameraModeName(int screen, bool eyeNotLookAt);

	// Is the matrix override on, which is the FMV and pre-rendered background path?
	// While it is, nothing else decides the view at all.
	bool CameraMatrixOverrideActive();

	// ---------------------------------------------------------------------------
	// POINTING A SCRIPT CAMERA AT SOMETHING
	// ---------------------------------------------------------------------------

	// The ATEL actor id that owns a character, or -1 when no actor does.
	//
	// -1 IS THE ANSWER FOR ANYTHING THIS MOD SPAWNED, and that is not a bug to work
	// around. The engine's only explicit follow-target setter takes an ATEL actor id,
	// so a character allocated outside the event system cannot be a target for it.
	// Move the player binding for those instead.
	int ActorIdForCharacter(Character* chr);

	// Point a camera handle's look-at point, or its eye, at one or two ATEL actors.
	// Pass -1 for the second to use just the first. offsetY lifts the target, which
	// is what the game does to aim at a head rather than a navel.
	//
	// Returns false when the handle does not currently own a priority slot, which the
	// engine reports by writing nothing at all.
	//
	// GAME THREAD, STEP PHASE ONLY. It writes through the shared scratch at 0xC8F588.
	bool SetCameraTargetActors(DWORD handle, bool eyeNotLookAt, int actorId0, int actorId1, float offsetY);

	// Open and close a camera handle of your own. classCode must be 1 to 15, and 0 is
	// refused by the engine. Returns 0 on failure.
	DWORD OpenCameraHandle(int owner, int screen, int classCode);
	void CloseCameraHandle(DWORD handle);

	// A camera handle's look-at point or eye position. False when unreadable.
	bool CameraHandlePos(DWORD handle, bool eyeNotLookAt, float* outXyz);

} // namespace ffx
