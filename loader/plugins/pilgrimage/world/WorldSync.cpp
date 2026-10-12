#include "world/WorldSync.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Character.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "world/Arrival.h"
#include "world/MinigameSync.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// How many chunks go out per frame. 27 chunks at 1024 bytes is the whole block, so
		// four a frame finishes a transfer in seven frames without putting 27 KB on the
		// wire at once. The transfer only happens at a join, so there is no reason to be
		// aggressive about it.
		const int ChunksPerFrame = 4;

		// A transfer that has not advanced in this long has stalled. Reliable delivery
		// means loss is not the cause, so the real causes are the peer going away or the
		// host never starting, and both want saying out loud rather than waiting forever.
		const DWORD TransferTimeoutMs = 15000;

		// ---------------------------------------------------------------------------
		// Host side, one of these per peer being sent to.
		// ---------------------------------------------------------------------------
		struct Outbound
		{
			bool active;
			uint32_t snapshotId;
			int nextChunk;
			int chunkCount;
			uint32_t hostStep; // the step the block was snapshotted on, same for every chunk
			DWORD startedMs;
			DWORD lastSendMs;
		};

		Outbound g_outbound[MaxPlayers];

		// The snapshot is taken ONCE when a transfer starts and then sent from this buffer,
		// not re-read per chunk. Re-reading would send a block that moved underneath the
		// transfer, so the client would assemble bytes from several different moments and
		// end up with a world that never existed on the host.
		unsigned char g_sendBuffer[SaveBlock::Size];
		bool g_sendBufferValid = false;
		uint32_t g_nextSnapshotId = 1;

		// ---------------------------------------------------------------------------
		// Client side, one transfer at a time.
		// ---------------------------------------------------------------------------
		unsigned char g_recvBuffer[SaveBlock::Size];

		// One bit per chunk. 27 chunks today, and the array is sized from the constants so
		// it cannot fall behind a change to either.
		const int MaxChunks = (int)((SaveBlock::Size + (DWORD)WorldChunkBytes - 1) / (DWORD)WorldChunkBytes);
		bool g_chunkArrived[MaxChunks];

		bool g_receiving = false;
		uint32_t g_recvSnapshotId = 0;
		int g_recvChunkCount = 0;
		int g_recvHave = 0;
		uint32_t g_recvHostStep = 0;
		DWORD g_recvStartedMs = 0;
		DWORD g_recvLastMs = 0;

		// Set when a transfer completes, so the install happens from ServiceWorldSync at a
		// known point in the frame rather than from inside the receive path, which runs
		// while the session is mid-pump.
		bool g_readyToInstall = false;

		// The APPLIED reply, held back until the arrival placement has finished.
		//
		// Replying as soon as the bytes land would be wrong, and not by a little. The
		// host unfreezes on the reply, so an early one lets it walk away while this
		// machine's map is still loading, and the anchor it sent goes stale before the
		// placement can use it. The transfer ends with the two worlds identical and the
		// two parties in different places, which is exactly the divergence the hold was
		// raised to prevent.
		//
		// The snapshot id and host step have to be stashed because ResetReceive clears
		// the receive state as soon as the install is done, and that is several frames
		// before the reply goes out.
		bool g_appliedPending = false;
		uint32_t g_appliedSnapshot = 0;
		uint32_t g_appliedHostStep = 0;

		bool g_awaitingRequest = false; // we asked and have not seen a chunk yet
		int g_lastRequestReason = 0;

		// Whether this machine is currently holding the simulation for a transfer, so the
		// reason is dropped exactly once and never left raised.
		bool g_holdingForTransfer = false;

		// Latched once a snapshot has been installed. Latched rather than derived from the
		// transfer state, because a later resync must not pull the clock back down: by then
		// the two worlds already agree, and a refresh is a correction, not a cold start.
		bool g_worldInstalled = false;

		// ---------------------------------------------------------------------------
		// PEOPLE WAITING FOR A GAME THE HOST HAS NOT STARTED YET.
		//
		// Connecting from the TITLE SCREEN is allowed, and it has to be. Sitting in the
		// main menu together and then picking New Game or a save is the obvious way to
		// start a co-op session, and it was the one way that did not work.
		//
		// What it used to do was worse than refusing. The save block is static .data, so
		// it is readable and ALL ZERO before a game exists, and every check on this path
		// was a readable check. A host at the title screen cheerfully snapshotted the
		// whole block of zeroes and sent it, and the joiner transplanted those zeroes
		// over its own save block. Nothing refused and nothing logged, because as far as
		// every test here was concerned that was a perfectly good world.
		//
		// So a request that arrives too early is REMEMBERED rather than answered. The
		// moment the host actually has a game, everybody on this list gets it.
		// ---------------------------------------------------------------------------
		bool g_wantsWorld[MaxPlayers];

		// Was this host playable last frame? The edge is what triggers the push, so New
		// Game and Load Game both work without either of them needing to know this file
		// exists.
		bool g_hostWasPlayable = false;

		// A client's own retry clock. The host pushing is the reliable half of this and
		// the retry is the belt to its braces, covering a request lost before the
		// session settled and a host that was restarted while we sat here.
		DWORD g_lastAskMs = 0;
		const DWORD AskAgainMs = 4000;

		// Asks without an answer, counted only to keep the log readable. A lobby wait
		// re-asks every few seconds and nobody needs fifteen lines a minute saying so.
		DWORD g_askCount = 0;

		// Has the current stall already been reported? The state reset underneath the
		// report has to happen every time, but the sentence only needs saying once per
		// episode, or a client that keeps politely re-asking turns the log into a wall
		// of the same complaint.
		bool g_stallReported = false;

		char g_status[224] = "world sync: idle";

		// Freeze or unfreeze the game for the duration of a transfer.
		//
		// The host has to do this. A snapshot is one moment, and if the host keeps
		// simulating while 27 chunks go out, the client installs a world the host has
		// already moved past. In practice very little in the block changes during the
		// seven frames a transfer takes, because position is not in the block and the
		// script work area only changes when a script writes to it. But "very little"
		// is not "nothing": a host who walks into an event trigger mid-transfer hands
		// over a world that is genuinely behind, and under lockstep that is a divergence
		// from the first step rather than a cosmetic one.
		//
		// A client holds too, for a simpler reason: it has nothing valid to simulate
		// until the world lands.
		//
		// The request goes through LockstepLink because that file owns the hold byte and
		// re-asserts it every animate. See HoldReason there.
		void SetTransferHold(bool wanted)
		{
			if (wanted == g_holdingForTransfer)
				return;

			g_holdingForTransfer = wanted;
			if (wanted)
				HoldSimulationFor(kHoldWorldTransfer);
			else
				ReleaseSimulationFor(kHoldWorldTransfer);
		}

		int ChunkCountFor(DWORD bytes)
		{
			return (int)((bytes + (DWORD)WorldChunkBytes - 1) / (DWORD)WorldChunkBytes);
		}

		void ResetReceive()
		{
			g_receiving = false;
			g_readyToInstall = false;
			g_recvSnapshotId = 0;
			g_recvChunkCount = 0;
			g_recvHave = 0;
			g_recvHostStep = 0;
			memset(g_chunkArrived, 0, sizeof(g_chunkArrived));
		}

		// ---------------------------------------------------------------------------
		// The install. This is the whole point of the file.
		// ---------------------------------------------------------------------------
		void InstallReceivedWorld()
		{
			g_readyToInstall = false;

			TransplantOptions options = DefaultTransplantOptions();

			// Same language on both ends is the overwhelmingly common case and the name
			// re-encode is pure text, so it stays off until there is a reason. If the two
			// machines ever do differ, this is the one line that changes.
			options.reencodeNames = false;

			TransplantResult result;
			const bool ok = TransplantSaveBlock(g_recvBuffer, SaveBlock::Size, options, &result);

			if (!ok)
			{
				Log("world sync: the transplant REFUSED, so this machine is still in its "
				    "own world and must not simulate alongside the host");
				strcpy_s(g_status, sizeof(g_status), "world sync: transplant refused");
				SetTransferHold(false);
				ResetReceive();
				return;
			}

			// The option flags are named here because they are the easiest thing in a
			// transplant to be silently wrong about: the bits arrive inside the block
			// either way, so nothing looks missing, and what differs is only the
			// engine's cached reading of them. The movie bit in particular gates FMV
			// playback, so a peer that skipped this disagrees about every cutscene.
			Log("world sync: installed %u bytes, derived stats %s, just-loaded flag %s, "
			    "option flags %s",
			    (unsigned)result.bytesCopied, result.recomputedDerived ? "rebuilt" : "SKIPPED",
			    result.setJustLoadedFlag ? "set" : "not set",
			    result.appliedOptionFlags ? "pushed into the engine" : "NOT APPLIED");
			Log("world sync: the block says map %d entry %d, checkpoint map %d entry %d",
			    result.mapIdAfter, result.entryPointAfter, result.checkpointMapAfter,
			    result.checkpointEntryAfter);

			// Now load where the host is. The block's LIVE fields already say that, because
			// they came over inside it, so this only has to ask for the load.
			//
			// Not RequestResumeFromCheckpoint, which the recipe in WORLD_STATE.md suggests:
			// that one reads +0xB8/+0xBA and writes them over the live fields, so it would
			// land this machine at the host's last SAVE SPHERE rather than at the host. For
			// loading a save that is right. For joining a game in progress it is not.
			if (!RequestLoadLiveLocation())
			{
				// ---------------------------------------------------------------------------
				// A WORLD WITH NOWHERE TO BE. This used to log and carry on into the
				// arrival, which then waited out its whole timeout for a map load that
				// was never going to be asked for.
				//
				// The host is not supposed to send one of these any more, because
				// SendWorldToPeer now waits for its CHR pool. So reaching here means
				// either an old host build or something genuinely unexpected, and in
				// both cases the useful thing is to say so plainly, let go of the hold,
				// and leave the re-ask to try again rather than pretend to be joining.
				// ---------------------------------------------------------------------------
				Log("world sync: the world state installed cleanly but the block names no "
				    "map, so there is nowhere to go and this machine has not moved. The "
				    "host probably sent it mid-load. Letting go and asking again.");
				strcpy_s(g_status, sizeof(g_status),
				    "world sync: the host's world had no map in it, asking again");
				SetTransferHold(false);
				ResetReceive();
				return;
			}

			// Now get this machine to where the host is standing, not just to the map it
			// is standing in. See world/Arrival.h for why that is a separate step and why
			// the reply below waits for it.
			g_appliedPending = true;
			g_appliedSnapshot = g_recvSnapshotId;
			g_appliedHostStep = g_recvHostStep;
			BeginArrival(g_recvSnapshotId);

			// The hold comes off HERE, before the reply, and that is not a slip. The map
			// load is consumed inside FFX_MainStep, which the hold skips, so holding
			// through the load would wait forever for something that cannot happen. The
			// lockstep clock is gated separately on g_worldInstalled, which stays false
			// until the arrival finishes, so this machine steps locally without
			// exchanging input against a world it has not reached yet.
			SetTransferHold(false);

			strcpy_s(g_status, sizeof(g_status), "world sync: installed, loading the host's map");

			ResetReceive();
		}

		// The second half of the install, once the arrival has settled one way or another.
		void FinishInstall()
		{
			g_appliedPending = false;

			// Hash AFTER the placement, so the number the host compares against covers the
			// world this machine is actually going to start simulating.
			GameStateHash hash;
			uint32_t combined = 0;
			if (HashGameState(&hash) && hash.valid)
				combined = (uint32_t)hash.combined;

			Session* session = ActiveSession();
			if (session)
			{
				WorldAppliedPayload reply;
				memset(&reply, 0, sizeof(reply));
				reply.snapshotId = g_appliedSnapshot;
				reply.combinedHash = combined;
				reply.step = g_appliedHostStep;
				reply.ok = 1;
				session->Send(HostPeer, MessageWorldApplied, &reply, sizeof(reply), SendReliable);
			}

			g_worldInstalled = true;

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "world sync: installed snapshot %u, hash %08X, %s", (unsigned)g_appliedSnapshot,
			    (unsigned)combined, ArrivalStatus());
		}

		// ---------------------------------------------------------------------------
		// The sink.
		// ---------------------------------------------------------------------------
		class WorldSink : public MessageSink
		{
		public:
			virtual bool OnSessionMessage(const MessageHeader& header,
			    const unsigned char* payload, int payloadLength)
			{
				switch (header.kind)
				{
				case MessageWorldRequest:
					return OnRequest(header, payload, payloadLength);
				case MessageWorldSnapshot:
					return OnSnapshot(header, payload, payloadLength);
				case MessageWorldApplied:
					return OnApplied(header, payload, payloadLength);
				case MessageWorldAnchor:
					return OnAnchor(header, payload, payloadLength);
				default:
					return false;
				}
			}

		private:
			bool OnRequest(const MessageHeader& header, const unsigned char* payload,
			    int payloadLength)
			{
				if (payloadLength < (int)sizeof(WorldRequestPayload))
					return true;

				Session* session = ActiveSession();
				if (!session || !session->IsHost())
				{
					// A client has no world to hand out. Claiming the message anyway is
					// right, because it IS ours, and silently ignoring it is better than
					// letting it be counted as an unknown kind.
					return true;
				}

				const WorldRequestPayload* req = (const WorldRequestPayload*)payload;
				Log("world sync: peer %u asked for the world, reason %u, their step %u",
				    (unsigned)header.sender, (unsigned)req->reason, (unsigned)req->step);

				SendWorldToPeer((int)header.sender);
				return true;
			}

			bool OnSnapshot(const MessageHeader& header, const unsigned char* payload,
			    int payloadLength)
			{
				if (payloadLength < (int)sizeof(WorldSnapshotPayload))
					return true;

				// Only the host hands out worlds. Without this check a client could push its
				// save state at the host, or at another client, and the receiver would
				// install it. The host never sees a message from itself, so for the host this
				// refuses every snapshot, which is correct: its own world is the reference.
				if (header.sender != HostPeer)
				{
					Log("world sync: ignoring a snapshot from peer %u. Only the host sends "
					    "the world.",
					    (unsigned)header.sender);
					return true;
				}

				const WorldSnapshotPayload* chunk = (const WorldSnapshotPayload*)payload;

				// Everything below validates before it writes, because this is remote data
				// landing in a fixed buffer and a bad offset is a memory corruption rather
				// than a bad frame.
				if (chunk->totalBytes != SaveBlock::Size)
				{
					Log("world sync: refusing a snapshot of %u bytes, this build's block is "
					    "%u. The two ends are not the same mod build.",
					    (unsigned)chunk->totalBytes, (unsigned)SaveBlock::Size);
					return true;
				}

				const int count = (int)chunk->chunkCount;
				const int index = (int)chunk->chunkIndex;
				const int bytes = (int)chunk->chunkBytes;

				if (count <= 0 || count > MaxChunks || index < 0 || index >= count)
					return true;
				if (bytes <= 0 || bytes > WorldChunkBytes)
					return true;
				if (chunk->offset > SaveBlock::Size ||
				    chunk->offset + (DWORD)bytes > SaveBlock::Size)
					return true;

				// A new id means a new transfer. Starting fresh rather than merging is what
				// makes a retry safe: a stray chunk from an abandoned attempt cannot end up
				// inside the current block.
				if (!g_receiving || chunk->snapshotId != g_recvSnapshotId)
				{
					ResetReceive();
					g_receiving = true;
					g_recvSnapshotId = chunk->snapshotId;
					g_recvChunkCount = count;
					g_recvHostStep = chunk->hostStep;
					g_recvStartedMs = GetTickCount();
					g_awaitingRequest = false;
					g_askCount = 0;
					g_stallReported = false;
					SetTransferHold(true);
					Log("world sync: receiving snapshot %u, %d chunks, host step %u",
					    (unsigned)chunk->snapshotId, count, (unsigned)chunk->hostStep);
				}

				g_recvLastMs = GetTickCount();

				if (!g_chunkArrived[index])
				{
					memcpy(g_recvBuffer + chunk->offset, chunk->data, (size_t)bytes);
					g_chunkArrived[index] = true;
					++g_recvHave;
				}

				if (g_recvHave >= g_recvChunkCount)
				{
					// Install from ServiceWorldSync instead of here. This runs while the
					// session is mid-pump, and the install replaces the entire world
					// including the script work area, so it wants a known point in the
					// frame rather than the middle of a receive loop.
					g_readyToInstall = true;
					Log("world sync: all %d chunks arrived in %u ms, installing on the next "
					    "service",
					    g_recvChunkCount, (unsigned)(g_recvLastMs - g_recvStartedMs));
				}

				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "world sync: receiving %d of %d chunks", g_recvHave, g_recvChunkCount);
				return true;
			}

			bool OnAnchor(const MessageHeader& header, const unsigned char* payload,
			    int payloadLength)
			{
				if (payloadLength < (int)sizeof(WorldAnchorPayload))
					return true;

				// Same rule as the snapshot: only the host says where anybody is. Without
				// this a client could place the whole party wherever it liked.
				if (header.sender != HostPeer)
				{
					Log("world sync: ignoring an anchor from peer %u. Only the host says "
					    "where the party is standing.",
					    (unsigned)header.sender);
					return true;
				}

				NoteArrivalAnchor(*(const WorldAnchorPayload*)payload);
				return true;
			}

			bool OnApplied(const MessageHeader& header, const unsigned char* payload,
			    int payloadLength)
			{
				if (payloadLength < (int)sizeof(WorldAppliedPayload))
					return true;

				const WorldAppliedPayload* applied = (const WorldAppliedPayload*)payload;

				if ((int)header.sender < MaxPlayers)
					g_outbound[header.sender].active = false;

				if (!applied->ok)
				{
					Log("world sync: peer %u REFUSED the world. It is not safe to simulate "
					    "alongside it.",
					    (unsigned)header.sender);
					return true;
				}

				// The host hashes its own state now and compares. Doing it here rather than
				// storing the snapshot's hash is deliberate: if the host's world moved
				// between the snapshot and now, that is worth seeing, because it means the
				// transfer raced something.
				GameStateHash hash;
				uint32_t mine = 0;
				if (HashGameState(&hash) && hash.valid)
					mine = (uint32_t)hash.combined;

				if (mine == applied->combinedHash)
					Log("world sync: peer %u installed snapshot %u and both ends hash %08X. "
					    "The two worlds are identical.",
					    (unsigned)header.sender, (unsigned)applied->snapshotId,
					    (unsigned)mine);
				else
					Log("world sync: peer %u installed snapshot %u but hashes DIFFER, host "
					    "%08X client %08X. Either the host's world moved during the "
					    "transfer, or something outside the block matters.",
					    (unsigned)header.sender, (unsigned)applied->snapshotId,
					    (unsigned)mine, (unsigned)applied->combinedHash);

				return true;
			}
		};

		WorldSink g_sink;
		bool g_started = false;

	} // namespace

	void StartWorldSync()
	{
		Session* session = ActiveSession();
		if (!session)
			return;

		if (!session->AddMessageSink(&g_sink))
		{
			Log("world sync: could not register the message sink, so a joining client "
			    "cannot be handed the world");
			return;
		}

		g_started = true;
		g_worldInstalled = false;
		g_appliedPending = false;
		memset(g_outbound, 0, sizeof(g_outbound));
		memset(g_wantsWorld, 0, sizeof(g_wantsWorld));
		g_hostWasPlayable = false;
		g_lastAskMs = 0;
		g_askCount = 0;
		g_stallReported = false;
		g_sendBufferValid = false;
		ResetReceive();
		StartArrival();

		// A client asks immediately. Waiting for somebody to press a key would mean the
		// clock starts exchanging input against two different worlds.
		if (!session->IsHost())
		{
			// ---------------------------------------------------------------------------
			// HELD ONLY IF THERE IS SOMETHING TO HOLD.
			//
			// This used to hold unconditionally from the moment we asked, reasoning that
			// there is nothing valid for a client to simulate in between. That is right
			// for a client with its own game already up. It IS simulating a world, that
			// world is about to be replaced, and every step it takes in the meantime is a
			// step the host never took.
			//
			// It is wrong for a client at the TITLE SCREEN, and badly so. There is no
			// world there to diverge, and the wait is not a few hundred milliseconds of
			// transfer, it is however long the host takes to pick a save. The hold stops
			// FFX_MainStep, so that is a frozen title screen for an unbounded time, which
			// is indistinguishable from a hang.
			//
			// GameLoaded is exactly the question "is there anything to stop". When the
			// chunks do start arriving the hold goes on then, at the top of the chunk
			// handler, so nothing is lost by waiting for them.
			// ---------------------------------------------------------------------------
			if (GameLoaded())
			{
				SetTransferHold(true);
				strcpy_s(g_status, sizeof(g_status),
				    "world sync: joining, asking for the world");
			}
			else
			{
				strcpy_s(g_status, sizeof(g_status),
				    "world sync: joining from the main menu, waiting for the host's game");
			}

			g_lastAskMs = GetTickCount();
			RequestWorldFromHost(kWorldRequestJoining);
		}
		else
		{
			strcpy_s(g_status, sizeof(g_status),
			    GameLoaded() ? "world sync: hosting, ready to send"
			                 : "world sync: hosting, no game yet so nothing to send");
		}
	}

	void StopWorldSync()
	{
		Session* session = ActiveSession();
		if (session)
			session->RemoveMessageSink(&g_sink);

		// Before anything else. A session that goes away mid-transfer must not leave the
		// game frozen, and this is the one path every teardown goes through.
		SetTransferHold(false);

		g_started = false;
		g_worldInstalled = false;
		g_awaitingRequest = false;
		g_appliedPending = false;
		memset(g_outbound, 0, sizeof(g_outbound));
		memset(g_wantsWorld, 0, sizeof(g_wantsWorld));
		g_hostWasPlayable = false;
		g_askCount = 0;
		g_stallReported = false;
		ResetReceive();
		StopArrival();
		strcpy_s(g_status, sizeof(g_status), "world sync: idle");
	}

	bool WorldSyncActive()
	{
		return g_started;
	}

	bool AppliedHostStep(uint32_t* out)
	{
		// Zero means the host had no clock running when it snapshotted, which should not
		// happen now that NetLink starts the clock ahead of ServiceWorldSync, but a zero
		// here would silently seed a client at step 1 against a host in the thousands.
		// Refusing is the safe answer, because a clock that never starts is visible in
		// the status line and a clock started at the wrong number is not visible at all.
		if (!out || !g_worldInstalled || g_appliedHostStep == 0)
			return false;

		*out = g_appliedHostStep;
		return true;
	}

	bool WorldSyncReady()
	{
		if (!g_started)
			return false;

		Session* session = ActiveSession();
		if (!session)
			return false;

		// The host's own world is the reference, so there is nothing for it to wait for.
		if (session->IsHost())
			return true;

		return g_worldInstalled;
	}

	bool RequestWorldFromHost(int reason)
	{
		Session* session = ActiveSession();
		if (!session)
			return false;

		if (session->IsHost())
		{
			Log("world sync: the host does not ask anybody for the world");
			return false;
		}

		WorldRequestPayload req;
		memset(&req, 0, sizeof(req));
		req.reason = (uint32_t)reason;
		req.step = 0;
		if (const workshop::Lockstep* clock = ActiveLockstep())
			req.step = (uint32_t)clock->CurrentStep();

		if (!session->Send(HostPeer, MessageWorldRequest, &req, sizeof(req), SendReliable))
			return false;

		g_awaitingRequest = true;
		g_lastRequestReason = reason;
		g_recvStartedMs = GetTickCount();
		g_recvLastMs = g_recvStartedMs;

		// A joining ask repeats on its own while we sit in the lobby, so the first two
		// and then roughly one a minute is enough to follow what is happening. Anything
		// a human asked for is always worth a line.
		if (reason == workshop::kWorldRequestJoining)
			++g_askCount;
		else
			g_askCount = 0;

		if (g_askCount <= 2 || (g_askCount % 15) == 0)
			Log("world sync: asked the host for the world, reason %d%s", reason,
			    g_askCount > 1 ? " (asking again, nothing has come back yet)" : "");
		return true;
	}

	int SendWorldToPeer(int peer)
	{
		Session* session = ActiveSession();
		if (!session || !session->IsHost())
			return 0;
		if (peer <= 0 || peer >= MaxPlayers)
			return 0;
		if (!session->PeerReachable((uint8_t)peer))
		{
			Log("world sync: peer %d is not reachable, so there is nobody to send to", peer);
			return 0;
		}

		// ---------------------------------------------------------------------------
		// A GAME FIRST, and this is the check that was missing.
		//
		// SnapshotSaveBlock below only asks whether the block is READABLE, and a static
		// buffer in .data always is. So this used to send an all-zero world from the
		// title screen, and the log line underneath it, which says exactly the right
		// thing, was unreachable.
		//
		// GameLoaded is the honest test: record 0 has a non-zero base max HP and
		// somebody is flagged into the party, neither of which a zeroed block can fake.
		// See ffx/GameState.h.
		//
		// Not an error. Somebody joining from the main menu before the host has picked
		// anything is the normal way to start, so the request is filed and answered
		// later rather than refused. See g_wantsWorld.
		// ---------------------------------------------------------------------------
		if (!GameLoaded())
		{
			g_wantsWorld[peer] = true;
			Log("world sync: peer %d wants the world but there is no game here yet, so "
			    "they are on the list and get it the moment one is loaded. Start a new "
			    "game or load a save.",
			    peer);
			strcpy_s(g_status, sizeof(g_status),
			    "world sync: hosting, waiting for a game to start or load");
			return 0;
		}

		// Snapshot ONCE per transfer. Re-reading per chunk would let the block move
		// underneath the transfer and hand the client a mixture of several moments, which
		// is a world that never existed on this machine.
		if (!SnapshotSaveBlock(g_sendBuffer, sizeof(g_sendBuffer)))
		{
			Log("world sync: cannot snapshot the save block yet, so load a save before "
			    "letting anybody join");
			return 0;
		}

		// ---------------------------------------------------------------------------
		// AND THE MAP HAS TO BE UP, which is a different failure from the one above and
		// not an obvious one.
		//
		// GameLoaded flips the moment the character records are in. During both a Load
		// Game and a New Game that is BEFORE the map has come up, and the live map id at
		// block offset 0 can still be zero in that window. A client that receives such a
		// block transplants it perfectly, then calls RequestLoadLiveLocation, which
		// refuses a zero, and ends up holding a complete and correct world with nowhere
		// to be. It logs one line about not having moved and then waits out an arrival
		// that can never complete.
		//
		// With the lobby re-ask running every few seconds that window is not a remote
		// possibility, it is a near certainty on the first attempt after the host picks a
		// save. So it has to be closed here rather than hoped past.
		//
		// PoolIsReady is the engine's own answer: a non-null CHR array with a non-zero
		// count, which means a map is up with characters authored into it. It is also
		// exactly the test the body layer uses before spawning, so host and client agree
		// on what playable means.
		// ---------------------------------------------------------------------------
		if (!PoolIsReady())
		{
			g_wantsWorld[peer] = true;
			Log("world sync: peer %d asked while a map is still loading here, so they are "
			    "on the list and get the world once it is up. Sending now would hand them "
			    "a world with no map in it.",
			    peer);
			strcpy_s(g_status, sizeof(g_status),
			    "world sync: hosting, waiting for the map to finish loading");
			return 0;
		}

		// Off the list, since they are getting it now.
		g_wantsWorld[peer] = false;

		g_sendBufferValid = true;
		SetTransferHold(true);

		uint32_t hostStep = 0;
		if (const workshop::Lockstep* clock = ActiveLockstep())
			hostStep = (uint32_t)clock->CurrentStep();

		Outbound& out = g_outbound[peer];
		out.active = true;
		out.snapshotId = g_nextSnapshotId++;
		out.nextChunk = 0;
		out.chunkCount = ChunkCountFor(SaveBlock::Size);
		out.hostStep = hostStep;
		out.startedMs = GetTickCount();
		out.lastSendMs = out.startedMs;

		// The anchor goes FIRST, before any chunk. Delivery is reliable and in order,
		// so sending it first is what guarantees the joiner has it in hand by the time it
		// has a world to place into. It is also read at the same moment as the snapshot,
		// which matters: both describe one instant, and the simulation is held from here
		// so that instant stays true for the whole transfer.
		WorldAnchorPayload anchor;
		if (CaptureArrivalAnchor(out.snapshotId, hostStep, &anchor))
		{
			session->Send((uint8_t)peer, MessageWorldAnchor, &anchor, sizeof(anchor),
			    SendReliable);
			Log("world sync: sent the anchor for snapshot %u, map %u with %d characters",
			    (unsigned)out.snapshotId, (unsigned)anchor.mapId, (int)anchor.slotCount);
		}
		else
		{
			Log("world sync: could not read where the party is standing, so peer %d will "
			    "land at the doorway the save block names rather than beside us",
			    peer);
		}

		// The RNG state lives outside the save block, so the snapshot cannot carry it.
		// It goes out here, under the same transfer hold and so from the same instant,
		// and reliable ordered delivery puts it ahead of the chunks.
		if (!PublishRandomStateTo(peer, workshop::RandomStateJoin))
			Log("world sync: could not send the RNG state to peer %d, so the two "
			    "machines will draw different numbers until a reseed",
			    peer);

		Log("world sync: sending snapshot %u to peer %d, %u bytes in %d chunks",
		    (unsigned)out.snapshotId, peer, (unsigned)SaveBlock::Size, out.chunkCount);
		return 1;
	}

	int SendWorldToAll()
	{
		int started = 0;
		for (int peer = 1; peer < MaxPlayers; ++peer)
			started += SendWorldToPeer(peer);

		return started;
	}

	void ServiceWorldSync()
	{
		if (!g_started)
			return;

		Session* session = ActiveSession();
		if (!session)
			return;

		// The client's install, at a known point in the frame.
		if (g_readyToInstall)
			InstallReceivedWorld();

		// The placement, and the reply that waits for it.
		ServiceArrival();
		if (g_appliedPending && ArrivalComplete())
			FinishInstall();

		// A stalled transfer, either direction. Reliable delivery means loss is not the
		// cause, so say what the likely cause actually is instead of just timing out.
		//
		// WAITING IN THE LOBBY IS NOT A STALL, which is what lobbyWait is for. A client
		// that connected from the main menu is waiting on a human deciding which save to
		// load, and that takes as long as it takes. There is also nothing to rescue it
		// from, because a client with no game of its own was never held. Calling that a
		// timeout would report a fault that is not happening and, worse, would reset the
		// receive state of a transfer that is about to start.
		const DWORD now = GetTickCount();
		const bool lobbyWait = g_awaitingRequest && !g_receiving && !GameLoaded();
		if (!lobbyWait && (g_receiving || g_awaitingRequest)
		    && now - g_recvLastMs > TransferTimeoutMs)
		{
			// Said once per episode. The reset below runs every time regardless, since
			// that is the part that actually lets the game go.
			if (!g_stallReported)
				Log("world sync: no progress for %u ms. %s",
				    (unsigned)(now - g_recvLastMs),
				    g_awaitingRequest
				        ? "The host never started sending. If it is sitting in its main "
				          "menu that is expected, and it pushes the world over as soon "
				          "as it starts a game. We keep asking in the meantime."
				        : "The transfer stopped part way, so the host likely went away.");
			g_stallReported = true;
			strcpy_s(g_status, sizeof(g_status),
			    g_awaitingRequest ? "world sync: waiting for the host to load a game"
			                      : "world sync: transfer stalled");
			g_awaitingRequest = false;
			g_appliedPending = false;
			SetTransferHold(false);
			StopArrival();
			ResetReceive();
		}

		// ---------------------------------------------------------------------------
		// THE HOST GETTING A GAME IS THE EVENT EVERYONE IN THE LOBBY IS WAITING FOR.
		//
		// Watched for as an edge rather than replicated as a command, deliberately.
		// Starting a new game and loading a save are two entirely different paths
		// through the engine, reached through menus this mod does not drive, and both
		// end with the same observable fact: there is a game in the save block. Watching
		// for the fact covers both, plus anything else that gets there, and needs no
		// hook in either path.
		//
		// A client cannot be told to replay the host's menu input either, which is worth
		// writing down since it is the obvious first idea. The two machines have
		// different save files, so "load slot 3" means a different game on each one. The
		// host's block is the only answer, and sending it is what this file already does.
		// ---------------------------------------------------------------------------
		if (session->IsHost())
		{
			// PLAYABLE, not merely loaded. Same two tests SendWorldToPeer makes, for the
			// same two reasons, and sharing the definition is the point: the edge that
			// triggers a push is the same moment that a push would be accepted.
			//
			// This edge is also crossed by an ordinary map change during play, since the
			// CHR pool goes down and comes back. That is harmless and even useful, a peer
			// whose earlier ask was deferred gets picked up at the next door. It does
			// mean the list being empty is the normal case, so that says nothing.
			const bool playable = GameLoaded() && PoolIsReady();
			if (playable && !g_hostWasPlayable)
			{
				int waiting = 0;
				int sent = 0;
				for (int peer = 1; peer < MaxPlayers; ++peer)
				{
					if (!g_wantsWorld[peer])
						continue;
					if (!session->PeerReachable((uint8_t)peer))
					{
						g_wantsWorld[peer] = false;
						continue;
					}
					++waiting;
					sent += SendWorldToPeer(peer);
				}

				if (waiting > 0)
					Log("world sync: this machine is playable now, so the %d peer(s) who "
					    "were waiting get the world. %d transfer(s) started.",
					    waiting, sent);
			}
			g_hostWasPlayable = playable;
		}
		else if (!g_worldInstalled && !g_receiving)
		{
			// ---------------------------------------------------------------------------
			// ASK AGAIN. The host pushing is the reliable half, so this is the belt to
			// its braces: a request lost before the session settled, or a host restarted
			// while we sat here.
			//
			// Two different clients reach this, and the difference matters. One has no
			// game of its own, joined from the main menu and was never held, so it can
			// sit and ask all day at no cost. The other has a game and IS held, and for
			// that one the stall timeout above has to stay reachable, because a held
			// client with a frozen step is worse than a slow join. Each ask resets the
			// stall clock, so that client only re-asks once the timeout has already
			// given up and let it go, which is what the g_awaitingRequest test means
			// here.
			// ---------------------------------------------------------------------------
			const bool wasHeld = GameLoaded();
			if ((!wasHeld || !g_awaitingRequest) && now - g_lastAskMs >= AskAgainMs)
			{
				g_lastAskMs = now;
				RequestWorldFromHost(kWorldRequestJoining);
			}
		}

		// The host's paced chunk sends.
		if (!session->IsHost() || !g_sendBufferValid)
			return;

		bool anyOutbound = false;

		for (int peer = 1; peer < MaxPlayers; ++peer)
		{
			Outbound& out = g_outbound[peer];
			if (!out.active)
				continue;

			if (!session->PeerReachable((uint8_t)peer))
			{
				Log("world sync: peer %d went away mid-transfer at chunk %d of %d", peer,
				    out.nextChunk, out.chunkCount);
				out.active = false;
				continue;
			}

			anyOutbound = true;

			for (int n = 0; n < ChunksPerFrame && out.nextChunk < out.chunkCount; ++n)
			{
				const int index = out.nextChunk;
				const DWORD offset = (DWORD)index * (DWORD)WorldChunkBytes;
				DWORD bytes = SaveBlock::Size - offset;
				if (bytes > (DWORD)WorldChunkBytes)
					bytes = (DWORD)WorldChunkBytes;

				WorldSnapshotPayload chunk;
				memset(&chunk, 0, sizeof(chunk));
				chunk.snapshotId = out.snapshotId;
				chunk.totalBytes = SaveBlock::Size;
				chunk.offset = offset;
				chunk.hostStep = out.hostStep;
				chunk.chunkBytes = (uint16_t)bytes;
				chunk.chunkIndex = (uint16_t)index;
				chunk.chunkCount = (uint16_t)out.chunkCount;
				memcpy(chunk.data, g_sendBuffer + offset, (size_t)bytes);

				if (!session->Send((uint8_t)peer, MessageWorldSnapshot, &chunk, sizeof(chunk),
				        SendReliable))
				{
					// Stop for this frame rather than spinning. The send queue is full, and
					// the next frame will have room.
					break;
				}

				++out.nextChunk;
				out.lastSendMs = now;
			}

			if (out.nextChunk >= out.chunkCount)
			{
				Log("world sync: all %d chunks of snapshot %u are away to peer %d, waiting "
				    "for it to confirm",
				    out.chunkCount, (unsigned)out.snapshotId, peer);
				// Left active until the applied reply arrives, so a missing confirmation is
				// visible in the log rather than looking like a clean finish.
			}

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "world sync: sent %d of %d chunks to peer %d", out.nextChunk, out.chunkCount,
			    peer);
		}

		// The host resumes once nothing is in flight. Note this waits for the APPLIED reply,
		// not just for the last chunk to be handed to the transport, because the point of
		// the hold is that the client's installed world matches this one. Releasing at the
		// last send would reintroduce exactly the gap the hold exists to close, just a
		// round trip narrower.
		if (!anyOutbound)
			SetTransferHold(false);
	}

	const char* WorldSyncStatus()
	{
		return g_status;
	}

	void LogWorldSync()
	{
		Log("=== world sync ===");
		Log("%s", g_status);

		Session* session = ActiveSession();
		if (!session)
		{
			Log("no session, so there is nobody to exchange a world with");
			LogWorldLocation();
			return;
		}

		Log("role %s, local peer %u, block %u bytes in %d chunks of %d",
		    session->IsHost() ? "host" : "client", (unsigned)session->LocalPeer(),
		    (unsigned)SaveBlock::Size, MaxChunks, WorldChunkBytes);

		if (g_receiving || g_awaitingRequest)
			Log("inbound: snapshot %u, %d of %d chunks, %s", (unsigned)g_recvSnapshotId,
			    g_recvHave, g_recvChunkCount,
			    g_awaitingRequest ? "still waiting for the first chunk" : "in progress");

		for (int peer = 1; peer < MaxPlayers; ++peer)
		{
			const Outbound& out = g_outbound[peer];
			if (out.active)
				Log("outbound to peer %d: snapshot %u, %d of %d chunks sent", peer,
				    (unsigned)out.snapshotId, out.nextChunk, out.chunkCount);
		}

		if (g_appliedPending)
			Log("the applied reply for snapshot %u is held back until the arrival placement "
			    "finishes, which is what keeps the host frozen where it said it was",
			    (unsigned)g_appliedSnapshot);

		LogWorldLocation();
		LogArrival();
	}

} // namespace pilgrimage
