#include "workshop/SteamUser.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "workshop/HostModule.h" // Readable
#include "workshop/Log.h"

namespace workshop
{
	namespace
	{

		// Resolved once and kept, because GetProcAddress on every call would be silly
		// and these never move. Both are re-resolved if Steam was not ready the first
		// time, which is the normal case during startup.
		ISteamUser017* cachedUser = NULL;
		ISteamFriends014* cachedFriends = NULL;
		bool warnedNoSteam = false;

		// Shared shape of "get an interface out of the already-loaded steam_api.dll and
		// satisfy yourself it is safe to call through".
		void* ResolveInterface(const char* exportName)
		{
			// The game imports steam_api.dll, so it is already loaded. GetModuleHandle,
			// never LoadLibrary, or we would be talking to a second copy.
			HMODULE steamApi = GetModuleHandleA("steam_api.dll");
			if (!steamApi)
			{
				if (!warnedNoSteam)
				{
					Log("steam: steam_api.dll is not loaded in this process, so there is "
					    "no Steam identity to read");
					warnedNoSteam = true;
				}
				return NULL;
			}

			typedef void*(__cdecl * GetInterfaceFn)();
			GetInterfaceFn getInterface = (GetInterfaceFn)GetProcAddress(steamApi, exportName);
			if (!getInterface)
			{
				Log("steam: steam_api.dll has no %s export", exportName);
				return NULL;
			}

			// Null here just means the game has not called SteamAPI_Init yet, which is a
			// retry rather than a failure, so it is not worth a log line every frame.
			void* iface = getInterface();
			if (!iface)
				return NULL;

			// A non-null pointer still has to be a readable object with a readable
			// vtable before anything calls through it. The alternative is a fault on the
			// frame path.
			if (!Readable(iface, sizeof(void*)))
			{
				Log("steam: %s() returned %p, which is not readable", exportName, iface);
				return NULL;
			}
			void* vtable = *(void**)iface;
			if (!Readable(vtable, sizeof(void*) * 8))
			{
				Log("steam: %s has an unreadable vtable at %p", exportName, vtable);
				return NULL;
			}
			return iface;
		}

		ISteamUser017* User()
		{
			if (!cachedUser)
				cachedUser = (ISteamUser017*)ResolveInterface("SteamUser");
			return cachedUser;
		}

		ISteamFriends014* Friends()
		{
			if (!cachedFriends)
				cachedFriends = (ISteamFriends014*)ResolveInterface("SteamFriends");
			return cachedFriends;
		}

		// Steam's string returns are documented as being freed or reallocated later, so
		// nothing is allowed to keep one. Copy on the way out, every time.
		void CopySteamString(const char* from, char* out, int count)
		{
			if (!out || count <= 0)
				return;
			if (!from || !Readable((void*)from, 1))
			{
				out[0] = 0;
				return;
			}
			strncpy_s(out, (size_t)count, from, _TRUNCATE);
		}

	} // namespace

	bool SteamLoggedOn()
	{
		ISteamUser017* user = User();
		if (!user)
			return false;
		return user->BLoggedOn();
	}

	uint64 LocalSteamId()
	{
		ISteamUser017* user = User();
		if (!user)
			return 0;

		// Asking before Steam is logged on gives an id that is not the player's, and
		// a wrong SteamID handed to someone to paste is worse than none at all.
		if (!user->BLoggedOn())
			return 0;

		// Returns by value through a hidden stack buffer, not in EAX:EDX. The
		// declaration in SteamAbi.h is what makes the compiler set that up, which is
		// why this call looks ordinary. Do not "simplify" it to a uint64 return.
		const SteamId me = user->GetSteamID();
		return me.m_unAll64Bits;
	}

	const char* LocalSteamIdText(char* out, int count)
	{
		if (!out || count <= 0)
			return out;

		const uint64 id = LocalSteamId();
		if (id == 0)
		{
			out[0] = 0;
			return out;
		}
		_snprintf_s(out, (size_t)count, _TRUNCATE, "%llu", id);
		return out;
	}

	const char* LocalPersonaName(char* out, int count)
	{
		if (!out || count <= 0)
			return out;
		out[0] = 0;

		ISteamFriends014* friends = Friends();
		if (!friends || !SteamLoggedOn())
			return out;

		CopySteamString(friends->GetPersonaName(), out, count);
		return out;
	}

	int SteamFriendCount()
	{
		ISteamFriends014* friends = Friends();
		if (!friends || !SteamLoggedOn())
			return 0;

		// Steam returns -1 for invalid flags. That is an error rather than a count,
		// and letting it through would make a caller's loop run against a negative
		// bound.
		const int count = friends->GetFriendCount(k_EFriendFlagImmediate);
		if (count < 0)
		{
			Log("steam: GetFriendCount returned %d, treating it as no friends", count);
			return 0;
		}
		return count;
	}

	bool SteamFriendAt(int index, uint64* outId, char* outName, int nameCount)
	{
		ISteamFriends014* friends = Friends();
		if (!friends || !SteamLoggedOn())
			return false;
		if (index < 0 || index >= SteamFriendCount())
			return false;

		// The same flags that produced the count, or the indices do not line up with
		// it. Using one named constant in both places is what guarantees that.
		const SteamId friendId = friends->GetFriendByIndex(index, k_EFriendFlagImmediate);
		if (friendId.m_unAll64Bits == 0)
			return false;

		if (outId)
			*outId = friendId.m_unAll64Bits;
		if (outName && nameCount > 0)
			CopySteamString(friends->GetFriendPersonaName(friendId.m_unAll64Bits),
			    outName, nameCount);
		return true;
	}

} // namespace workshop
