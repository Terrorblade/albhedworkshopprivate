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
//   ffx::HookAnimate(&MyAnimate);
//
// Skipping VerifyLayout is not an option. These addresses are valid for one build
// of one game, and patching the wrong build is worse than not running.

#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include "ffx/Addresses.h"
#include "ffx/AnimateHook.h"
#include "ffx/Atel.h"
#include "ffx/Api.h"
#include "ffx/Camera.h"
#include "ffx/Character.h"
#include "ffx/Cutscene.h"
#include "ffx/Encounter.h"
#include "ffx/EscMenu.h"
#include "ffx/GameState.h"
#include "ffx/GfxContext.h"
#include "ffx/HideFlags.h"
#include "ffx/Input.h"
#include "ffx/Layout.h"
#include "ffx/MainLoop.h"
#include "ffx/MenuSystem.h"
#include "ffx/Pad.h"
#include "ffx/RenderProbe.h"
#include "ffx/VerifyLayout.h"
#include "ffx/Walkmesh.h"
