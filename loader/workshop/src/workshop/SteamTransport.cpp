#include "workshop/SteamTransport.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workshop/HostModule.h" // Readable
#include "workshop/Log.h"

namespace workshop
{
	namespace
	{

		// How often to re-accept every known peer. See the header: calling
		// AcceptP2PSessionWithUser repeatedly is documented as safe, but it is an IPC
		// round trip, so once every two seconds at 30 Hz is plenty for a handshake.
		const unsigned long AcceptEveryFrames = 60;

		// One channel is enough. Keeping it at 0 everywhere means the channel argument
		// never becomes a thing to get wrong, and the session layer already multiplexes
		// by message kind.
		const int Channel = 0;

		ISteamNetworking005* Interface(void* raw)
		{
			return (ISteamNetworking005*)raw;
		}

		// ---------------------------------------------------------------------------
		// The callback plumbing.
		//
		// One transport exists at a time, so the handlers reach it through a file
		// static rather than Steam carrying a context pointer for us, which it does
		// not. Cleared by Stop, so a callback arriving after a disconnect is a null
		// check and not a crash.
		//
		// The shims are file statics rather than members because steam_api.dll keeps
		// the pointer we register until we unregister it, and a static outliving a
		// mistake is a dangling pointer that never fires instead of one that does.
		// ---------------------------------------------------------------------------
		SteamTransport* g_owner = NULL;
		SteamCallbackShim g_onSessionRequest;
		SteamCallbackShim g_onConnectFail;
		FnSteamAPI_RegisterCallback g_registerCallback = NULL;
		FnSteamAPI_UnregisterCallback g_unregisterCallback = NULL;

		void OnSessionRequest(void* param)
		{
			if (!param || !g_owner)
				return;
			const P2PSessionRequest_t* request = (const P2PSessionRequest_t*)param;
			g_owner->NoteSessionRequest(request->m_steamIDRemote.m_unAll64Bits);
		}

		void OnConnectFail(void* param)
		{
			if (!param || !g_owner)
				return;
			const P2PSessionConnectFail_t* fail = (const P2PSessionConnectFail_t*)param;
			g_owner->NoteConnectFail(fail->m_steamIDRemote.m_unAll64Bits,
			    (int)fail->m_eP2PSessionError);
		}

		const char* SessionErrorText(int error)
		{
			switch (error)
			{
				case k_EP2PSessionErrorNotRunningApp:
					return "they are not running the same game";
				case k_EP2PSessionErrorNoRightsToApp:
					return "this account does not own the app";
				case k_EP2PSessionErrorDestinationNotLoggedIn:
					return "they are not signed in to Steam";
				case k_EP2PSessionErrorTimeout:
					return "they never answered, which usually means their end did not "
					       "accept the session, or UDP 3478, 4379 and 4380 are blocked "
					       "outbound";
				default:
					return "no reason given";
			}
		}

	} // namespace

	SteamTransport::SteamTransport()
	    : networking(NULL),
	      running(false),
	      acceptUnknown(false),
	      frameCounter(0),
	      incomingCount(0),
	      sent(0),
	      received(0),
	      dropped(0)
	{
		memset(peers, 0, sizeof(peers));
		memset(incoming, 0, sizeof(incoming));
	}

	SteamTransport::~SteamTransport()
	{
		Stop();
	}

