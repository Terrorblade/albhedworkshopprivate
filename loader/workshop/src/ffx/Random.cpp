#include "ffx/Random.h"

#include <string.h>

#include "ffx/addresses/Minigames.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using workshop::Log;
	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		typedef char RandomStreamSizeCheck
		    [(sizeof(((RandomState*)0)->stream) == Rva::RandStreamStateBytes) ? 1 : -1];

		BYTE* StreamPtr()
		{
			BYTE* p = (BYTE*)ModuleAddress(Rva::RandStreamState);
			return Readable(p, Rva::RandStreamStateBytes) ? p : nullptr;
		}

		DWORD* BattlePtr()
		{
			DWORD* p = (DWORD*)ModuleAddress(Rva::BattleRandState);
			return Readable(p, sizeof(DWORD)) ? p : nullptr;
		}

		DWORD* EffectPtr()
		{
			DWORD* p = (DWORD*)ModuleAddress(Rva::EffectRandState);
			return Readable(p, sizeof(DWORD)) ? p : nullptr;
		}

		uint32_t Mix(uint32_t h, uint32_t v)
		{
			h ^= v;
			h *= 16777619u;
			return h;
		}

	}

	bool RandomStateAvailable()
	{
		return StreamPtr() != nullptr && BattlePtr() != nullptr && EffectPtr() != nullptr;
	}

	bool ReadRandomState(RandomState* out)
	{
		if (!out)
			return false;
		memset(out, 0, sizeof(*out));

		BYTE* stream = StreamPtr();
		DWORD* battle = BattlePtr();
		DWORD* effect = EffectPtr();
		if (!stream || !battle || !effect)
			return false;

		memcpy(out->stream, stream, Rva::RandStreamStateBytes);
		out->battle = *battle;
		out->effect = *effect;
		out->valid = true;
		return true;
	}

	bool WriteRandomState(const RandomState& in)
	{
		if (!in.valid)
			return false;

		BYTE* stream = StreamPtr();
		DWORD* battle = BattlePtr();
		DWORD* effect = EffectPtr();
		if (!stream || !battle || !effect)
			return false;

		memcpy(stream, in.stream, Rva::RandStreamStateBytes);
		*battle = in.battle;
		*effect = in.effect;
		return true;
	}

	uint32_t RandomStateHash()
	{
		BYTE* stream = StreamPtr();
		DWORD* battle = BattlePtr();
		DWORD* effect = EffectPtr();
		if (!stream || !battle || !effect)
			return 0;

		uint32_t h = 2166136261u;
		for (int i = 0; i < Rva::RandStreamStateBytes; i += 4)
			h = Mix(h, *(const uint32_t*)(stream + i));
		h = Mix(h, (uint32_t)*battle);
		h = Mix(h, (uint32_t)*effect);
		return h ? h : 1u; // 0 means "unavailable" to callers, so never return it
	}

	void LogRandomState()
	{
		if (!RandomStateAvailable())
		{
			Log("random  : globals not readable");
			return;
		}
		const BYTE* s = StreamPtr();
		Log("random  : hash %08X battle %08X effect %08X stream[0..7] %02X %02X %02X %02X %02X %02X %02X %02X",
		    RandomStateHash(), *BattlePtr(), *EffectPtr(),
		    s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
	}

}
