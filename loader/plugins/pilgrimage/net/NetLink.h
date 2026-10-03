#pragma once

#include <windows.h>

// The mod's one network link: a transport, a session, and the glue that drives
// them from the frame hook.
//
// Everything here must run on the game thread. The UI thread raises a request
// flag instead, same rule as everything that touches a character. See ModState.h.
//
// ## How to try it with two copies on one PC
//
// Start the game twice. In one, press F2 to host. In the other, press F3 to join.
// The control panel readout shows the session state and how far apart the two
// step counters are. Shift+F3 disconnects.
//
// The joiner binds an ephemeral local port and dials the host's, so the two
// instances never fight over a port number and no per-instance configuration is
// needed. That matters because both instances read the same settings file.
//
// ## Two backends, chosen by a setting
//
// Steam P2P is the shipping path and gives NAT traversal plus Valve's relay, so
// three friends on home connections can play without configuring anything. A
// client needs the host's SteamID, which is the 17 digit number on their Steam
// profile page, pasted into the control panel.
//
// UDP on loopback is the other one, and it is how everything above the transport
// gets tested. There is no LAN address setting: the host is always 127.0.0.1,
// because an IP address setting is work the shipping path throws away. It is a
// small change if a real LAN link is ever wanted.

// Only the pointer is handed out, so the full header is not needed here.
namespace workshop
{
	class Session;
}

namespace pilgrimage
{

	// Both return false and log why on failure. The usual reason for the host is
	// that the port is already taken, which means the other instance got there first.
	bool StartHosting();
	bool StartJoining();

	void StopNetworking();

	// Called once per frame from the frame hook, after the game has stepped. Pumps
	// the transport, runs the session, and keeps the step counter it reports current.
	void StepNetworking(LONG frame);

	bool NetworkingActive();

	// Reads the local Steam identity once and publishes it into telemetry, so the
	// control panel can show the host the SteamID they need to pass to friends.
	// Cheap to call every frame: it returns immediately once it has an answer.
	// Game thread only, because it calls into Steam.
	void PublishLocalIdentity();

	// A one-line status for the control panel. Never NULL.
	const char* NetworkingSummary();

	// Which backend a connection would use right now, "steam" or "udp loopback".
	// Reads the setting, so it is correct before anything is started.
	const char* NetworkingBackendName();

	// The live session, or NULL when nothing is running. The lockstep clock attaches
	// to this. Returned rather than copied, because the session is the one object
	// both layers have to agree about.
	workshop::Session* ActiveSession();

} // namespace pilgrimage
