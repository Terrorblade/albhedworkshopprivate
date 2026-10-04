#pragma once

#include <windows.h>
#include <stdint.h>

// The game's RNG state, for transfer and for hashing.
//
// All three RNGs are seeded from constants at boot with no clock read, so two
// processes that launch and play the same inputs agree. A peer joining mid
// session does not, and nothing in the save block carries this, so it has to be
// sent separately.

namespace ffx
{

	struct RandomState
	{
		BYTE stream[272]; // the 68 channel script RNG, the whole mutable state
		DWORD battle;     // single LCG, battle formation and party slot picks
		DWORD effect;     // effect and particle RNG, held as float bits
		bool valid;
	};

	// Read or install the whole set. WriteRandomState refuses a state whose
	// valid flag is false.
	bool ReadRandomState(RandomState* out);
	bool WriteRandomState(const RandomState& in);

	// One hash over all three, for a checksum region.
	uint32_t RandomStateHash();

	// True when the three globals are readable.
	bool RandomStateAvailable();

	void LogRandomState();

}