	bool SteamTransport::Start(bool acceptUnknownSenders)
	{
		if (running)
			return true;

		// The game imports steam_api.dll, so it is already loaded and we must not
		// load a second copy. GetModuleHandle, never LoadLibrary.
		HMODULE steamApi = GetModuleHandleA("steam_api.dll");
		if (!steamApi)
		{
			Log("steam: steam_api.dll is not loaded in this process, so there is no "
			    "Steam to talk to. Is the game running outside Steam?");
			return false;
		}

		FnSteamNetworking getNetworking =
		    (FnSteamNetworking)GetProcAddress(steamApi, "SteamNetworking");
		if (!getNetworking)
		{
			Log("steam: steam_api.dll has no SteamNetworking export, which should be "
			    "impossible for this build. Refusing to continue.");
			return false;
		}

		// The game owns SteamAPI_Init and calls it on its own schedule, so being
		// early gives a null here. That is a retry, not a failure.
		void* iface = getNetworking();
		if (!iface)
		{
			Log("steam: SteamNetworking() returned null, so the game has not "
			    "initialised Steam yet. Try again in a moment.");
			return false;
		}

		// A returned pointer still has to be a readable object with a readable
		// vtable before anything calls through it. Cheap, and the alternative is a
		// fault on the frame path.
		if (!Readable(iface, sizeof(void*)))
		{
			Log("steam: SteamNetworking() returned %p, which is not readable. "
			    "Refusing to call through it.",
			    iface);
			return false;
		}
		void* vtable = *(void**)iface;
		if (!Readable(vtable, sizeof(void*) * 8))
		{
			Log("steam: the interface at %p has an unreadable vtable at %p. "
			    "Refusing to call through it.",
			    iface, vtable);
			return false;
		}

		networking = iface;
		acceptUnknown = acceptUnknownSenders;
		running = true;
		frameCounter = 0;
		memset(peers, 0, sizeof(peers));

		// Relay is on by default, so this is not strictly needed. It is here because
		// relying on a default we did not set is how a later SDK change becomes a
		// mystery, and because for three friends behind home routers the relay is
		// the difference between working and not.
		Interface(networking)->AllowP2PPacketRelay(true);

		// ---------------------------------------------------------------------------
		// THE SESSION REQUEST CALLBACK, WITHOUT WHICH A HOST CANNOT BE JOINED.
		//
		// Steam will not deliver a packet from a peer we have no session with, so a
		// host that waits to learn a joiner's SteamID from ReadP2PPacket waits
		// forever. This callback is the only place Steam tells us who is trying.
		//
		// Registration failing is not fatal for a client, which already knows the
		// host's SteamID and accepts it directly, so this logs and carries on rather
		// than refusing to start. For a host it is fatal in practice and the log says
		// so, because a silent inability to be joined is what this whole block exists
		// to stop happening twice.
		// ---------------------------------------------------------------------------
		g_registerCallback =
		    (FnSteamAPI_RegisterCallback)GetProcAddress(steamApi, "SteamAPI_RegisterCallback");
		g_unregisterCallback =
		    (FnSteamAPI_UnregisterCallback)GetProcAddress(steamApi, "SteamAPI_UnregisterCallback");

		if (g_registerCallback && g_unregisterCallback)
		{
			g_owner = this;

			g_onSessionRequest.Init(P2PSessionRequest_t::k_iCallback,
			    (int)sizeof(P2PSessionRequest_t), &OnSessionRequest);
			g_registerCallback((void*)&g_onSessionRequest, P2PSessionRequest_t::k_iCallback);

			g_onConnectFail.Init(P2PSessionConnectFail_t::k_iCallback,
			    (int)sizeof(P2PSessionConnectFail_t), &OnConnectFail);
			g_registerCallback((void*)&g_onConnectFail, P2PSessionConnectFail_t::k_iCallback);

			// ---------------------------------------------------------------------------
			// A FREE ABI CHECK, AND IT IS WORTH TAKING.
			//
			// SteamAPI_RegisterCallback ORs k_ECallbackFlagsRegistered into
			// m_nCallbackFlags, which it reaches at +4 of the object we just handed it.
			// So if IsRegistered now reads true, the DLL and our hand-rolled
			// CCallbackBase agree about where that field lives. That is the one thing
			// about this object that could not be checked without running it.
			//
			// If it reads false the layout is wrong, and the next thing that would
			// happen is steam_api.dll calling through a vtable it has mislocated. So
			// take it back rather than find out.
			// ---------------------------------------------------------------------------
			if (g_onSessionRequest.IsRegistered() && g_onConnectFail.IsRegistered())
			{
				Log("steam: listening for session requests and connect failures, and "
				    "steam_api.dll set the registered flag on both, which confirms it "
				    "agrees with our CCallbackBase layout. FFX pumps Steam callbacks "
				    "from animate, so these arrive once a frame.");
			}
			else
			{
				g_unregisterCallback((void*)&g_onSessionRequest);
				g_unregisterCallback((void*)&g_onConnectFail);
				g_owner = NULL;
				Log("steam: REFUSING THE CALLBACKS. steam_api.dll did not set the "
				    "registered flag where we expect it at +4, so our CCallbackBase "
				    "layout does not match this DLL and letting it call us would be a "
				    "crash. Registration has been taken back. A client can still join "
				    "a host. THIS MACHINE CANNOT BE JOINED. Re-derive "
				    "SteamCallbackShim against this steam_api.dll.");
			}
		}
		else
		{
			Log("steam: steam_api.dll is missing SteamAPI_RegisterCallback or "
			    "SteamAPI_UnregisterCallback, so nothing can tell us who is trying to "
			    "reach us. A client can still join a host. THIS MACHINE CANNOT BE "
			    "JOINED, because accepting a joiner needs their SteamID and that only "
			    "arrives on a callback.");
		}

		Log("steam: ISteamNetworking005 at %p, %s", iface,
		    acceptUnknown ? "accepting new SteamIDs as peers" : "only talking to peers we added");
		return true;
	}

