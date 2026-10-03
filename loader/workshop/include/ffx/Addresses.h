#pragma once

// Every FFX.exe address the mod kit knows, split one file per subsystem.
//
// Addresses are RVAs: the VA you see in IDA minus 0x00400000. They are resolved
// at runtime against GetModuleHandle(NULL), because this exe is ASLR'd per boot
// rather than per launch, so a hardcoded VA appears to work perfectly right up
// until the machine reboots.
//
// ## The rule
//
// Nothing else in the project may contain a literal address. If you need a new
// one it goes in one of the files below.
//
// ## Adding a subsystem
//
// Research lands here as code, so a new area is a new file rather than more
// lines in an existing one. That also means two people, or two agents, can map
// two subsystems at once without ever touching the same file.
//
//   1. Add include/ffx/addresses/<Area>.h. Copy the shape of Character.h:
//      reopen namespace ffx { namespace Rva { ... } }, one const DWORD per
//      address, a comment on each saying what it is and its signature.
//   2. End that file with an RvaList function, as the others do, so the startup
//      build check can see the new addresses without anyone editing a shared
//      list by hand.
//   3. Add the include below, and the one call in src/ffx/VerifyLayout.cpp.
//
// Steps 1 and 2 touch only your own file. Steps 3 is two lines in files
// everybody shares, so do it last and keep it to those two lines.
//
// ## The one collision the file split does not prevent
//
// Two areas can legitimately need the same address, and two people mapping them
// at once will both declare it. That is a redefinition error at build time, so it
// cannot slip through, but it does need a rule for who wins.
//
// The rule: the address lives in the area that OWNS the thing, and the other area
// gets a comment pointing at it. Not a second const, and not a #define, because
// then two places have to be kept in step.
//
// It has happened once so far, with the atel event VM's pad snapshot, which both
// the input pass and the atel pass declared. The values agreed exactly, which is
// a better check on both than either pass alone, so when this happens compare the
// values before deleting either one.

#include "ffx/addresses/Atel.h"
#include "ffx/addresses/Character.h"
#include "ffx/addresses/Cutscene.h"
#include "ffx/addresses/Encounter.h"
#include "ffx/addresses/EscMenu.h"
#include "ffx/addresses/Input.h"
#include "ffx/addresses/GameState.h"
#include "ffx/addresses/MainLoop.h"
#include "ffx/addresses/MenuSystem.h"
