#pragma once

// Umbrella header. Include this to get the whole library, or include the
// individual headers when you want to see at a glance what a file depends on.
//
// WHAT THIS LIBRARY IS. Everything known about FINAL FANTASY X HD Remaster's
// FFX.exe that is worth reusing: where its functions and globals are, how its
// structures are laid out, and thin wrappers that make them safe to touch. Plus,
// under workshop, the generic plumbing every plugin needs.
//
// It is a static library, so each plugin DLL links its own copy and keeps its own
// resolved pointers. Nothing is shared between plugins and nothing is exported.
//
// THE CONTRACT EVERY PLUGIN FOLLOWS:
//
//   workshop::OpenLog(L"my_plugin.log");
//   workshop::BindHostModule();
//   if (!ffx::VerifyLayout()) return;   // wrong build, hook nothing
//   ffx::BindApi();
//   ffx::StartHub();                                          // ffx/Hub.h
//   workshop::Subscribe(workshop::EventFrame, &OnFrame);       // workshop/Events.h
//   workshop::RegisterOverlayPanel("My Plugin", &Draw);        // workshop/Overlay.h
//
// Skipping VerifyLayout is not an option. These addresses are valid for one build
// of one game, and patching the wrong build is worse than not running.
//
// Subscribe rather than hook wherever the hub already raises what you want. Two
// plugins can each take EventFrame, where only one of them can own the animate
// slot. Anything the hub does not raise, detour it and Publish your own event.

#include "workshop/Detour.h"
#include "workshop/Events.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "workshop/Overlay.h"

#include "ffx/Addresses.h"
#include "ffx/AnimateHook.h"
#include "ffx/Atel.h"
#include "ffx/Api.h"
#include "ffx/Battle.h"
#include "ffx/Camera.h"
#include "ffx/Character.h"
#include "ffx/Cutscene.h"
#include "ffx/Encounter.h"
#include "ffx/EscMenu.h"
#include "ffx/GameState.h"
#include "ffx/GfxContext.h"
#include "ffx/HideFlags.h"
#include "ffx/Hub.h"
#include "ffx/Input.h"
#include "ffx/Layout.h"
#include "ffx/MainLoop.h"
#include "ffx/MenuSystem.h"
#include "ffx/Pad.h"
#include "ffx/Random.h"
#include "ffx/RenderProbe.h"
#include "ffx/StepClock.h"
#include "ffx/VerifyLayout.h"
#include "ffx/Walkmesh.h"
#include "ffx/WorldState.h"
