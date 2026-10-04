#include "ffx/StepClock.h"

#include <stdio.h>

#include "ffx/addresses/Battle.h"
#include "ffx/addresses/Input.h"
#include "ffx/addresses/MainLoop.h"
#include "ffx/addresses/MenuSystem.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// Both clocks return their value in st(0), so a double returning hook is
		// binary compatible with the float returning one at 0x230C60.
		typedef double(__cdecl* ClockFn)(void);

		bool g_installed = false;
		bool g_feeding = false;
		float g_time = 0.0f;
		int g_patched = 0;
		char g_status[200] = "not installed";

		double CallOriginal(DWORD rva)
		{
			ClockFn fn = (ClockFn)ModuleAddress(rva);
			if (!Readable((void*)fn, 1))
				return 0.0;
			return fn();
		}

		// The two hooks differ only in which original they fall back to, which
		// matters: the two clocks have different epochs and a caller that mixed them
		// would see a jump the moment we start or stop feeding.
		double __cdecl InputTimeHook()
		{
			if (g_feeding)
				return (double)g_time;
			return CallOriginal(Rva::InputGetTimeSeconds);
		}

		double __cdecl AppElapsedHook()
		{
			if (g_feeding)
				return (double)g_time;
			return CallOriginal(Rva::TimeAppElapsedSeconds);
		}

		struct Site
		{
			const char* what;
			DWORD siteRva;
			DWORD expectedTargetRva;
			void* replacement;
			CallSitePatch patch;
		};

		Site g_sites[] = {
			{ "pad auto repeat", Rva::PadAutoRepeatClockCallSite, Rva::InputGetTimeSeconds,
			    (void*)&InputTimeHook, { 0, false } },
			{ "menu pad sample", Rva::MenuSysPadClockCallSite, Rva::InputGetTimeSeconds,
			    (void*)&InputTimeHook, { 0, false } },
			{ "message window pad sample", Rva::MesWinPadClockCallSite, Rva::InputGetTimeSeconds,
			    (void*)&InputTimeHook, { 0, false } },
			{ "battle menu page open", Rva::BtlMenuPageStartClockCallSite, Rva::TimeAppElapsedSeconds,
			    (void*)&AppElapsedHook, { 0, false } },
			{ "battle menu page step", Rva::BtlMenuPageElapsedClockCallSite, Rva::TimeAppElapsedSeconds,
			    (void*)&AppElapsedHook, { 0, false } },
			{ "lulu fury now", Rva::BtlOdLuluClockCallSiteNow, Rva::TimeAppElapsedSeconds,
			    (void*)&AppElapsedHook, { 0, false } },
			{ "lulu fury mark", Rva::BtlOdLuluClockCallSiteMark, Rva::TimeAppElapsedSeconds,
			    (void*)&AppElapsedHook, { 0, false } },
		};

		const int SiteCount = (int)(sizeof(g_sites) / sizeof(g_sites[0]));

	} // namespace

	bool InstallStepClock()
	{
		if (g_installed)
			return true;

		g_patched = 0;
		for (int i = 0; i < SiteCount; ++i)
		{
			Site& site = g_sites[i];
			if (PatchCallSite(site.patch, site.siteRva, site.expectedTargetRva,
			        site.replacement, site.what))
				++g_patched;
		}

		g_installed = g_patched > 0;
		if (!g_installed)
		{
			Log("step clock: no site took the patch, so input timing stays on the wall "
			    "clock and held buttons will repeat differently on each machine");
			return false;
		}

		Log("step clock: %d of %d sites patched", g_patched, SiteCount);
		if (g_patched < SiteCount)
			for (int i = 0; i < SiteCount; ++i)
				if (!g_sites[i].patch.patched)
					Log("step clock: %s is still on the wall clock", g_sites[i].what);
		return true;
	}

	bool StepClockInstalled()
	{
		return g_installed;
	}

	int StepClockSitesPatched()
	{
		return g_patched;
	}

	int StepClockSiteCount()
	{
		return SiteCount;
	}

	void SetStepClockTime(float seconds)
	{
		g_time = seconds;
		g_feeding = true;
	}

	float StepClockTime()
	{
		return g_time;
	}

	bool StepClockFeeding()
	{
		return g_feeding;
	}

	void ReleaseStepClock()
	{
		g_feeding = false;
	}

	const char* StepClockStatus()
	{
		if (!g_installed)
			return "off";
		_snprintf(g_status, sizeof(g_status) - 1, "%d/%d sites, %s, t=%.3f",
		    g_patched, SiteCount, g_feeding ? "step derived" : "wall clock", g_time);
		g_status[sizeof(g_status) - 1] = 0;
		return g_status;
	}

	void LogStepClock()
	{
		Log("stepclk : %s", StepClockStatus());
		for (int i = 0; i < SiteCount; ++i)
			Log("stepclk : %-26s %s", g_sites[i].what,
			    g_sites[i].patch.patched ? "patched" : "NOT patched");
	}

} // namespace ffx
