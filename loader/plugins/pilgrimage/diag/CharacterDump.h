#pragma once

#include "ffx/Api.h"

namespace pilgrimage
{

	// One line of character state plus the draw-gate verdict. Cheap enough to call
	// every frame for a few seconds after a spawn.
	void LogCharacterDiag(const char* tag, ffx::Character* chr);

	// Field by field against the real player, which is the control: same chr id,
	// allocated by the same function, and it draws.
	//
	// The whole 0x880 is not worth printing, because the middle is matrices and bone
	// state that legitimately differ every frame. So three sections:
	//   A  0x000 to 0x200   the id, flags and state block
	//   B  0x700 to 0x880   the tail, where the instance pointers live
	//   C  the whole struct, but only dwords where one side holds a pointer and the
	//      other holds zero. That is the "something was not allocated for me"
	//      detector, and it is the one that finds a missing mesh.
	void DumpCharacterDiff(ffx::Character* clone, ffx::Character* player);

} // namespace pilgrimage