	void SteamTransport::NoteSessionRequest(uint64 steamId)
	{
		if (steamId == 0)
			return;

		// Interlocked because this is the one function in the file that is not called
		// from our own tick. FFX pumps callbacks from animate, which is the thread we
		// run on, so in practice there is no race. In practice is not a guarantee, and
		// the cost of being right here is one instruction.
		const long slot = InterlockedIncrement(&incomingCount) - 1;
		if (slot >= TransportMaxPeers)
		{
			InterlockedDecrement(&incomingCount);
			return;
		}
		incoming[slot] = steamId;
	}

	void SteamTransport::NoteConnectFail(uint64 steamId, int sessionError)
	{
		// Logging from inside a Steam callback is the one thing worth doing here. The
		// log is its own lock and touches no game state, and a join that cannot get
		// through is exactly the moment someone is reading the log.
		Log("steam: cannot reach SteamID %llu, error %d, %s", steamId, sessionError,
		    SessionErrorText(sessionError));
	}

	int SteamTransport::AddPeer(uint64 steamId)
	{
		if (steamId == 0)
			return InvalidPeer;

		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].steamId == steamId)
				return i;

		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].steamId = steamId;
			peers[i].inUse = true;
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%llu", steamId);

			// Accept immediately as well as on the timer, so a handshake does not
			// wait two seconds for no reason.
			if (running)
				Interface(networking)->AcceptP2PSessionWithUser(steamId);

			Log("steam: peer %d is SteamID %llu", i, steamId);
			return i;
		}

		Log("steam: no room for another peer, %d is the capacity", TransportMaxPeers);
		return InvalidPeer;
	}

	int SteamTransport::AddPeerFromText(const char* steamIdText)
	{
		if (!steamIdText || !*steamIdText)
		{
			Log("steam: no SteamID given. It is the 17 digit number from the host's "
			    "Steam profile page.");
			return InvalidPeer;
		}

		// Deliberately strict. _strtoui64 would happily read "76561" out of
		// "76561abc" and connect to nobody, which is a worse failure than a clear
		// refusal.
		const char* p = steamIdText;
		while (*p == ' ' || *p == '\t')
			++p;
		int digits = 0;
		for (const char* q = p; *q; ++q)
		{
			if (*q >= '0' && *q <= '9')
			{
				++digits;
				continue;
			}
			if (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
				continue;
			Log("steam: '%s' is not a SteamID. Expected only digits, found '%c'.",
			    steamIdText, *q);
			return InvalidPeer;
		}
		if (digits < 10)
		{
			Log("steam: '%s' is too short to be a SteamID, it should be about 17 digits",
			    steamIdText);
			return InvalidPeer;
		}

		const uint64 id = (uint64)_strtoui64(p, NULL, 10);

		// An individual account on the public universe. Catches a lobby id or a
		// group id pasted by mistake, which otherwise connects to silence.
		if (SteamIdAccountType(id) != k_EAccountTypeIndividual_value ||
		    SteamIdUniverse(id) != k_EUniversePublic_value)
		{
			Log("steam: %llu is a valid number but not an individual public account. "
			    "That looks like a lobby or group id rather than a person.",
			    id);
			return InvalidPeer;
		}

		return AddPeer(id);
	}

	const char* SteamTransport::Name() const
	{
		return "steam";
	}
	bool SteamTransport::Running() const
	{
		return running;
	}

	void SteamTransport::Stop()
	{
		if (running && networking)
		{
			for (int i = 0; i < TransportMaxPeers; ++i)
				if (peers[i].inUse)
					Interface(networking)->CloseP2PSessionWithUser(peers[i].steamId);
			Log("steam: stopped, sent %lu received %lu dropped %lu", sent, received, dropped);
		}

		// UNREGISTER BEFORE ANYTHING ELSE GOES AWAY. steam_api.dll holds the pointer we
		// gave it until we take it back, and it will call through it on the next
		// SteamAPI_RunCallbacks whether or not we are still interested.
		if (g_unregisterCallback)
		{
			if (g_onSessionRequest.IsRegistered())
				g_unregisterCallback((void*)&g_onSessionRequest);
			if (g_onConnectFail.IsRegistered())
				g_unregisterCallback((void*)&g_onConnectFail);
		}
		g_owner = NULL;

		running = false;
		networking = NULL;
		incomingCount = 0;
		memset(incoming, 0, sizeof(incoming));
		memset(peers, 0, sizeof(peers));
	}

	void SteamTransport::Pump()
	{
		if (!running)
			return;
		++frameCounter;

		// ---------------------------------------------------------------------------
		// Session requests Steam handed us since the last tick.
		//
		// This is where a host gains a joiner. Taking the count once and zeroing it
		// means a request arriving mid-drain is simply picked up next tick rather than
		// read half written.
		// ---------------------------------------------------------------------------
		const long waiting = InterlockedExchange(&incomingCount, 0);
		const long count = (waiting > TransportMaxPeers) ? TransportMaxPeers : waiting;
		for (long i = 0; i < count; ++i)
		{
			const uint64 steamId = incoming[i];
			if (steamId == 0)
				continue;

			if (!acceptUnknown && PeerForSteamId(steamId) == InvalidPeer)
			{
				// A client only ever talks to peers it was told about. Accepting
				// anyone who asks would let a stranger hold a peer slot.
				Log("steam: SteamID %llu asked to talk to us and is not a peer we "
				    "added, so it is being ignored.",
				    steamId);
				continue;
			}

			// Accept first, then record. Accepting is what releases the packet Steam
			// is already holding, and AddPeer accepts again, which is harmless.
			Interface(networking)->AcceptP2PSessionWithUser(steamId);
			const int peer = AddPeer(steamId);
			if (peer == InvalidPeer)
				Log("steam: SteamID %llu wanted in but there is no peer slot free", steamId);
			else
				Log("steam: accepted a session request from SteamID %llu as peer %d",
				    steamId, peer);
		}

		// Re-accepting every known peer on a timer. Documented as harmless, and it is
		// what lets a session recover when one side restarts: the far end's request
		// after a restart may land before we notice the old session died.
		if ((frameCounter % AcceptEveryFrames) == 0)
		{
			for (int i = 0; i < TransportMaxPeers; ++i)
				if (peers[i].inUse)
					Interface(networking)->AcceptP2PSessionWithUser(peers[i].steamId);
		}
	}

	bool SteamTransport::Send(int peer, const void* data, int length, SendMode mode)
	{
		if (!running || !data)
			return false;
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return false;
		if (length <= 0)
			return false;

		// The ceiling is per mode, not global: 1200 bytes unreliable, 1 MB reliable
		// with Steam fragmenting and reassembling for us. Checked here rather than
		// trusted, because an oversized unreliable send fails silently and that is a
		// horrible thing to debug.
		const int ceiling = (mode == SendReliable)
		                        ? (int)k_cbMaxP2PPacketReliable
		                        : (int)k_cbMaxP2PPacketUnreliable;
		if (length > ceiling)
		{
			Log("steam: refusing a %d byte %s message, the limit for that mode is %d",
			    length, (mode == SendReliable) ? "reliable" : "unreliable", ceiling);
			return false;
		}

		const EP2PSend how = (mode == SendReliable) ? k_EP2PSendReliable
		                                            : k_EP2PSendUnreliableNoDelay;

		const bool ok = Interface(networking)->SendP2PPacket(peers[peer].steamId, data, (uint32)length, how, Channel);
		if (!ok)
		{
			++dropped;
			return false;
		}
		++sent;
		return true;
	}

	int SteamTransport::SendToAll(const void* data, int length, SendMode mode)
	{
		int reached = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && Send(i, data, length, mode))
				++reached;
		return reached;
	}

	bool SteamTransport::Receive(Packet& out)
	{
		if (!running)
			return false;

		// Loops rather than giving up on the first packet from a stranger, so one
		// stray message does not hide the real traffic queued behind it.
		for (;;)
		{
			uint32 waiting = 0;
			if (!Interface(networking)->IsP2PPacketAvailable(&waiting, Channel))
				return false;

			// ReadP2PPacket TRUNCATES silently if the buffer is too small, so a
			// message bigger than Packet can hold has to be read and discarded
			// rather than half delivered. Nothing we send is this big, which is
			// exactly why hitting it means something is wrong.
			if (waiting > (uint32)MaxPacketBytes)
			{
				unsigned char scratch[MaxPacketBytes];
				uint32 read = 0;
				SteamId from;
				from.m_unAll64Bits = 0;
				Interface(networking)->ReadP2PPacket(scratch, sizeof(scratch), &read, &from, Channel);
				++dropped;
				Log("steam: dropped a %lu byte message from %llu, which is over the %d "
				    "byte limit this transport carries",
				    (unsigned long)waiting, from.m_unAll64Bits, MaxPacketBytes);
				continue;
			}

			uint32 read = 0;
			SteamId from;
			from.m_unAll64Bits = 0;
			if (!Interface(networking)->ReadP2PPacket(out.data, (uint32)MaxPacketBytes, &read, &from, Channel))
				return false;
			if (read == 0)
				return false;

			const int peer = PeerForSteamId(from.m_unAll64Bits);
			if (peer == InvalidPeer)
			{
				++dropped;
				continue;
			}

			out.peer = peer;
			out.length = (int)read;
			++received;
			return true;
		}
	}

	int SteamTransport::PeerForSteamId(uint64 steamId)
	{
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse && peers[i].steamId == steamId)
				return i;

		if (!acceptUnknown || steamId == 0)
			return InvalidPeer;

		// The host learning a joiner's SteamID from its first message.
		//
		// THIS IS A BACKSTOP, NOT THE WAY IN. It only runs after ReadP2PPacket already
		// succeeded, and that cannot happen until the session is accepted, so a join
		// gets here through P2PSessionRequest_t and Pump. Reaching this line means the
		// session was accepted and then the peer slot was lost, which a reconnect can
		// do, so it stays.
		for (int i = 0; i < TransportMaxPeers; ++i)
		{
			if (peers[i].inUse)
				continue;
			peers[i].steamId = steamId;
			peers[i].inUse = true;
			_snprintf_s(peers[i].label, sizeof(peers[i].label), _TRUNCATE, "%llu", steamId);
			Interface(networking)->AcceptP2PSessionWithUser(steamId);
			Log("steam: accepted peer %d, SteamID %llu", i, steamId);
			return i;
		}
		return InvalidPeer;
	}

	int SteamTransport::PeerCount() const
	{
		int count = 0;
		for (int i = 0; i < TransportMaxPeers; ++i)
			if (peers[i].inUse)
				++count;
		return count;
	}

	bool SteamTransport::PeerConnected(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers)
			return false;
		return peers[peer].inUse;
	}

	const char* SteamTransport::PeerLabel(int peer) const
	{
		if (peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return "none";
		return peers[peer].label;
	}

	unsigned long SteamTransport::PacketsSent() const
	{
		return sent;
	}
	unsigned long SteamTransport::PacketsReceived() const
	{
		return received;
	}
	unsigned long SteamTransport::PacketsDropped() const
	{
		return dropped;
	}

	bool SteamTransport::DescribeLink(int peer, char* out, int count) const
	{
		if (!out || count <= 0)
			return false;
		out[0] = 0;
		if (!running || peer < 0 || peer >= TransportMaxPeers || !peers[peer].inUse)
			return false;

		P2PSessionState_t state;
		memset(&state, 0, sizeof(state));
		if (!Interface(networking)->GetP2PSessionState(peers[peer].steamId, &state))
		{
			strncpy_s(out, (size_t)count, "no session", _TRUNCATE);
			return false;
		}

		_snprintf_s(out, (size_t)count, _TRUNCATE, "%s%s, %s, %d bytes queued",
		    state.m_bConnectionActive ? "active" : "not active",
		    state.m_bConnecting ? " (connecting)" : "",
		    state.m_bUsingRelay ? "via Steam relay" : "direct",
		    (int)state.m_nBytesQueuedForSend);
		return true;
	}

} // namespace workshop
