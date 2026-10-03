#include "ffx/Pad.h"

#include <string.h>

#include "workshop/Log.h"
#include "ffx/Api.h"
#include "ffx/Layout.h"

namespace ffx
{

	using workshop::Log;
	namespace
	{

		bool ButtonDown(BYTE* device, int semantic)
		{
			// LatchedButtons is 16 bytes indexed by (semantic - 11). Same bounds check
			// the engine's own accessor does.
			if ((unsigned)(semantic - 11) >= 16)
				return false;
			return *(volatile BYTE*)(device + PadDevice::LatchedButtons + (semantic - 11)) != 0;
		}

	} // namespace

	BYTE* PadDeviceAt(LONG slot)
	{
		if (slot < 0 || (DWORD)slot >= InputMapper::PadSlots)
			return NULL;
		BYTE* application = *Game.application;
		if (!application)
			return NULL;
		BYTE* mapper = *(BYTE**)(application + App::InputMapper);
		if (!mapper)
			return NULL;
		return *(BYTE**)(mapper + InputMapper::PadArray + 4 * (DWORD)slot);
	}

	bool ReadPad(LONG slot, PadState* out)
	{
		memset(out, 0, sizeof(*out));
		BYTE* device = PadDeviceAt(slot);
		if (!device)
			return false;
		if (*(volatile BYTE*)(device + PadDevice::Connected) == 0)
			return false;

		out->bound = true;
		out->leftX = (float)*(volatile int*)(device + PadDevice::AxisLeftX) / 255.0f;
		out->leftY = (float)*(volatile int*)(device + PadDevice::AxisLeftY) / 255.0f;

		out->cross = ButtonDown(device, PadDevice::Button::Cross);
		out->circle = ButtonDown(device, PadDevice::Button::Circle);
		out->square = ButtonDown(device, PadDevice::Button::Square);
		out->triangle = ButtonDown(device, PadDevice::Button::Triangle);
		out->l1 = ButtonDown(device, PadDevice::Button::L1);
		out->r1 = ButtonDown(device, PadDevice::Button::R1);
		out->up = ButtonDown(device, PadDevice::Button::Up);
		out->down = ButtonDown(device, PadDevice::Button::Down);
		out->left = ButtonDown(device, PadDevice::Button::Left);
		out->right = ButtonDown(device, PadDevice::Button::Right);
		return true;
	}

	LONG BoundPadCount()
	{
		LONG count = 0;
		for (DWORD slot = 0; slot < InputMapper::PadSlots; ++slot)
			if (PadDeviceAt((LONG)slot))
				++count;
		return count;
	}

	void LogPadSlots()
	{
		Log("pads: %ld of %lu Phyre slots bound", BoundPadCount(),
		    (unsigned long)InputMapper::PadSlots);
		for (DWORD slot = 0; slot < InputMapper::PadSlots; ++slot)
		{
			BYTE* device = PadDeviceAt((LONG)slot);
			if (device)
				Log("  pad slot %lu: device 0x%08X, connected=%d", (unsigned long)slot,
				    (unsigned)(UINT_PTR)device,
				    *(volatile BYTE*)(device + PadDevice::Connected));
		}
	}

} // namespace ffx
