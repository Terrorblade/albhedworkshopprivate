#include "menu/CoopConfig.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "coop/Ownership.h"
#include "ffx/GameState.h"
#include "ffx/MenuSystem.h"
#include "menu/MenuSync.h"
#include "net/Commands.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Detour.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	// The game library and the mod plumbing. Anything unqualified below that looks
	// like FFX knowledge comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// ---------------------------------------------------------------------------
		// THE ROWS
		// ---------------------------------------------------------------------------
		enum CoopRowIndex
		{
			kRowHeader = 0, // "Co-op", not selectable, no values
			kRowSlot0 = 1,  // who plays the character in active party slot 0
			kRowSlot1 = 2,
			kRowSlot2 = 3,
			kRowOverride = 4, // "Menu control", normal or host only
			kCoopRowCount = 5
		};

		// Which active party slot a slot row is about. Only valid for kRowSlot0..2.
		int SlotOfRow(int row)
		{
			return row - kRowSlot0;
		}

		bool IsSlotRow(int row)
		{
			return row >= kRowSlot0 && row <= kRowSlot2;
		}

		// Our row objects, 44 bytes each. DWORDs rather than bytes so the storage is
		// four-aligned: the game reads ints out of these at +4, +8 and +12, and 44 is
		// exactly eleven dwords.
		DWORD g_rowStorage[kCoopRowCount][ConfigRow::Size / 4];

		void* RowObject(int row)
		{
			return (void*)g_rowStorage[row];
		}

		// The combined table we point the game at. The game's eight pointers copied
		// into the front, ours appended. Sized for the shipped count plus ours, and the
		// install refuses to proceed if the live count is not the shipped eight,
		// because then this build is not the one the layout was read off.
		void* g_table[kConfigShippedRowCount + kCoopRowCount];

		// What the globals held before we touched them, so a restore puts back exactly
		// what was there rather than an array we picked.
		void** g_savedTable = NULL;
		int g_savedCount = 0;
		bool g_haveSaved = false;

		// ---------------------------------------------------------------------------
		// THE RESERVED STRING IDS
		//
		// The id is masked to 12 bits by the lookup, so the whole space is 0..4095, and
		// the shipped Config rows use 4 to 0x25. 0x0F00 is far above anything the menu
		// is plausibly carrying while leaving room below the mask.
		//
		// The layout is fixed arithmetic rather than a table, because the provider has
		// to turn an id back into "which row, which value" with nothing but the number.
		// ---------------------------------------------------------------------------
		const int kIdBase = 0x0F00;

		const int kIdHeaderLabel = 0;   // "Co-op"
		const int kIdOverrideLabel = 1; // "Menu control"
		const int kIdOverrideOff = 2;
		const int kIdOverrideOn = 3;
		const int kIdSlotLabel = 4;  // + slot, so 4, 5, 6
		const int kIdSlotValue = 8;  // + slot * 4 + peer, so 8..18
		const int kIdSlotCount = 20; // one past the last id we own

		// The kernel string group the in-game menu's UI strings live in. The kit knows
		// this too, and it is repeated rather than exported because a provider has to
		// name the group it is answering for and a magic 7 in the body would be worse.
		const int kUiStringGroup = 7;

		// Two buffers per id, one for the label and one for the description, because a
		// value id is looked up for BOTH in the same frame and FFX_Menu_SetHelpString
		// keeps the description POINTER rather than copying the bytes. Sharing one
		// buffer would mean the help line showed the value name.
		//
		// 96 encoded bytes is one glyph each plus a terminator, and the help line runs
		// off the right of the screen well before that.
		const int kTextBytes = 96;
		char g_label[kIdSlotCount][kTextBytes];
		char g_desc[kIdSlotCount][kTextBytes];

		// ---------------------------------------------------------------------------
		// STATE
		// ---------------------------------------------------------------------------
		bool g_installed = false; // both hooks went in
		bool g_started = false;   // a session is active
		bool g_injected = false;  // our rows are on the Config screen right now

		// Set when a session ends while the Config screen is still up. The table cannot
		// be shortened under a cursor that is past the new end, so the restore waits
		// for the screen to go away and the per-step tick keeps running until it has.
		bool g_restorePending = false;

		// One ask in flight per row, so holding Right does not queue a command a step.
		// Cleared when the authoritative state agrees with what was asked for, or when
		// the ask has clearly gone nowhere.
		bool g_askPending[kCoopRowCount];
		int g_askedValue[kCoopRowCount];
		uint32_t g_askStep[kCoopRowCount];

		// How long a row holds the value that was asked for before giving up and going
		// back to showing the truth.
		//
		// THIS IS NOT OPTIONAL and it is not a nicety. There are several ways an ask
		// reaches nobody: a client pressing the host-only override row, a session that
		// would not carry the command, no menu driver because MenuSync is not running.
		// Without a timeout the row would sit on the value the player chose for the
		// rest of the session while the state underneath said something else, which is
		// the single most misleading thing a settings row can do.
		//
		// Counted in SIMULATION STEPS rather than frames, so the two machines give up
		// on the same step and their rows never show different values. Two seconds at
		// 30 Hz, which is many times a lockstep round trip.
		const uint32_t kAskTimeoutSteps = 60;

		// The step the game is running right now, filled at the top of every step. Read
		// by the row setter, which runs inside the same step, and by the refresh.
		uint32_t g_nowStep = 0;

		// So the command drain runs once per simulation step even if the step tick ever
		// fires twice for one step. The drain is idempotent, this is about the log.
		uint32_t g_drainedStep = 0;
		bool g_haveDrainedStep = false;

		LONG g_applied = 0; // ownership commands applied
		LONG g_asked = 0;   // asks sent
		LONG g_refused = 0; // asks the session would not carry
		bool g_saidNoRoom = false;

		char g_status[200] = "co-op config: not installed";

		// ---------------------------------------------------------------------------
		// TEXT
		//
		// Everything the rows say is built here, in ASCII, and encoded on the way out.
		// It is rebuilt on every lookup rather than cached, because the character names
		// and the peer presence change while the screen is up and a cached label would
		// go stale in front of the player.
		// ---------------------------------------------------------------------------
		const char* PeerName(int peer)
		{
			switch (peer)
			{
			case 0:
				return "Host";
			case 1:
				return "Player 2";
			case 2:
				return "Player 3";
			default:
				return "nobody";
			}
		}

		// The character in an active party slot, or kCharNone. The rows are keyed on
		// the slot and the ownership table is keyed on the character, so this is the
		// join between them and it is read fresh every time: the party changes with the
		// in-battle Switch command, which the menu knows nothing about.
		BYTE CharacterInSlot(int slot)
		{
			if (slot < 0 || slot >= kActivePartySize)
				return kCharNone;
			return ActivePartyMember(slot);
		}

		void BuildLabelAscii(int id, char* out, int bytes)
		{
			if (id == kIdHeaderLabel)
			{
				strcpy_s(out, (size_t)bytes, "Co-op");
				return;
			}

			if (id == kIdOverrideLabel)
			{
				strcpy_s(out, (size_t)bytes, "Menu control");
				return;
			}

			if (id == kIdOverrideOff)
			{
				strcpy_s(out, (size_t)bytes, "Normal");
				return;
			}

			if (id == kIdOverrideOn)
			{
				strcpy_s(out, (size_t)bytes, "Host only");
				return;
			}

			if (id >= kIdSlotLabel && id < kIdSlotLabel + kActivePartySize)
			{
				const BYTE charIndex = CharacterInSlot(id - kIdSlotLabel);
				if (charIndex == kCharNone)
				{
					// An empty slot. A dash on its own rather than a blank, so the row
					// reads as deliberately empty rather than as a draw that failed.
					strcpy_s(out, (size_t)bytes, "-");
					return;
				}
				strcpy_s(out, (size_t)bytes, CharacterName(charIndex));
				return;
			}

			if (id >= kIdSlotValue && id < kIdSlotValue + kActivePartySize * 4)
			{
				// JUST THE NAME, NOTHING APPENDED, and the length matters.
				//
				// A row's values are drawn CENTRED at x = 960 + 420 * i in the game's
				// 1920 wide UI space, so the third of three sits at 1800 with 120 to
				// the right edge. Eight glyphs ("Player 2") is about the longest thing
				// that fits there, and it is the same length as the game's own longest
				// value name. Anything appended, "(away)" for instance, runs off the
				// screen on the third column only, which is the one place a test on a
				// two player session would not notice.
				//
				// Whether that peer is actually here goes in the help line instead,
				// which is drawn at the bottom of the screen with room to spare.
				//
				// The value COUNT stays at MaxPlayers whoever is connected, because a
				// count that followed the peer set would have the two machines cycling
				// through different lists from the same replicated press.
				strcpy_s(out, (size_t)bytes, PeerName((id - kIdSlotValue) % 4));
				return;
			}

			out[0] = '\0';
		}

		void BuildDescAscii(int id, char* out, int bytes)
		{
			if (id == kIdOverrideOff)
			{
				strcpy_s(out, (size_t)bytes, "Whoever opens a menu drives it.");
				return;
			}

			if (id == kIdOverrideOn)
			{
				strcpy_s(out, (size_t)bytes, "The host drives every menu, whoever opened it.");
				return;
			}

			if (id >= kIdSlotValue && id < kIdSlotValue + kActivePartySize * 4)
			{
				const int slot = (id - kIdSlotValue) / 4;
				const int peer = (id - kIdSlotValue) % 4;
				const BYTE charIndex = CharacterInSlot(slot);

				if (charIndex == kCharNone)
				{
					strcpy_s(out, (size_t)bytes, "Nobody is in this party slot.");
					return;
				}

				// The help line is the place to say a peer is not here, because it has
				// room and the value name does not. See the note in BuildLabelAscii.
				if (!PeerInSession(peer))
				{
					_snprintf_s(out, (size_t)bytes, _TRUNCATE,
					    "%s is not in this session, so %s falls back to the host.",
					    PeerName(peer), CharacterName(charIndex));
					return;
				}

				_snprintf_s(out, (size_t)bytes, _TRUNCATE,
				    "%s plays %s in battle and on the grid.", PeerName(peer),
				    CharacterName(charIndex));
				return;
			}

			// The label ids never have their description looked up, because the help
			// line is keyed on the selected VALUE's id rather than on the row. Answered
			// with an empty string anyway, so a future change to the draw cannot make
			// this fall through to the real table with one of our ids.
			out[0] = '\0';
		}

		// ---------------------------------------------------------------------------
		// THE PROVIDER. Runs on the game thread, inside the Config screen's draw.
		//
		// It answers for group 7 and only for our reserved window, and only while our
		// rows are actually installed. Returning NULL hands the lookup back to the game
		// untouched, which is what happens for every other id in the game.
		// ---------------------------------------------------------------------------
		const char* __cdecl UiStringProvider(int group, int id, bool wantDescription)
		{
			if (group != kUiStringGroup || !g_injected)
				return NULL;

			const int ours = id - kIdBase;
			if (ours < 0 || ours >= kIdSlotCount)
				return NULL;

			char ascii[kTextBytes];
			ascii[0] = '\0';

			char* out = wantDescription ? g_desc[ours] : g_label[ours];

			if (wantDescription)
				BuildDescAscii(ours, ascii, (int)sizeof(ascii));
			else
				BuildLabelAscii(ours, ascii, (int)sizeof(ascii));

			if (!EncodeFfxText(ascii, out, kTextBytes))
			{
				// Too long for the buffer. EncodeFfxText leaves a valid empty string
				// rather than half a word, so the row draws blank instead of drawing
				// nonsense, and this is a bug in the text above rather than a runtime
				// condition.
				out[0] = '\0';
			}
			return out;
		}

		// ---------------------------------------------------------------------------
		// WHAT EACH ROW'S VALUE AND SELECTABLE FLAG SHOULD BE.
		//
		// Both are pure functions of state the two machines already share. That is not
		// tidiness: Up and Down skip rows whose selectable flag is not 1, and the
		// navigation runs off replicated input, so two machines with different flags
		// put their cursors on different rows.
		// ---------------------------------------------------------------------------
		int WantedValue(int row)
		{
			if (row == kRowOverride)
				return MenuControlOverrideHeld() ? 1 : 0;

			if (IsSlotRow(row))
			{
				const BYTE charIndex = CharacterInSlot(SlotOfRow(row));
				if (charIndex == kCharNone)
					return 0;

				const int owner = OwnerOfCharacter((int)charIndex);
				if (owner < 0 || owner >= MaxPlayers)
					return 0;
				return owner;
			}

			return 0;
		}

		bool WantedSelectable(int row)
		{
			if (row == kRowHeader)
				return false;

			// Selectable on a client too, even though only the host may change it. See
			// the note in CoopConfig.h: a flag that differs between the two machines
			// diverges the cursor, and a refused press only costs a log line.
			if (row == kRowOverride)
				return true;

			if (IsSlotRow(row))
				return CharacterInSlot(SlotOfRow(row)) != kCharNone;

			return false;
		}

		// Pushes the wanted value and flag into one row. Called from the row's own
		// getter, which the game runs on entry to the screen, and from the per-step
		// tick, which is what keeps the row honest while the screen is up.
		void RefreshRow(int row)
		{
			if (row < 0 || row >= kCoopRowCount)
				return;

			void* object = RowObject(row);
			SetConfigRowSelectable(object, WantedSelectable(row));

			int count = 0;
			if (!ConfigRowValueCount(object, &count) || count <= 0)
				return; // the header, which has nothing to show

			const int wanted = WantedValue(row);

			if (g_askPending[row])
			{
				if (g_askedValue[row] == wanted)
				{
					// The command landed. "The state now matches what was asked for" is
					// the only definition of done that does not guess.
					g_askPending[row] = false;
				}
				else if ((uint32_t)(g_nowStep - g_askStep[row]) > kAskTimeoutSteps)
				{
					g_askPending[row] = false;
					Log("co-op config: the Config row change asked for %u steps ago never "
					    "took effect, so the row is going back to showing what is actually "
					    "set. Nothing is out of step, the ask simply reached nobody.",
					    (unsigned)kAskTimeoutSteps);
				}
			}

			// The value is only pushed back once no ask is in flight. Without this the
			// row would snap back the instant the player pressed Right and then jump
			// again when the command landed, which reads as the press being lost.
			if (!g_askPending[row])
				SetConfigRowCurrentValue(object, wanted);
		}

		int RowIndexOfObject(const void* object)
		{
			for (int i = 0; i < kCoopRowCount; ++i)
				if (object == (const void*)g_rowStorage[i])
					return i;
			return -1;
		}

		// ---------------------------------------------------------------------------
		// ASKING. Neither of these writes any state.
		// ---------------------------------------------------------------------------

		// Is this machine the one that should send the ask.
		//
		// The setter runs on EVERY machine, because every machine stepped module 10
		// from the same replicated pad block. So without this, one press produces one
		// identical command per player. MenuDriver is the same answer on both machines,
		// which is what makes gating on it safe.
		bool WeAreTheOneToAsk()
		{
			const int driver = MenuDriver();
			if (driver < 0)
				return false; // no menu is up as far as MenuSync is concerned
			return driver == LocalPeerIndex();
		}

		void AskForOverride(bool wanted)
		{
			if (wanted == MenuControlOverrideHeld())
				return;

			// THE SAME FUNCTION ctrl+F4 CALLS. One path to this state, two callers, so
			// the key and the row cannot end up disagreeing about what was asked for.
			// It is a toggle rather than a state, which is fine because the row only
			// gets here when the wanted value differs from the live one.
			RequestMenuControlOverride();
			InterlockedIncrement(&g_asked);
		}

		void AskForSlotOwner(int slot, int peer)
		{
			const BYTE charIndex = CharacterInSlot(slot);
			if (charIndex == kCharNone)
				return;
			if (peer < 0 || peer >= MaxPlayers)
				return;

			// NO SESSION MEANS BIND IT RIGHT HERE. There is no clock to stamp a step
			// on, and there is also nobody to disagree with, so the ordering that the
			// rest of this function exists to guarantee has nothing to guarantee. This
			// is what makes the rows useful before anybody joins: a host can set up who
			// plays whom, and the bindings are already in the table when the join lands.
			//
			// The hazard this does NOT open: a binding made alone cannot desync, because
			// the table is seeded from the same place on both machines and a joiner gets
			// the host's world before it ever steps alongside it.
			Lockstep* clock = ActiveLockstep();
			if (!clock)
			{
				if (!SetCharacterOwner(peer, (int)charIndex))
					return;

				InterlockedIncrement(&g_applied);
				Log("co-op config: %s will play %s. Set locally, because there is no "
				    "session yet, so it is a choice rather than a change.",
				    PeerName(peer), CharacterName(charIndex));
				return;
			}

			CharOwnerCommand payload;
			memset(&payload, 0, sizeof(payload));
			payload.peer = (uint8_t)peer;
			payload.charIndex = (uint8_t)charIndex;

			if (!clock->RequestCommand((uint8_t)kCommandCharOwner, &payload, (int)sizeof(payload)))
			{
				InterlockedIncrement(&g_refused);
				Log("co-op config: could not ask for %s to play %s, so nothing changed. "
				    "Binding it locally would mean the two machines were acting on "
				    "different ownership, which is the one failure here with no way back.",
				    PeerName(peer), CharacterName(charIndex));
				return;
			}

			InterlockedIncrement(&g_asked);
			Log("co-op config: asking for %s to play %s. It takes effect on the step the "
			    "host stamps it for, on both machines at once.",
			    PeerName(peer), CharacterName(charIndex));
		}

		// ---------------------------------------------------------------------------
		// THE ROW CALLBACKS the game calls.
		// ---------------------------------------------------------------------------

		// Called once per row by FFX_Menu_ConfigInitRowValues on entry to the screen.
		// It must not fault and it must not be null, because the game calls it for
		// every row in the live table without checking either.
		void __cdecl CoopRowGetter(void* object)
		{
			const int row = RowIndexOfObject(object);
			if (row < 0)
				return;
			RefreshRow(row);
		}

		// Called by module 10 on every Left or Right press, AFTER it has already moved
		// and wrapped the row's current value. So the new value is read back out of the
		// row rather than computed here.
		void __cdecl CoopRowSetter(void* object)
		{
			const int row = RowIndexOfObject(object);
			if (row < 0 || !g_started)
				return;

			int value = 0;
			if (!ConfigRowCurrentValue(object, &value))
				return;

			// One ask in flight per row. Holding Right would otherwise queue one command
			// per step for the whole round trip, and the last one to arrive would win
			// rather than the one the player stopped on.
			if (g_askPending[row] && g_askedValue[row] == value)
				return;

			g_askPending[row] = true;
			g_askedValue[row] = value;
			g_askStep[row] = g_nowStep;

			if (!WeAreTheOneToAsk())
			{
				// The driver's machine is sending it. The latch is still set here, so
				// this machine's row holds the pressed value until the command lands,
				// exactly as the driver's does.
				return;
			}

			if (row == kRowOverride)
				AskForOverride(value != 0);
			else if (IsSlotRow(row))
				AskForSlotOwner(SlotOfRow(row), value);
		}

		// ---------------------------------------------------------------------------
		// APPLYING WHAT THE HOST ORDERED
		// ---------------------------------------------------------------------------
		void ApplyCharOwner(const Command& command)
		{
			if (command.length < (int)sizeof(CharOwnerCommand))
			{
				Log("co-op config: an ownership command from peer %u is %d bytes and the "
				    "payload is %d, so the two builds do not agree on the protocol",
				    (unsigned)command.issuer, command.length, (int)sizeof(CharOwnerCommand));
				return;
			}

			CharOwnerCommand payload;
			memcpy(&payload, command.data, sizeof(payload));

			const int peer = (int)payload.peer;
			const int charIndex = (payload.charIndex == 0xFF) ? -1 : (int)payload.charIndex;

			if (!SetCharacterOwner(peer, charIndex))
			{
				// Refused for a range reason, which means the two machines have just
				// taken different paths through this function. Loud, because it is the
				// shape of a protocol mismatch rather than a user error.
				Log("co-op config: peer %u's ownership command (peer %d, character %d) was "
				    "REFUSED on step %u. If the other machine accepted it, the two are now "
				    "acting on different ownership.",
				    (unsigned)command.issuer, peer, charIndex, (unsigned)command.step);
				return;
			}

			InterlockedIncrement(&g_applied);
			Log("co-op config: %s now plays %s, applied on step %u, asked for by peer %u",
			    PeerName(peer),
			    (charIndex >= 0) ? CharacterName((BYTE)charIndex) : "nothing in particular",
			    (unsigned)command.step, (unsigned)command.issuer);
		}

		void ApplyCommandsForStep(Lockstep* clock)
		{
			// Three is already generous: one row change per step is the most a human can
			// produce, and the two other command consumers in the mod use the same
			// small bound for the same reason.
			const Command* commands[4];
			const int count = clock->CommandsForStep(commands, 4);

			for (int i = 0; i < count; ++i)
				if (commands[i]->kind == (uint8_t)kCommandCharOwner)
					ApplyCharOwner(*commands[i]);
		}

		// ---------------------------------------------------------------------------
		// THE ROW TABLE: getting ours in, and getting the game's back
		// ---------------------------------------------------------------------------
		void BuildRows()
		{
			WORD valueIds[kConfigRowMaxValues];

			// The header. A value count of 0 draws the label and the separator line and
			// nothing else, which is exactly what a heading is. Not selectable, so the
			// cursor can never reach it and its setter is never called.
			memset(valueIds, 0, sizeof(valueIds));
			BuildConfigRow(RowObject(kRowHeader), 0, false, &CoopRowGetter, &CoopRowSetter,
			    (WORD)(kIdBase + kIdHeaderLabel), valueIds);

			// One row per active party slot, cycling through the peers. The value count
			// is MaxPlayers on both machines whatever the peer set looks like, because a
			// count that followed who is connected would make the same replicated press
			// land on different values on the two machines. Absent peers are marked in
			// the value's own text instead.
			for (int slot = 0; slot < kActivePartySize; ++slot)
			{
				for (int peer = 0; peer < MaxPlayers && peer < kConfigRowMaxValues; ++peer)
					valueIds[peer] = (WORD)(kIdBase + kIdSlotValue + slot * 4 + peer);

				BuildConfigRow(RowObject(kRowSlot0 + slot), MaxPlayers, true, &CoopRowGetter,
				    &CoopRowSetter, (WORD)(kIdBase + kIdSlotLabel + slot), valueIds);
			}

			// The host override. Off is value 0 so the row reads the same way round as
			// the flag it mirrors.
			memset(valueIds, 0, sizeof(valueIds));
			valueIds[0] = (WORD)(kIdBase + kIdOverrideOff);
			valueIds[1] = (WORD)(kIdBase + kIdOverrideOn);
			BuildConfigRow(RowObject(kRowOverride), 2, true, &CoopRowGetter, &CoopRowSetter,
			    (WORD)(kIdBase + kIdOverrideLabel), valueIds);

			for (int row = 0; row < kCoopRowCount; ++row)
			{
				g_askPending[row] = false;
				g_askedValue[row] = 0;
				RefreshRow(row);
			}
		}

		void Inject()
		{
			int shipped = 0;
			if (!CopyConfigRowTable(g_table, (int)(sizeof(g_table) / sizeof(g_table[0])),
			        &shipped))
				return;

			if (shipped != kConfigShippedRowCount)
			{
				// Said once. Eight is what the layout was read off, and a different
				// number means either another mod got here first or this is not the
				// analysed build. Either way, appending to it is a guess.
				if (!g_saidNoRoom)
				{
					g_saidNoRoom = true;
					Log("co-op config: the Config screen has %d rows and the layout was "
					    "read off a build with %d, so the co-op rows are NOT being added. "
					    "Use ctrl+F4 and the control panel instead.",
					    shipped, kConfigShippedRowCount);
				}
				return;
			}

			g_savedTable = ConfigRowTable();
			g_savedCount = shipped;
			g_haveSaved = (g_savedTable != NULL);

			for (int row = 0; row < kCoopRowCount; ++row)
				g_table[shipped + row] = RowObject(row);

			BuildRows();

			if (!SetConfigRowTable(g_table, shipped + kCoopRowCount))
			{
				g_haveSaved = false;
				Log("co-op config: the row table would not take our array, so the co-op "
				    "rows are not on the Config screen. ctrl+F4 still works.");
				return;
			}

			g_injected = true;
			Log("co-op config: %d co-op rows added to the Config screen behind the game's "
			    "%d. Nothing of the game's was modified, so the worst this can go wrong is "
			    "a missing row.",
			    (int)kCoopRowCount, shipped);
		}

		void Restore()
		{
			if (!g_injected)
			{
				g_restorePending = false;
				return;
			}

			// NOT while the screen is up. The cursor is a signed char that nothing
			// clamps, so shortening the array under a cursor sitting on one of our rows
			// is an out of bounds row-pointer read on the player's next Up or Down.
			// SetConfigRowTable does clamp it, but a cursor that jumps to row 0 under
			// the player's hand is still worse than waiting a moment.
			if (ConfigScreenActive())
			{
				g_restorePending = true;

				// And in the meantime our rows stop responding, so a press during the
				// wait cannot ask for anything. g_started is already false, which is
				// what stops the setter, and this is what stops the cursor reaching
				// them at all.
				for (int row = 0; row < kCoopRowCount; ++row)
					SetConfigRowSelectable(RowObject(row), false);
				return;
			}

			if (!g_haveSaved)
			{
				// Nothing to put back, so put nothing back. Setting the table to NULL
				// and the count to 0 would be worse than leaving ours in: the exec's Up
				// handler decrements the cursor and then wraps it to count - 1, which is
				// -1 with no rows, and then dereferences rows[-1]. Inject cannot reach
				// here with this unset, and it is written out anyway so that a future
				// change to Inject cannot quietly arm it.
				Log("co-op config: the original row table was never recorded, so it is "
				    "being left alone. The next menu open rewrites both globals itself.");
				g_restorePending = false;
				return;
			}

			SetConfigRowTable(g_savedTable, g_savedCount);

			g_injected = false;
			g_restorePending = false;
			g_haveSaved = false;
			Log("co-op config: the Config screen is back to the game's own %d rows",
			    g_savedCount);
		}

		void MaintainTable()
		{
			if (!g_started)
			{
				Restore();
				return;
			}

			// The game rewrites both globals from module 10's prepare slot on every menu
			// open, so ours has to go back in each time. Noticing that the pointer is a
			// shipped array again is the whole detection: there is no hook to put on
			// FFX_Menu_ConfigSelectRowTable, whose first instruction is a rel32 call.
			//
			// The identity check is against OUR array rather than just "not a shipped
			// one", because anything else in there belongs to somebody else and
			// claiming it would have the string provider answering for rows it did not
			// build.
			if (ConfigRowTable() == g_table)
			{
				g_injected = true; // still ours from last time
				return;
			}

			g_injected = false;

			if (!ConfigRowTableIsShipped())
			{
				if (!g_saidNoRoom)
				{
					g_saidNoRoom = true;
					Log("co-op config: the Config screen's row table belongs to somebody "
					    "else, so the co-op rows are NOT being added. ctrl+F4 and the "
					    "control panel still work.");
				}
				return;
			}

			// Nothing to append to yet. Both globals are zero until the menu has been
			// opened once, and the game fills them itself at that point.
			if (ConfigRowCount() <= 0)
				return;

			Inject();
		}

		void BuildStatus()
		{
			if (!g_installed)
			{
				strcpy_s(g_status, sizeof(g_status), "co-op config: not installed");
				return;
			}

			if (!g_started)
			{
				strcpy_s(g_status, sizeof(g_status),
				    g_restorePending
				        ? "co-op config: rows retiring, waiting for the Config screen to close"
				        : "co-op config: off");
				return;
			}

			if (!g_injected)
			{
				strcpy_s(g_status, sizeof(g_status),
				    "co-op config: waiting for the menu to be opened once");
				return;
			}

			char slots[120];
			slots[0] = '\0';

			for (int slot = 0; slot < kActivePartySize; ++slot)
			{
				const BYTE charIndex = CharacterInSlot(slot);
				char one[48];
				if (charIndex == kCharNone)
					strcpy_s(one, sizeof(one), " -");
				else
					_snprintf_s(one, sizeof(one), _TRUNCATE, " %s/%s",
					    CharacterName(charIndex), PeerName(WantedValue(kRowSlot0 + slot)));
				strcat_s(slots, sizeof(slots), one);
			}

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "co-op config: %d rows in,%s, override %s, %ld applied", (int)kCoopRowCount,
			    slots, MenuControlOverrideHeld() ? "ON" : "off", g_applied);
		}

		// ---------------------------------------------------------------------------
		// THE PER-SIMULATION-STEP TICK, and why it is a detour on a function that looks
		// nothing to do with any of this.
		//
		// An ordered command may only be consumed on the step it was stamped for:
		// Lockstep::CommandsForStep matches the current step exactly and AdvanceStep
		// retires anything at or behind it. So a consumer on the frame path misses a
		// command whenever one presented frame covers two simulation steps, which is
		// what a catch-up step is. For an ownership binding, missing it on one machine
		// is precisely the split brain this whole file is arranged to prevent.
		//
		// FFX_MainStep is the natural place and the kit's lockstep layer already owns
		// it. So this detours FFX_MainStep_SetTopOfStepFloat 0x71E460 instead, which
		// FFX_MainStep calls at 0x820B04 with a constant 0, unconditionally, in the
		// straight-line run before its first conditional branch, and which has exactly
		// one caller in the whole binary. Once per simulation step, at the top of the
		// step, on every path. The function itself writes one float and does nothing
		// else, so running it unchanged costs nothing.
		//
		// THE RIGHT FIX IS FOR THE LOCKSTEP GATE TO CALL A STEP FUNCTION HERE, next to
		// PrepareMenuInput, and for this detour to go away. It is a detour because the
		// pass that wrote this file was not allowed to edit net/LockstepLink.cpp.
		//
		// Verified in IDA. Seven bytes, three whole instructions, nothing position
		// dependent:
		//
		// ---------------------------------------------------------------------------
		// THE PER-SIMULATION-STEP TICK.
		//
		// An ordered command may only be consumed on the step it was stamped for:
		// Lockstep::CommandsForStep matches the current step exactly and AdvanceStep
		// retires anything at or behind it. So a consumer on the frame path misses a
		// command whenever one presented frame covers two simulation steps, which is
		// what a catch-up step is. For an ownership binding, missing it on one machine
		// is precisely the split brain this file is arranged to prevent.
		//
		// So the lockstep gate calls ServiceCoopConfigStep from its FFX_MainStep
		// pre-hook, next to PrepareMenuInput, which is once per simulation step by
		// construction. An earlier pass of this file detoured
		// FFX_MainStep_SetTopOfStepFloat to get the same effect, because the pass that
		// wrote it was not allowed to edit net/LockstepLink.cpp. That detour is gone.
		//
		// THE FRAME PATH CALLS IT TOO, and that is not a duplicate. GateCallback returns
		// early when the clock is not running, so without the frame call a session that
		// ends with the Config screen up would never put the row table back. The step
		// guard below makes the second call a no-op for anything step-sensitive.
		// ---------------------------------------------------------------------------
		void ServiceStep()
		{
			if (!g_started && !g_restorePending)
				return;

			if (!g_started)
			{
				// A session ended with the Config screen up. Keep coming back until the
				// screen has gone and the table can be put back.
				Restore();
				BuildStatus();
				return;
			}

			// ONLY THE ORDERED COMMANDS NEED A CLOCK. The rows themselves do not, and
			// keeping that distinction is what lets the screen work solo. An earlier
			// pass returned here when there was no clock, which meant the table was
			// never maintained outside a running session and the rows never appeared.
			if (Lockstep* clock = ActiveLockstep())
			{
				g_nowStep = clock->CurrentStep();

				if (!g_haveDrainedStep || g_nowStep != g_drainedStep)
				{
					g_drainedStep = g_nowStep;
					g_haveDrainedStep = true;
					ApplyCommandsForStep(clock);
				}
			}

			MaintainTable();

			// Last, and on every step rather than only while the screen is up. The rows
			// have to already be right the moment FFX_Menu_ConfigInitRowValues runs, and
			// that happens inside the step that opens the screen.
			if (g_injected)
				for (int row = 0; row < kCoopRowCount; ++row)
					RefreshRow(row);

			BuildStatus();
		}

	} // namespace

	void ServiceCoopConfigStep()
	{
		ServiceStep();
	}

	bool InstallCoopConfig()
	{
		if (g_installed)
			return true;

		// Only the string override is a hook now. The per-step tick is a plain call
		// from the lockstep gate, so there is nothing to refuse there.
		if (!InstallUiStringOverride(&UiStringProvider))
		{
			Log("co-op config: without the UI string override the rows would have no "
			    "labels, so they are not being added at all. ctrl+F4 still works.");
			return false;
		}

		g_installed = true;

		// ARMED IMMEDIATELY, not when a session starts. These rows are SETTINGS, and the
		// point of settings is to be able to set them beforehand. Gating them on a live
		// session meant they could not be pre-configured and could not be tested at all,
		// since hosting with nobody joined is not an active session either.
		//
		// This is the opposite choice from the dialogue, battle and menu layers, and the
		// difference is real: those replicate input, which means nothing with one player,
		// so they stay idle until a session. This one stores a choice.
		StartCoopConfig();
		BuildStatus();
		Log("co-op config: installed. The co-op rows appear on the game's Config screen "
		    "whether or not a session is running. In a session every change goes out as an "
		    "ordered command, and alone it is just stored.");
		return true;
	}

	bool CoopConfigInstalled()
	{
		return g_installed;
	}

	void StartCoopConfig()
	{
		g_started = g_installed;
		g_saidNoRoom = false;
		g_haveDrainedStep = false;
		g_applied = 0;
		g_asked = 0;
		g_refused = 0;

		// A second session must not inherit a restore the first one never finished,
		// which can happen when the link dropped with the Config screen up and came
		// back before the player closed it.
		g_restorePending = false;

		for (int row = 0; row < kCoopRowCount; ++row)
		{
			g_askPending[row] = false;
			g_askedValue[row] = 0;
			g_askStep[row] = 0;
		}

		BuildStatus();

		if (!g_installed)
		{
			Log("co-op config: not installed, so the Config screen has no co-op rows this "
			    "session. ctrl+F4 and the control panel still work.");
			return;
		}

		// Not injected here. The globals are whatever the last menu open left them, and
		// the tick puts our array in on the next pass, which is well before anybody can
		// reach the Config screen.
		Log("co-op config: armed. The rows go in on the next pass.");
	}

	void StopCoopConfig()
	{
		g_started = false;

		// Attempt the restore straight away. It defers itself if the Config screen
		// happens to be up, and the per-step tick finishes the job.
		Restore();
		BuildStatus();
	}

	bool CoopConfigActive()
	{
		return g_started;
	}

	bool CoopConfigRowsInjected()
	{
		return g_injected;
	}

	const char* CoopConfigStatus()
	{
		return g_status;
	}

	void LogCoopConfig()
	{
		Log("=== co-op config rows ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("neither hook is in, so the only way to change ownership is the control "
			    "panel and the only way to toggle the override is ctrl+F4");
			return;
		}

		Log("string override: %s, %lu lookups answered",
		    UiStringOverrideInstalled() ? "installed" : "NOT installed",
		    (unsigned long)UiStringOverrideHits());
		Log("row table: %d rows live, cursor on row %d, ours %s", ConfigRowCount(),
		    ConfigCursorRow(), g_injected ? "injected" : "not injected");
		Log("%ld asks sent, %ld refused by the session, %ld ownership commands applied",
		    g_asked, g_refused, g_applied);

		for (int slot = 0; slot < kActivePartySize; ++slot)
		{
			const BYTE charIndex = CharacterInSlot(slot);
			if (charIndex == kCharNone)
			{
				Log("  slot %d: empty", slot);
				continue;
			}
			Log("  slot %d: %-10s -> %s%s", slot, CharacterName(charIndex),
			    PeerName(WantedValue(kRowSlot0 + slot)),
			    g_askPending[kRowSlot0 + slot] ? "  (an ask is in flight)" : "");
		}

		Log("host override: %s%s", MenuControlOverrideHeld() ? "ON" : "off",
		    g_askPending[kRowOverride] ? ", an ask is in flight" : "");
		Log("a row press asks rather than writes, and only the menu driver's machine "
		    "sends the ask, so one press is one command rather than one per player");
	}

} // namespace pilgrimage
